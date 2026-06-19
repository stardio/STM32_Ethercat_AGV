# GPS 통합 계획서 — AGV Phase 7

> U-blox GNSS 수신기를 미니PC Ubuntu USB 포트에 연결하여  
> ROS2 EKF에 GPS를 융합하고 글로벌 절대 위치를 확보한다.

## 진행 현황 (2026-06-18)

| Step | 내용 | 상태 |
|------|------|------|
| Step 1 | 하드웨어 인식, udev 고정명 `/dev/gps` | ✅ 완료 |
| Step 2 | U-blox 10Hz 설정 (38400 baud, SBAS) | ✅ 완료 |
| Step 3 | ROS2 GPS 드라이버 `/gps/fix` 발행 | ✅ 완료 |
| Step 4 | navsat_transform_node `/odometry/gps` (ENU 변환) | ✅ 완료 |
| Step 5 | EKF GPS 융합 (`odom1: /odometry/gps`) | ✅ 완료 |
| Step 6 | URDF gps_link 추가 | ✅ 완료 |

### 실제 구현 vs 계획 차이점

- **GPS 드라이버**: `nmea_navsat_driver` 대신 **커스텀 `gps_driver_node`** 사용
  - 이유: U-blox가 NMEA+UBX 바이너리 혼합 출력 → nmea_navsat_driver 파싱 오류
  - `gps_driver_node.py`가 UBX 바이너리를 필터링하고 NMEA만 파싱
- **FastDDS SHM 비활성화 필요**: Nav2 실행 중일 때 SHM 포트 충돌
  - `FASTRTPS_DEFAULT_PROFILES_FILE=$WS/fastdds_no_shm.xml` 설정 필수
- **U-blox 디바이스**: `/dev/ttyUSB0` (FTDI FT232, `0403:6001`), baud=38400
  - udev 고정: `/dev/gps → ttyUSB0`
- **GPS 성능**: Fix quality=2 (SBAS), HDOP=0.91, 위성 12개, 위치 안정
- **navsat_transform 전용 EKF 추가**: `ekf_gps_node` (`config/ekf_gps.yaml`)
  - navsat_transform은 `/odometry/filtered`(EKF 출력)가 필수
  - row_follow.launch의 `ekf_filter_node`와 이름 분리하여 충돌 없음
  - `/odom`(STM32) → EKF → `/odometry/filtered` → navsat_transform → `/odometry/gps`
- **EKF GPS 융합 (Step 5)**: `ekf_gps.yaml` + `ekf.yaml` + `ekf_odom_only.yaml` 모두 `odom1: /odometry/gps` 추가
  - EKF는 navsat_transform 시작(t≈8s) 전까지 /odom만으로 동작
  - /odometry/gps 수신 시작 → 자동으로 GPS 위치 보정 융합 (재시작 불필요)
  - GPS 없을 때도 sensor_timeout으로 graceful degradation
- **URDF gps_link (Step 6)**: `agv.urdf.xacro`에 base_link → gps_link 고정 조인트 추가 (z=0.35m)
  - 현장 실측 후 xyz 오프셋 수정 필요 (`agv.urdf.xacro` L111)

### 실행 방법

```bash
# 전제: bridge.py 실행 중 (STM32 /odom 필요)

# GPS 전체 스택 실행 (gps_config → gps_driver → ekf_gps_node → navsat_transform)
cd ros2_ws && ./start_gps.sh

# 타임라인:
#   t=0s  gps_config      — U-blox UBX 비활성화, 10Hz 설정
#   t=4s  gps_driver      — /gps/fix 발행 시작
#   t=4s  ekf_gps_node    — /odom → /odometry/filtered (GPS 피드백 준비)
#   t=8s  navsat_transform — /gps/fix → /odometry/gps (첫 fix 후 ENU 변환)
#   t≈10s ekf_gps_node    — /odometry/gps 수신 시작 → GPS+odom 융합 활성화

# 토픽 확인
ros2 topic echo /gps/fix            # 원시 GPS 위치 (10Hz)
ros2 topic echo /odometry/filtered  # EKF 융합 출력 (GPS 반영, 20Hz)
ros2 topic echo /odometry/gps       # ENU 로컬 좌표 (10Hz)
ros2 topic hz /odometry/gps         # 목표: ~10Hz

# 정상 출력 예시 (/odometry/gps 첫 fix 기준)
# pose.pose.position.x: ~0.0  (원점에서 동쪽 거리 m)
# pose.pose.position.y: ~0.0  (원점에서 북쪽 거리 m)
# 10m 북쪽 이동 후: y ≈ 10.0

# GPS 융합 품질 확인
ros2 topic echo /gps/filtered       # EKF 결과를 GPS 좌표로 역변환 (디버그)
```

---

## 시스템 아키텍처

```
[U-blox 수신기 + 안테나]
        │ USB (/dev/gps → ttyUSB0, 38400 baud)
        ▼
[gps_config]  — UBX 비활성화, 10Hz, BBR 저장 (t=0s, 1회)
        │
[gps_driver_node]  →  /gps/fix  (NavSatFix, 10Hz, t=4s~)
        │             /gps/vel  (TwistWithCovarianceStamped)
        │
        ├── [ekf_gps_node]  ←  /odom (STM32 엔코더)    (t=4s~)
        │         │  /odometry/filtered (20Hz)
        │         ▼
        └── [navsat_transform_node]  ←  /odometry/filtered  (t=8s~)
                  │  /odometry/gps (ENU 로컬 x/y m, 10Hz)   ← Step 4 출력
                  ▼
        [EKF (Step 5)] ←  /odom + /imu + /odometry/gps 융합
                  │  /odometry/filtered (글로벌 보정 위치)
                  ▼
        [Nav2 / 브라우저 경로 주행]
```

---

## 진행 체크리스트

### ✅ 준비 (완료 전제)
- [ ] U-blox 수신기 USB 연결
- [ ] 안테나 외부 설치 (개방 하늘 시야 확보)

---

### Step 1 — 하드웨어 인식 확인

**목표**: Jetson이 U-blox를 USB 시리얼로 인식하고 NMEA 문장이 출력되는지 확인

**명령어**:
```bash
# 디바이스 확인
ls /dev/ttyACM* /dev/ttyUSB*
dmesg | grep -E "tty|u-blox|ublox|CP210|FTDI" | tail -20

# NMEA 출력 확인 (Ctrl+A K 로 종료)
screen /dev/ttyACM0 9600
# 또는 115200이면:
screen /dev/ttyACM0 115200
```

**정상 출력 예시**:
```
$GNGGA,123519.00,3546.123,N,12838.456,E,1,08,0.9,545.4,M,46.9,M,,*47
$GNRMC,123519.00,A,3546.123,N,12838.456,E,0.0,0.0,160618,,,A*7C
```

**완료 기준**: `$GNGGA` 또는 `$GNRMC` 문장이 주기적으로 출력됨

**문제 해결**:
```bash
# 권한 오류 시
sudo usermod -aG dialout bs
# 로그아웃 후 재로그인 필요

# baud rate 모를 때 순서대로 시도
for baud in 9600 38400 115200; do
  echo "--- $baud ---"
  timeout 3 cat <(stty -F /dev/ttyACM0 $baud; cat /dev/ttyACM0) 2>/dev/null | head -5
done
```

---

### Step 2 — U-blox 설정 최적화

**목표**: 10Hz 출력, SBAS 활성화, 필요한 NMEA 문장만 활성화

**방법 A — ubxtool 사용 (CLI, 권장)**:
```bash
pip3 install gpsd-py3 pyserial
sudo apt install gpsd gpsd-clients

# gpsd로 디바이스 연결
sudo gpsd /dev/ttyACM0 -F /var/run/gpsd.sock

# 상태 확인
cgps -s
gpsmon
```

**방법 B — u-center (Windows PC에서 USB 연결하여 사전 설정)**:
- Configuration → Rate → Measurement Period: 100ms (10Hz)
- Configuration → NMEA → GGA, RMC, GSA 활성화 / GLL, GSV, VTG 비활성화
- Configuration → SBAS → Enable 체크
- Configuration → Save (BBR + Flash 저장)

**권장 NMEA 문장**:
| 문장 | 내용 | 활성화 |
|------|------|--------|
| GGA  | 위치 + 고도 + fix quality | ✅ |
| RMC  | 위치 + 속도 + 날짜 | ✅ |
| GSA  | 위성 DOP | ✅ |
| GSV  | 위성 상세 | ❌ (데이터 과다) |
| GLL  | 위치만 | ❌ (GGA 중복) |
| VTG  | 지면 속도/방향 | ❌ (RMC 중복) |

**완료 기준**: `$GNGGA` 10Hz 출력, fix quality field = `1` 이상 (SBAS=`2`)

---

### Step 3 — ROS2 nmea_navsat_driver 설치 및 테스트

**목표**: `/fix` (NavSatFix) 토픽이 ROS2에서 발행되는지 확인

**설치**:
```bash
sudo apt install ros-humble-nmea-navsat-driver
```

**launch 파일 작성** (`ros2_ws/src/agv_bringup/launch/gps.launch.py`):
```python
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(
            package='nmea_navsat_driver',
            executable='nmea_serial_driver',
            name='gps_driver',
            output='screen',
            parameters=[{
                'port': '/dev/ttyACM0',
                'baud': 115200,
                'frame_id': 'gps_link',
                'time_ref_source': 'gps',
                'useRMC': False,
            }],
            remappings=[('/fix', '/gps/fix'), ('/vel', '/gps/vel')],
        ),
    ])
```

**테스트**:
```bash
source ros2_ws/install/setup.bash
ros2 launch agv_bringup gps.launch.py

# 다른 터미널
ros2 topic echo /gps/fix
ros2 topic hz /gps/fix   # 목표: ~10Hz
```

**완료 기준**:
- `status.status >= 0` (0=fix, 1=SBAS, 2=GBAS)
- `position_covariance[0]` < 25.0 (표준편차 5m 이내)
- 10Hz 수신

---

### Step 4 — navsat_transform_node 설정

**목표**: GPS lat/lon → 로컬 ENU 좌표(x, y m)로 변환하여 `/odometry/gps` 발행

**설치 확인**:
```bash
sudo apt install ros-humble-robot-localization
```

**설정 파일** (`ros2_ws/src/agv_bringup/config/navsat_transform.yaml`):
```yaml
navsat_transform_node:
  ros__parameters:
    frequency: 10.0
    delay: 0.0
    # 현장 자기 편각 — 아래 사이트에서 확인
    # https://www.magnetic-declination.com/
    magnetic_declination_radians: 0.0   # 한국 중부: 약 -0.13 rad (-7.5°)
    yaw_offset: 0.0                     # GPS 안테나 장착 방향 보정
    zero_altitude: true                 # 고도 무시 (평지 AGV)
    broadcast_utm_transform: true       # UTM 좌표 TF 발행
    broadcast_utm_transform_as_parent_frame: false
    publish_filtered_gps: true          # 필터 결과를 GPS 좌표로 역변환 발행
    use_odometry_yaw: true              # IMU yaw 우선 사용 (GPS heading 노이즈 큼)
    wait_for_datum: false               # 첫 fix를 자동 원점(datum)으로 사용
```

**노드 추가** (gps.launch.py에 추가):
```python
Node(
    package='robot_localization',
    executable='navsat_transform_node',
    name='navsat_transform',
    output='screen',
    parameters=['config/navsat_transform.yaml'],
    remappings=[
        ('/imu/data',           '/camera/imu'),      # D435i IMU
        ('/gps/fix',            '/gps/fix'),
        ('/odometry/filtered',  '/odometry/filtered'),
        ('/odometry/gps',       '/odometry/gps'),
    ],
),
```

**테스트**:
```bash
ros2 topic echo /odometry/gps
# x, y가 미터 단위로 출력되어야 함 (제자리: ~0,0)
# 1m 이동 시 x 또는 y가 ~1 변화
```

**완료 기준**: `/odometry/gps`의 pose.pose.position.x/y가 이동에 따라 합리적으로 변화

---

### Step 5 — EKF에 GPS 융합

**목표**: 기존 odom+IMU EKF에 GPS를 추가하여 글로벌 위치 보정

**ekf.yaml 수정** (`ros2_ws/src/agv_bringup/config/ekf.yaml`):
```yaml
# 기존 설정 유지 + 아래 추가
odom1: /odometry/gps
odom1_config: [true,  true,  false,    # x, y 위치 사용
               false, false, false,
               false, false, false,
               false, false, false,
               false, false, false]
odom1_differential: false
odom1_relative: false
odom1_queue_size: 10
odom1_nodelay: false

# GPS 위치 공분산 허용 임계값
odom1_rejection_threshold: 2.0
```

**재빌드 및 테스트**:
```bash
cd ros2_ws && colcon build --symlink-install --packages-select agv_bringup
source install/setup.bash

ros2 launch agv_bringup hardware.launch.py   # STM32 브릿지
ros2 launch agv_bringup gps.launch.py        # GPS + navsat_transform
# ekf는 hardware.launch.py 안에 포함됨

# GPS 융합 확인
ros2 topic echo /odometry/filtered
```

**완료 기준**:
- 로봇 정지 시 `/odometry/filtered`의 x/y 드리프트가 GPS 없을 때보다 작음
- 장거리 주행 후 귀환 시 원점 오차 < 3m

---

### Step 6 — URDF에 GPS 링크 추가

**목표**: GPS 안테나 위치를 TF 트리에 등록

**agv.urdf.xacro에 추가**:
```xml
<!-- GPS 안테나 링크 (로봇 중심 기준 오프셋 측정 후 수정) -->
<link name="gps_link"/>
<joint name="gps_joint" type="fixed">
  <parent link="base_link"/>
  <child  link="gps_link"/>
  <origin xyz="0.0 0.0 0.3" rpy="0 0 0"/>  <!-- 실측 후 수정 -->
</joint>
```

---

### Step 7 — 활용 기능 구현

완료 후 확장 가능한 기능:

#### 7-A. GPS 좌표로 경로 웨이포인트 저장
- 브라우저 HMI에서 현재 GPS fix → 웨이포인트 추가
- `_navRoutePts`에 lat/lon 병기 → 다음 날 같은 경로 재현

#### 7-B. Geofencing GPS 기반 강화
- `config/geofence.yaml`의 다각형을 GPS 좌표(UTM)로 정의
- `enabled: true`로 활성화

#### 7-C. Return-to-Home GPS 기반
- 시작 시 GPS 좌표 저장
- 미션 완료 또는 비상 시 GPS 좌표로 귀환

#### 7-D. RTK 업그레이드 (추후)
- U-blox F9P + RTK base station 추가
- 정확도 5m → 2cm

---

## 정확도 기대치

| 모드 | 위치 정확도 | 과수원 활용 |
|------|------------|------------|
| 표준 GNSS | 2~5m | 밭 진입/귀환 |
| SBAS (WAAS/MSAS) | 1~2m | 구역 간 이동 |
| RTK (추후) | 1~3cm | 줄 간 정밀 추종 |

> 과수원 캐노피 아래에서는 신호 감쇠로 정확도 저하 가능.  
> EKF 융합으로 GPS 신호 끊기면 odom+IMU로 자동 전환됨.

---

## 현장 설치 주의사항

1. **안테나 위치**: 최대한 높게, 금속 지붕 위 부착 (지평선 시야각 > 15°)
2. **자기 편각**: [magnetic-declination.com](https://www.magnetic-declination.com/) 에서 현장 좌표 입력 → `navsat_transform.yaml`에 라디안 값 입력
3. **Warm-up**: 첫 fix까지 수분 소요 (Cold start). 이후 Hot start는 수초
4. **멀티패스**: 건물/철제 구조물 근처에서 반사 신호로 오차 증가
5. **udev 규칙**: `/dev/ttyACM0` → `/dev/gps`로 고정 이름 설정 (권장)

```bash
# udev 규칙 (U-blox 제품 ID 확인 후)
lsusb | grep -i "u-blox\|1546"
# 예: ID 1546:01a8 → 아래 파일 생성
sudo tee /etc/udev/rules.d/99-ublox.rules << 'EOF'
SUBSYSTEM=="tty", ATTRS{idVendor}=="1546", ATTRS{idProduct}=="01a8", SYMLINK+="gps", MODE="0666"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
# 이후 /dev/gps 로 접근 가능
```

---

## 파일 목록 (이 Phase 완료 시)

```
ros2_ws/src/agv_bringup/
  agv_bringup/
    gps_config.py          ✅ U-blox UBX 비활성화, 10Hz 설정
    gps_driver_node.py     ✅ NMEA 파싱, UBX 필터링, /gps/fix 발행
  launch/
    gps.launch.py          ✅ GPS 드라이버 + EKF + navsat_transform
  config/
    navsat_transform.yaml  ✅ navsat_transform_node 파라미터
    ekf_gps.yaml           ✅ GPS 융합 EKF (odom1=/odometry/gps 포함)
    ekf.yaml               ✅ GPS 융합 항목 추가 (row_follow+GPS 겸용)
    ekf_odom_only.yaml     ✅ GPS 융합 항목 추가 (카메라 없음+GPS)
ros2_ws/
  start_gps.sh             ✅ 전체 GPS 스택 시작 스크립트
  fastdds_no_shm.xml       ✅ SHM 비활성화 (Nav2 충돌 방지)
/etc/udev/rules.d/
  99-ublox-gps.rules       ✅ /dev/gps → ttyUSB0 고정명
agv_description/
  urdf/agv.urdf.xacro      ✅ gps_link 추가 (z=0.35m, 현장 실측 후 조정)
```

---

*작성: 2026-06-18 | 프로젝트: STM32H753ZI EtherCAT AGV*
