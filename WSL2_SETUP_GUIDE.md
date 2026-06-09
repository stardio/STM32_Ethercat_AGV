# WSL2 환경 설정 가이드
## AGV RTAB-Map + Nav2 개발 환경 (Windows → Ubuntu 이전 전략)

---

## 개요

| 항목 | 내용 |
|------|------|
| 목적 | Windows에서 ROS2 SLAM/Nav2 전체 스택 테스트 후 Ubuntu 이전 |
| 방식 | WSL2 Ubuntu 22.04 + usbipd-win (USB 장치 공유) |
| ROS2 | Humble (LTS) |
| 플랫폼 | Windows 11 + WSL2 |

### 전체 아키텍처
```
Windows 호스트
├── bridge.py (WebSocket ↔ UART)     ← WSL2에서 실행
├── D:\Ethercat_N753-6AX_AGV\ros2_ws ← 소스 코드 (Windows에서 편집)
│
└── WSL2 Ubuntu 22.04
    ├── ROS2 Humble
    ├── ros2_ws (심볼릭 링크 → D:\...)  ← 빌드/실행
    ├── Nav2 + RTAB-Map + RealSense
    └── /dev/ttyACM0 (STM32, usbipd)
        /dev/video*  (RealSense, usbipd)
```

---

## Step 1 — WSL2 + Ubuntu 22.04 설치

### 1-1. 관리자 PowerShell 실행
시작 메뉴 → `PowerShell` 검색 → **우클릭 → 관리자로 실행**

### 1-2. Ubuntu 22.04 설치
```powershell
wsl --install -d Ubuntu-22.04
```

### 1-3. PC 재시작
설치 완료 후 PC를 재시작합니다.

### 1-4. Ubuntu 계정 생성
재시작 후 Ubuntu 터미널이 자동으로 열리지 않으면:
- 시작 메뉴 → **"Ubuntu 22.04"** 검색 후 실행

```
Create a default Unix user account: son      ← 사용자명 입력
New password:                                 ← 비밀번호 (화면에 안 보임)
Retype new password:
```

### 1-5. 설치 확인
```powershell
wsl --list --verbose
```
```
  NAME            STATE           VERSION
* Ubuntu-22.04    Running         2        ← VERSION 2 확인
```

---

## Step 2 — 시스템 업데이트

WSL2 Ubuntu 터미널(또는 아래처럼 PowerShell에서 직접 실행):

```powershell
# PowerShell에서 root로 실행
wsl -d Ubuntu-22.04 -u root -- bash -c "DEBIAN_FRONTEND=noninteractive apt update -y && DEBIAN_FRONTEND=noninteractive apt upgrade -y"
```

---

## Step 3 — ROS2 Humble 설치

### 3-1. ROS2 apt 저장소 추가
```powershell
wsl -d Ubuntu-22.04 -u root -- bash -c "
apt install -y curl gnupg
curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o /usr/share/keyrings/ros-archive-keyring.gpg
echo 'deb [arch=amd64 signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] https://packages.ros.org/ros2/ubuntu jammy main' > /etc/apt/sources.list.d/ros2.list
"
```

> **SSL 오류 발생 시** (회사 네트워크 방화벽):
> ```powershell
> wsl -d Ubuntu-22.04 -u root -- bash -c "
> echo 'Acquire::https::packages.ros.org::Verify-Peer \"false\";' > /etc/apt/apt.conf.d/99ros-ssl
> echo 'Acquire::https::packages.ros.org::Verify-Host \"false\";' >> /etc/apt/apt.conf.d/99ros-ssl
> apt update
> "
> ```

### 3-2. ROS2 Humble 설치 (약 10~15분)
```powershell
wsl -d Ubuntu-22.04 -u root -- bash -c "
DEBIAN_FRONTEND=noninteractive apt install -y ros-humble-desktop python3-colcon-common-extensions python3-rosdep python3-argcomplete
"
```

### 3-3. Nav2 + RTAB-Map + RealSense 패키지 설치 (약 5~10분)
```powershell
wsl -d Ubuntu-22.04 -u root -- bash -c "
DEBIAN_FRONTEND=noninteractive apt install -y \
  ros-humble-navigation2 \
  ros-humble-nav2-bringup \
  ros-humble-rtabmap-ros \
  ros-humble-realsense2-camera \
  ros-humble-realsense2-description \
  ros-humble-tf2-tools \
  ros-humble-robot-state-publisher \
  ros-humble-xacro \
  ros-humble-rviz2 \
  ros-humble-rqt-graph \
  python3-pip \
  python3-websockets
"
```

### 3-4. Python 브릿지 의존성 설치
```powershell
wsl -d Ubuntu-22.04 -u root -- bash -c "
pip3 install pyserial-asyncio websockets pyserial
"
```

---

## Step 4 — 환경 설정

아래 스크립트를 PowerShell에서 실행:

```powershell
wsl -d Ubuntu-22.04 -u root -- bash /mnt/d/Ethercat_N753-6AX_AGV/tools/wsl_setup/setup_env.sh
```

스크립트 내용 (`tools/wsl_setup/setup_env.sh`):
- `.bashrc`에 ROS2 환경 자동 source 추가
- rosdep 초기화
- `~/ros2_ws` → `D:\Ethercat_N753-6AX_AGV\ros2_ws` 심볼릭 링크 생성
- son 계정 sudo 패스워드 없이 설정
- dialout 그룹 추가 (시리얼 포트 접근)

**수동으로 설정하려면:**
```bash
# Ubuntu 터미널에서
echo "source /opt/ros/humble/setup.bash" >> ~/.bashrc
echo "source /usr/share/colcon_argcomplete/hook/colcon-argcomplete.bash" >> ~/.bashrc
echo "export ROS_DOMAIN_ID=0" >> ~/.bashrc
echo "[ -f \$HOME/ros2_ws/install/setup.bash ] && source \$HOME/ros2_ws/install/setup.bash" >> ~/.bashrc

rosdep init && rosdep update
ln -sfn /mnt/d/Ethercat_N753-6AX_AGV/ros2_ws ~/ros2_ws
sudo usermod -aG dialout $USER
```

---

## Step 5 — ros2_ws 빌드

```powershell
wsl -d Ubuntu-22.04 -u son -- bash -c "
source /opt/ros/humble/setup.bash
cd ~/ros2_ws
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Release
"
```

**성공 출력:**
```
Summary: 2 packages finished [7s]
  agv_bringup
  agv_description
```

---

## Step 6 — usbipd-win 설치 (USB → WSL2 공유)

### 6-1. 설치 (관리자 PowerShell)
```powershell
winget install --id dorssel.usbipd-win --source winget --accept-source-agreements --accept-package-agreements
```

### 6-2. 연결 USB 장치 확인
```powershell
usbipd list
```
```
BUSID  VID:PID    DEVICE                                    STATE
6-2    0483:374e  STMicroelectronics STLink Virtual COM...  Not shared
7-1    8086:0b3a  Intel RealSense D435                      Not shared
```

### 6-3. STM32 WSL2 연결 (관리자 PowerShell)
```powershell
usbipd bind --busid 6-2 --force   # 최초 1회
usbipd attach --wsl --busid 6-2
```

### 6-4. RealSense WSL2 연결 (카메라 연결 후)
```powershell
usbipd bind --busid 7-1 --force   # BUSID는 실제 값으로 변경
usbipd attach --wsl --busid 7-1
```

### 6-5. WSL2에서 장치 확인
```bash
ls /dev/ttyACM0    # STM32 VCP
ls /dev/video*     # RealSense
```

> **재부팅 후**: `bind`는 유지되므로 `attach`만 다시 실행하면 됩니다.

---

## Step 7 — 동작 확인

### 7-1. bridge.py STM32 연결 테스트
```bash
# Ubuntu 터미널
cd /mnt/d/Ethercat_N753-6AX_AGV/PC_GUI/bridge
python3 bridge.py --port /dev/ttyACM0 --verbose
```
**정상 출력:**
```
INFO  bridge: Serial open: /dev/ttyACM0
INFO  bridge: [STM32] [TIMING] cycles=5000  avg=1000 us ...
```

### 7-2. ROS2 하드웨어 런치 테스트
```bash
source ~/ros2_ws/install/setup.bash
ros2 launch agv_bringup hardware.launch.py
```

### 7-3. 전체 스택 (SLAM 모드, RealSense 연결 후)
```bash
# 터미널 1 — STM32 브릿지
cd /mnt/d/Ethercat_N753-6AX_AGV/PC_GUI/bridge
python3 bridge.py --port /dev/ttyACM0

# 터미널 2 — ROS2 전체 스택 (맵핑)
source ~/ros2_ws/install/setup.bash
ros2 launch agv_bringup full_slam.launch.py

# 터미널 3 — TF 트리 확인
ros2 run tf2_tools view_frames
```

---

## 트러블슈팅

| 증상 | 원인 | 해결 |
|------|------|------|
| `apt update` SSL 오류 | 회사 방화벽 SSL 검사 | `99ros-ssl` apt 설정 추가 (Step 3-1 참고) |
| `sudo` 비밀번호 대기 멈춤 | 백그라운드 실행 한계 | `wsl -u root` 로 직접 실행 |
| `/dev/ttyACM0` 없음 | USB 미연결 | `usbipd attach` 재실행 |
| `pip3: command not found` | pip 미설치 | `apt install python3-pip` |
| Ubuntu 자동 시작 안 됨 | 정상 동작 | 시작 메뉴에서 "Ubuntu 22.04" 수동 실행 |
| `colcon build` 이메일 오류 | package.xml 형식 | email을 `x@x.local` 형식으로 수정 |

---

## Ubuntu 이전 시 체크리스트

WSL2에서 검증 완료 후 네이티브 Ubuntu로 이전 시:

- [ ] `02_setup_ros2.sh` 동일하게 실행 (패키지 동일)
- [ ] `D:\Ethercat_N753-6AX_AGV\ros2_ws` → `~/ros2_ws` 복사
- [ ] `PC_GUI/bridge/` 복사
- [ ] STM32 VCP는 `/dev/ttyACM0` 그대로 사용
- [ ] usbipd 불필요 (USB 직접 연결)
- [ ] `.bashrc` 동일하게 설정

---

## ✅ 현재 완료 상태 (2026-06-08 기준)

> 카메라 미연결로 여기서 중단. 카메라 준비 후 **Step 8부터 재개**.

| 항목 | 상태 | 비고 |
|------|------|------|
| WSL2 + Ubuntu 22.04 | ✅ 완료 | 계정: `son` |
| ROS2 Humble | ✅ 완료 | `/opt/ros/humble` |
| Nav2 + RTAB-Map + RealSense ROS2 패키지 | ✅ 완료 | apt 설치 |
| Python 의존성 (pyserial-asyncio, websockets) | ✅ 완료 | pip3 설치 |
| ros2_ws 빌드 (agv_bringup, agv_description) | ✅ 완료 | `~/ros2_ws` |
| usbipd-win 설치 | ✅ 완료 | v5.3.0 |
| STM32 `/dev/ttyACM0` WSL2 연결 | ✅ 완료 | BUSID 6-2 |
| bridge.py STM32 통신 확인 | ✅ 완료 | 타이밍 로그 수신 확인 |
| **Intel RealSense 연결** | ⏸ 대기 중 | 카메라 준비 후 Step 8 진행 |
| RTAB-Map SLAM 테스트 | ⏸ 대기 중 | — |
| Nav2 자율주행 테스트 | ⏸ 대기 중 | — |

---

## Step 8 — RealSense 카메라 연결 및 확인
> **재개 지점**: 카메라를 PC에 연결한 후 여기서부터 시작

### 8-1. USB 장치 목록 확인 (관리자 PowerShell)
```powershell
usbipd list
```
Intel RealSense는 VID `8086`으로 식별됩니다:
```
BUSID  VID:PID    DEVICE                         STATE
6-2    0483:374e  STMicroelectronics STLink ...   Shared
X-X    8086:XXXX  Intel RealSense Depth Camera   Not shared  ← 이 BUSID 확인
```

### 8-2. RealSense WSL2 연결 (관리자 PowerShell)
```powershell
# BUSID는 8-1에서 확인한 실제 값으로 변경
usbipd bind --busid X-X --force
usbipd attach --wsl --busid X-X
```

### 8-3. WSL2에서 장치 인식 확인
```bash
# Ubuntu 터미널
ls /dev/video*
# 출력 예: /dev/video0  /dev/video1  /dev/video2  /dev/video3
```

### 8-4. RealSense SDK 동작 확인
```bash
rs-enumerate-devices
# 출력 예:
# Device info:
#   Name: Intel RealSense D435
#   Serial number: XXXXXXXX
#   Firmware version: XX.XX.XX.XX
```
`rs-enumerate-devices` 명령이 없으면:
```bash
sudo apt install -y librealsense2-utils
```

### 8-5. RealSense 권한 설정
```bash
# son 계정에 video 그룹 추가
sudo usermod -aG video son
# 재로그인 후 적용 (WSL2 재시작)
wsl --shutdown   # PowerShell에서 실행
```

---

## Step 9 — RealSense ROS2 노드 테스트

### 9-1. 카메라 단독 실행
```bash
# Ubuntu 터미널
source ~/ros2_ws/install/setup.bash
ros2 launch realsense2_camera rs_launch.py \
  enable_color:=true \
  enable_depth:=true \
  align_depth.enable:=true
```

### 9-2. 토픽 수신 확인
```bash
# 새 Ubuntu 터미널
source ~/ros2_ws/install/setup.bash
ros2 topic list | grep camera
# 확인해야 할 토픽:
#   /camera/color/image_raw
#   /camera/color/camera_info
#   /camera/aligned_depth_to_color/image_raw
ros2 topic hz /camera/color/image_raw
# 출력: average rate: 30.000  ← 30Hz 확인
```

### 9-3. RViz2로 영상 확인
```bash
rviz2
```
RViz2 에서:
1. **Add** → **By topic** → `/camera/color/image_raw` → **Image** 추가
2. **Add** → **By topic** → `/camera/depth/color/points` → **PointCloud2** 추가

---

## Step 10 — hardware.launch.py 전체 실행

STM32 브릿지 + URDF + 브릿지 노드를 한번에 실행합니다.

### 10-1. 터미널 1 — STM32 브릿지 시작
```bash
cd /mnt/d/Ethercat_N753-6AX_AGV/PC_GUI/bridge
python3 bridge.py --port /dev/ttyACM0
```

### 10-2. 터미널 2 — ROS2 하드웨어 런치
```bash
source ~/ros2_ws/install/setup.bash
ros2 launch agv_bringup hardware.launch.py
```

### 10-3. 오도메트리 확인
```bash
# 터미널 3
source ~/ros2_ws/install/setup.bash
ros2 topic hz /odom          # 100Hz 확인
ros2 topic echo /odom --once  # 데이터 확인
ros2 run tf2_tools view_frames && evince frames.pdf  # TF 트리 시각화
```

**정상 TF 트리:**
```
odom → base_link → camera_link
```

---

## Step 11 — unit_scale 보정 (필수)

오도메트리 정확도를 위해 **반드시** 실제 엔코더 스케일을 측정해야 합니다.

### 11-1. Web UI에서 측정 (Windows 브라우저)
1. bridge.py 실행 중 상태에서 `http://localhost:5100` 접속
2. **unit_scale 측정 마법사** 패널 사용:
   - ① 측정 시작 클릭 → 엔코더 시작값 기록
   - AGV를 직선으로 정확히 **1000mm** 이동
   - 이동 거리 입력 → ③ 측정 완료
   - **양쪽 적용** → **플래시 저장**

### 11-2. ROS2 노드에서 자동 반영
`param_report`가 수신되면 `stm32_bridge_node`가 자동으로 `unit_scale_left/right` 파라미터를 갱신합니다. 로그 확인:
```
[stm32_bridge_node] unit_scale updated from firmware — L=XXXX.0  R=XXXX.0 counts/mm
```

---

## Step 12 — RTAB-Map SLAM 맵핑

### 12-1. 전체 스택 실행 (맵핑 모드)
```bash
# 터미널 1
cd /mnt/d/Ethercat_N753-6AX_AGV/PC_GUI/bridge
python3 bridge.py --port /dev/ttyACM0

# 터미널 2
source ~/ros2_ws/install/setup.bash
ros2 launch agv_bringup full_slam.launch.py
```

### 12-2. RViz2 시각화 확인 항목
| 토픽 | 확인 내용 |
|------|----------|
| `/map` | 2D 점유 격자 지도 생성 여부 |
| `/rtabmap/cloud_map` | 3D 포인트 클라우드 누적 |
| `/odom` | 100Hz 오도메트리 |
| TF: `map → odom` | RTAB-Map이 발행하는지 확인 |

### 12-3. 맵 저장
맵은 `~/.ros/rtabmap.db`에 자동 저장됩니다.
종료 후에도 DB 파일이 유지되므로 별도 저장 명령 불필요.

---

## Step 13 — Nav2 자율주행 테스트

### 13-1. 자율주행 모드 실행 (저장된 맵 사용)
```bash
# 터미널 1
python3 bridge.py --port /dev/ttyACM0

# 터미널 2
source ~/ros2_ws/install/setup.bash
ros2 launch agv_bringup full_nav.launch.py db_path:=~/.ros/rtabmap.db
```

### 13-2. RViz2에서 목표점 전송
1. RViz2 상단 툴바 → **"2D Goal Pose"** 클릭
2. 지도 위 목표 위치를 클릭+드래그로 방향 지정
3. Nav2가 경로를 계획하고 AGV가 자동 이동

### 13-3. 확인 토픽
```bash
ros2 topic echo /cmd_vel   # Nav2 → AGV 속도 명령 확인
ros2 topic echo /odom      # 위치 피드백 확인
```

---

## 파라미터 튜닝 포인트

| 파라미터 | 파일 | 설명 |
|---------|------|------|
| `camera_x`, `camera_z` | `hardware.launch.py` 인자 | 카메라 실제 마운트 위치 (m) |
| `unit_scale_left/right` | Web UI 마법사로 측정 | 엔코더 스케일 (counts/mm) |
| `robot_radius` | `config/nav2_params.yaml` | 로봇 반경 (기본 0.35m) |
| `Grid/CellSize` | `config/rtabmap_params.yaml` | 맵 해상도 (기본 0.05m) |
| `max_vel_x` | `config/nav2_params.yaml` | 최대 직선 속도 (기본 0.5 m/s) |

카메라 마운트 위치 override 예시:
```bash
ros2 launch agv_bringup hardware.launch.py camera_x:=0.30 camera_z:=0.25
```

---

## 설치 스크립트 목록

| 파일 | 용도 |
|------|------|
| `tools/wsl_setup/01_install_wsl2.ps1` | WSL2 + Ubuntu 설치 (PowerShell) |
| `tools/wsl_setup/02_setup_ros2.sh` | ROS2 전체 환경 설치 (Ubuntu) |
| `tools/wsl_setup/03_build_workspace.sh` | ros2_ws 빌드 (Ubuntu) |
| `tools/wsl_setup/04_attach_usb.ps1` | USB 장치 WSL2 연결 (PowerShell) |
| `tools/wsl_setup/05_verify.sh` | 환경 전체 점검 (Ubuntu) |
| `tools/wsl_setup/setup_env.sh` | .bashrc + 심볼릭 링크 설정 (Ubuntu) |
