#pragma once
#include <stdint.h>

// ── Packet type IDs ─────────────────────────────────────────────────────────
// ESP32 → Bridge
#define PROTO_PKT_STATUS         0x01U   // axis status (compat with STM32)
#define PROTO_PKT_PARAM_REPORT   0x02U
#define PROTO_PKT_ACK            0x03U
#define PROTO_PKT_LOG            0x04U

// Bridge → ESP32
#define PROTO_PKT_HOME           0x14U
#define PROTO_PKT_SET_PARAM      0x15U
#define PROTO_PKT_SAVE_FLASH     0x16U
#define PROTO_PKT_STOP           0x17U
#define PROTO_PKT_RUN_ENABLE     0x18U
#define PROTO_PKT_PARAM_READ_REQ 0x19U

// AGV packets
#define PROTO_PKT_AGV_VELOCITY   0x30U   // Bridge→ESP32: linear_mps + angular_rps
#define PROTO_PKT_AGV_ODOMETRY   0x31U   // ESP32→Bridge: wheel position/velocity
#define PROTO_PKT_AGV_STATUS     0x32U   // ESP32→Bridge: drive health

// I/O 확장 (Phase D)
#define PROTO_PKT_IO_SET         0x33U   // Bridge→ESP32: DO/PWM output
#define PROTO_PKT_IO_STATUS      0x34U   // ESP32→Bridge: DI/AI/DO/PWM state

// ── Result codes ────────────────────────────────────────────────────────────
#define PROTO_RESULT_OK          0x00U
#define PROTO_RESULT_BAD_PARAM   0x02U
#define PROTO_RESULT_BAD_PKT     0xFFU

// ── Parameter IDs ────────────────────────────────────────────────────────────
#define PROTO_PARAM_UNIT_SCALE   0x01U   // ERPM per (m/s) scale
#define PROTO_PARAM_HOME_OFFSET  0x02U   // encoder zero offset
#define PROTO_PARAM_PROFILE_VEL  0x07U   // not used (VESC), kept for compat

// ── Packed payload structs ──────────────────────────────────────────────────
#pragma pack(push, 1)

typedef struct {
    uint8_t seq;
    uint8_t result;
} ProtoPktAck_t;                // 2 bytes

typedef struct {
    float linear_mps;            // positive = forward
    float angular_rps;           // positive = CCW
} ProtoPktAgvVelocity_t;        // 8 bytes

typedef struct {
    int32_t  pos_left_hw;        // left  wheel tachometer (ERPM accumulated)
    int32_t  pos_right_hw;       // right wheel tachometer
    int32_t  vel_left_hw;        // left  ERPM current
    int32_t  vel_right_hw;       // right ERPM current
    uint32_t timestamp_ms;       // millis()
} ProtoPktAgvOdometry_t;        // 20 bytes

typedef struct {
    uint8_t all_ready;           // 1 if both VESCs responding
    uint8_t cia402_left;         // VESC state enum (repurposed field)
    uint8_t cia402_right;
    uint8_t flags;               // bit0=left_en, bit1=right_en
} ProtoPktAgvStatus_t;          // 4 bytes

typedef struct {
    uint8_t  axis;
    uint8_t  param_id;
    uint8_t  _pad[2];
    int32_t  value;
} ProtoPktSetParam_t;           // 8 bytes

typedef struct {
    int32_t  erpm_scale;         // ERPM per (m/s) — replaces unit_scale
    int32_t  home_offset;
    int32_t  _reserved[4];
    int32_t  profile_velocity;
    int32_t  _res2[5];
    int16_t  torque_limit;
    int32_t  position_gain;
    uint8_t  limits_enabled;
    uint8_t  limits_blocked;
} ProtoAxisParam_t;              // 44 bytes (wire-compat with STM32)

typedef struct {
    ProtoAxisParam_t axis[2];
} ProtoPktParamReport_t;        // 88 bytes

typedef struct {
    uint8_t  do_mask;
    uint8_t  do_val;
    uint8_t  pwm_ch;
    uint16_t pwm_duty;
} ProtoPktIoSet_t;              // 5 bytes

typedef struct {
    uint8_t  di_val;
    uint8_t  do_val;
    uint16_t ai_val[4];
    uint16_t pwm_duty[4];
} ProtoPktIoStatus_t;           // 18 bytes

#pragma pack(pop)

// ── Public API ───────────────────────────────────────────────────────────────
void AgvProto_Init(void);
void AgvProto_PollRx(void);
void AgvProto_SendOdometry(void);
void AgvProto_SendStatus(void);
void AgvProto_SendParamReport(void);
void AgvProto_SendLog(const char *msg);
void AgvProto_SendFrame(uint8_t pkt_type, const void *payload, uint16_t len);
