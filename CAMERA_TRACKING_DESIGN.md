# 카메라 기반 로봇 시각 추적 시스템 설계 제안서

**프로젝트**: N753-6AX 6축 다관절 로봇  
**작성일**: 2026-06-04  
**대상 보드**: STM32H753ZI (N753-6AX)

---

## 1. 개요

현재 6축 다관절 로봇 시스템에 카메라를 추가하여 목표물을 자동으로 인식하고  
로봇이 실시간으로 추적(Visual Servoing)할 수 있는 시스템을 구축한다.

### 핵심 설계 원칙

- **STM32 펌웨어 무변경**: 모든 비전 처리는 PC에서 수행
- **기존 bridge.py 확장**: WebSocket 인터페이스로 `tracker.py` 연결
- **단계적 구현**: Phase 1 → 4 순서로 복잡도 증가
- **실시간 제어 루프**: 10~30Hz 추적 주기 목표

---

## 2. 전체 시스템 아키텍처

```
┌──────────────────────────────────────────────────────────────┐
│                            PC                                │
│                                                              │
│  ┌───────────┐   ┌──────────────────┐   ┌────────────────┐  │
│  │  Camera   │──▶│   tracker.py     │──▶│   bridge.py    │  │
│  │(USB/Depth)│   │  ┌────────────┐  │   │ (기존 브릿지)   │  │
│  └───────────┘   │  │ 객체 감지  │  │   └───────┬────────┘  │
│                  │  ├────────────┤  │           │           │
│                  │  │ 좌표 변환  │  │      WebSocket         │
│                  │  ├────────────┤  │           │           │
│                  │  │ 역기구학   │  │           │           │
│                  │  └────────────┘  │           │           │
│                  └──────────────────┘           │           │
│                         HMI (index.html) ◀──────┘           │
└─────────────────────────────────────────────────┼───────────┘
                                                  │ UART 921600bps
                                       ┌──────────▼──────────┐
                                       │    STM32H753ZI      │
                                       │  (펌웨어 수정 없음)  │
                                       └──────────┬──────────┘
                                                  │ EtherCAT
                                       ┌──────────▼──────────┐
                                       │  6축 로봇 + 카메라   │
                                       └─────────────────────┘
```

### 데이터 흐름

```
Camera Frame (30fps)
  │
  ▼
객체 감지 / 포즈 추정
  │  target_xyz [X, Y, Z] (mm)
  ▼
좌표계 변환 (Camera → Robot Base)
  │  target_xyz_robot
  ▼
역기구학 (IK Solver)
  │  joint_angles [J1~J6] (°)
  ▼
MOVE_JOINT_SYNC → bridge.py → STM32 → 서보
```

---

## 3. 추천 하드웨어

### 3.1 카메라 선택

| 모델 | 해상도 | Depth | FPS | 가격대 | 추천 용도 |
|---|---|---|---|---|---|
| Logitech C920 | 1080p | ✗ | 30 | 저가 | Phase 1·3 (색상·YOLO) |
| **Intel RealSense D435** | 1280×720 | ✅ | 30~90 | 중가 | **Phase 2·4 (정밀 추적)** |
| Intel RealSense D455 | 1280×800 | ✅ | 30~90 | 중고가 | 넓은 시야각 필요 시 |
| Basler acA1920 | 1920×1200 | ✗ | 164 | 고가 | 산업용 정밀 작업 |

> **최우선 추천: Intel RealSense D435**  
> RGB + 깊이(Depth) 동시 제공 → Z축 모호성 해결 → IK 정확도 향상

### 3.2 카메라 장착 방식

| 방식 | 특징 | 적합한 Phase |
|---|---|---|
| **Eye-to-Hand** | 작업 공간 전체 관찰, 좌표 변환 1회 | Phase 1~3 (추천 시작점) |
| **Eye-in-Hand** | 툴 끝단 장착, 더 정밀하나 캘리브레이션 복잡 | Phase 4 |

---

## 4. 구현 단계 (Phase)

---

### Phase 1 — 색상 기반 추적

**난이도**: ★☆☆☆☆  
**구현 시간**: 1~2일  
**원리**: HSV 색공간에서 특정 색 마스크 → 무게중심 → 관절 보정

#### 제어 개념
```
화면 중심 오차 (px) → 비례 게인 → 관절 보정량 (°)

err_x = target_cx - frame_cx   →  ΔJ1 (좌우 회전)
err_y = target_cy - frame_cy   →  ΔJ2 (상하 회전)
```

#### 핵심 코드
```python
# tracker_phase1.py
import cv2
import numpy as np
import asyncio, websockets, json

GAIN_X = 0.05   # 픽셀 → 도 변환 게인 (튜닝 필요)
GAIN_Y = 0.03
LOOP_HZ = 10

async def track():
    ws = await websockets.connect("ws://localhost:8765")
    cap = cv2.VideoCapture(0)
    H, W = 480, 640

    while True:
        ret, frame = cap.read()
        if not ret:
            continue

        hsv  = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        mask = cv2.inRange(hsv, (0, 120, 70), (10, 255, 255))  # 빨간색
        M    = cv2.moments(mask)

        if M["m00"] > 500:
            cx = int(M["m10"] / M["m00"])
            cy = int(M["m01"] / M["m00"])

            err_x = cx - W / 2
            err_y = cy - H / 2

            await ws.send(json.dumps({
                "cmd":      "jog",
                "axis":     0,           # J1
                "direction": 1 if err_x > 0 else -1,
                "step_deg": abs(err_x) * GAIN_X,
            }))

        await asyncio.sleep(1 / LOOP_HZ)

asyncio.run(track())
```

#### 한계
- 조명 변화에 민감
- 2D 제어만 가능 (깊이 정보 없음)
- 같은 색 배경에서 오동작

---

### Phase 2 — ArUco 마커 추적

**난이도**: ★★☆☆☆  
**구현 시간**: 3~5일  
**원리**: 마커 4 코너 → 6DOF 포즈 추정 → IK → 관절 제어

#### 시스템 구성
```
ArUco 마커 (목표물 부착)
        │
        ▼
cv2.aruco.detectMarkers()
        │ corners, ids
        ▼
cv2.aruco.estimatePoseSingleMarkers()
        │ rvec (회전), tvec (위치 mm)
        ▼
좌표 변환: Camera Frame → Robot Base Frame
        │ target_xyz (로봇 기저 좌표계)
        ▼
IK Solver (ikpy 등)
        │ joint_angles [J1~J6]
        ▼
MOVE_JOINT_SYNC
```

#### 카메라 캘리브레이션 필수
```python
# 체스보드 패턴으로 내부 파라미터 추정
camera_matrix = np.array([[fx,  0, cx],
                           [ 0, fy, cy],
                           [ 0,  0,  1]])
dist_coeffs = np.array([k1, k2, p1, p2, k3])
```

#### Eye-to-Hand 좌표 변환
```python
# 카메라 → 로봇 베이스 변환행렬 (핸드-아이 캘리브레이션)
T_cam_to_base = calibrate_hand_eye(R_gripper2base,
                                    t_gripper2base,
                                    R_target2cam,
                                    t_target2cam)

target_robot = T_cam_to_base @ np.append(tvec, 1)
```

#### 특징
- 조명 변화에 강함
- 정확한 3D 위치·자세 추정
- 마커 크기 알면 절대 거리 계산 가능

---

### Phase 3 — YOLO 객체 인식 + 추적

**난이도**: ★★★☆☆  
**구현 시간**: 1주  
**원리**: 딥러닝 객체 감지 → 바운딩박스 중심 → 깊이 카메라로 Z 추정

#### 권장 모델

| 모델 | 속도 | 정확도 | 사용 환경 |
|---|---|---|---|
| YOLOv8n | 매우 빠름 | 보통 | CPU 실시간 가능 |
| YOLOv8s | 빠름 | 좋음 | GPU 권장 |
| YOLOv8m | 보통 | 우수 | GPU 필수 |
| **커스텀 학습** | 빠름 | **최우수** | 특정 목표물 전용 |

#### 핵심 코드
```python
from ultralytics import YOLO
import pyrealsense2 as rs

model = YOLO('yolov8n.pt')

# RealSense D435 설정
pipeline = rs.pipeline()
config   = rs.config()
config.enable_stream(rs.stream.color, 640, 480, rs.format.bgr8, 30)
config.enable_stream(rs.stream.depth, 640, 480, rs.format.z16,  30)
pipeline.start(config)

while True:
    frames      = pipeline.wait_for_frames()
    color_frame = frames.get_color_frame()
    depth_frame = frames.get_depth_frame()

    image   = np.asanyarray(color_frame.get_data())
    results = model(image, classes=[0])  # 0 = person

    for box in results[0].boxes:
        cx, cy = map(int, box.xywh[0][:2])
        depth  = depth_frame.get_distance(cx, cy)  # 미터

        # 픽셀 → 3D 변환
        target_xyz = rs.rs2_deproject_pixel_to_point(
            depth_frame.profile.as_video_stream_profile().intrinsics,
            [cx, cy], depth
        )
        # target_xyz = [X, Y, Z] (미터, 카메라 좌표계)
        # → 좌표 변환 → IK → MOVE_JOINT_SYNC
```

---

### Phase 4 — 완전한 Visual Servoing

**난이도**: ★★★★☆  
**구현 시간**: 3~4주  
**원리**: 이미지 특징점 오차를 직접 관절 속도로 변환

#### 두 가지 방식

**IBVS (Image-Based Visual Servoing)**
```
이미지 특징점 오차 e = s_d - s (픽셀 공간)
   ↓
관절 속도 = λ · L†_s · e    (이미지 야코비안 역행렬)
```

**PBVS (Position-Based Visual Servoing)** ← 추천
```
3D 목표 위치 추정 (카메라 포즈)
   ↓
로봇 기저 좌표계 변환
   ↓
역기구학 (IK) → 관절각
   ↓
MOVE_JOINT_SYNC 전송
```

#### IK 라이브러리

```python
# ikpy 사용 예
from ikpy.chain import Chain

robot_chain = Chain.from_urdf_file("robot.urdf")

target_position = [x, y, z]
target_orientation = [[1,0,0],[0,1,0],[0,0,1]]

joints = robot_chain.inverse_kinematics(
    target_position    = target_position,
    target_orientation = target_orientation,
    orientation_mode   = "X"
)
# joints[1:7] → J1~J6 각도 (라디안)
```

#### 완전한 제어 루프
```python
class VisualServoController:
    LOOP_HZ  = 20        # 제어 주기
    GAIN     = 0.4       # 비례 게인
    MAX_STEP = 5.0       # 최대 관절 이동량 (°/cycle)

    async def servo_loop(self):
        while self.running:
            # 1. 목표물 감지
            target_cam = self.detector.get_target_pose()
            if target_cam is None:
                await asyncio.sleep(1 / self.LOOP_HZ)
                continue

            # 2. 좌표 변환 (카메라 → 로봇 베이스)
            target_base = self.T_cam2base @ target_cam

            # 3. 역기구학
            joints_target = self.ik.solve(target_base[:3])

            # 4. 현재 관절각과 비교 → 게인 적용
            joints_cmd = self.current_joints + \
                         self.GAIN * (joints_target - self.current_joints)

            # 5. 최대 이동량 제한 (안전)
            delta = joints_cmd - self.current_joints
            delta = np.clip(delta, -self.MAX_STEP, self.MAX_STEP)
            joints_cmd = self.current_joints + delta

            # 6. 브릿지로 전송
            await self.ws.send(json.dumps({
                "cmd":      "move_joint_sync",
                "j1":  float(joints_cmd[0]),
                "j2":  float(joints_cmd[1]),
                "j3":  float(joints_cmd[2]),
                "j4":  float(joints_cmd[3]),
                "j5":  float(joints_cmd[4]),
                "j6":  float(joints_cmd[5]),
                "duration": 1.2 / self.LOOP_HZ,
            }))

            await asyncio.sleep(1 / self.LOOP_HZ)
```

---

## 5. 핸드-아이 캘리브레이션

추적 정확도의 핵심 — **카메라와 로봇 좌표계 관계 측정**

### Eye-to-Hand 방식 (추천)

```
카메라 고정 장착 → 로봇이 여러 자세로 이동 →
각 자세에서 마커 포즈 측정 → 최소자승법으로 T 추정
```

```python
import cv2

# 여러 자세에서 측정한 데이터
R_gripper2base_list = [...]  # 각 자세의 회전행렬
t_gripper2base_list = [...]  # 각 자세의 위치벡터
R_target2cam_list   = [...]  # 카메라에서 본 마커 회전
t_target2cam_list   = [...]  # 카메라에서 본 마커 위치

R, t = cv2.calibrateHandEye(
    R_gripper2base_list,
    t_gripper2base_list,
    R_target2cam_list,
    t_target2cam_list,
    method=cv2.CALIB_HAND_EYE_TSAI  # 또는 PARK, HORAUD
)
# R, t → T_cam_to_base 변환행렬 구성
```

**권장 캘리브레이션 자세 수**: 최소 10~15개, 다양한 방향 포함

---

## 6. 소프트웨어 스택 및 설치

### 필수 패키지

```bash
# 기본 비전
pip install opencv-python
pip install opencv-contrib-python   # ArUco 포함

# 딥러닝 추적
pip install ultralytics             # YOLOv8

# 깊이 카메라
pip install pyrealsense2            # Intel RealSense

# 역기구학
pip install ikpy                    # 간단한 IK
pip install roboticstoolbox-python  # 풍부한 로봇 수학

# 추가 유틸
pip install mediapipe               # 손·얼굴·자세
pip install numpy scipy
pip install websockets              # bridge.py 연결
```

### URDF 모델 (Phase 4 필수)

```xml
<!-- robot.urdf — 실제 DH 파라미터로 작성 -->
<robot name="6axis">
  <link name="base"/>
  <joint name="J1" type="revolute">
    <parent link="base"/>
    <child  link="link1"/>
    <axis xyz="0 0 1"/>
    <limit lower="-3.14" upper="3.14" velocity="1.0" effort="100"/>
    <origin xyz="0 0 0.4" rpy="0 0 0"/>  <!-- d1 = 400mm -->
  </joint>
  <!-- J2~J6 동일 구조 -->
</robot>
```

---

## 7. 파일 구조

```
PC_GUI/
├── bridge/
│   ├── bridge.py          ← 기존 (수정 없음)
│   ├── packet_defs.py     ← 기존 (수정 없음)
│   ├── index.html         ← 기존 (추적 상태 표시 추가)
│   └── slip_codec.py      ← 기존 (수정 없음)
│
└── tracker/               ← 신규 추가
    ├── tracker.py         ← 메인 추적 프로그램
    ├── detector/
    │   ├── color.py       ← Phase 1: 색상 감지
    │   ├── aruco.py       ← Phase 2: ArUco 감지
    │   ├── yolo.py        ← Phase 3: YOLO 감지
    │   └── realsense.py   ← RealSense 래퍼
    ├── ik/
    │   ├── ik_solver.py   ← 역기구학 통합 인터페이스
    │   └── robot.urdf     ← 로봇 URDF 모델
    ├── calibration/
    │   ├── camera_cal.py  ← 카메라 내부 파라미터 캘리브레이션
    │   ├── hand_eye.py    ← 핸드-아이 캘리브레이션
    │   └── params/        ← 캘리브레이션 결과 저장
    │       ├── camera_matrix.npy
    │       ├── dist_coeffs.npy
    │       └── T_cam2base.npy
    └── config.yaml        ← 설정 파일 (게인, IP, 포트 등)
```

---

## 8. 안전 고려사항

| 위험 요소 | 대책 |
|---|---|
| 목표물 갑자기 사라짐 | 목표 소실 시 즉시 정지 (Interp_Stop) |
| 과도한 관절 이동 | MAX_STEP 제한 + 소프트 리밋 확인 |
| IK 수렴 실패 | 예외 처리 → 현재 위치 유지 |
| 통신 지연 (레이턴시) | duration = 제어주기 × 1.5 (여유분) |
| 작업 공간 이탈 | IK 전 목표 위치 가용 작업공간 확인 |
| 카메라 캘리브레이션 오차 | 초기 동작 속도 저감 (GAIN 낮게 시작) |

### 권장 안전 순서
1. GAIN = 0.1 (매우 느리게) 로 시작
2. E-STOP 버튼 항상 준비
3. 관절 소프트 리밋 반드시 설정
4. 처음에는 저속 MOVE_JOINT_SYNC (duration 길게)

---

## 9. 구현 로드맵

```
Week 1  : 카메라 설치 + Phase 1 색상 추적 동작 확인
Week 2  : 카메라 캘리브레이션 + ArUco Phase 2 구현
Week 3  : YOLO 모델 선택 + Phase 3 객체 추적
Week 4  : 핸드-아이 캘리브레이션 + IK 통합
Week 5+ : Phase 4 Visual Servoing 튜닝 및 최적화
```

---

## 10. 추천 시작점

> 1. **Intel RealSense D435** 구매
> 2. **Phase 2 ArUco** 부터 시작 (색상 추적보다 안정적)
> 3. 마커를 목표물에 부착, 카메라 고정 (Eye-to-Hand)
> 4. 캘리브레이션 완료 후 `ikpy` 로 IK 검증
> 5. 브릿지에 연결하여 실제 로봇 동작 확인

---

*이 문서는 N753-6AX 6축 다관절 로봇 프로젝트의 카메라 추적 시스템 설계 제안서입니다.*  
*실제 구현 시 로봇 DH 파라미터 및 카메라 캘리브레이션 결과에 따라 수정이 필요합니다.*
