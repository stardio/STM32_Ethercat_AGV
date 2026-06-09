# 농업용 AGV 시스템 설계 제안서

**프로젝트**: N753-6AX 기반 과수원 자율주행 AGV  
**작성일**: 2026-06-04  
**대상 보드**: STM32H753ZI (N753-6AX)  
**구동 방식**: 차동 구동 (Differential Drive) 2륜

---

## 1. 개요

기존 6축 다관절 로봇 컨트롤러(STM32H753ZI)를 농업용 AGV에 재활용하여  
카메라만을 이용한 과수원 자율 맵핑·장애물 회피·작업 자동화 시스템을 구축한다.

### 설계 원칙

- **기존 STM32 펌웨어 최소 수정**: 명령 2~3개 추가만으로 AGV 구동
- **AI 연산은 Jetson에 위임**: STM32는 모터 저수준 제어에 집중
- **카메라 단독 내비게이션**: RGB-D 카메라로 SLAM + 장애물 회피 통합
- **단계적 구현**: Phase 1~5 순서로 기능 추가
- **기존 자산 최대 재사용**: bridge 프로토콜·SOEM·GUI 그대로 활용

---

## 2. 전체 시스템 아키텍처

```
┌──────────────────────────────────────────────────────────────────┐
│                          AGV 본체                                │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                 Jetson Orin Nano 8GB (두뇌)                │  │
│  │                                                            │  │
│  │  ┌────────────┐  ┌──────────────┐  ┌───────────────────┐  │  │
│  │  │ RTAB-Map   │  │     Nav2     │  │   YOLOv8          │  │  │
│  │  │  (SLAM)    │  │  (경로계획)  │  │  (장애물 인식)    │  │  │
│  │  └─────┬──────┘  └──────┬───────┘  └────────┬──────────┘  │  │
│  │        └────────────────┴──────────────────-─┘             │  │
│  │                       ROS2 Humble                          │  │
│  │        ┌──────────────────────────────────┐               │  │
│  │        │     stm32_bridge_node            │               │  │
│  │        │   (기존 bridge.py → ROS2 래퍼)   │               │  │
│  │        └──────────────┬───────────────────┘               │  │
│  └─────────────────────-─┼──────────────────────────────────-┘  │
│                           │ UART 921600bps (기존 프로토콜)        │
│  ┌────────────────────────▼───────────────────────────────────┐  │
│  │                  STM32H753ZI (기존 보드)                   │  │
│  │         EtherCAT SOEM — 축 2개 (AXIS_J1, AXIS_J2)         │  │
│  │         새 명령: AGV_VELOCITY(0x30) / AGV_ODOMETRY(0x31)  │  │
│  └──────────────┬──────────────────────────┬──────────────────┘  │
│                 │ EtherCAT                  │ EtherCAT            │
│          ┌──────▼──────┐             ┌──────▼──────┐             │
│          │  좌측 모터   │             │  우측 모터   │             │
│          │  (+ 엔코더)  │             │  (+ 엔코더)  │             │
│          └──────┬──────┘             └──────┬──────┘             │
│                 └──────────┬────────────────┘                    │
│                        차동 구동 바퀴                             │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │  센서: RealSense D435i │ 선택: RTK-GPS │ 선택: 후방 카메라│    │
│  └──────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────┘

원격 모니터링 PC
  └── index.html (기존 HMI + AGV 탭 추가)
        ├── 실시간 맵 시각화
        ├── 작업 스케줄 관리
        └── 배터리·속도·위치 모니터링
```

### 데이터 흐름

```
RealSense D435i
  ├── RGB  → RTAB-Map (특징점 추출)
  ├── Depth → Nav2 Costmap (장애물 거리)
  ├── Depth → YOLOv8 (물체 분류 + 거리)
  └── IMU  → robot_localization EKF (오도메트리 보정)

STM32 엔코더
  └── AGV_ODOMETRY → ROS2 /odom 토픽 → EKF 융합

ROS2 /cmd_vel (linear.x, angular.z)
  └── stm32_bridge_node
        └── AGV_VELOCITY 패킷 → STM32 → 모터
```

---

## 3. 핵심 하드웨어 구성

### 3.1 부품 목록

| 부품 | 선택 모델 | 역할 | 비고 |
|---|---|---|---|
| **메인 컨트롤러** | STM32H753ZI (기존) | 모터 저수준 제어 | 수정 최소화 |
| **AI 컴퓨터** | Jetson Orin Nano 8GB | SLAM·AI·경로계획 | 40 TOPS |
| **주 카메라** | Intel RealSense D435i | Depth + RGB + IMU | SLAM 핵심 |
| **보조 카메라** | USB 광각 180° (선택) | 후방·측면 감시 | 선택사항 |
| **GPS** | RTK-GPS (선택) | 과수원 절대 위치 | 정밀도 2cm |
| **좌/우 모터** | BLDC 서보 ×2 | 차동 구동 | EtherCAT/RS-485 |
| **배터리** | 48V LiFePO4 | 전원 | 4~8시간 운용 목표 |
| **통신** | Wi-Fi 6 또는 4G LTE | 원격 모니터링 | |

### 3.2 Jetson Orin Nano 선택 이유

```
Raspberry Pi 5 대비 Jetson Orin Nano 8GB 장점:
  ├── GPU 1024 CUDA Core → YOLOv8 실시간 추론 가능
  ├── 40 TOPS → RTAB-Map + YOLO 동시 실행 가능
  ├── JetPack → CUDA·cuDNN·TensorRT 최적화 내장
  └── ROS2 공식 지원 (Ubuntu 22.04 기반)

Raspberry Pi 5 (15 TOPS)는
SLAM + AI 동시 실행 시 성능 부족 → 비추천
```

### 3.3 카메라 장착 위치

```
Eye-to-World (전방 고정 장착) 권장:

         전방
    ┌──────────┐
    │  D435i   │  ← 높이: 지면에서 80~120cm
    │  (전방)  │     각도: 수평에서 -15° 하향
    └──────────┘
         │
    ┌────▼─────┐
    │   AGV    │
    │  차체     │
    └──────────┘

주의: 진동 감쇄 마운트 필수 (D435i IMU 노이즈 최소화)
```

---

## 4. 소프트웨어 스택

```
Jetson Orin Nano
├── Ubuntu 22.04 (JetPack 6.x)
├── ROS2 Humble Hawksbill
│   ├── rtabmap_ros          ← Visual SLAM
│   ├── nav2_bringup         ← 자율 주행 내비게이션
│   ├── robot_localization   ← EKF 센서 융합
│   ├── realsense2_ros       ← D435i 드라이버
│   └── 커스텀 패키지
│       ├── stm32_bridge_node    ← STM32 통신 인터페이스
│       ├── orchard_navigator    ← 과수원 전용 경로 생성
│       ├── row_follower         ← 나무 열 추종
│       ├── obstacle_classifier  ← 장애물 분류 대응
│       └── task_scheduler       ← 작업 스케줄 관리
│
└── Python 패키지
    ├── ultralytics            ← YOLOv8
    ├── pyrealsense2           ← RealSense SDK
    ├── numpy, scipy, opencv-python
    └── pyyaml                 ← 작업 설정 파일 파싱

설치 명령:
  sudo apt install ros-humble-rtabmap-ros
  sudo apt install ros-humble-navigation2
  sudo apt install ros-humble-nav2-bringup
  sudo apt install ros-humble-robot-localization
  pip install ultralytics pyrealsense2
```

---

## 5. 핵심 기술 상세 구현

---

### 5.1 SLAM — 맵핑과 위치 추정

#### 알고리즘 비교

| 알고리즘 | 센서 | 과수원 적합성 | 특징 |
|---|---|---|---|
| **RTAB-Map** | RGB-D + IMU | ★★★★★ | Loop closure 강력, ROS2 성숙 |
| ORB-SLAM3 | RGB-D/Stereo | ★★★★☆ | 정밀하나 설정 복잡 |
| OpenVSLAM | Monocular | ★★★☆☆ | 깊이 없으면 스케일 모호 |
| Kimera | RGB-D + IMU | ★★★★☆ | 의미론적 맵 생성 가능 |

> **RTAB-Map 최우선 추천**  
> D435i RGB-D + IMU 최적화, Loop Closure 감지로 반복 패턴 과수원에 강함

#### RTAB-Map 실행

```bash
# 1단계: 과수원 맵핑 (최초 1회 수동 주행)
ros2 launch rtabmap_ros rtabmap.launch.py \
    rgb_topic:=/camera/color/image_raw \
    depth_topic:=/camera/depth/image_rect_raw \
    camera_info_topic:=/camera/color/camera_info \
    imu_topic:=/camera/imu \
    odom_topic:=/odom \
    approx_sync:=false \
    frame_id:=base_link \
    Mem/NotLinkedNodesKept:=false

# 2단계: 저장된 맵으로 자율 주행 (일상 운용)
ros2 launch rtabmap_ros rtabmap.launch.py \
    localization:=true \
    map_file_path:=/maps/orchard_map.db
```

#### 맵핑 절차

```
Step 1: AGV를 수동으로 과수원 전체 한 바퀴 주행 (30~60분)
         → RTAB-Map이 3D Point Cloud + 2D 격자 지도 자동 생성

Step 2: 생성된 맵 검토 및 저장
         rtabmap-databaseViewer orchard_map.db

Step 3: 이후 방문마다 저장 맵 로드 → 위치 추정(Localization)만 수행

Step 4: 계절별 재맵핑 (나뭇잎 변화 대응)
         봄(개화) / 여름(만엽) / 가을(낙엽) 3회 맵 권장
```

---

### 5.2 차동 구동 인터페이스 (STM32)

#### 새 프로토콜 패킷 추가

```c
/* uart_protocol.h 추가 */
#define PROTO_PKT_AGV_VELOCITY   0x30U  /* Bridge → STM32 */
#define PROTO_PKT_AGV_ODOMETRY   0x31U  /* STM32 → Bridge */
#define PROTO_PKT_AGV_STATUS     0x32U  /* STM32 → Bridge */

/* AGV_VELOCITY (0x30) — linear + angular 속도 명령 */
typedef struct __attribute__((packed)) {
    float linear_mps;    /* 전진 속도 [m/s], 음수=후진 */
    float angular_rps;   /* 회전 속도 [rad/s], 양수=좌회전 */
} ProtoPktAgvVelocity_t;

/* AGV_ODOMETRY (0x31) — 엔코더 기반 주행 거리 */
typedef struct __attribute__((packed)) {
    float left_m;        /* 좌측 바퀴 누적 거리 [m] */
    float right_m;       /* 우측 바퀴 누적 거리 [m] */
    float vl_mps;        /* 좌측 현재 속도 [m/s] */
    float vr_mps;        /* 우측 현재 속도 [m/s] */
} ProtoPktAgvOdometry_t;
```

#### 차동 구동 속도 계산

```c
/* uart_protocol.c — AGV_VELOCITY 처리 */
case PROTO_PKT_AGV_VELOCITY: {
    ProtoPktAgvVelocity_t cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    float wheel_base = 0.60f;   /* 좌우 바퀴 간격 (m), 실측 필요 */
    float wheel_r    = 0.15f;   /* 바퀴 반지름 (m), 실측 필요 */

    /* 차동 구동 공식 */
    float v_left  = cmd.linear_mps - (cmd.angular_rps * wheel_base / 2.0f);
    float v_right = cmd.linear_mps + (cmd.angular_rps * wheel_base / 2.0f);

    /* m/s → RPM 변환 */
    float rpm_left  = (v_left  / (2.0f * M_PI * wheel_r)) * 60.0f;
    float rpm_right = (v_right / (2.0f * M_PI * wheel_r)) * 60.0f;

    /* EtherCAT 속도 명령 (unit_scale 적용) */
    int32_t hw_left  = (int32_t)(rpm_left  * g_axis_param[AXIS_J1].unit_scale);
    int32_t hw_right = (int32_t)(rpm_right * g_axis_param[AXIS_J2].unit_scale);

    SOEM_SetTargetVelocity(AXIS_J1, hw_left);
    SOEM_SetTargetVelocity(AXIS_J2, hw_right);

    dispatch_ack(seq, PROTO_RESULT_OK);
    break;
}
```

#### ROS2 브릿지 노드

```python
# stm32_bridge_node.py
import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist
from nav_msgs.msg import Odometry
import websockets, json, asyncio

class STM32BridgeNode(Node):
    def __init__(self):
        super().__init__('stm32_bridge')
        # /cmd_vel 구독 → STM32로 전달
        self.sub_vel = self.create_subscription(
            Twist, '/cmd_vel', self.cmd_vel_cb, 10)
        # 오도메트리 발행
        self.pub_odom = self.create_publisher(Odometry, '/odom', 10)

    async def cmd_vel_cb(self, msg: Twist):
        await self.ws.send(json.dumps({
            "cmd":        "agv_velocity",
            "linear_mps":  msg.linear.x,
            "angular_rps": msg.angular.z,
        }))

    def publish_odometry(self, left_m, right_m, vl, vr):
        # 엔코더 → ROS2 /odom 토픽 변환
        odom = Odometry()
        # ... (헤더, pose, twist 계산)
        self.pub_odom.publish(odom)
```

---

### 5.3 장애물 회피 — 3단계 안전 레이어

```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
레이어 1: Depth Costmap (즉각 반응, 0.3~4m)
  D435i Depth Map → Nav2 Obstacle Layer
  → 장애물 주변 Inflation 반경 적용
  → 로컬 플래너(DWB)가 실시간 우회 경로 계산

레이어 2: YOLOv8 분류 (0.5~6m, 맥락 대응)
  객체 분류에 따라 다른 동작:
  ┌─────────┬───────────────────────────────┐
  │ 감지 대상│ 대응 동작                     │
  ├─────────┼───────────────────────────────┤
  │ 사람     │ 완전 정지 + 경보음 + 원격 알림 │
  │ 동물     │ 감속 + 저속 우회              │
  │ 농기계   │ 정지 + 5초 대기 후 재시도      │
  │ 나무     │ Nav2 Costmap 자동 회피        │
  │ 미확인   │ 감속 후 정지                  │
  └─────────┴───────────────────────────────┘

레이어 3: 가상 울타리 (Geofence)
  맵 위에 통행 불가 폴리곤 사전 정의
  → 소프트웨어적 진입 원천 차단
  → 절벽·수로·도로 등 위험 구역 보호
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

```python
# obstacle_classifier.py
import cv2
from ultralytics import YOLO

OBSTACLE_ACTIONS = {
    'person':  ('FULL_STOP',   0.0),   # (동작, 재개 대기 초)
    'animal':  ('SLOW_AVOID',  0.0),
    'car':     ('WAIT',        5.0),
    'truck':   ('WAIT',        5.0),
    'unknown': ('SLOW_STOP',   0.0),
}

class ObstacleClassifier:
    def __init__(self):
        self.model = YOLO('yolov8n.pt')

    def classify_and_act(self, rgb_frame, depth_frame):
        results = self.model(rgb_frame, conf=0.5)
        for box in results[0].boxes:
            label  = self.model.names[int(box.cls)]
            cx, cy = map(int, box.xywh[0][:2])
            dist   = depth_frame.get_distance(cx, cy)  # 미터

            if dist < 2.0 and label in OBSTACLE_ACTIONS:
                action, wait = OBSTACLE_ACTIONS[label]
                return action, label, dist

        return 'CLEAR', None, None
```

---

### 5.4 과수원 경로 계획

#### 나무 열 추종 알고리즘

```
과수원 구조:
┌──────────────────────────────────────────┐
│  🌳  🌳  🌳  🌳  🌳  🌳  🌳  🌳  🌳     │
│                                          │
│  ←────────── 주행 통로 ──────────→       │
│       ← 중심선 추종 →                    │
│  🌳  🌳  🌳  🌳  🌳  🌳  🌳  🌳  🌳     │
│                                          │
│  ← U턴 ──────────────────────── U턴 →   │
│                                          │
│  🌳  🌳  🌳  🌳  🌳  🌳  🌳  🌳  🌳     │
└──────────────────────────────────────────┘
```

```python
# row_follower.py
class RowFollower:
    """깊이 카메라로 좌우 나무 열 감지 → PID 중심선 추종"""

    KP, KI, KD = 0.5, 0.01, 0.1    # PID 게인 (튜닝 필요)
    TARGET_SPEED = 0.4              # m/s

    def detect_row_center(self, depth_image):
        H, W = depth_image.shape
        scan_row = int(H * 0.6)     # 화면 하단 60% 지점 스캔

        # 좌/우 방향으로 나무까지 거리 측정
        left_strip  = depth_image[scan_row, :W//2]
        right_strip = depth_image[scan_row, W//2:]

        # 유효 깊이 픽셀의 평균 (나무 벽까지 거리)
        left_valid  = left_strip[left_strip > 0]
        right_valid = right_strip[right_strip > 0]

        d_left  = float(np.median(left_valid))  if len(left_valid)  > 10 else None
        d_right = float(np.median(right_valid)) if len(right_valid) > 10 else None

        if d_left is None or d_right is None:
            return None  # 열 감지 실패 → 정지

        return d_left - d_right   # 양수: 오른쪽으로 치우침

    def compute_cmd_vel(self, depth_image):
        error = self.detect_row_center(depth_image)
        if error is None:
            return 0.0, 0.0   # linear=0, angular=0 (정지)

        correction = self.pid.update(error)
        return self.TARGET_SPEED, -correction  # linear, angular

    def detect_row_end(self, depth_image):
        """열 끝 감지: 전방 나무가 사라지면 열 종료"""
        front_center = depth_image[depth_image.shape[0]//2,
                                   depth_image.shape[1]//2]
        return front_center > 5.0 or front_center == 0  # 5m 이상 = 열 끝
```

#### U턴 절차

```python
async def execute_u_turn(self, direction='right'):
    """열 끝 도달 시 자동 U턴"""
    # 1. 전진 정지
    await self.send_velocity(0.0, 0.0)
    await asyncio.sleep(0.5)

    # 2. 전진으로 열 간격만큼 이동
    await self.send_velocity(0.4, 0.0)
    await asyncio.sleep(self.row_spacing / 0.4)

    # 3. 제자리 회전 180°
    angular = 0.5 if direction == 'right' else -0.5
    await self.send_velocity(0.0, angular)
    await asyncio.sleep(math.pi / abs(angular))

    # 4. 다음 열 진입
    await self.send_velocity(0.4, 0.0)
    await asyncio.sleep(1.0)
```

---

### 5.5 작업 스케줄링

#### 작업 정의 파일

```yaml
# mission.yaml
agv:
  wheel_base: 0.60        # m
  wheel_radius: 0.15      # m
  max_speed: 0.8          # m/s
  row_spacing: 3.0        # m (나무 열 간격)
  num_rows: 12            # 총 열 수

schedule:
  - cron: "0 6 * * *"     # 매일 06:00
    task: survey
    speed: 0.5
    description: "전체 과수원 순찰 및 상태 촬영"

  - cron: "0 10 * * 2,4"  # 화·목 10:00
    task: spray
    speed: 0.3
    rows: [1, 2, 3, 4, 5, 6]
    payload: pesticide
    description: "방제 작업"

  - cron: "0 15 * * *"    # 매일 15:00
    task: fruit_detection
    speed: 0.2
    save_images: true
    description: "착과 상태 모니터링"

  - cron: "0 18 * * *"    # 매일 18:00
    task: return_home
    description: "충전 스테이션으로 복귀"
```

#### 스케줄러 구현

```python
# task_scheduler.py
import schedule, time, yaml
from enum import Enum

class TaskType(Enum):
    SURVEY         = 'survey'
    SPRAY          = 'spray'
    FRUIT_DETECT   = 'fruit_detection'
    RETURN_HOME    = 'return_home'

class TaskScheduler(Node):
    def __init__(self, mission_file: str):
        super().__init__('task_scheduler')
        with open(mission_file) as f:
            self.config = yaml.safe_load(f)
        self._register_schedules()

    def _register_schedules(self):
        for job in self.config['schedule']:
            schedule.every().day.at(
                job['cron'].split()[1] + ':00'
            ).do(self.execute_task, job)

    async def execute_task(self, job: dict):
        task = TaskType(job['task'])
        self.get_logger().info(f"작업 시작: {job['description']}")

        if task == TaskType.SURVEY:
            await self.navigate_all_rows(job.get('speed', 0.5))

        elif task == TaskType.SPRAY:
            rows = job.get('rows', range(self.config['agv']['num_rows']))
            for row in rows:
                await self.navigate_row(row, job.get('speed', 0.3))
                await self.activate_sprayer()

        elif task == TaskType.FRUIT_DETECT:
            await self.navigate_all_rows(
                speed=job.get('speed', 0.2),
                save_images=job.get('save_images', False)
            )

        elif task == TaskType.RETURN_HOME:
            await self.navigate_to_home()

        self.get_logger().info(f"작업 완료: {job['description']}")
```

---

### 5.6 센서 융합 (EKF)

```yaml
# robot_localization 설정 (ekf.yaml)
ekf_filter_node:
  ros__parameters:
    frequency: 30.0
    sensor_timeout: 0.1
    two_d_mode: true           # 2D 주행 (평지 가정)

    odom0: /odom               # STM32 엔코더 오도메트리
    odom0_config: [true,  true,  false,
                   false, false, true,
                   true,  true,  false,
                   false, false, true,
                   false, false, false]

    imu0: /camera/imu          # RealSense D435i IMU
    imu0_config: [false, false, false,
                  true,  true,  true,
                  false, false, false,
                  true,  true,  true,
                  true,  false, false]

    # GPS 사용 시 추가
    # gps0: /gps/fix
    # gps0_config: [true, true, false, ...]
```

---

## 6. Nav2 설정 — 자율 주행 내비게이션

```yaml
# nav2_params.yaml 핵심 설정
controller_server:
  ros__parameters:
    controller_plugins: ["FollowPath"]
    FollowPath:
      plugin: "dwb_core::DWBLocalPlanner"
      min_vel_x: 0.0
      max_vel_x: 0.6          # m/s 최대 전진 속도
      max_vel_theta: 0.8      # rad/s 최대 회전 속도
      min_speed_xy: 0.0
      acc_lim_x: 0.5          # m/s² 가속도 제한
      decel_lim_x: -0.8       # m/s² 감속도 제한

local_costmap:
  local_costmap:
    ros__parameters:
      width: 4.0              # m
      height: 4.0
      resolution: 0.05        # 5cm 격자
      plugins: ["obstacle_layer", "inflation_layer"]
      obstacle_layer:
        observation_sources: depth_scan
        depth_scan:
          topic: /camera/depth/image_rect_raw
          max_obstacle_height: 2.0
          obstacle_max_range: 4.0
      inflation_layer:
        inflation_radius: 0.4  # 장애물 팽창 반경 (m)
        cost_scaling_factor: 3.0
```

---

## 7. 기존 시스템 재사용 범위

| 기존 자산 | 재사용 여부 | 비고 |
|---|---|---|
| STM32H753ZI 보드 | ✅ 100% 재사용 | 명령 2개 추가 |
| UART bridge 프로토콜 | ✅ 그대로 사용 | AGV_VELOCITY 패킷 추가 |
| EtherCAT SOEM | ✅ 재사용 | 6축 → 2축, 속도 모드 변경 |
| SLIP 통신 프레임 | ✅ 그대로 사용 | |
| PC GUI (index.html) | ✅ AGV 탭 추가 | 맵·스케줄 모니터링 |
| 확장 명령 (FAULT_RESET 등) | ✅ 그대로 활용 | |
| 인터폴레이터 | ⚠️ 불필요 | Nav2 /cmd_vel로 대체 |
| 6축 관절 로직 | ⚠️ 2축만 사용 | |

---

## 8. 파일 구조 (신규 추가)

```
PC_GUI/
├── bridge/                     ← 기존 (최소 수정)
│   ├── bridge.py               ← AGV_VELOCITY 명령 추가
│   └── packet_defs.py          ← 0x30~0x32 패킷 추가
│
└── agv/                        ← 신규 추가
    ├── requirements.txt
    ├── config/
    │   ├── mission.yaml        ← 작업 스케줄 정의
    │   ├── nav2_params.yaml    ← Nav2 설정
    │   ├── ekf.yaml            ← 센서 융합 설정
    │   └── robot.yaml          ← 로봇 물리 파라미터
    ├── launch/
    │   ├── slam.launch.py      ← 맵핑 모드 실행
    │   ├── nav.launch.py       ← 자율 주행 실행
    │   └── full_system.launch.py
    ├── maps/                   ← 생성된 맵 저장
    │   ├── orchard_spring.db
    │   ├── orchard_summer.db
    │   └── orchard_autumn.db
    └── src/
        ├── stm32_bridge_node.py    ← ROS2 ↔ STM32 인터페이스
        ├── row_follower.py         ← 나무 열 추종
        ├── obstacle_classifier.py  ← 장애물 분류 대응
        ├── task_scheduler.py       ← 작업 스케줄 관리
        └── orchard_navigator.py    ← 과수원 전용 경로 생성

Appli/Core/
├── Inc/
│   └── uart_protocol.h    ← 0x30~0x32 패킷 추가
└── Src/
    └── uart_protocol.c    ← AGV 명령 디스패처 추가
```

---

## 9. 안전 고려사항

| 위험 요소 | 대책 |
|---|---|
| 사람 접근 | YOLOv8 감지 즉시 완전 정지 + 경보 |
| SLAM 위치 추정 실패 | 추정 신뢰도 임계값 이하 → 즉시 정지 |
| 배터리 저하 | 잔량 20% 이하 → 자동 충전 스테이션 복귀 |
| 통신 두절 | Heartbeat 소실 2초 → 안전 정지 |
| 경사로·함몰 지형 | IMU 자세각 ±15° 초과 → 정지 |
| GPS 신호 없음 | Visual SLAM 단독 모드로 자동 전환 |
| 모터 과열 | 온도 센서 80°C 초과 → 저속 모드 |
| 소프트 리밋 이탈 | Geofence 경계 접근 시 감속 → 정지 |

### 권장 초기 운용 순서

```
1. 수동 조이스틱 주행으로 과수원 맵 생성
2. GAIN 매우 낮게 설정 후 짧은 구간 자율 주행 테스트
3. 장애물 회피 단독 테스트 (사람이 앞에 서기)
4. 한 열(Row) 전체 자율 주행 검증
5. 전체 과수원 자율 순찰 (저속 0.2m/s)
6. 속도 단계적 증가 및 스케줄 작업 추가
```

---

## 10. 구현 로드맵

```
Phase 1 — 기본 주행 (2주)
  └─ STM32 AGV_VELOCITY 패킷 추가
  └─ 엔코더 오도메트리 구현
  └─ ROS2 /cmd_vel → STM32 인터페이스
  └─ 조이스틱 수동 주행 검증

Phase 2 — 맵핑 (3주)
  └─ Jetson + RealSense D435i + RTAB-Map 설치
  └─ 과수원 수동 주행으로 첫 맵 생성
  └─ 계절별 맵 3종 생성 및 저장

Phase 3 — 자율 주행 (2주)
  └─ Nav2 설정 (DWB 로컬 플래너)
  └─ 웨이포인트 자동 주행 검증
  └─ 나무 열 추종 알고리즘 구현 및 튜닝

Phase 4 — 장애물 회피 (2주)
  └─ YOLOv8 장애물 분류 적용
  └─ 3단계 안전 레이어 통합
  └─ 사람·동물·차량 감지 대응 검증

Phase 5 — 작업 자동화 (2주)
  └─ 작업 스케줄러 구현 (mission.yaml)
  └─ 방제기·수확 장치 액추에이터 연동
  └─ 원격 모니터링 웹 대시보드 추가
  └─ 착과 감지 데이터 수집 및 저장

Total: 약 11주
```

---

## 11. 비용 예상 (참고)

| 항목 | 예상 가격 |
|---|---|
| Jetson Orin Nano 8GB Dev Kit | 약 500 USD |
| Intel RealSense D435i | 약 280 USD |
| RTK-GPS 모듈 (선택) | 약 200~500 USD |
| 48V BLDC 서보 모터 ×2 | 약 300~800 USD |
| 배터리 48V LiFePO4 | 약 300~600 USD |
| AGV 차체 프레임 | 약 500~1000 USD |
| 기타 (배선, 마운트, 방수 케이스) | 약 200 USD |
| **합계 (기존 STM32 보드 제외)** | **약 2,300~3,900 USD** |

---

## 12. 확장 가능성

```
현재 설계에서 추가 가능한 기능:

단기 (완성 후 즉시):
  ├── 착과 개수 카운팅 (YOLO 과실 감지)
  ├── 병해충 조기 탐지 (이미지 분류)
  └── 작업 이력 데이터베이스 저장

중기 (6개월):
  ├── 수확 로봇 팔 (6축 ARM) 탑재 → 완전 자동화
  ├── 드론 연계 상공 촬영 + 지상 AGV 협업
  └── 다중 AGV 군집 주행 (Fleet Management)

장기 (1년+):
  ├── AI 수확 시기 예측 (과실 색상 분석)
  ├── 클라우드 데이터 분석 대시보드
  └── 5G 원격 제어 및 실시간 스트리밍
```

---

*이 문서는 N753-6AX 컨트롤러 기반 농업용 과수원 AGV 시스템 설계 제안서입니다.*  
*실제 구현 시 과수원 규모·지형·작업 종류에 따라 파라미터 튜닝이 필요합니다.*
