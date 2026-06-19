#pragma once

// RS485 (MAX13487EESA+ — auto-direction half-duplex)
#define RS485_TX          22
#define RS485_RX          21
#define RS485_RE_PIN      17    // Receive Enable (HIGH=RX mode, LOW=TX mode)
#define RS485_SHUTDOWN_PIN 19   // Shutdown: HIGH=Enable RS485, LOW=Shutdown

// CAN (SN65HVD231 transceiver — TWAI controller)
#define CAN_TX            27
#define CAN_RX            26
#define CAN_SPEED_MODE    23    // LOW=500kbps (high speed), HIGH=250kbps

// RS485 + CAN Boost power supply (ME2107 step-up)
#define ME2107_EN         16    // HIGH=power enabled — MUST be HIGH at startup

// WS2812B status LED
#define WS2812B_DATA       4

// SD card
#define SD_MISO            2
#define SD_MOSI           15
#define SD_SCLK           14
#define SD_CS             13
