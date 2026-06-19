/**
 * @file    uart_protocol.c
 * @brief   6-axis articulated robot — binary UART protocol implementation
 *
 * SLIP encoding (RFC 1055)
 * ─────────────────────────
 *   END  = 0xC0  (frame boundary)
 *   ESC  = 0xDB
 *   ESC followed by 0xDC → literal 0xC0
 *   ESC followed by 0xDD → literal 0xDB
 *
 * CRC: CRC16-CCITT, poly 0x1021, init 0xFFFF, no final XOR.
 *   Covers [pkt_type] [seq] [payload...].
 *   Appended as 2 bytes little-endian AFTER the payload.
 *
 * RX pipeline
 * ────────────
 *   HAL_UART_RxCpltCallback → UartProto_FeedRxByte()
 *     → push to g_proto_rx_ring (interrupt-safe single-producer/single-consumer)
 *   DefaultTask → UartProto_PollRx()
 *     → SLIP decode from ring → complete frame → dispatch
 *
 * TX pipeline
 * ────────────
 *   UartProto_SendXxx() → proto_send_frame()
 *     → SLIP-encode into local stack buffer
 *     → call g_tx_fn() (provided callback → pushes to USART3 ring buffer)
 */

#include "uart_protocol.h"
#include "axis_config.h"     /* g_axis_param[], g_shadow[]   */
#include "soem_port.h"       /* SOEM_Set*(), SOEM_Get*()     */
#include "io_handler.h"      /* IO_DO_Set(), IO_PWM_Set() 등 */
#include "stm32h7xx_hal.h"   /* HAL_GetTick()                */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* g_flashSavePending is defined in main.c; trigger async flash save from here */
extern volatile uint8_t g_flashSavePending;

/* ── SLIP constants ──────────────────────────────────────────────────────── */
#define SLIP_END      0xC0U
#define SLIP_ESC      0xDBU
#define SLIP_ESC_END  0xDCU
#define SLIP_ESC_ESC  0xDDU

/* ── Module state ────────────────────────────────────────────────────────── */

static ProtoTxFn_t g_tx_fn = NULL;
static uint8_t     g_tx_seq = 0U;   /* auto-incrementing TX sequence counter */

/* ── RX ring buffer (filled by ISR, drained by DefaultTask) ─────────────── */

static volatile uint8_t  g_proto_rx_ring[PROTO_RX_RING_SIZE];
static volatile uint16_t g_proto_rx_head = 0U;   /* written by ISR   */
static volatile uint16_t g_proto_rx_tail = 0U;   /* read by PollRx   */

/* axis_active(): true if ax is within the number of slaves actually detected.
 * axis_valid() (from axis_types.h) only guards against array OOB (< AXIS_COUNT).
 * axis_active() is the correct gate for runtime commands from the PC. */
static uint8_t axis_active(uint8_t ax)
{
    uint8_t n = SOEM_GetActiveAxes();
    /* Before EtherCAT discovery completes, n==0; fall back to AXIS_COUNT so
     * boot-time tool use (e.g. manual console commands) still works. */
    if (n == 0U) n = (uint8_t)AXIS_COUNT;
    return (ax < n) ? 1U : 0U;
}

/* SLIP decoder state */
static uint8_t  g_slip_buf[PROTO_MAX_PAYLOAD + 8U]; /* raw decoded frame */
static uint16_t g_slip_len = 0U;
static uint8_t  g_slip_esc = 0U;
static uint8_t  g_slip_active = 0U;   /* inside a SLIP frame */

/* ── CRC16-CCITT ─────────────────────────────────────────────────────────── */

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t  bit;

    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U)
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            else
                crc = (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

/* ── SLIP encoder ────────────────────────────────────────────────────────── */

/* Encode 'in_len' raw bytes → SLIP frame in 'out'.
 * Returns total encoded length (including leading/trailing 0xC0).
 * Caller must ensure out[] is large enough: 2 + 2*in_len bytes worst case.
 */
static uint16_t slip_encode(const uint8_t *in, uint16_t in_len,
                            uint8_t *out, uint16_t out_max)
{
    uint16_t n = 0U;
    uint16_t i;

    if (n >= out_max) return 0U;
    out[n++] = SLIP_END;

    for (i = 0U; i < in_len; i++) {
        if (in[i] == (uint8_t)SLIP_END) {
            if (n + 2U > out_max) return 0U;
            out[n++] = SLIP_ESC;
            out[n++] = SLIP_ESC_END;
        } else if (in[i] == (uint8_t)SLIP_ESC) {
            if (n + 2U > out_max) return 0U;
            out[n++] = SLIP_ESC;
            out[n++] = SLIP_ESC_ESC;
        } else {
            if (n >= out_max) return 0U;
            out[n++] = in[i];
        }
    }

    if (n >= out_max) return 0U;
    out[n++] = SLIP_END;
    return n;
}

/* ── Generic frame builder ───────────────────────────────────────────────── */
/*
 * raw[]  = [pkt_type: 1B] [seq: 1B] [payload: payload_len B] [crc16: 2B LE]
 * Then SLIP-encode raw[] and pass to g_tx_fn.
 */
static void proto_send_frame(uint8_t pkt_type,
                             const void *payload, uint16_t payload_len)
{
    /* raw frame: type(1) + seq(1) + payload + crc(2) */
    uint8_t  raw[PROTO_MAX_PAYLOAD + 4U];
    uint8_t  enc[PROTO_MAX_PAYLOAD * 2U + 6U];   /* SLIP worst case */
    uint16_t raw_len;
    uint16_t enc_len;
    uint16_t crc;

    if (g_tx_fn == NULL) return;
    if (payload_len > PROTO_MAX_PAYLOAD) return;

    raw[0] = pkt_type;
    raw[1] = g_tx_seq++;

    if (payload_len > 0U && payload != NULL)
        memcpy(&raw[2], payload, payload_len);

    raw_len = (uint16_t)(2U + payload_len);

    crc = crc16_ccitt(raw, raw_len);
    raw[raw_len]     = (uint8_t)(crc & 0xFFU);         /* CRC low  byte */
    raw[raw_len + 1U] = (uint8_t)((crc >> 8U) & 0xFFU); /* CRC high byte */
    raw_len += 2U;

    enc_len = slip_encode(raw, raw_len, enc, (uint16_t)sizeof(enc));
    if (enc_len > 0U)
        g_tx_fn(enc, enc_len);
}

/* ── Command dispatcher ──────────────────────────────────────────────────── */

static void dispatch_ack(uint8_t seq, uint8_t result)
{
    ProtoPktAck_t ack;
    ack.seq    = seq;
    ack.result = result;
    proto_send_frame(PROTO_PKT_ACK, &ack, sizeof(ack));
}

static void dispatch_packet(const uint8_t *frame, uint16_t frame_len)
{
    uint16_t crc_recv;
    uint16_t crc_calc;
    uint8_t  pkt_type;
    uint8_t  seq;
    const uint8_t *payload;
    uint16_t payload_len;

    /* Minimum frame: type(1) + seq(1) + crc(2) = 4 bytes */
    if (frame_len < 4U) return;

    /* Verify CRC (covers all bytes except the trailing 2-byte CRC) */
    crc_calc = crc16_ccitt(frame, frame_len - 2U);
    crc_recv = (uint16_t)frame[frame_len - 2U]
             | ((uint16_t)frame[frame_len - 1U] << 8U);
    if (crc_calc != crc_recv) {
        dispatch_ack(frame[1], PROTO_RESULT_BAD_PKT);
        return;
    }

    pkt_type    = frame[0];
    seq         = frame[1];
    payload     = &frame[2];
    payload_len = (uint16_t)(frame_len - 4U);   /* subtract type + seq + crc */

    switch (pkt_type)
    {
    /* ── Home ───────────────────────────────────────────────────────── */
    case PROTO_PKT_HOME:
        if (payload_len == sizeof(ProtoPktHome_t)) {
            ProtoPktHome_t cmd;
            memcpy(&cmd, payload, sizeof(cmd));
            if (cmd.type == PROTO_HOME_SET_CURRENT) {
                if (cmd.axis == 0xFFU) {
                    uint8_t ax;
                    for (ax = 0U; ax < SOEM_GetActiveAxes(); ax++)
                        SOEM_SetHomePosition((AxisId_t)ax);
                } else if (axis_active(cmd.axis)) {
                    SOEM_SetHomePosition((AxisId_t)cmd.axis);
                }
                g_flashSavePending |= 0x01U;   /* trigger flash save */
                dispatch_ack(seq, PROTO_RESULT_OK);
            } else {
                dispatch_ack(seq, PROTO_RESULT_BAD_PARAM);
            }
        }
        break;

    /* ── Set axis parameter ─────────────────────────────────────────── */
    case PROTO_PKT_SET_PARAM:
        if (payload_len == sizeof(ProtoPktSetParam_t)) {
            ProtoPktSetParam_t cmd;
            memcpy(&cmd, payload, sizeof(cmd));
            if (!axis_active(cmd.axis)) { dispatch_ack(seq, PROTO_RESULT_BAD_PARAM); break; }
            uint8_t ax = cmd.axis;
            switch (cmd.param_id) {
                case PROTO_PARAM_UNIT_SCALE:
                    SOEM_SetUnitScale((AxisId_t)ax, cmd.value); break;
                case PROTO_PARAM_HOME_OFFSET:
                    SOEM_LoadHomeHwOffset((AxisId_t)ax, cmd.value); break;
                case PROTO_PARAM_LIMIT_PLUS_HW:
                    SOEM_SetLimitPlusHw((AxisId_t)ax, cmd.value); break;
                case PROTO_PARAM_LIMIT_MINUS_HW:
                    SOEM_SetLimitMinusHw((AxisId_t)ax, cmd.value); break;
                case PROTO_PARAM_LIMIT_PLUS_USER:
                    SOEM_SetLimitPlusUser((AxisId_t)ax, cmd.value); break;
                case PROTO_PARAM_LIMIT_MINUS_USER:
                    SOEM_SetLimitMinusUser((AxisId_t)ax, cmd.value); break;
                case PROTO_PARAM_LIMITS_ENABLED:
                    break;  /* derived flag — set automatically by limit_plus/minus_user */
                case PROTO_PARAM_PROFILE_VEL:
                    SOEM_SetProfileVelocity((AxisId_t)ax, cmd.value); break;
                case PROTO_PARAM_PROFILE_ACCEL_MS:
                    SOEM_SetProfileAccel((AxisId_t)ax, cmd.value); break;
                case PROTO_PARAM_PROFILE_DECEL_MS:
                    SOEM_SetProfileDecel((AxisId_t)ax, cmd.value); break;
                case PROTO_PARAM_TORQUE_LIMIT:
                    SOEM_SetTorqueLimit((AxisId_t)ax, (uint16_t)cmd.value); break;
                case PROTO_PARAM_POSITION_GAIN:
                    SOEM_SetPositionGain((AxisId_t)ax, cmd.value); break;
                case PROTO_PARAM_RAMP_VEL:
                    SOEM_SetRampVelocity((AxisId_t)ax, cmd.value); break;
                default:
                    dispatch_ack(seq, PROTO_RESULT_BAD_PARAM); return;
            }
            dispatch_ack(seq, PROTO_RESULT_OK);
        }
        break;

    /* ── Save flash ─────────────────────────────────────────────────── */
    case PROTO_PKT_SAVE_FLASH:
        g_flashSavePending |= 0x01U;
        dispatch_ack(seq, PROTO_RESULT_OK);
        break;

    /* ── Stop (E-stop: zero velocity, servo disable) ──────────────── */
    case PROTO_PKT_STOP:
        SOEM_SetAllRunEnable(0U);
        dispatch_ack(seq, PROTO_RESULT_OK);
        break;

    /* ── Run enable ─────────────────────────────────────────────────── */
    case PROTO_PKT_RUN_ENABLE:
        if (payload_len == sizeof(ProtoPktRunEnable_t)) {
            ProtoPktRunEnable_t cmd;
            memcpy(&cmd, payload, sizeof(cmd));
            if (cmd.axis == 0xFFU)
                SOEM_SetAllRunEnable(cmd.enable);
            else if (axis_active(cmd.axis))
                SOEM_SetRunEnable((AxisId_t)cmd.axis, cmd.enable);
            dispatch_ack(seq, PROTO_RESULT_OK);
        }
        break;

    /* ── Request param read ─────────────────────────────────────────── */
    case PROTO_PKT_PARAM_READ_REQ:
        UartProto_SendParamReport();
        dispatch_ack(seq, PROTO_RESULT_OK);
        break;

    /* ── Fault reset (force CiA402 re-enable sequence) ─────────────── */
    case PROTO_PKT_FAULT_RESET:
        if (payload_len == sizeof(ProtoPktFaultReset_t)) {
            ProtoPktFaultReset_t cmd;
            memcpy(&cmd, payload, sizeof(cmd));
            if (cmd.axis == 0xFFU) {
                SOEM_FaultReset(AXIS_ALL);
            } else if (axis_active(cmd.axis)) {
                SOEM_FaultReset((AxisId_t)cmd.axis);
            } else {
                dispatch_ack(seq, PROTO_RESULT_BAD_PARAM);
                break;
            }
            dispatch_ack(seq, PROTO_RESULT_OK);
        }
        break;

    /* ── AGV differential drive velocity command ────────────────────── */
    case PROTO_PKT_AGV_VELOCITY:
        if (payload_len == sizeof(ProtoPktAgvVelocity_t)) {
            ProtoPktAgvVelocity_t cmd;
            memcpy(&cmd, payload, sizeof(cmd));

            /* Differential drive kinematics: v_l = v - ω·b/2, v_r = v + ω·b/2 */
            float v_left  = cmd.linear_mps - (cmd.angular_rps * AGV_WHEEL_BASE_M * 0.5f);
            float v_right = cmd.linear_mps + (cmd.angular_rps * AGV_WHEEL_BASE_M * 0.5f);

            /* m/s → mm/s → HW counts/s using unit_scale [counts/mm] */
            float scale_l = (g_axis_param[AXIS_J1].unit_scale > 0)
                            ? (float)g_axis_param[AXIS_J1].unit_scale : 1.0f;
            float scale_r = (g_axis_param[AXIS_J2].unit_scale > 0)
                            ? (float)g_axis_param[AXIS_J2].unit_scale : 1.0f;

            int32_t vel_l_hw = (int32_t)(v_left  * 1000.0f * scale_l);
            int32_t vel_r_hw = (int32_t)(v_right * 1000.0f * scale_r);

            SOEM_SetTargetVelocity(AXIS_J1, vel_l_hw);
            SOEM_SetTargetVelocity(AXIS_J2, vel_r_hw);

            dispatch_ack(seq, PROTO_RESULT_OK);
        } else {
            dispatch_ack(seq, PROTO_RESULT_BAD_PARAM);
        }
        break;

    /* ── I/O 확장 출력 설정 ─────────────────────────────────────────────── */
    case PROTO_PKT_IO_SET:
        if (payload_len >= sizeof(ProtoPktIoSet_t)) {
            ProtoPktIoSet_t cmd;
            memcpy(&cmd, payload, sizeof(cmd));
            if (cmd.do_mask != 0U) {
                IO_DO_Set(cmd.do_mask, cmd.do_val);
            }
            if (cmd.pwm_ch < IO_PWM_COUNT) {
                IO_PWM_Set(cmd.pwm_ch, cmd.pwm_duty);
            }
            dispatch_ack(seq, PROTO_RESULT_OK);
        } else {
            dispatch_ack(seq, PROTO_RESULT_BAD_PARAM);
        }
        break;

    default:
        dispatch_ack(seq, PROTO_RESULT_BAD_PKT);
        break;
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void UartProto_Init(ProtoTxFn_t tx_fn)
{
    g_tx_fn        = tx_fn;
    g_tx_seq       = 0U;
    g_proto_rx_head = 0U;
    g_proto_rx_tail = 0U;
    g_slip_len     = 0U;
    g_slip_esc     = 0U;
    g_slip_active  = 0U;
}

void UartProto_FeedRxByte(uint8_t b)
{
    /* Single-producer (ISR), single-consumer (DefaultTask) ring — no lock needed */
    uint16_t next = (uint16_t)((g_proto_rx_head + 1U) % PROTO_RX_RING_SIZE);
    if (next != g_proto_rx_tail) {
        g_proto_rx_ring[g_proto_rx_head] = b;
        g_proto_rx_head = next;
    }
    /* Byte silently dropped if ring is full — bridge will detect missing STATUS */
}

void UartProto_PollRx(void)
{
    while (g_proto_rx_tail != g_proto_rx_head)
    {
        uint8_t b = g_proto_rx_ring[g_proto_rx_tail];
        g_proto_rx_tail = (uint16_t)((g_proto_rx_tail + 1U) % PROTO_RX_RING_SIZE);

        if (b == (uint8_t)SLIP_END) {
            if (g_slip_active && g_slip_len > 0U) {
                /* Complete frame received */
                dispatch_packet(g_slip_buf, g_slip_len);
            }
            /* Start fresh regardless */
            g_slip_len    = 0U;
            g_slip_esc    = 0U;
            g_slip_active = 1U;
            continue;
        }

        if (!g_slip_active) continue;   /* ignore bytes before first 0xC0 */

        if (g_slip_esc) {
            g_slip_esc = 0U;
            if (b == (uint8_t)SLIP_ESC_END)       b = SLIP_END;
            else if (b == (uint8_t)SLIP_ESC_ESC)  b = SLIP_ESC;
            /* Other ESC sequences are protocol errors — drop silently */
        } else if (b == (uint8_t)SLIP_ESC) {
            g_slip_esc = 1U;
            continue;
        }

        if (g_slip_len < sizeof(g_slip_buf)) {
            g_slip_buf[g_slip_len++] = b;
        } else {
            /* Frame too long — reset decoder */
            g_slip_len    = 0U;
            g_slip_active = 0U;
        }
    }
}

void UartProto_SendStatus(void)
{
    ProtoPktStatus_t pkt;
    uint8_t ax;

    for (ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++) {
        SOEM_GetStatusPkt((AxisId_t)ax, &pkt.axis[ax]);
    }

    pkt.interp_state = 0U;  /* AGV: no joint interpolator */
    pkt.sys_flags    = 0U;
    if (SOEM_AllAxesReady())       pkt.sys_flags |= 0x01U;
    if (SOEM_AllTargetsReached())  pkt.sys_flags |= 0x02U;
    /* bits 6-4: active slave count (0 = not yet discovered, 1-6 = detected) */
    pkt.sys_flags |= (uint8_t)((SOEM_GetActiveAxes() & 0x07U) << 4U);

    proto_send_frame(PROTO_PKT_STATUS, &pkt, sizeof(pkt));

    /* AGV: send wheel odometry and drive status alongside STATUS */
    UartProto_SendAgvOdometry();
    UartProto_SendAgvStatus();
}

void UartProto_SendParamReport(void)
{
    ProtoPktParamReport_t rpt;
    uint8_t ax;

    for (ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++) {
        rpt.axis[ax].unit_scale        = g_axis_param[ax].unit_scale;
        rpt.axis[ax].home_offset       = g_axis_param[ax].home_offset;
        rpt.axis[ax].limit_plus_hw     = g_axis_param[ax].limit_plus_hw;
        rpt.axis[ax].limit_minus_hw    = g_axis_param[ax].limit_minus_hw;
        rpt.axis[ax].limit_plus_user   = g_axis_param[ax].limit_plus_user;
        rpt.axis[ax].limit_minus_user  = g_axis_param[ax].limit_minus_user;
        rpt.axis[ax].profile_velocity  = g_axis_param[ax].profile_velocity;
        rpt.axis[ax].profile_accel_ms  = g_axis_param[ax].profile_accel_ms;
        rpt.axis[ax].profile_decel_ms  = g_axis_param[ax].profile_decel_ms;
        rpt.axis[ax].torque_limit      = (int16_t)g_axis_param[ax].torque_limit;
        rpt.axis[ax].position_gain     = g_axis_param[ax].position_gain;
        rpt.axis[ax].limits_enabled    = g_axis_param[ax].limits_enabled;
        rpt.axis[ax].limits_blocked    = g_axis_param[ax].limits_blocked;
    }

    proto_send_frame(PROTO_PKT_PARAM_REPORT, &rpt, sizeof(rpt));
}

void UartProto_SendLog(const char *msg)
{
    if (msg == NULL) return;
    uint16_t len = 0U;
    while (msg[len] != '\0' && len < (uint16_t)(PROTO_MAX_PAYLOAD - 1U))
        len++;
    if (len > 0U)
        proto_send_frame(PROTO_PKT_LOG, msg, len);
}

void UartProto_SendAgvOdometry(void)
{
    ProtoPktAgvOdometry_t pkt;
    pkt.pos_left_hw   = SOEM_GetPositionHw(AXIS_J1);
    pkt.pos_right_hw  = SOEM_GetPositionHw(AXIS_J2);
    pkt.vel_left_hw   = SOEM_GetVelocity(AXIS_J1);
    pkt.vel_right_hw  = SOEM_GetVelocity(AXIS_J2);
    pkt.timestamp_ms  = HAL_GetTick();
    proto_send_frame(PROTO_PKT_AGV_ODOMETRY, &pkt, sizeof(pkt));
}

void UartProto_SendAgvStatus(void)
{
    ProtoPktAgvStatus_t pkt;
    uint8_t n = SOEM_GetActiveAxes();
    pkt.all_ready    = (n >= 2U)
                       ? (SOEM_GetPdoReady(AXIS_J1) & SOEM_GetPdoReady(AXIS_J2))
                       : 0U;
    pkt.cia402_left  = SOEM_GetCia402State(AXIS_J1);
    pkt.cia402_right = SOEM_GetCia402State(AXIS_J2);
    pkt.flags        = (uint8_t)((SOEM_GetRunEnable(AXIS_J1) ? 0x01U : 0U) |
                                  (SOEM_GetRunEnable(AXIS_J2) ? 0x02U : 0U));
    proto_send_frame(PROTO_PKT_AGV_STATUS, &pkt, sizeof(pkt));
}

void UartProto_SendIoStatus(void)
{
    ProtoPktIoStatus_t pkt;
    pkt.di_val    = IO_DI_Get();
    pkt.do_val    = IO_DO_Get();
    for (uint8_t i = 0U; i < IO_AI_COUNT; i++)  { pkt.ai_val[i]   = IO_AI_Get(i);  }
    for (uint8_t i = 0U; i < IO_PWM_COUNT; i++) { pkt.pwm_duty[i] = IO_PWM_Get(i); }
    proto_send_frame(PROTO_PKT_IO_STATUS, &pkt, sizeof(pkt));
}
