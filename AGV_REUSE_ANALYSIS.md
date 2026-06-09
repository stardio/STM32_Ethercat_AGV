# AGV 코드 재사용 분석

**기준 문서**: AGV_DESIGN.md  
**분석일**: 2026-06-05  
**결론**: 기존 코드 약 70% 재사용 가능 — 전체 개조 불필요, 선택적 수정

---

## 핵심 발견: CSP → CSV 모드 전환 필수

현재 펌웨어는 **CSP (Cyclic Synchronous Position, mode=8)** 고정:

```c
// soem_port.c:751
uint8_t mode = 8;  // CSP — AGV에서는 9(CSV) 또는 3(PV)로 변경 필요
ecx_SDOwrite(..., 0x6060, ..., &mode, ...);

// soem_port.c:727 — RxPDO 0x1600
u32 = 0x607A0020U;  // TargetPosition → AGV에서는 0x60FF (TargetVelocity) 필요
```

AGV 차동 구동은 속도 명령이 필요하므로 PDO 매핑과 동작 모드를 함께 변경해야 한다.

---

## 레이어별 재사용 분석

| 레이어 | 재사용 가능성 | 필요 작업 |
|--------|-------------|----------|
| EtherCAT SOEM 인프라 | ✅ 100% 재사용 | 없음 |
| CiA402 FSM | ✅ 100% 재사용 | 없음 |
| UART/SLIP 프레임 | ✅ 100% 재사용 | 없음 |
| bridge.py / packet_defs.py | ✅ 거의 재사용 | 패킷 3개 추가 |
| bridge/index.html GUI | ✅ AGV 탭 추가 | ~200줄 추가 |
| soem_port.c PDO 맵핑 | ⚠️ 수정 필요 | CSP→CSV 모드 전환 |
| interpolator.c | ⚠️ 불필요 | Nav2 /cmd_vel이 대체 |
| 6축 관절 로직 | ⚠️ 2축만 사용 | J3~J6 비활성화 |
| Jetson / ROS2 / RTAB-Map | ❌ 신규 구축 | Phase 2 전체 |

---

## 펌웨어 수정 상세

### 1. `soem_port.h` / `soem_port.c` — 약 50줄 수정

**PDO 구조체에 target_velocity 필드 추가:**
```c
typedef struct __attribute__((packed)) {
    uint16_t controlword;
    int32_t  target_position;   // CSP 기존 유지
    int32_t  target_velocity;   // CSV 추가
} RxPDO_t;
```

**PDO 매핑 변경 (0x1600):**
```c
// 기존: 0x607A (TargetPosition, 32bit)
// 변경: 0x60FF (TargetVelocity, 32bit)
u32 = 0x60FF0020U;
```

**모드 변경:**
```c
uint8_t mode = 9;  // CSV (Cyclic Synchronous Velocity)
ecx_SDOwrite(..., 0x6060, ..., &mode, ...);
```

**새 함수 추가:**
```c
void SOEM_SetTargetVelocity(AxisId_t ax, int32_t vel_hw);
```

### 2. `uart_protocol.h` / `uart_protocol.c` — 약 80줄 추가

추가할 패킷:
```c
#define PROTO_PKT_AGV_VELOCITY   0x30U   /* Bridge → STM32 */
#define PROTO_PKT_AGV_ODOMETRY   0x31U   /* STM32 → Bridge */
#define PROTO_PKT_AGV_STATUS     0x32U   /* STM32 → Bridge */
```

AGV_VELOCITY 처리 (차동 구동 공식):
```c
case PROTO_PKT_AGV_VELOCITY: {
    float wheel_base = 0.60f;   // m (실측 필요)
    float wheel_r    = 0.15f;   // m (실측 필요)
    float v_left  = cmd.linear_mps - (cmd.angular_rps * wheel_base / 2.0f);
    float v_right = cmd.linear_mps + (cmd.angular_rps * wheel_base / 2.0f);
    // m/s → unit_scale 변환 후 SOEM_SetTargetVelocity() 호출
}
```

---

## PC/Bridge 수정 상세

### `packet_defs.py` — 약 40줄 추가
- `PKT_AGV_VELOCITY = 0x30`
- `build_agv_velocity(linear_mps, angular_rps)` 빌더
- `AgvOdometry` 파서

### `bridge.py` — 약 30줄 추가
- `"agv_velocity"` 명령 핸들러 추가
- AGV_ODOMETRY 수신 시 WebSocket으로 전달

### 신규: `stm32_bridge_node.py` (ROS2 래퍼)
- `/cmd_vel` 구독 → `agv_velocity` 명령 변환
- AGV_ODOMETRY 수신 → `/odom` 토픽 발행
- 기존 bridge.py를 import하거나 WebSocket으로 연결

---

## 선행 확인 사항 (구현 시작 전 필수)

### 1. 드라이브 CSV 지원 여부
연결된 EtherCAT 서보 드라이브가 CSV(mode=9)를 지원하는지 SDO로 확인:
```
0x6502 — Supported Drive Modes
  bit 8 = CSV 지원
  bit 2 = PV 지원
  bit 0 = PP 지원 (현재 사용 중인 CSP 아님 — CSP는 bit 7)
```
CSV 미지원 시 PV(Profile Velocity, mode=3)로 대체 가능하나 응답성 차이 있음.

### 2. 엔코더 → 거리 변환 계수
`unit_scale` 이 이미 `g_axis_param[ax].unit_scale`에 있으므로
AGV_VELOCITY 처리 시 그대로 활용 가능.

---

## 구현 순서 (Phase 1 기준)

```
Step 1. 드라이브 CSV 지원 여부 SDO 확인
Step 2. soem_port.c PDO 매핑 → CSV 모드로 변경 + 컴파일/다운로드 확인
Step 3. SOEM_SetTargetVelocity() 구현 및 단축 조그 테스트
Step 4. uart_protocol.c — AGV_VELOCITY (0x30) 파서 + 차동 구동 계산 추가
Step 5. uart_protocol.c — AGV_ODOMETRY (0x31) 송신 추가
Step 6. packet_defs.py + bridge.py — PC 측 패킷 추가
Step 7. bridge/index.html — 조이스틱 조작 탭 추가 (수동 주행 검증)
Step 8. stm32_bridge_node.py 작성 (Jetson ROS2 연동)
```

---

## 관련 파일 위치

| 파일 | 경로 |
|------|------|
| STM32 EtherCAT | `Appli/Core/Src/soem_port.c` |
| STM32 EtherCAT 헤더 | `Appli/Core/Inc/soem_port.h` |
| STM32 UART 프로토콜 | `Appli/Core/Src/uart_protocol.c` |
| STM32 UART 헤더 | `Appli/Core/Inc/uart_protocol.h` |
| PC 패킷 정의 | `PC_GUI/bridge/packet_defs.py` |
| PC 브릿지 | `PC_GUI/bridge/bridge.py` |
| PC GUI (로봇 제어) | `PC_GUI/bridge/index.html` |
| AGV 설계 원본 | `AGV_DESIGN.md` |
