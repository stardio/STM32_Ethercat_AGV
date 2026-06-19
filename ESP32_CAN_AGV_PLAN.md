# ESP32-S3 + CAN + BLDC 모터 AGV 마이그레이션 계획서

작성일: 2026-06-19  
작성자: stardio

---

## 1. 개요

### 1.1 변경 배경

기존 STM32H753ZI + EtherCAT 서보드라이브 구성에서 ESP32-S3 + CAN 버스 + BLDC 모터 구성으로 전환한다.

**변경 이유**:
- EtherCAT 서보드라이브는 가격이 높고 산업용 AC 서보모터 의존성이 강함
- BLDC 모터는 경량·저비용으로 AGV 구동에 적합
- ESP32-S3는 WiFi/BT 내장, TWAI(CAN) 하드웨어 지원, 저비용
- RS485 채널로 기존 PC 브릿지 프로토콜을 그대로 재사용 가능

### 1.2 시스템 구성 비교

```
[기존 시스템]
Mini PC (Jetson Orin Nano / Ubuntu)
  └─ USB-UART ─── STM32H753ZI ─── EtherCAT (1ms) ─── LS L7NH 서보드라이브 × 2 ─── AC 서보모터

[신규 시스템]
Mini PC (Jetson Orin Nano / Ubuntu)
  └─ USB-RS485 ── ESP32-S3 ──────── CAN 2.0B ──────── VESC 6 × 2 ────────────── BLDC 모터 × 2
```

---

## 2. 하드웨어 구성

### 2.1 구성 요소

| 구성 요소 | 모델 | 수량 | 비고 |
|----------|------|------|------|
| MCU 보드 | ESP32-S3 (RS485 + CAN 탑재 보드) | 1 | TWAI 컨트롤러 내장 |
| BLDC 컨트롤러 | VESC 6 (또는 VESC 6 MkVI) | 2 | 좌·우 바퀴 각 1개 |
| CAN 트랜시버 | TJA1050 또는 SN65HVD230 | 1~2 | ESP32 보드 미탑재 시 |
| RS485 어댑터 | USB-RS485 (CP2102N 또는 CH340) | 1 | Mini PC 측 |
| BLDC 모터 | Hall 센서 내장 BLDC | 2 | 사양 별도 결정 |
| 종단 저항 | 120Ω | 2 | CAN 버스 양 끝 |
| 전원 | 배터리 + DC/DC (24V 또는 36V) | 1 set | VESC 입력 전압에 맞춤 |

### 2.2 시스템 배선도

```
┌───────────────────────────────────────────────────────────┐
│               Mini PC (Jetson Orin Nano)                  │
│   bridge.py ←→ /dev/rs485 (USB-RS485 어댑터)              │
│   ROS2 / Nav2 / RTAB-Map / YOLOv8 (변경 없음)             │
└──────────────────────┬────────────────────────────────────┘
                       │ RS485 (3선: A / B / GND)
                       │ 921600 baud, SLIP+CRC16-CCITT
                       │ (최대 100m, 노이즈 강인)
┌──────────────────────┴────────────────────────────────────┐
│                  ESP32-S3 (CAN 마스터)                     │
│                                                           │
│  ┌────────────────┐    ┌──────────────────────────────┐   │
│  │ MAX485 트랜시버 │    │ TJA1050 CAN 트랜시버          │   │
│  │ IO17: TX       │    │ IO4: TWAI TX                 │   │
│  │ IO18: RX       │    │ IO5: TWAI RX                 │   │
│  │ IO19: DE/RE    │    └──────────────┬───────────────┘   │
│  └────────────────┘                   │                   │
└──────────────────────────────────────┼───────────────────┘
                                       │ CAN 버스 (H/L/GND)
                    ┌──────────────────┴──────────────────┐
              [120Ω]│                                     │[120Ω]
            ┌───────┴────────┐                  ┌─────────┴──────┐
            │    VESC 6      │                  │    VESC 6      │
            │  CAN Node ID:1 │                  │  CAN Node ID:2 │
            │  (좌 바퀴)      │                  │  (우 바퀴)      │
            └───────┬────────┘                  └─────────┬──────┘
                    │ 3상 + Hall 5핀                        │ 3상 + Hall 5핀
              BLDC 모터 (좌)                          BLDC 모터 (우)
```

### 2.3 ESP32-S3 핀 배정

| GPIO | 기능 | 연결 대상 |
|------|------|---------|
| IO17 | UART1 TX | MAX485 DI |
| IO18 | UART1 RX | MAX485 RO |
| IO19 | GPIO OUT | MAX485 DE/RE (송수신 방향 제어) |
| IO4  | TWAI TX | TJA1050 TXD |
| IO5  | TWAI RX | TJA1050 RXD |
| 3.3V / GND | 전원 | 트랜시버 VCC/GND |

> ESP32-S3 보드에 RS485·CAN 트랜시버가 이미 탑재된 경우 핀 번호는 보드 스펙 확인

### 2.4 VESC 설정 (VESC Tool)

1. **모터 설정**: BLDC / FOC 선택, Hall 센서 감지 자동 실행
2. **CAN 버스**: CAN 속도 500kbps (또는 1Mbps), Node ID 좌=1 / 우=2
3. **제어 모드**: ERPM (전기 RPM) 속도 제어
4. **전류 제한**: 모터 사양에 맞게 설정
5. **방향**: 좌 바퀴 정방향 확인, 우 바퀴 필요 시 반전

---

## 3. 소프트웨어 재사용 분석

### 3.1 PC 측 (재사용 가능 ✅)

| 파일 | 재사용 여부 | 필요 변경 |
|------|------------|---------|
| `PC_GUI/bridge/bridge.py` | ✅ 그대로 사용 | 포트 경로 `/dev/rs485` udev 설정 |
| `PC_GUI/bridge/index.html` | ✅ 그대로 사용 | 없음 (프로토콜 동일) |
| `PC_GUI/bridge/packet_defs.py` | ✅ 그대로 사용 | 없음 |
| `PC_GUI/bridge/slip_codec.py` | ✅ 그대로 사용 | 없음 |
| `PC_GUI/bridge/json_interpreter.py` | ✅ 그대로 사용 | 없음 |
| `ros2_ws/` 전체 | ✅ 그대로 사용 | `stm32_bridge_node.py` 파일명 변경 권장 |
| ROS2 Nav2 / RTAB-Map | ✅ 그대로 사용 | 없음 |
| `agv_bringup/row_follower.py` | ✅ 그대로 사용 | 없음 |
| `agv_bringup/obstacle_classifier.py` | ✅ 그대로 사용 | 없음 |
| `agv_bringup/task_scheduler.py` | ✅ 그대로 사용 | 없음 |
| GPS 스택 전체 | ✅ 그대로 사용 | 없음 |

**재사용 핵심 원칙**: AGV UART 패킷 프로토콜(0x30~0x34)을 ESP32-S3 펌웨어에서 동일하게 구현하면 PC 전체 소프트웨어는 수정 없이 재사용된다.

### 3.2 STM32 펌웨어 (재작성 필요 ❌)

| 모듈 | 처리 방법 |
|------|---------|
| `soem_port.c` (EtherCAT) | 삭제 → VESC CAN 드라이버로 교체 |
| `uart_protocol.c` | 로직 재사용, ESP32 UART로 포팅 |
| `axis_config.c` | 로직 재사용, NVS 파라미터로 교체 |
| `ui_flash_storage.c` | ESP32 NVS (Non-Volatile Storage)로 교체 |
| `main.c` FreeRTOS 태스크 | ESP32 FreeRTOS로 재작성 |

---

## 4. ESP32-S3 펌웨어 설계

### 4.1 태스크 구조

```
[RS485 수신 태스크] — 우선순위 높음
  SLIP 디코딩 → CRC16-CCITT 검증 → 패킷 디스패치
    0x30 AGV_VELOCITY → 운동학 계산 → CAN ERPM 명령

[CAN 수신 태스크] — 1ms~5ms
  VESC STATUS 프레임 수신 (COMM_GET_VALUES 응답)
  ERPM → 오도메트리 적분 (m/s, rad)

[주기 송신 태스크] — 200ms
  0x31 AGV_ODOMETRY → RS485 → bridge.py
  0x32 AGV_STATUS   → RS485 → bridge.py
  0x34 IO_STATUS    → RS485 → bridge.py  (Phase D)

[I/O 태스크] — 필요 시
  GPIO DO/DI, ADC AI, LEDC PWM
  0x33 IO_SET 수신 처리
```

### 4.2 운동학 계산 (기존 동일)

```c
// AGV_VELOCITY(0x30) 수신 시
float v_left  = linear_mps - angular_rps * WHEEL_BASE_M / 2.0f;
float v_right = linear_mps + angular_rps * WHEEL_BASE_M / 2.0f;

// BLDC ERPM 변환 (VESC)
// ERPM = (m/s) / (wheel_radius * 2 * PI) * 60 * pole_pairs
int32_t erpm_left  = (int32_t)(v_left  / WHEEL_CIRCUMFERENCE_M * 60.0f * POLE_PAIRS);
int32_t erpm_right = (int32_t)(v_right / WHEEL_CIRCUMFERENCE_M * 60.0f * POLE_PAIRS);

vesc_set_erpm(CAN_ID_LEFT,  erpm_left);
vesc_set_erpm(CAN_ID_RIGHT, erpm_right);
```

### 4.3 VESC CAN 프로토콜 (주요 프레임)

| CAN ID (11bit) | 방향 | 설명 |
|----------------|------|------|
| `0x000 + NodeID` | ESP32→VESC | COMM_SET_DUTY (듀티 제어) |
| `0x300 + NodeID` | ESP32→VESC | COMM_SET_ERPM (속도 제어) ← **사용** |
| `0x400 + NodeID` | VESC→ESP32 | STATUS_1 (ERPM, 전류, 듀티) ← **수신** |
| `0x500 + NodeID` | VESC→ESP32 | STATUS_4 (위치, 온도) |

### 4.4 오도메트리 적분

```c
// VESC STATUS_1 수신 시 (5ms 주기 가정)
float erpm_l = vesc_left.erpm;
float erpm_r = vesc_right.erpm;

// ERPM → m/s
float v_l = erpm_l / POLE_PAIRS / 60.0f * WHEEL_CIRCUMFERENCE_M;
float v_r = erpm_r / POLE_PAIRS / 60.0f * WHEEL_CIRCUMFERENCE_M;

// 위치 적분 (dt = 0.005s)
float dL = v_l * dt;
float dR = v_r * dt;
float dC = (dL + dR) / 2.0f;
float dT = (dR - dL) / WHEEL_BASE_M;

odom.x     += dC * cosf(odom.theta + dT / 2.0f);
odom.y     += dC * sinf(odom.theta + dT / 2.0f);
odom.theta += dT;
```

### 4.5 파라미터 저장 (ESP32 NVS)

기존 Flash 파라미터(unit_scale, wheel_base, home_origin 등)를 ESP32 NVS로 이전한다.

```c
// NVS 네임스페이스: "agv_params"
nvs_set_i32(handle, "unit_scale_j1", unit_scale_j1);
nvs_set_i32(handle, "unit_scale_j2", unit_scale_j2);
nvs_set_f32(handle, "wheel_base",    wheel_base_m);
nvs_commit(handle);
```

---

## 5. AGV 패킷 프로토콜 (변경 없음)

| ID | 방향 | 이름 | 페이로드 |
|----|------|------|---------|
| 0x30 | Mini PC→ESP32 | AGV_VELOCITY | linear_mps(f32) + angular_rps(f32) = 8B |
| 0x31 | ESP32→Mini PC | AGV_ODOMETRY | pos_L(i32) + pos_R(i32) + vel_L(i32) + vel_R(i32) = 16B |
| 0x32 | ESP32→Mini PC | AGV_STATUS | drive_state(u8) × 2 + flags(u8) = 3B |
| 0x33 | Mini PC→ESP32 | IO_SET | do_mask(u8) + do_val(u8) + pwm_ch(u8) + pwm_duty(u16) = 5B |
| 0x34 | ESP32→Mini PC | IO_STATUS | DI(u8) + DO(u8) + AI×4(u16) + PWM×4(u16) = 18B |

> 0x31 오도메트리 단위: NVS unit_scale 기반 HW counts (기존 호환성 유지)  
> 또는 ESP32에서 직접 m/s + rad 단위로 전송하는 방식으로 간소화 가능

---

## 6. 개발 로드맵

### Phase E-1 — 하드웨어 준비 (1주)

- [ ] VESC 6 × 2 구매
- [ ] BLDC 모터 × 2 선정 및 구매 (Hall 센서 내장)
- [ ] USB-RS485 어댑터 구매 (Mini PC 측)
- [ ] VESC Tool로 모터 파라미터 설정 (FOC 캘리브레이션)
- [ ] CAN 버스 배선 및 종단 저항 설치
- [ ] ESP32-S3 보드 CAN/RS485 핀 확인

### Phase E-2 — ESP32 펌웨어 기초 (1주)

- [ ] PlatformIO 또는 ESP-IDF 프로젝트 생성
- [ ] RS485 UART 드라이버 (SLIP + CRC16-CCITT)
- [ ] AGV_VELOCITY(0x30) 수신 핸들러
- [ ] VESC CAN ERPM 명령 전송 (COMM_SET_ERPM)
- [ ] VESC STATUS_1 수신 및 ERPM 파싱
- [ ] 동작 확인: PC에서 velocity 명령 → 모터 회전

### Phase E-3 — 오도메트리 + 패킷 완성 (1주)

- [ ] ERPM → m/s → 위치 적분 구현
- [ ] AGV_ODOMETRY(0x31) 송신 (200ms 주기)
- [ ] AGV_STATUS(0x32) 송신
- [ ] NVS 파라미터 저장 (wheel_base, pole_pairs, erpm_scale)
- [ ] param_read_req / param_write_req 핸들러 구현
- [ ] 동작 확인: Web HMI 오도메트리 표시 정상

### Phase E-4 — 캘리브레이션 및 통합 테스트 (1주)

- [ ] ERPM 스케일 캘리브레이션 (기존 unit_scale 마법사 대체)
- [ ] Wheel base 캘리브레이션 (회전 반경 측정)
- [ ] ROS2 odom `/odom` 토픽 정상 발행 확인
- [ ] Nav2 자율주행 통합 테스트
- [ ] 열 추종 (row_follower) 통합 테스트

### Phase E-5 — I/O 확장 재구현 (선택, 0.5주)

- [ ] GPIO DO/DI (ESP32 핀 재배정)
- [ ] ADC AI (ESP32 ADC1)
- [ ] PWM (ESP32 LEDC)
- [ ] IO_SET(0x33) / IO_STATUS(0x34) 패킷 핸들러

---

## 7. PC 측 최소 변경 사항

### 7.1 udev 규칙 (RS485 포트 고정)

```bash
# /etc/udev/rules.d/99-rs485.rules
SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", \
  SYMLINK+="rs485", MODE="0666"
# CP2102N: idVendor=10c4, idProduct=ea60
# CH340:   idVendor=1a86, idProduct=7523
```

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### 7.2 bridge.py 포트 경로

```python
# 기존
DEFAULT_PORT = "/dev/ttyUSB0"

# 변경 (udev 심링크 사용)
DEFAULT_PORT = "/dev/rs485"
```

bridge.py 나머지 코드는 변경 없음.

### 7.3 stm32_bridge_node.py 파일명 변경 (권장)

```bash
cd ros2_ws/src/agv_bringup/agv_bringup
mv stm32_bridge_node.py esp32_bridge_node.py
```

`hardware.launch.py`에서 노드 이름 참조 업데이트 필요.

---

## 8. 주요 파라미터 (ESP32 NVS)

| 파라미터 | 기본값 | 설명 |
|---------|--------|------|
| `wheel_base_m` | 0.60 | 바퀴 간격 (m), 캘리브레이션 필요 |
| `wheel_radius_m` | 0.075 | 바퀴 반경 (m), 모터 선정 후 확인 |
| `pole_pairs` | 7 | BLDC 극쌍 수, 모터 사양에서 확인 |
| `erpm_scale` | 1.0 | ERPM 보정 계수 |
| `can_speed_kbps` | 500 | CAN 버스 속도 |
| `can_id_left` | 1 | 좌 VESC Node ID |
| `can_id_right` | 2 | 우 VESC Node ID |
| `rs485_baud` | 921600 | RS485 통신 속도 |
| `odom_pub_ms` | 200 | 오도메트리 송신 주기 (ms) |

---

## 9. 위험 요소 및 대책

| 위험 요소 | 대책 |
|----------|------|
| VESC CAN 프로토콜 버전 차이 | VESC 펌웨어 버전 고정 (6.x 권장), STATUS 프레임 ID 확인 |
| CAN 버스 노이즈 | 꼬임 차동선(twisted pair), 120Ω 종단, GND 공통화 |
| ERPM → 속도 변환 오차 | pole_pairs 확인 후 실측 캘리브레이션 |
| RS485 방향 제어 타이밍 | DE/RE 핀 제어 딜레이 최소화 (하드웨어 자동 방향 IC 고려) |
| ESP32 WDT 리셋 | CAN/UART 태스크 스택 크기 충분히 확보 (4096+ bytes) |
| 오도메트리 드리프트 | 200ms 주기 정밀 타이머 사용, 적분 오차 누적 모니터링 |

---

## 10. 디렉토리 구조 변경 계획

```
Appli/              ← STM32 펌웨어 (Phase 완료 후 아카이브 또는 삭제)

ESP32/              ← 신규 생성 (ESP32-S3 PlatformIO 프로젝트)
  platformio.ini
  src/
    main.cpp
    rs485_slip.c / rs485_slip.h    ← SLIP+CRC16 드라이버
    agv_protocol.c / agv_protocol.h ← 패킷 핸들러 (0x30~0x34)
    vesc_can.c / vesc_can.h         ← VESC CAN 드라이버
    odometry.c / odometry.h         ← 오도메트리 적분
    nvs_params.c / nvs_params.h     ← NVS 파라미터 관리
    io_handler.c / io_handler.h     ← GPIO/ADC/PWM (Phase E-5)

PC_GUI/bridge/      ← 변경 없음 (포트 경로 설정만 변경)
ros2_ws/            ← 변경 없음 (파일명 rename 권장)
```

---

## 11. 체크리스트

### 하드웨어 준비 완료 기준
- [ ] VESC Tool에서 양쪽 모터 FOC 캘리브레이션 성공
- [ ] VESC Tool CAN Bus Terminal에서 양쪽 VESC Node ID 통신 확인
- [ ] ESP32 ↔ PC RS485 루프백 테스트 통과

### 펌웨어 완료 기준
- [ ] Web HMI 조이스틱 조작 시 양쪽 모터 정상 응답
- [ ] 오도메트리 X/Y/θ 값이 실제 이동 방향으로 갱신
- [ ] 파라미터 저장 → ESP32 재시작 후 값 유지

### 통합 완료 기준
- [ ] `ros2 topic echo /odom` 정상 발행
- [ ] Nav2 `ros2 launch agv_bringup full_nav.launch.py` 자율주행 성공
- [ ] 기존 모든 launch 파일 정상 동작 확인

---

*이 문서는 개발 진행에 따라 업데이트된다.*
