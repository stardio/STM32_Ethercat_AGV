# 프로젝트: STM32H753ZI EtherCAT AGV 컨트롤러

## 빠른 컨텍스트 (세션 시작 시 읽기)

### 프로젝트 요약
기존 6축 다관절 로봇 STM32H753ZI 보드를 **농업용 차동구동 AGV**로 전환하는 프로젝트.
- J1=좌측 바퀴(AXIS_J1), J2=우측 바퀴(AXIS_J2) — AXIS_COUNT=2
- STM32 ↔ Jetson Orin Nano ↔ ROS2 Humble 구조

### 하드웨어
- **MCU**: STM32H753ZI, NUCLEO-H753ZI, ARM Cortex-M7 @ 480 MHz
- **EtherCAT**: SOEM, 1ms PDO 주기, LS L7NH 서보드라이브 (CSV 모드, 속도제어)
- **UART**: USART3, 921600 baud, SLIP+CRC16-CCITT
- **PC↔MCU**: UART → bridge.py → WebSocket → Web HMI / ROS2 노드
- **AI**: Jetson Orin Nano 8GB
- **카메라**: Intel RealSense D435i (RGB-D + IMU), SN: 261222073771

### 디렉토리 구조
```
Appli/Core/Src/          ← STM32 펌웨어 (C)
  main.c                 ← FreeRTOS tasks, EtherCAT 1ms 루프 (~350줄)
  soem_port.c            ← EtherCAT SOEM, CiA402, CSV 모드, SOEM_SetTargetVelocity()
  uart_protocol.c        ← SLIP 프레임, AGV_VELOCITY(0x30) 핸들러
  axis_config.c          ← 전역 축 파라미터 (AXIS_COUNT=2)
  ui_flash_storage.c     ← Flash 파라미터 저장 (128 bytes, 4×32B words)

PC_GUI/bridge/
  bridge.py              ← UART↔WebSocket 브릿지 + HTTP 서버(port 5100)
                            + RealSense D435i MJPEG 스트리밍 (port 5100)
  index.html             ← AGV 전용 Web HMI (📟 I/O 모달 포함)
  packet_defs.py         ← SLIP 패킷 빌더/파서 (IO_SET/IO_STATUS 포함)
  slip_codec.py          ← SLIP 인코더/디코더
  json_interpreter.py    ← JSON 프로그램 인터프리터 (Phase A~D)

ros2_ws/src/
  agv_bringup/           ← ROS2 패키지: 노드, launch, config
    agv_bringup/stm32_bridge_node.py   ← /cmd_vel→STM32, /odom 발행
    agv_bringup/row_follower.py        ← depth PID 열 추종 (Phase 3+4)
    agv_bringup/obstacle_classifier.py ← YOLOv8 + Geofence 안전 레이어 (Phase 4)
    agv_bringup/task_scheduler.py      ← cron 스케줄 + HTTP REST port 8080 (Phase 5)
    launch/hardware.launch.py          ← STM32 브릿지 + URDF
    launch/slam.launch.py              ← RealSense + RTAB-Map (맵핑)
    launch/localization.launch.py      ← RTAB-Map 위치추정 모드
    launch/nav2.launch.py              ← Nav2 자율주행
    launch/full_slam.launch.py         ← 전체 스택 (맵핑)
    launch/full_nav.launch.py          ← 전체 스택 (자율주행)
    launch/row_follow.launch.py        ← STM32+카메라+EKF+열추종+장애물 (Phase 3+4)
    launch/obstacle.launch.py          ← 장애물 감지 단독 테스트 (Phase 4)
    config/rtabmap_params.yaml         ← RTAB-Map 설정
    config/nav2_params.yaml            ← Nav2/DWB 설정
    config/ekf.yaml                    ← robot_localization odom+IMU EKF (Phase 3)
    config/geofence.yaml               ← 금지구역 다각형 (Phase 4, enabled:false)
    config/mission.yaml                ← 작업 스케줄 정의 (Phase 5)
  agv_description/
    urdf/agv.urdf.xacro                ← 차동구동 URDF (wheel_base=0.60m, radius=0.15m)
```

### AGV UART 프로토콜
| ID | 방향 | 이름 | 설명 |
|----|------|------|------|
| 0x30 | Bridge→STM32 | AGV_VELOCITY | linear_mps(f32) + angular_rps(f32) |
| 0x31 | STM32→Bridge | AGV_ODOMETRY | 좌우 엔코더 위치/속도 스냅샷 |
| 0x32 | STM32→Bridge | AGV_STATUS | 드라이브 상태 |
| 0x33 | Bridge→STM32 | IO_SET | DO mask/val + PWM ch/duty (Phase D) |
| 0x34 | STM32→Bridge | IO_STATUS | DI/DO/AI/PWM 상태 스냅샷, 200ms (Phase D) |

차동구동 운동학:
```
v_left  = linear - angular × wheel_base/2
v_right = linear + angular × wheel_base/2
```
단위 변환: m/s → mm/s × unit_scale[counts/mm] = HW counts/s

### bridge.py HTTP API
| 엔드포인트 | 설명 |
|-----------|------|
| `GET /` | index.html 서빙 |
| `GET /ports` | 시리얼 포트 목록 JSON |
| `GET /camera/status` | D435i 연결 상태 JSON (connected, model, serial, resolution) |
| `GET /camera/stream` | MJPEG 스트림 (~15fps, 320×240) — multipart/x-mixed-replace |

### 빌드
```bash
./build.sh          # cmake + arm-none-eabi-gcc
# 결과: build/Debug/Appli/CartesianRobot_H753.elf/.bin
# 빌드 검증: FLASH 134KB/1920KB (6.86%), RAM 256KB/448KB (56.00%)  ← Phase D 반영
```

### Flash 파라미터 저장
- 위치: Bank2 Sector7 `0x081E0000`
- Magic: `0xC4754100` (AGV v1, robot_config.h ROBOT_FLASH_MAGIC)
- 크기: 128 bytes = 4×32B flash words (AXIS_COUNT=2 기준)

### Python 패키지 의존성 (bridge.py)
```bash
pip3 install websockets pyserial pyserial-asyncio pyrealsense2 numpy Pillow
# opencv-python 있으면 Pillow 대신 사용 (JPEG 품질 동일)
```

---

## 구현 로드맵 현황

### ✅ Phase 1+2 완료 (기본 주행 + 인프라)
- **코드 정리**: 6축 로봇/프레스머신 레거시 파일 18개 삭제, main.c 2044→350줄 축소
  - 삭제: interpolator, press_state_machine, recipe_manager, cycle_logger,
    alarm_manager, interlock_manager, user_auth, maint_counter, settings_persistence
  - AXIS_COUNT=2 (J1 좌/J2 우), robot_config.h AGV 설정으로 완전 교체
- STM32 CSV 모드 (속도제어), AGV 패킷 3종 (0x30~0x32)
- bridge.py AGV 핸들러, stm32_bridge_node.py, URDF, launch 6종
- rtabmap_params.yaml, nav2_params.yaml
- 빌드 검증 완료

### ✅ Phase 3 완료 (열 추종 + EKF)
- `row_follower.py` — depth PID 나무 열 추종 (kp=0.5, ki=0.01, kd=0.1)
- `config/ekf.yaml` — robot_localization odom+IMU → /odometry/filtered
- `launch/row_follow.launch.py` — STM32+카메라+EKF+열추종 통합

### ✅ Phase 4 완료 (YOLOv8 + 3단계 안전 레이어)
- `obstacle_classifier.py` — person→FULL_STOP, vehicle→WAIT, animal→SLOW_AVOID
  hysteresis confirm/clear frames, /obstacle/action 토픽 발행
- `config/geofence.yaml` — 금지구역 다각형 (enabled:false, 현장 캘리브레이션 후 활성화)
- `launch/obstacle.launch.py` — 단독 테스트용

### ✅ Phase 5 완료 (미션 스케줄러 + 웹 대시보드)
- `task_scheduler.py` — HH:MM cron + HTTP REST API port 8080
  survey/spray/fruit_detect/return_home, ROW_END 자동완료, 60분 타임아웃
- `config/mission.yaml` — 아침 순찰(06:00), 방제(09:30 화·금), 착과(14:00), 귀환(18:00)

### ✅ Phase A~C 완료 (JSON 프로그램 인터프리터)
- `json_interpreter.py` — Phase A(move/stop/wait/log) + B(변수/흐름제어) + C(센서 조건)
- `editor.html` — JSON 편집기 + Claude AI 어시스턴트
- `/program/*` REST API (저장·로드·실행·삭제)

### ✅ Phase D 완료 — I/O 확장 (2026-06-13)
- **STM32**: io_handler.c (DO=PF0-7, DI=PD0-7, AI=PA3-6 ADC1 DMA, PWM=PC6-9 TIM3 20kHz)
- **패킷**: IO_SET(0x33) 5B / IO_STATUS(0x34) 18B, 200ms 주기 자동 송신
- **PC**: packet_defs.py build_io_set/parse_io_status, bridge.py /io/set POST + /io/status GET
- **HMI**: 📟 I/O 모달 (DI LED×8, DO 토글×8, AI 바/전압×4, PWM 슬라이더×4)
- **인터프리터**: `do_set` / `pwm_set` / `read_io` 명령, DI/AI 센서 스토어 연결

### ✅ Phase 7 완료 — U-blox GPS 통합 (2026-06-18)
- **하드웨어**: U-blox (FTDI FT232, `/dev/gps → ttyUSB0`), 38400 baud, 10Hz SBAS
- **udev**: `/etc/udev/rules.d/99-ublox-gps.rules` — 디바이스 고정명
- **gps_config.py**: UBX 바이너리 출력 비활성화, 10Hz, BBR 저장 (UBX CFG-PRT/RATE/CFG)
- **gps_driver_node.py**: NMEA+UBX 혼합 필터링, `/gps/fix` 발행 (10Hz)
- **ekf_gps.yaml**: GPS 지원 전용 EKF (`ekf_gps_node`) — `odom1: /odometry/gps` 포함
- **navsat_transform.yaml**: GPS lat/lon → ENU 로컬 좌표, 한국 자기편각 -0.1309 rad
- **gps.launch.py**: `gps_config → gps_driver(t=4s) + ekf_gps(t=4s) → navsat_transform(t=8s)`
- **ekf.yaml / ekf_odom_only.yaml**: `odom1: /odometry/gps` 추가 (GPS 융합 준비)
- **agv.urdf.xacro**: `gps_link` 조인트 추가 (base_link → gps_link, z=0.35m)
- **start_gps.sh**: 전체 GPS 스택 단일 실행 (`./ros2_ws/start_gps.sh`)
- **FastDDS**: `fastdds_no_shm.xml` — Nav2 SHM 충돌 방지 (UDP 전용)
- **토픽**: `/gps/fix` → `/odometry/gps`(ENU) → EKF 융합 → `/odometry/filtered`

### ✅ Phase 6 완료 — UI 전면 재작성 + D435i 카메라 연동 (2026-06-10)

#### Web HMI 재작성 (`PC_GUI/bridge/index.html`)
- **레거시 완전 제거**: DH 파라미터 모달, G-code 에디터, 3D canvas, 시뮬레이션 모드
  (2686줄 → 1228줄, AXIS_COUNT=6 → 2)
- **Row 1**: Drive Status (CiA402), Wheel Odometry, Safety Layer 뱃지, Drive Control 버튼
- **Row 2**: 속도 슬라이더, Virtual Joystick (MJPEG처럼 실시간), 키보드 단축키, 열 추종 제어 패널
- **Row 3**: Mission Scheduler 패널 (/api/status 2초 폴링, /api/schedule 30초 폴링, ▶ 즉시 실행)
- **Row 4**: unit_scale 측정 마법사 (기본 접힘, 클릭으로 펼침)
- Parameter 모달: J1/J2 탭만 (J3~J6 제거)
- Safety Layer: obstacle_action → CLEAR/SLOW/WAIT/FULL_STOP 색상 뱃지
- Row Follow 상태: DISABLED/FOLLOWING/OBSTACLE/ROW_END 색상 뱃지

#### RealSense D435i 카메라 스트리밍 (`PC_GUI/bridge/bridge.py`)
- `_ThreadingHTTPServer` (socketserver.ThreadingMixIn) — 동시 MJPEG 연결 지원
- `_camera_loop()` 백그라운드 스레드 — pyrealsense2로 320×240 @30fps 캡처
  JPEG 인코딩: OpenCV 우선, 없으면 Pillow 자동 폴백
- `/camera/status` — JSON: `{connected, model, serial, resolution, error}`
- `/camera/stream` — `multipart/x-mixed-replace` MJPEG ~15fps
- 카메라 미연결 / pyrealsense2 미설치 시 graceful degradation (브릿지 정상 시작)
- UI: 상단바 📷 CAM 버튼 → 우하단 플로팅 패널 (320×240), 5초 상태 폴링

---

## 실행 커맨드 (운용 참고)

```bash
# ── STM32 펌웨어 빌드 ──────────────────────────────────────────────────────
./build.sh

# ── PC 브릿지 시작 (UART↔WebSocket port 8765, HTTP port 5100) ─────────────
cd PC_GUI/bridge && python3 bridge.py
# → 브라우저: http://<host>:5100/
# → 카메라:   http://<host>:5100/camera/stream
# → 카메라 상태: http://<host>:5100/camera/status

# ── ROS2 빌드 ──────────────────────────────────────────────────────────────
cd ros2_ws && colcon build --symlink-install

# ── [Phase 1+2] 기본 하드웨어 + 맵핑 ──────────────────────────────────────
ros2 launch agv_bringup hardware.launch.py
ros2 launch agv_bringup full_slam.launch.py     # 맵 생성
ros2 launch agv_bringup full_nav.launch.py      # Nav2 자율주행

# ── [Phase 3+4] 열 추종 + 장애물 회피 (통합 스택) ─────────────────────────
ros2 launch agv_bringup row_follow.launch.py

# ── [Phase 4] 장애물 감지 단독 테스트 ─────────────────────────────────────
ros2 launch agv_bringup obstacle.launch.py

# ── [Phase 5] 미션 스케줄러 ───────────────────────────────────────────────
ros2 run agv_bringup task_scheduler --ros-args -p mission_file:=/path/to/mission.yaml

# 열 추종 수동 제어
ros2 topic pub --once /row_follow/enable std_msgs/Bool '{data: true}'
ros2 topic pub --once /row_follow/enable std_msgs/Bool '{data: false}'
ros2 topic echo /row_follow/status

# 스케줄러 REST API
curl http://localhost:8080/api/status
curl http://localhost:8080/api/schedule
curl -X POST http://localhost:8080/api/run/morning_survey
curl -X POST http://localhost:8080/api/stop

# ── 카메라 확인 (bridge.py 실행 중) ───────────────────────────────────────
python3 -c "import pyrealsense2 as rs; ctx=rs.context(); [print(d.get_info(rs.camera_info.name)) for d in ctx.devices]"

# ── [Phase 7] GPS 스택 (bridge.py + STM32 /odom 필요) ─────────────────────
cd ros2_ws && ./start_gps.sh
#   t=0s  gps_config     : U-blox 10Hz 설정
#   t=4s  gps_driver     : /gps/fix (10Hz)
#   t=4s  ekf_gps_node   : /odom → /odometry/filtered
#   t=8s  navsat_transform: /gps/fix → /odometry/gps (ENU)
# 확인:
ros2 topic echo /gps/fix
ros2 topic echo /odometry/gps
ros2 topic echo /gps/filtered       # EKF 역변환 디버그
```

---

## 주요 설계 문서
- `AGV_DESIGN.md` — 전체 시스템 설계 (알고리즘, 파라미터, 로드맵)
- `JSON_COMMAND_REFERENCE.md` — JSON 프로그램 명령어 전체 참조서 (Phase A~D)
- `IO_BASEBOARD_DESIGN.md` — I/O 핀 배치 및 베이스보드 설계 주의사항

## 세션 운영 팁
- **CLAUDE.md** (이 파일): 매 세션 자동 로드. 구현 완료 시 ✅ 표 업데이트
- **메모리**: `/home/bs/.claude/projects/-home-bs-Ethercat-N753-6AX-AGV/memory/`
- 세션이 길어지면 컨텍스트가 요약됨 — 이 파일로 재진입 시 즉시 복원
- `PC_GUI/UartWeb/`, `backup/`, `blog_posts/`, `tools/` 등 비AGV 파일은 모두 삭제됨
