#include "soem_port.h"

#ifdef SOEM_ENABLED

#include "soem/ec_main.h"
#include "soem/ec_type.h"
#include "stm32h7rsxx.h"  /* For SCB_CleanDCache_by_Addr, __DSB, __ISB */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef SOEM_IFNAME
#define SOEM_IFNAME "st_eth"
#endif

#ifndef SOEM_ENABLE_PERIODIC_PDO_DEBUG
#define SOEM_ENABLE_PERIODIC_PDO_DEBUG 0
#endif

#ifndef SOEM_ENABLE_CMD_DEBUG_LOG
#define SOEM_ENABLE_CMD_DEBUG_LOG 0
#endif

#ifndef SOEM_TARGET_REACHED_TOLERANCE_HW
#define SOEM_TARGET_REACHED_TOLERANCE_HW 5
#endif

/* Forward declaration */
static void soem_log(const char *msg);
static int32_t soem_clamp_hw_position(int32_t hwPos);

ecx_contextt soem_context;
/* Keep IOmap in AXI SRAM non-cacheable region (RAM_CMD) for ETH DMA coherency.
 * ETH DMA uses AXI bus — cannot access SRAMAHB (AHB-only). */
static uint8 soem_iomap[4096] __attribute__((section(".eth_dma"), aligned(32)));
static uint8 soem_group = 0;
static uint8 soem_initialized = 0;
static uint8 soem_configured = 0;
static uint8 soem_pdo_ready = 0;
static void (*soem_log_fn)(const char *msg) = NULL;

typedef struct __attribute__((packed))
{
  uint16 controlword;
  int32 target_position;
} soem_rxpdo_t;

typedef struct __attribute__((packed))
{
  uint16 statusword;
  int32 position_actual;
  int32 velocity_actual;
  int16 torque_actual;
} soem_txpdo_t;

static soem_rxpdo_t *soem_rxpdo = NULL;
static soem_txpdo_t *soem_txpdo = NULL;
static uint16 soem_last_statusword = 0xFFFFU;
static uint16 soem_last_controlword = 0xFFFFU;
static int32 soem_target_position = 0;
static int32 soem_target_position_output = 0;
static uint8 soem_last_state = 0xFFU;

/* Volatile shadow copies for safe UI access (TouchGFX) */
static volatile int32_t  soem_shadow_position = 0;
static volatile int32_t  soem_shadow_velocity = 0;
static volatile int16_t  soem_shadow_torque   = 0;
static volatile uint16_t soem_shadow_statusword = 0;
static volatile uint8_t  soem_shadow_pdo_ready  = 0;
static volatile uint8_t  soem_shadow_run_enable = 0;
static volatile int32_t  soem_profile_velocity = 0;
static volatile uint16_t soem_torque_limit_percent = 0;
static volatile uint8_t  soem_profile_velocity_pending = 0U;
static volatile uint8_t  soem_torque_limit_pending = 0U;
static volatile int32_t  soem_profile_acceleration = 0;
static volatile int32_t  soem_profile_deceleration = 0;
static volatile uint8_t  soem_profile_acceleration_pending = 0U;
static volatile uint8_t  soem_profile_deceleration_pending = 0U;
static volatile int32_t  soem_limit_plus_user = INT32_MAX;
static volatile int32_t  soem_limit_minus_user = INT32_MIN;
static volatile int32_t  soem_limit_plus_hw = INT32_MAX;
static volatile int32_t  soem_limit_minus_hw = INT32_MIN;
static volatile uint8_t  soem_software_limits_pending = 0U;
static volatile uint8_t  soem_software_limits_enabled = 0U;
static volatile uint8_t  soem_software_limits_blocked = 0U;
static volatile int32_t  soem_unit_scale = 1;
static volatile uint8_t  soem_unit_scale_pending = 0U;
static volatile uint8_t  soem_home_offset_pending = 0U;

/* Home offset: set by SOEM_SetHomePosition().
 * All position reads are relative to this origin.
 * All target writes are translated back to absolute hardware counts. */
static volatile int32_t  soem_home_offset = 0;

static void soem_latch_target_to_actual(const char *reason)
{
  soem_target_position = soem_clamp_hw_position((int32_t)soem_shadow_position);
  soem_target_position_output = soem_target_position;
  if ((reason != NULL) && (reason[0] != '\0'))
  {
    soem_log(reason);
  }
}

static void soem_log_fault_diagnostics(uint16 statusword)
{
  char line[96];
  uint16 faultCode = 0U;
  uint8 faultReg = 0U;
  int okCode = ecx_SDOread(&soem_context, 1, 0x603F, 0, FALSE, (int[]){ (int)sizeof(faultCode) }, &faultCode, EC_TIMEOUTRXM);
  int okReg = ecx_SDOread(&soem_context, 1, 0x1001, 0, FALSE, (int[]){ (int)sizeof(faultReg) }, &faultReg, EC_TIMEOUTRXM);

  if (okCode > 0)
  {
    (void)snprintf(line, sizeof(line), "CIA402: fault code=0x%04X status=0x%04X", faultCode, statusword);
    soem_log(line);
  }
  else
  {
    (void)snprintf(line, sizeof(line), "CIA402: fault code read failed status=0x%04X", statusword);
    soem_log(line);
  }

  if (okReg > 0)
  {
    (void)snprintf(line, sizeof(line), "CIA402: fault register=0x%02X", faultReg);
    soem_log(line);
  }

  (void)snprintf(line, sizeof(line), "CIA402: actual=%ld target=%ld hwLimits=[%ld,%ld]",
                 (long)soem_shadow_position,
                 (long)soem_target_position,
                 (long)soem_limit_minus_hw,
                 (long)soem_limit_plus_hw);
  soem_log(line);

  (void)snprintf(line, sizeof(line), "CIA402: userLimits=[%ld,%ld] unit=%ld home=%ld limits=%s",
                 (long)soem_limit_minus_user,
                 (long)soem_limit_plus_user,
                 (long)soem_unit_scale,
                 (long)soem_home_offset,
                 (soem_software_limits_enabled != 0U) ? "on" : "off");
  soem_log(line);
}

/* SOEM_PortPoll is called every ~100 ms in main task. */
#define SOEM_CIA402_HOLD_CYCLES 5U      /* ~500 ms */
#define SOEM_CIA402_TIMEOUT_CYCLES 50U  /* ~5 s */

typedef enum
{
  SOEM_CIA402_STAGE_SEND_SHUTDOWN = 0,
  SOEM_CIA402_STAGE_WAIT_READY,
  SOEM_CIA402_STAGE_SEND_SWITCH_ON,
  SOEM_CIA402_STAGE_WAIT_SWITCHED_ON,
  SOEM_CIA402_STAGE_SEND_ENABLE_OP,
  SOEM_CIA402_STAGE_WAIT_OPERATION_ENABLED,
  SOEM_CIA402_STAGE_OPERATION_ENABLED
} soem_cia402_stage_t;

static soem_cia402_stage_t soem_cia402_stage = SOEM_CIA402_STAGE_SEND_SHUTDOWN;
static uint16 soem_cia402_hold_counter = 0U;
static uint16 soem_cia402_timeout_counter = 0U;
static uint16 soem_last_sdo_statusword = 0xFFFFU;
static uint8 soem_fault_active = 0U;

/* Manual 1st PDO mapping: RxPDO=0x1600, TxPDO=0x1A00 */
#define SOEM_RXPDO_ASSIGN_INDEX 0x1600U
#define SOEM_TXPDO_ASSIGN_INDEX 0x1A00U
#define SOEM_DC_CYCLE_NS 100000000U /* 100 ms */
#define SOEM_DC_SHIFT_NS 0

static int soem_apply_manual_pdo_mapping(uint16 slave)
{
  int wkc;
  uint8 u8val;
  uint16 u16val;
  uint32 u32val;

  /* [Step 1] Clear SM2/SM3 assignment lists */
  u8val = 0;
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1C12, 0, FALSE, sizeof(u8val), &u8val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: SM2 clear fail"); return 0; }
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1C13, 0, FALSE, sizeof(u8val), &u8val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: SM3 clear fail"); return 0; }
  soem_log("PDO: SM assignment cleared");

  /* Small delay for slave internal processing */
  for (volatile int d = 0; d < 500000; d++) { }

  /* [Step 2] Configure RxPDO 0x1600: Controlword(16) + Target Position(32) = 6 bytes */
  u8val = 0;
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1600, 0, FALSE, sizeof(u8val), &u8val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: 0x1600 sub0 clear fail"); return 0; }
  u32val = 0x60400010U;  /* Controlword, 16-bit */
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1600, 1, FALSE, sizeof(u32val), &u32val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: 0x1600 sub1 fail"); return 0; }
  u32val = 0x607A0020U;  /* Target Position, 32-bit */
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1600, 2, FALSE, sizeof(u32val), &u32val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: 0x1600 sub2 fail"); return 0; }
  u8val = 2;
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1600, 0, FALSE, sizeof(u8val), &u8val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: 0x1600 sub0 set fail"); return 0; }
  soem_log("PDO: RxPDO 0x1600 configured (6 bytes)");

  /* [Step 3] Configure TxPDO 0x1A00: Statusword(16) + Position(32) + Velocity(32) + Torque(16) = 12 bytes */
  u8val = 0;
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1A00, 0, FALSE, sizeof(u8val), &u8val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: 0x1A00 sub0 clear fail"); return 0; }
  u32val = 0x60410010U;  /* Statusword, 16-bit */
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1A00, 1, FALSE, sizeof(u32val), &u32val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: 0x1A00 sub1 fail"); return 0; }
  u32val = 0x60640020U;  /* Actual Position, 32-bit */
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1A00, 2, FALSE, sizeof(u32val), &u32val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: 0x1A00 sub2 fail"); return 0; }
  u32val = 0x606C0020U;  /* Actual Velocity, 32-bit */
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1A00, 3, FALSE, sizeof(u32val), &u32val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: 0x1A00 sub3 fail"); return 0; }
  u32val = 0x60770010U;  /* Actual Torque, 16-bit */
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1A00, 4, FALSE, sizeof(u32val), &u32val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: 0x1A00 sub4 fail"); return 0; }
  u8val = 4;
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1A00, 0, FALSE, sizeof(u8val), &u8val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: 0x1A00 sub0 set fail"); return 0; }
  soem_log("PDO: TxPDO 0x1A00 configured (12 bytes)");

  /* [Step 4] Assign PDOs to SM2/SM3 */
  u16val = 0x1600U;
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1C12, 1, FALSE, sizeof(u16val), &u16val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: SM2 assign fail"); return 0; }
  u16val = 0x1A00U;
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1C13, 1, FALSE, sizeof(u16val), &u16val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: SM3 assign fail"); return 0; }

  /* Small delay before activation */
  for (volatile int d = 0; d < 500000; d++) { }

  /* [Step 5] Activate: set count = 1 */
  u8val = 1;
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1C12, 0, FALSE, sizeof(u8val), &u8val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: SM2 activate fail"); return 0; }
  wkc = ecx_SDOwrite(&soem_context, slave, 0x1C13, 0, FALSE, sizeof(u8val), &u8val, EC_TIMEOUTRXM);
  if (wkc <= 0) { soem_log("PDO: SM3 activate fail"); return 0; }

  soem_log("PDO: Manual mapping complete (Rx=6B, Tx=12B)");
  return 1;
}

static uint16 soem_read_statusword_sdo(uint16 slave, uint8 *ok)
{
  uint16 statusword = 0U;
  int size = (int)sizeof(statusword);
  int wkc = ecx_SDOread(&soem_context, slave, 0x6041, 0, FALSE, &size, &statusword, EC_TIMEOUTRXM);

  if ((wkc > 0) && (size >= (int)sizeof(statusword)))
  {
    *ok = 1U;
    return statusword;
  }

  *ok = 0U;
  return 0U;
}

static uint8 soem_decode_cia402_state(uint16 statusword)
{
  uint16 masked = (uint16)(statusword & 0x006FU);

  switch (masked)
  {
    case 0x0000U:
      return 0U; /* Not ready to switch on */
    case 0x0040U:
      return 1U; /* Switch on disabled */
    case 0x0021U:
      return 2U; /* Ready to switch on */
    case 0x0023U:
      return 3U; /* Switched on */
    case 0x0027U:
      return 4U; /* Operation enabled */
    case 0x0007U:
      return 5U; /* Quick stop active */
    case 0x000FU:
      return 6U; /* Fault reaction active */
    case 0x0008U:
      return 7U; /* Fault */
    default:
      return 255U; /* Unknown */
  }
}

static void soem_log(const char *msg)
{
  if (soem_log_fn != NULL)
  {
    size_t n = strlen(msg);
    if ((n > 0U) && ((msg[n - 1U] == '\n') || (msg[n - 1U] == '\r')))
    {
      soem_log_fn(msg);
    }
    else
    {
      char line[192];
      (void)snprintf(line, sizeof(line), "%s\r\n", msg);
      soem_log_fn(line);
    }
  }
}

static void soem_log_cia402_state(uint8 state)
{
  switch (state)
  {
    case 0U:
      soem_log("CIA402: Not ready to switch on");
      break;
    case 1U:
      soem_log("CIA402: Switch on disabled");
      break;
    case 2U:
      soem_log("CIA402: Ready to switch on");
      break;
    case 3U:
      soem_log("CIA402: Switched on");
      break;
    case 4U:
      soem_log("CIA402: Operation enabled");
      break;
    case 5U:
      soem_log("CIA402: Quick stop active");
      break;
    case 6U:
      soem_log("CIA402: Fault reaction active");
      break;
    case 7U:
      soem_log("CIA402: Fault");
      break;
    default:
      soem_log("CIA402: Unknown state");
      break;
  }
}

static int32_t soem_saturated_user_to_hw(int32_t userPos)
{
  int32_t scale = (soem_unit_scale > 0) ? soem_unit_scale : 1;
  int64_t hw = (int64_t)userPos * (int64_t)scale + (int64_t)soem_home_offset;
  if (hw > (int64_t)INT32_MAX)
  {
    hw = (int64_t)INT32_MAX;
  }
  else if (hw < (int64_t)INT32_MIN)
  {
    hw = (int64_t)INT32_MIN;
  }
  return (int32_t)hw;
}

static int32_t soem_hw_to_user(int32_t hwPos)
{
  int32_t scale = (soem_unit_scale > 0) ? soem_unit_scale : 1;
  int64_t user = ((int64_t)hwPos - (int64_t)soem_home_offset) / (int64_t)scale;
  if (user > (int64_t)INT32_MAX)
  {
    user = (int64_t)INT32_MAX;
  }
  else if (user < (int64_t)INT32_MIN)
  {
    user = (int64_t)INT32_MIN;
  }
  return (int32_t)user;
}

static int32_t soem_clamp_user_position(int32_t userPos)
{
  if (soem_software_limits_enabled == 0U)
  {
    return userPos;
  }

  if (userPos > soem_limit_plus_user)
  {
    return soem_limit_plus_user;
  }
  if (userPos < soem_limit_minus_user)
  {
    return soem_limit_minus_user;
  }
  return userPos;
}

static int32_t soem_clamp_hw_position(int32_t hwPos)
{
  if (soem_software_limits_enabled == 0U)
  {
    return hwPos;
  }

  if (hwPos > soem_limit_plus_hw)
  {
    return soem_limit_plus_hw;
  }
  if (hwPos < soem_limit_minus_hw)
  {
    return soem_limit_minus_hw;
  }
  return hwPos;
}

static int32_t soem_get_target_step_hw_per_cycle(void)
{
  int64_t velocityUser = (int64_t)((soem_profile_velocity < 0) ? -soem_profile_velocity : soem_profile_velocity);
  int64_t scale = (int64_t)((soem_unit_scale > 0) ? soem_unit_scale : 1);
  int64_t velocityHwPerSec = velocityUser * scale;
  int64_t stepHwPerCycle = velocityHwPerSec / 1000LL; /* 1 ms cycle */

  if (stepHwPerCycle < 1LL)
  {
    stepHwPerCycle = 1LL;
  }
  else if (stepHwPerCycle > (int64_t)INT32_MAX)
  {
    stepHwPerCycle = (int64_t)INT32_MAX;
  }

  return (int32_t)stepHwPerCycle;
}

static void soem_update_target_position_output(void)
{
  int32_t command = soem_target_position;
  int32_t output = soem_target_position_output;

  int64_t errorToActual = (int64_t)command - (int64_t)soem_shadow_position;
  if (errorToActual < 0)
  {
    errorToActual = -errorToActual;
  }

  if (errorToActual <= (int64_t)SOEM_TARGET_REACHED_TOLERANCE_HW)
  {
    soem_target_position = (int32_t)soem_shadow_position;
    soem_target_position_output = (int32_t)soem_shadow_position;
    return;
  }

  if (output == command)
  {
    return;
  }

  const int32_t step = soem_get_target_step_hw_per_cycle();
  const int64_t diff = (int64_t)command - (int64_t)output;

  if (diff > (int64_t)step)
  {
    output += step;
  }
  else if (diff < -(int64_t)step)
  {
    output -= step;
  }
  else
  {
    output = command;
  }

  soem_target_position_output = soem_clamp_hw_position(output);
}

static void soem_refresh_hw_limits(void)
{
  /* Treat zero-width or inverted ranges as disabled.
   * This avoids immediate servo faults when uninitialized UI parameters save
   * Limit+ = 0 and Limit- = 0, which would otherwise force the allowed range
   * to a single point at the origin. */
  if ((soem_limit_plus_user <= soem_limit_minus_user) ||
      ((soem_limit_plus_user == 0) && (soem_limit_minus_user == 0)))
  {
    soem_limit_plus_hw = INT32_MAX;
    soem_limit_minus_hw = INT32_MIN;
    soem_software_limits_enabled = 0U;
    soem_software_limits_blocked = 0U;
  }
  else
  {
    soem_software_limits_enabled = 1U;
    soem_software_limits_blocked = 0U;
    if (soem_limit_plus_user >= (INT32_MAX / 2))
    {
      soem_limit_plus_hw = INT32_MAX;
    }
    else
    {
      soem_limit_plus_hw = soem_saturated_user_to_hw(soem_limit_plus_user);
    }

    if (soem_limit_minus_user <= (INT32_MIN / 2))
    {
      soem_limit_minus_hw = INT32_MIN;
    }
    else
    {
      soem_limit_minus_hw = soem_saturated_user_to_hw(soem_limit_minus_user);
    }
  }

  if (soem_limit_minus_hw > soem_limit_plus_hw)
  {
    int32_t tmp = soem_limit_minus_hw;
    soem_limit_minus_hw = soem_limit_plus_hw;
    soem_limit_plus_hw = tmp;
  }

  if (soem_software_limits_enabled != 0U)
  {
    const int32_t actualHw = (int32_t)soem_shadow_position;
    const int32_t targetHw = soem_target_position;

    if ((actualHw < soem_limit_minus_hw) || (actualHw > soem_limit_plus_hw) ||
        (targetHw < soem_limit_minus_hw) || (targetHw > soem_limit_plus_hw))
    {
      soem_limit_plus_hw = INT32_MAX;
      soem_limit_minus_hw = INT32_MIN;
      soem_software_limits_enabled = 0U;
      soem_software_limits_blocked = 1U;
    }
  }

  soem_target_position = soem_clamp_hw_position(soem_target_position);
  soem_target_position_output = soem_clamp_hw_position(soem_target_position_output);
}

static void soem_apply_pending_motion_settings(void)
{
  if ((soem_profile_velocity_pending == 0U) &&
      (soem_torque_limit_pending == 0U) &&
      (soem_profile_acceleration_pending == 0U) &&
      (soem_profile_deceleration_pending == 0U) &&
      (soem_software_limits_pending == 0U) &&
      (soem_unit_scale_pending == 0U) &&
      (soem_home_offset_pending == 0U))
  {
    return;
  }

  if (soem_context.slavecount < 1)
  {
    return;
  }

  /* Avoid blocking SDO writes while in Operation Enabled.
   * Some drives fault when mailbox writes occur during active CSP motion. */
  if (soem_cia402_stage == SOEM_CIA402_STAGE_OPERATION_ENABLED)
  {
    return;
  }

  if (soem_profile_velocity_pending != 0U)
  {
    uint32 velocity = (uint32)soem_profile_velocity;
    int wkc = ecx_SDOwrite(&soem_context, 1, 0x6081, 0, FALSE, sizeof(velocity), &velocity, EC_TIMEOUTRXM);
    if (wkc > 0)
    {
      soem_profile_velocity_pending = 0U;
      soem_log("SOEM: profile velocity applied");
    }
    else
    {
      soem_profile_velocity_pending = 0U;
      soem_log("SOEM: profile velocity apply failed");
    }
  }

  if (soem_torque_limit_pending != 0U)
  {
    /* 0x6072 is per-mille (1000 = 100.0%). */
    uint16 torquePermille = (uint16)(soem_torque_limit_percent * 10U);
    int wkc = ecx_SDOwrite(&soem_context, 1, 0x6072, 0, FALSE, sizeof(torquePermille), &torquePermille, EC_TIMEOUTRXM);
    if (wkc > 0)
    {
      soem_torque_limit_pending = 0U;
      soem_log("SOEM: torque limit applied");
    }
    else
    {
      soem_torque_limit_pending = 0U;
      soem_log("SOEM: torque limit apply failed");
    }
  }

  if (soem_profile_acceleration_pending != 0U)
  {
    uint32 acceleration = (uint32)soem_profile_acceleration;
    int wkc = ecx_SDOwrite(&soem_context, 1, 0x6083, 0, FALSE, sizeof(acceleration), &acceleration, EC_TIMEOUTRXM);
    if (wkc > 0)
    {
      soem_profile_acceleration_pending = 0U;
      soem_log("SOEM: profile acceleration applied");
    }
    else
    {
      soem_profile_acceleration_pending = 0U;
      soem_log("SOEM: profile acceleration apply failed");
    }
  }

  if (soem_profile_deceleration_pending != 0U)
  {
    uint32 deceleration = (uint32)soem_profile_deceleration;
    int wkc = ecx_SDOwrite(&soem_context, 1, 0x6084, 0, FALSE, sizeof(deceleration), &deceleration, EC_TIMEOUTRXM);
    if (wkc > 0)
    {
      soem_profile_deceleration_pending = 0U;
      soem_log("SOEM: profile deceleration applied");
    }
    else
    {
      soem_profile_deceleration_pending = 0U;
      soem_log("SOEM: profile deceleration apply failed");
    }
  }

  if (soem_software_limits_pending != 0U)
  {
    if (soem_software_limits_enabled == 0U)
    {
      /* Explicitly clear drive-side stale software limits (0x607D)
       * when limits are disabled in the UI/model. Otherwise previously
       * written narrow limits may remain active in the drive and trigger
       * immediate fault on large ABS/INC commands. */
      int32_t minLimit = INT32_MIN;
      int32_t maxLimit = INT32_MAX;
      int wkcMin = ecx_SDOwrite(&soem_context, 1, 0x607D, 1, FALSE, sizeof(minLimit), &minLimit, EC_TIMEOUTRXM);
      int wkcMax = ecx_SDOwrite(&soem_context, 1, 0x607D, 2, FALSE, sizeof(maxLimit), &maxLimit, EC_TIMEOUTRXM);

      soem_software_limits_pending = 0U;
      if ((wkcMin > 0) && (wkcMax > 0))
      {
        if (soem_software_limits_blocked != 0U)
        {
          soem_log("SOEM: software limits disabled and cleared in drive (current actual/target outside configured range)");
        }
        else
        {
          soem_log("SOEM: software limits disabled and cleared in drive (invalid or zero-width range)");
        }
      }
      else
      {
        soem_log("SOEM: software limits clear failed");
      }
    }
    else
    {
      int32_t minLimit = soem_limit_minus_hw;
      int32_t maxLimit = soem_limit_plus_hw;
      int wkcMin = ecx_SDOwrite(&soem_context, 1, 0x607D, 1, FALSE, sizeof(minLimit), &minLimit, EC_TIMEOUTRXM);
      int wkcMax = ecx_SDOwrite(&soem_context, 1, 0x607D, 2, FALSE, sizeof(maxLimit), &maxLimit, EC_TIMEOUTRXM);
      if ((wkcMin > 0) && (wkcMax > 0))
      {
        soem_software_limits_pending = 0U;
        soem_log("SOEM: software limits applied");
      }
      else
      {
        soem_software_limits_pending = 0U;
        soem_log("SOEM: software limits apply failed");
      }
    }
  }

  if (soem_unit_scale_pending != 0U)
  {
    uint32 feedConst = (uint32)((soem_unit_scale > 0) ? soem_unit_scale : 1);
    uint32 shaftRevs = 1U;
    int wkcFeed = ecx_SDOwrite(&soem_context, 1, 0x6092, 1, FALSE, sizeof(feedConst), &feedConst, EC_TIMEOUTRXM);
    int wkcRevs = ecx_SDOwrite(&soem_context, 1, 0x6092, 2, FALSE, sizeof(shaftRevs), &shaftRevs, EC_TIMEOUTRXM);
    if ((wkcFeed > 0) && (wkcRevs > 0))
    {
      soem_unit_scale_pending = 0U;
      soem_log("SOEM: unit scale applied");
    }
    else
    {
      soem_unit_scale_pending = 0U;
      soem_log("SOEM: unit scale apply failed");
    }
  }

  if (soem_home_offset_pending != 0U)
  {
    int32_t homeOffset = soem_home_offset;
    int wkc = ecx_SDOwrite(&soem_context, 1, 0x607C, 0, FALSE, sizeof(homeOffset), &homeOffset, EC_TIMEOUTRXM);
    if (wkc > 0)
    {
      soem_home_offset_pending = 0U;
      soem_log("SOEM: home offset applied");
    }
    else
    {
      soem_home_offset_pending = 0U;
      soem_log("SOEM: home offset apply failed");
    }
  }
}

static void soem_update_pdo_pointers(void)
{
  ec_groupt *grp = soem_context.grouplist + soem_group;
  char line[96];
  soem_rxpdo = NULL;
  soem_txpdo = NULL;
  soem_pdo_ready = 0U;

  if ((grp->outputs != NULL) && (grp->Obytes >= (int)sizeof(soem_rxpdo_t)))
  {
    soem_rxpdo = (soem_rxpdo_t *)grp->outputs;
  }

  if ((grp->inputs != NULL) && (grp->Ibytes >= (int)sizeof(soem_txpdo_t)))
  {
    soem_txpdo = (soem_txpdo_t *)grp->inputs;
  }

  if ((soem_rxpdo != NULL) && (soem_txpdo != NULL))
  {
    soem_pdo_ready = 1U;
    (void)snprintf(line, sizeof(line), "SOEM: PDO ready Obytes=%lu Ibytes=%lu",
             (unsigned long)grp->Obytes, (unsigned long)grp->Ibytes);
    soem_log(line);
    (void)snprintf(line, sizeof(line), "SOEM: IOmap=0x%08lX O=0x%08lX I=0x%08lX",
             (unsigned long)(uintptr_t)soem_iomap,
             (unsigned long)(uintptr_t)grp->outputs,
             (unsigned long)(uintptr_t)grp->inputs);
    soem_log(line);
    soem_log("SOEM: PDO ready");
  }
  else
  {
    (void)snprintf(line, sizeof(line), "SOEM: PDO mismatch Obytes=%lu Ibytes=%lu",
             (unsigned long)grp->Obytes, (unsigned long)grp->Ibytes);
    soem_log(line);
    (void)snprintf(line, sizeof(line), "SOEM: IOmap=0x%08lX O=0x%08lX I=0x%08lX",
             (unsigned long)(uintptr_t)soem_iomap,
             (unsigned long)(uintptr_t)grp->outputs,
             (unsigned long)(uintptr_t)grp->inputs);
    soem_log(line);
    soem_log("SOEM: PDO size mismatch");
  }
}

static void soem_cia402_step(uint16 statusword)
{
  uint16 effective_statusword = statusword;
  uint16 controlword = soem_last_controlword;

  soem_update_target_position_output();

  /* If PDO statusword is still zero, skip CiA402 processing —
   * the slave hasn't started sending TxPDO yet.  Do NOT use SDO
   * fallback here because SDO frames inside the cyclic PDO loop
   * corrupt the ETH DMA state and cause HardFault. */
  if (statusword == 0U)
  {
    static uint16 zero_count = 0U;
    zero_count++;
    if ((zero_count % 10U) == 1U)
    {
      soem_log("CIA402: waiting for PDO statusword");
    }
    /* Keep sending controlword=0 (no action) while waiting. */
    soem_rxpdo->controlword = 0U;
    soem_rxpdo->target_position = soem_target_position_output;
    return;
  }

  /* RUN/STOP direct gate:
   * RUN=0 -> hold at Shutdown command (0x0006), never auto-progress to Enable Op.
   * RUN=1 -> allow full CiA402 sequence to Operation Enabled.
   */
  if (soem_shadow_run_enable == 0U)
  {
    controlword = 0x0006U;
    soem_cia402_stage = SOEM_CIA402_STAGE_WAIT_READY;
    soem_cia402_hold_counter = 0U;
    soem_cia402_timeout_counter = SOEM_CIA402_TIMEOUT_CYCLES;

    if (controlword != soem_last_controlword)
    {
      char line[64];
      soem_last_controlword = controlword;
      (void)snprintf(line, sizeof(line), "CIA402: CW=0x%04X", controlword);
      soem_log(line);
    }

    soem_rxpdo->controlword = controlword;
    soem_rxpdo->target_position = soem_target_position_output;
    return;
  }

  /* Fault bit set: send fault reset first. */
  if ((effective_statusword & 0x0008U) != 0U)
  {
    if (soem_fault_active == 0U)
    {
      soem_fault_active = 1U;
      soem_log_fault_diagnostics(effective_statusword);
    }

    /* Before clearing the fault, align CSP target to the current actual position.
     * If RUN stays logically ON during recovery, a stale target can immediately
     * retrigger a following/position fault after Operation Enabled is restored. */
    soem_latch_target_to_actual("CIA402: fault recovery -> latch target to actual");

    controlword = 0x0080U;
    soem_cia402_stage = SOEM_CIA402_STAGE_SEND_SHUTDOWN;
    soem_cia402_hold_counter = 0U;
    soem_cia402_timeout_counter = 0U;
    soem_log("CIA402: Fault reset (0x0080)");
  }
  else
  {
    soem_fault_active = 0U;

    switch (soem_cia402_stage)
    {
      case SOEM_CIA402_STAGE_SEND_SHUTDOWN:
        controlword = 0x0006U;
        soem_cia402_hold_counter = SOEM_CIA402_HOLD_CYCLES;
        soem_cia402_timeout_counter = SOEM_CIA402_TIMEOUT_CYCLES;
        soem_cia402_stage = SOEM_CIA402_STAGE_WAIT_READY;
        soem_log("CIA402: Shutdown (0x0006)");
        break;

      case SOEM_CIA402_STAGE_WAIT_READY:
        controlword = 0x0006U;
        if (soem_cia402_hold_counter > 0U)
        {
          soem_cia402_hold_counter--;
          break;
        }

        /* Accept L7NH-ready variants: 0x21 or 0x31 (masked to 0x21). */
        if ((effective_statusword & 0x006FU) == 0x0021U)
        {
          soem_cia402_stage = SOEM_CIA402_STAGE_SEND_SWITCH_ON;
        }
        else if (soem_cia402_timeout_counter > 0U)
        {
          soem_cia402_timeout_counter--;
        }
        else
        {
          soem_log("CIA402: Timeout waiting ready, retry shutdown");
          soem_cia402_stage = SOEM_CIA402_STAGE_SEND_SHUTDOWN;
        }
        break;

      case SOEM_CIA402_STAGE_SEND_SWITCH_ON:
        controlword = 0x0007U;
        soem_cia402_hold_counter = SOEM_CIA402_HOLD_CYCLES;
        soem_cia402_timeout_counter = SOEM_CIA402_TIMEOUT_CYCLES;
        soem_cia402_stage = SOEM_CIA402_STAGE_WAIT_SWITCHED_ON;
        soem_log("CIA402: Switch On (0x0007)");
        break;

      case SOEM_CIA402_STAGE_WAIT_SWITCHED_ON:
        controlword = 0x0007U;
        if (soem_cia402_hold_counter > 0U)
        {
          soem_cia402_hold_counter--;
          break;
        }

        /* Accept L7NH-switched-on variants: 0x23 or 0x33 (masked to 0x23). */
        if ((effective_statusword & 0x006FU) == 0x0023U)
        {
          soem_cia402_stage = SOEM_CIA402_STAGE_SEND_ENABLE_OP;
        }
        else if (soem_cia402_timeout_counter > 0U)
        {
          soem_cia402_timeout_counter--;
        }
        else
        {
          soem_log("CIA402: Timeout waiting switched-on, retry switch-on");
          soem_cia402_stage = SOEM_CIA402_STAGE_SEND_SWITCH_ON;
        }
        break;

      case SOEM_CIA402_STAGE_SEND_ENABLE_OP:
        controlword = 0x000FU;
        soem_cia402_hold_counter = SOEM_CIA402_HOLD_CYCLES;
        soem_cia402_timeout_counter = SOEM_CIA402_TIMEOUT_CYCLES;
        soem_cia402_stage = SOEM_CIA402_STAGE_WAIT_OPERATION_ENABLED;
        soem_log("CIA402: Enable Op (0x000F)");
        break;

      case SOEM_CIA402_STAGE_WAIT_OPERATION_ENABLED:
        controlword = 0x000FU;
        if (soem_cia402_hold_counter > 0U)
        {
          soem_cia402_hold_counter--;
          break;
        }

        /* Accept L7NH-op-enabled variants: 0x27 or 0x37 (masked to 0x27). */
        if ((effective_statusword & 0x006FU) == 0x0027U)
        {
          soem_cia402_stage = SOEM_CIA402_STAGE_OPERATION_ENABLED;
          soem_log("CIA402: Operation enabled confirmed");
        }
        else if (soem_cia402_timeout_counter > 0U)
        {
          soem_cia402_timeout_counter--;
        }
        else
        {
          soem_log("CIA402: Timeout waiting op-enabled, retry enable-op");
          soem_cia402_stage = SOEM_CIA402_STAGE_SEND_ENABLE_OP;
        }
        break;

      case SOEM_CIA402_STAGE_OPERATION_ENABLED:
      default:
        controlword = 0x000FU;
        break;
    }
  }

  if (controlword != soem_last_controlword)
  {
    char line[64];
    soem_last_controlword = controlword;
    (void)snprintf(line, sizeof(line), "CIA402: CTL=0x%04X", controlword);
    soem_log(line);
  }

  soem_rxpdo->controlword = controlword;
  soem_rxpdo->target_position = soem_target_position_output;
}

void SOEM_PortSetLog(void (*log_fn)(const char *msg))
{
  soem_log_fn = log_fn;
}

void SOEM_PortInit(void)
{
  if (soem_initialized != 0U)
  {
    return;
  }
  if (ecx_init(&soem_context, SOEM_IFNAME) > 0)
  {
    soem_initialized = 1U;
    soem_log("SOEM: init ok");
  }
  else
  {
    soem_log("SOEM: init failed");
  }
}

void SOEM_PortPoll(void)
{
  if (soem_initialized == 0U)
  {
    return;
  }
  if (soem_configured == 0U)
  {
    static uint8 config_phase = 0;  /* 0=init, 1=wait_safeop, 2=wait_op */
    static uint16 phase_counter = 0;

    switch (config_phase)
    {
      case 0: /* --- INIT: config + request SAFE_OP --- */
      {
        char log_line[128];
        int slavecount = ecx_config_init(&soem_context);
        if (slavecount <= 0)
        {
          soem_log("SOEM: no slaves");
          return;
        }

        /* Force INIT → PRE_OP transition (SDO/mailbox requires PRE_OP) */
        soem_log("SOEM: forcing PRE_OP");
        soem_context.slavelist[0].state = EC_STATE_INIT;
        ecx_writestate(&soem_context, 0);
        for (volatile int d = 0; d < 1000000; d++) { }

        soem_context.slavelist[0].state = EC_STATE_PRE_OP;
        ecx_writestate(&soem_context, 0);

        /* Wait for PRE_OP (poll up to ~2 seconds) */
        {
          int preop_wait = 0;
          while (preop_wait < 20)
          {
            for (volatile int d = 0; d < 1000000; d++) { }
            ecx_statecheck(&soem_context, 0, EC_STATE_PRE_OP, 100000);
            ecx_readstate(&soem_context);
            uint16 st = soem_context.slavelist[1].state & 0x0FU;
            if (st >= EC_STATE_PRE_OP)
            {
              (void)snprintf(log_line, sizeof(log_line),
                             "SOEM: PRE_OP reached (try %d) state=0x%02X",
                             preop_wait, soem_context.slavelist[1].state);
              soem_log(log_line);
              break;
            }
            preop_wait++;
          }
          if (preop_wait >= 20)
          {
            (void)snprintf(log_line, sizeof(log_line),
                           "SOEM: PRE_OP timeout state=0x%02X",
                           soem_context.slavelist[1].state);
            soem_log(log_line);
            return;
          }
        }

        /* ACK any error state so slave is in clean PRE_OP */
        if ((soem_context.slavelist[1].state & 0x10U) != 0U)
        {
          soem_log("SOEM: Error ACK");
          soem_context.slavelist[0].state = (EC_STATE_PRE_OP | EC_STATE_ERROR);
          ecx_writestate(&soem_context, 0);
          for (volatile int d = 0; d < 1000000; d++) { }
          soem_context.slavelist[0].state = EC_STATE_PRE_OP;
          ecx_writestate(&soem_context, 0);
          for (volatile int d = 0; d < 500000; d++) { }
        }

        /* Manual PDO mapping BEFORE ec_config_map (matches working Linux code) */
        soem_log("SOEM: manual PDO mapping");
        if (soem_apply_manual_pdo_mapping(1) == 0)
        {
          soem_log("SOEM: manual PDO mapping FAILED");
          return;
        }

        /* Now let SOEM read the updated PDO assignments and configure SM */
        soem_log("SOEM: config_map_group");
        ecx_config_map_group(&soem_context, soem_iomap, soem_group);

        soem_update_pdo_pointers();

        /* Dump SOEM's SM configuration for slave 1 */
        {
          ec_slavet *sl = &soem_context.slavelist[1];
          (void)snprintf(log_line, sizeof(log_line),
                         "SM2: addr=0x%04X len=%u flags=0x%08lX",
                         sl->SM[2].StartAddr, sl->SM[2].SMlength,
                         (unsigned long)sl->SM[2].SMflags);
          soem_log(log_line);
          (void)snprintf(log_line, sizeof(log_line),
                         "SM3: addr=0x%04X len=%u flags=0x%08lX",
                         sl->SM[3].StartAddr, sl->SM[3].SMlength,
                         (unsigned long)sl->SM[3].SMflags);
          soem_log(log_line);
          (void)snprintf(log_line, sizeof(log_line),
                         "SOEM: Obits=%u Ibits=%u Obytes=%lu Ibytes=%lu",
                         sl->Obits, sl->Ibits,
                         (unsigned long)sl->Obytes, (unsigned long)sl->Ibytes);
          soem_log(log_line);
        }

        /* Set operation mode to CSP (Cyclic Synchronous Position) */
        {
          int8 mode = 8;
          (void)ecx_SDOwrite(&soem_context, 1, 0x6060, 0, FALSE, sizeof(mode),
                             &mode, EC_TIMEOUTRXM);
          soem_log("CIA402: Mode=CSP(8)");
        }

        soem_log("SOEM: requesting SAFE_OP");
        soem_context.slavelist[0].state = EC_STATE_SAFE_OP;
        ecx_writestate(&soem_context, 0);

        config_phase = 1;
        phase_counter = 0;
        return;
      }

      case 1: /* --- WAIT SAFE_OP (non-blocking, one check per poll) --- */
      {
        (void)ecx_send_processdata(&soem_context);
        (void)ecx_receive_processdata(&soem_context, EC_TIMEOUTRET);

        ecx_readstate(&soem_context);
        uint16 sl_state = soem_context.slavelist[1].state & 0x0FU;
        uint16 sl_al = soem_context.slavelist[1].ALstatuscode;
        phase_counter++;

        if (sl_state >= EC_STATE_SAFE_OP)
        {
          char msg[96];
          (void)snprintf(msg, sizeof(msg),
                         "SOEM: SAFE_OP reached (cycle %u) AL=0x%04X",
                         phase_counter, sl_al);
          soem_log(msg);

          /* PDO burst + OP request + wait in tight loop */
          soem_log("SOEM: PDO burst + OP transition");
          for (int burst = 0; burst < 200; burst++)
          {
            (void)ecx_send_processdata(&soem_context);
            (void)ecx_receive_processdata(&soem_context, EC_TIMEOUTRET);
            for (volatile int d = 0; d < 100000; d++) { } /* ~1ms delay */

            /* Request OP after first 50 bursts */
            if (burst == 50)
            {
              soem_log("SOEM: requesting OPERATIONAL");
              soem_context.slavelist[0].state = EC_STATE_OPERATIONAL;
              ecx_writestate(&soem_context, 0);
            }

            /* Check if OP reached */
            if (burst > 50 && (burst % 10) == 0)
            {
              ecx_readstate(&soem_context);
              if (soem_context.slavelist[1].state == EC_STATE_OPERATIONAL)
              {
                soem_configured = 1U;
                soem_log("[SOEM] OPERATIONAL reached in burst");
                break;
              }
            }
          }

          if (soem_configured == 0U)
          {
            /* Fall through to phase 2 polling */
            ecx_readstate(&soem_context);
            char m2[96];
            (void)snprintf(m2, sizeof(m2),
                           "SOEM: burst done state=0x%02X AL=0x%04X",
                           soem_context.slavelist[1].state,
                           soem_context.slavelist[1].ALstatuscode);
            soem_log(m2);
          }

          config_phase = 2;
          phase_counter = 0;
        }
        else if ((phase_counter % 10U) == 0U)
        {
          char msg[96];
          (void)snprintf(msg, sizeof(msg),
                         "SOEM: waiting SAFE_OP (%u) state=0x%02X AL=0x%04X",
                         phase_counter, soem_context.slavelist[1].state, sl_al);
          soem_log(msg);

          /* Re-request SAFE_OP periodically */
          soem_context.slavelist[0].state = EC_STATE_SAFE_OP;
          ecx_writestate(&soem_context, 0);
        }

        if (phase_counter > 100U)
        {
          soem_log("SOEM: SAFE_OP timeout — restarting config");
          config_phase = 0;
          phase_counter = 0;
        }
        return;
      }

      case 2: /* --- WAIT OPERATIONAL (non-blocking) --- */
      {
        (void)ecx_send_processdata(&soem_context);
        (void)ecx_receive_processdata(&soem_context, EC_TIMEOUTRET);

        ecx_readstate(&soem_context);
        uint16 sl_state = soem_context.slavelist[1].state;
        phase_counter++;

        if (sl_state == EC_STATE_OPERATIONAL)
        {
          soem_configured = 1U;
          soem_log("[SOEM] Slave reached OPERATIONAL state");
        }
        else if ((phase_counter % 10U) == 0U)
        {
          char msg[96];
          uint16 sl_al = soem_context.slavelist[1].ALstatuscode;
          (void)snprintf(msg, sizeof(msg),
                         "SOEM: waiting OP (%u) state=0x%02X AL=0x%04X",
                         phase_counter, sl_state, sl_al);
          soem_log(msg);

          soem_context.slavelist[0].state = EC_STATE_OPERATIONAL;
          ecx_writestate(&soem_context, 0);
        }

        if (phase_counter > 100U)
        {
          soem_log("SOEM: OP timeout — restarting config");
          config_phase = 0;
          phase_counter = 0;
        }
        return;
      }

      default:
        config_phase = 0;
        return;
    }
  }

  /* IOmap is in non-cacheable RAM_CMD (0x2406C000) — no cache clean needed.
   * The DMA reads directly from physical RAM for this region. */

  (void)ecx_send_processdata(&soem_context);
  {
    int wkc = ecx_receive_processdata(&soem_context, EC_TIMEOUTRET);
    static int last_wkc = -1;
    if (wkc != last_wkc)
    {
      char line[48];
      last_wkc = wkc;
      (void)snprintf(line, sizeof(line), "SOEM: wkc=%d", wkc);
      soem_log(line);
    }
  }

  if (soem_pdo_ready != 0U)
  {
  #if SOEM_ENABLE_PERIODIC_PDO_DEBUG
    static uint32 pdo_debug_counter = 0;
  #endif
    uint16 statusword = soem_txpdo->statusword;

    /* Update shadow copies for UI access */
    soem_shadow_position   = soem_txpdo->position_actual;
    soem_shadow_velocity   = soem_txpdo->velocity_actual;
    soem_shadow_torque     = soem_txpdo->torque_actual;
    soem_shadow_statusword = statusword;
    soem_shadow_pdo_ready  = 1U;
    if (statusword != soem_last_statusword)
    {
      char line[64];
      soem_last_statusword = statusword;
      (void)snprintf(line, sizeof(line), "CIA402: SW=0x%04X", statusword);
      soem_log(line);
      {
        uint8 state = soem_decode_cia402_state(statusword);
        if (state != soem_last_state)
        {
          soem_last_state = state;
          soem_log_cia402_state(state);
        }
      }
    }

#if SOEM_ENABLE_PERIODIC_PDO_DEBUG
    /* Debug: periodic statusword check every 5000 cycles (~5s) */
    pdo_debug_counter++;
    if ((pdo_debug_counter % 5000U) == 0U)
    {
      char line[128];
      ec_groupt *grp = soem_context.grouplist + soem_group;
      uint8 *inp = (uint8 *)grp->inputs;
      int ilen = (grp->Ibytes > 20) ? 20 : (int)grp->Ibytes;
      int pos = snprintf(line, sizeof(line), "IN[%d]:", ilen);
      for (int i = 0; i < ilen && pos < 120; i++)
      {
        pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " %02X", inp[i]);
      }
      soem_log(line);
      (void)snprintf(line, sizeof(line), "PDO: SW=0x%04X CTL=0x%04X stg=%u cnt=%lu",
                     statusword, soem_rxpdo->controlword,
                     (unsigned)soem_cia402_stage, (unsigned long)pdo_debug_counter);
      soem_log(line);
    }
#endif

    soem_apply_pending_motion_settings();

    soem_cia402_step(statusword);
  }
}

/* ─── UI accessors (callable from TouchGFX task) ─────────────────────────── */

int32_t SOEM_GetPositionActual(void)  { return soem_hw_to_user((int32_t)soem_shadow_position); }
int32_t SOEM_GetVelocityActual(void)  { return (int32_t)soem_shadow_velocity; }
int16_t SOEM_GetTorqueActual(void)    { return (int16_t)soem_shadow_torque;   }
uint16_t SOEM_GetStatusword(void)     { return (uint16_t)soem_shadow_statusword; }
uint8_t  SOEM_GetPdoReady(void)       { return (uint8_t)soem_shadow_pdo_ready; }
uint8_t  SOEM_GetRunEnable(void)      { return (uint8_t)soem_shadow_run_enable; }

void SOEM_SetRunEnable(uint8_t enable)
{
  uint8_t requested = (enable != 0U) ? 1U : 0U;
  if (soem_shadow_run_enable != requested)
  {
    if ((soem_shadow_run_enable == 0U) && (requested != 0U))
    {
      /* Latch target to current actual position before enabling operation.
       * This prevents immediate following-error faults caused by stale target=0. */
      soem_latch_target_to_actual("CIA402: RUN ON -> latch target to actual");
    }

    soem_shadow_run_enable = requested;
    soem_log((requested != 0U) ? "CIA402: RUN request ON" : "CIA402: RUN request OFF");
  }
}

void SOEM_SetTargetPositionDelta(int32_t delta)
{
  int64_t nextUser = (int64_t)soem_hw_to_user(soem_target_position) + (int64_t)delta;
  if (nextUser > (int64_t)INT32_MAX)
  {
    nextUser = (int64_t)INT32_MAX;
  }
  else if (nextUser < (int64_t)INT32_MIN)
  {
    nextUser = (int64_t)INT32_MIN;
  }

  soem_target_position = soem_saturated_user_to_hw(soem_clamp_user_position((int32_t)nextUser));
  soem_target_position = soem_clamp_hw_position(soem_target_position);
}

void SOEM_SetTargetPositionAbs(int32_t pos)
{
#if SOEM_ENABLE_CMD_DEBUG_LOG
  char line[96];
#endif
  int32_t requestedUser = pos;
  soem_target_position = soem_saturated_user_to_hw(soem_clamp_user_position(pos));
  soem_target_position = soem_clamp_hw_position(soem_target_position);
#if SOEM_ENABLE_CMD_DEBUG_LOG
  (void)snprintf(line, sizeof(line), "CMD: set abs user=%ld hw=%ld out=%ld actual=%ld",
                 (long)requestedUser,
                 (long)soem_target_position,
                 (long)soem_target_position_output,
                 (long)soem_shadow_position);
  soem_log(line);
#else
  (void)requestedUser;
#endif
}

void SOEM_SetProfileVelocity(int32_t velocity)
{
  if (velocity < 0)
  {
    velocity = -velocity;
  }
  soem_profile_velocity = velocity;
  soem_profile_velocity_pending = 1U;
}

void SOEM_SetTorqueLimitPercent(uint16_t percent)
{
  if (percent > 100U)
  {
    percent = 100U;
  }
  soem_torque_limit_percent = percent;
  soem_torque_limit_pending = 1U;
}

void SOEM_SetProfileAcceleration(int32_t acceleration)
{
  if (acceleration < 0)
  {
    acceleration = -acceleration;
  }
  soem_profile_acceleration = acceleration;
  soem_profile_acceleration_pending = 1U;
}

void SOEM_SetProfileDeceleration(int32_t deceleration)
{
  if (deceleration < 0)
  {
    deceleration = -deceleration;
  }
  soem_profile_deceleration = deceleration;
  soem_profile_deceleration_pending = 1U;
}

void SOEM_SetSoftwareLimitPlus(int32_t limitPlus)
{
  soem_limit_plus_user = limitPlus;
  soem_refresh_hw_limits();
  soem_software_limits_pending = 1U;
}

void SOEM_SetSoftwareLimitMinus(int32_t limitMinus)
{
  soem_limit_minus_user = limitMinus;
  soem_refresh_hw_limits();
  soem_software_limits_pending = 1U;
}

void SOEM_SetUnitScale(int32_t scale)
{
  if (scale <= 0)
  {
    scale = 1;
  }
  soem_unit_scale = scale;
  soem_refresh_hw_limits();
  soem_unit_scale_pending = 1U;
}

void SOEM_SetHomeOffset(int32_t offset)
{
  int32_t scale = (soem_unit_scale > 0) ? soem_unit_scale : 1;
  int64_t hwOffset = (int64_t)offset * (int64_t)scale;
  if (hwOffset > (int64_t)INT32_MAX)
  {
    hwOffset = (int64_t)INT32_MAX;
  }
  else if (hwOffset < (int64_t)INT32_MIN)
  {
    hwOffset = (int64_t)INT32_MIN;
  }

  soem_home_offset = (int32_t)hwOffset;
  soem_refresh_hw_limits();
  soem_home_offset_pending = 1U;
}

void SOEM_SetHomePosition(void)
{
  /* Define the current hardware position as the new origin (0).
   * After this call, GetPositionActual() returns 0 and all subsequent
   * SetTargetPositionAbs() calls are relative to this new origin. */
  soem_home_offset = soem_shadow_position;
  soem_refresh_hw_limits();
  soem_home_offset_pending = 1U;
  /* Also reset the motion target to 0 in hardware space = stay put. */
  soem_target_position = soem_clamp_hw_position((int32_t)soem_shadow_position);
  soem_log("HOME: origin set to current position");
}

#endif
