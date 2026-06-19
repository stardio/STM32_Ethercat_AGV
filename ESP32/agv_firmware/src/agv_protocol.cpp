#include "agv_protocol.h"
#include "rs485_slip.h"
#include "vesc_can.h"
#include "odometry.h"
#include "nvs_params.h"
#include <Arduino.h>
#include <string.h>

static uint8_t _tx_seq = 0;
static bool    _run_enabled = false;

// ── TX helper ────────────────────────────────────────────────────────────────

void AgvProto_SendFrame(uint8_t pkt_type, const void *payload, uint16_t len)
{
    static uint8_t raw[512];
    if (len > sizeof(raw) - 4) return;

    raw[0] = pkt_type;
    raw[1] = _tx_seq++;
    if (len > 0 && payload) memcpy(&raw[2], payload, len);

    uint16_t raw_len = 2 + len;
    uint16_t crc     = Crc16Ccitt(raw, raw_len);
    raw[raw_len]     = crc & 0xFF;
    raw[raw_len + 1] = (crc >> 8) & 0xFF;
    raw_len += 2;

    Slip_SendFrame(raw, raw_len);
}

static void _send_ack(uint8_t seq, uint8_t result)
{
    ProtoPktAck_t ack = {seq, result};
    AgvProto_SendFrame(PROTO_PKT_ACK, &ack, sizeof(ack));
}

// ── Packet dispatcher ─────────────────────────────────────────────────────────

static void _dispatch(const uint8_t *frame, uint16_t frame_len)
{
    if (frame_len < 4) return;

    uint16_t crc_calc = Crc16Ccitt(frame, frame_len - 2);
    uint16_t crc_recv = (uint16_t)frame[frame_len - 2] | ((uint16_t)frame[frame_len - 1] << 8);
    if (crc_calc != crc_recv) { _send_ack(frame[1], PROTO_RESULT_BAD_PKT); return; }

    uint8_t  pkt_type   = frame[0];
    uint8_t  seq        = frame[1];
    const uint8_t *pay  = &frame[2];
    uint16_t pay_len    = frame_len - 4;

    switch (pkt_type) {

    case PROTO_PKT_AGV_VELOCITY:
        if (pay_len == sizeof(ProtoPktAgvVelocity_t)) {
            ProtoPktAgvVelocity_t cmd;
            memcpy(&cmd, pay, sizeof(cmd));

            AgvParams_t *p = Params_Get();
            float v_left  = cmd.linear_mps - cmd.angular_rps * p->wheel_base_m * 0.5f;
            float v_right = cmd.linear_mps + cmd.angular_rps * p->wheel_base_m * 0.5f;

            int32_t erpm_l = (int32_t)(v_left  * (float)p->erpm_scale * p->erpm_dir_left);
            int32_t erpm_r = (int32_t)(v_right * (float)p->erpm_scale_r * p->erpm_dir_right);

            if (_run_enabled) {
                Vesc_SetErpm(p->can_id_left,  erpm_l);
                Vesc_SetErpm(p->can_id_right, erpm_r);
            }
            _send_ack(seq, PROTO_RESULT_OK);
        } else {
            _send_ack(seq, PROTO_RESULT_BAD_PARAM);
        }
        break;

    case PROTO_PKT_RUN_ENABLE:
        if (pay_len >= 2) {
            uint8_t enable = pay[1];
            _run_enabled = (enable != 0);
            if (!_run_enabled) Vesc_Stop();
            _send_ack(seq, PROTO_RESULT_OK);
        }
        break;

    case PROTO_PKT_STOP:
        _run_enabled = false;
        Vesc_Stop();
        _send_ack(seq, PROTO_RESULT_OK);
        break;

    case PROTO_PKT_HOME:
        Odom_Reset();
        _send_ack(seq, PROTO_RESULT_OK);
        break;

    case PROTO_PKT_PARAM_READ_REQ:
        AgvProto_SendParamReport();
        _send_ack(seq, PROTO_RESULT_OK);
        break;

    case PROTO_PKT_SET_PARAM:
        if (pay_len == sizeof(ProtoPktSetParam_t)) {
            ProtoPktSetParam_t cmd;
            memcpy(&cmd, pay, sizeof(cmd));
            AgvParams_t *p = Params_Get();
            if (cmd.param_id == PROTO_PARAM_UNIT_SCALE) {
                p->erpm_scale = p->erpm_scale_r = cmd.value;
            } else if (cmd.param_id == PROTO_PARAM_HOME_OFFSET) {
                Odom_Reset();
            }
            _send_ack(seq, PROTO_RESULT_OK);
        }
        break;

    case PROTO_PKT_SAVE_FLASH:
        Params_Save();
        _send_ack(seq, PROTO_RESULT_OK);
        break;

    default:
        _send_ack(seq, PROTO_RESULT_BAD_PKT);
        break;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void AgvProto_Init(void) { _tx_seq = 0; _run_enabled = false; }

void AgvProto_PollRx(void)
{
    static uint8_t frame[512];
    uint16_t len = Slip_PollRx(frame, sizeof(frame));
    if (len > 0) _dispatch(frame, len);
}

void AgvProto_SendOdometry(void)
{
    const OdomState_t *o = Odom_Get();
    AgvParams_t *p = Params_Get();

    // Pack odometry: pos as ERPM-tachometer, vel as ERPM
    ProtoPktAgvOdometry_t pkt;
    pkt.pos_left_hw  = o->tach_left;
    pkt.pos_right_hw = o->tach_right;
    // vel in ERPM-equivalent for PC-side odom calculation
    pkt.vel_left_hw  = (int32_t)(o->v_left  * (float)p->erpm_scale);
    pkt.vel_right_hw = (int32_t)(o->v_right * (float)p->erpm_scale_r);
    pkt.timestamp_ms = millis();
    AgvProto_SendFrame(PROTO_PKT_AGV_ODOMETRY, &pkt, sizeof(pkt));
}

void AgvProto_SendStatus(void)
{
    ProtoPktAgvStatus_t pkt;
    AgvParams_t *p = Params_Get();
    bool alive_l = Vesc_IsAlive(p->can_id_left);
    bool alive_r = Vesc_IsAlive(p->can_id_right);
    pkt.all_ready    = (alive_l && alive_r) ? 1 : 0;
    pkt.cia402_left  = alive_l ? 8 : 0;   // 8 = OPERATION_ENABLED (CiA402 compat)
    pkt.cia402_right = alive_r ? 8 : 0;
    pkt.flags        = (_run_enabled ? 0x03 : 0x00);
    AgvProto_SendFrame(PROTO_PKT_AGV_STATUS, &pkt, sizeof(pkt));
}

void AgvProto_SendParamReport(void)
{
    AgvParams_t *p = Params_Get();
    ProtoPktParamReport_t rpt;
    memset(&rpt, 0, sizeof(rpt));
    // axis[0] = left (J1), axis[1] = right (J2)
    rpt.axis[0].erpm_scale      = p->erpm_scale;
    rpt.axis[1].erpm_scale      = p->erpm_scale_r;
    AgvProto_SendFrame(PROTO_PKT_PARAM_REPORT, &rpt, sizeof(rpt));
}

void AgvProto_SendLog(const char *msg)
{
    if (!msg) return;
    uint16_t len = strnlen(msg, 255);
    AgvProto_SendFrame(PROTO_PKT_LOG, msg, len);
}
