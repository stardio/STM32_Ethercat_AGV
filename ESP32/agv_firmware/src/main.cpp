/**
 * AGV ESP32 Firmware  —  T-CAN485 (LILYGO)
 *
 * RS485 (SLIP+CRC16) ←→ Mini PC bridge.py  (921600 baud)
 * CAN  (VESC protocol) ←→ VESC 6 × 2       (500 kbps)
 *
 * Tasks:
 *   vesc_rx_task  : CAN RX polling  (5ms)
 *   odom_task     : odometry update (10ms)
 *   pub_task      : RS485 TX odom+status (100ms)
 *   proto_rx_task : RS485 RX dispatch   (1ms)
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pin_config.h"
#include "rs485_slip.h"
#include "vesc_can.h"
#include "odometry.h"
#include "nvs_params.h"
#include "agv_protocol.h"

// ── Task handles ─────────────────────────────────────────────────────────────
static TaskHandle_t hVescRx  = NULL;
static TaskHandle_t hOdom    = NULL;
static TaskHandle_t hPub     = NULL;
static TaskHandle_t hProtoRx = NULL;

// ── VESC CAN RX Task (Core 0, 5ms) ──────────────────────────────────────────
static void vesc_rx_task(void *arg)
{
    for (;;) {
        Vesc_PollRx();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ── Odometry Update Task (Core 0, 10ms) ──────────────────────────────────────
static void odom_task(void *arg)
{
    for (;;) {
        const VescStatus_t *sl = Vesc_GetStatus(VESC_ID_LEFT);
        const VescStatus_t *sr = Vesc_GetStatus(VESC_ID_RIGHT);
        // Accumulate tachometer from ERPM (approximate integration)
        // For accurate tach, enable VESC STATUS_5 which provides tachometer value
        static int32_t tach_l = 0, tach_r = 0;
        tach_l += (int32_t)(sl->erpm * 0.01f);  // dt=10ms integral estimate
        tach_r += (int32_t)(sr->erpm * 0.01f);
        Odom_Update(tach_l, tach_r, millis());
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── RS485 Publish Task (Core 1, 100ms) ───────────────────────────────────────
static void pub_task(void *arg)
{
    AgvParams_t *p = Params_Get();
    for (;;) {
        AgvProto_SendOdometry();
        AgvProto_SendStatus();
        vTaskDelay(pdMS_TO_TICKS(p->odom_pub_ms));
    }
}

// ── RS485 RX Dispatch Task (Core 1, 1ms) ────────────────────────────────────
static void proto_rx_task(void *arg)
{
    for (;;) {
        AgvProto_PollRx();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ── Arduino setup ────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    Serial.println("[AGV] Booting...");

    // Load parameters from NVS
    Params_Init();
    AgvParams_t *p = Params_Get();
    Serial.printf("[AGV] wb=%.3fm, wr=%.3fm, pole=%d, erpm_scale=%d\n",
                  p->wheel_base_m, p->wheel_radius_m, p->pole_pairs, p->erpm_scale);

    // Init subsystems
    Slip_Init(921600);
    Serial.println("[AGV] RS485 SLIP ready");

    Vesc_Init();
    Serial.println("[AGV] CAN/VESC ready");

    Odom_Init();
    AgvProto_Init();

    // FreeRTOS tasks
    // Core 0: CAN-heavy tasks
    xTaskCreatePinnedToCore(vesc_rx_task, "vesc_rx", 4096, NULL, 5, &hVescRx, 0);
    xTaskCreatePinnedToCore(odom_task,    "odom",    4096, NULL, 4, &hOdom,   0);

    // Core 1: RS485 communication
    xTaskCreatePinnedToCore(pub_task,     "pub",     4096, NULL, 3, &hPub,    1);
    xTaskCreatePinnedToCore(proto_rx_task,"proto_rx",4096, NULL, 5, &hProtoRx,1);

    AgvProto_SendLog("AGV ESP32 ready");
    Serial.println("[AGV] All tasks started");
}

// loop() is intentionally empty — all work is in FreeRTOS tasks
void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
