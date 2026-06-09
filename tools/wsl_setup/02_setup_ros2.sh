#!/usr/bin/env bash
# =============================================================================
# 02_setup_ros2.sh  —  ROS2 Humble + rtabmap + Nav2 + RealSense 자동 설치
#
# Ubuntu 22.04 WSL2 내부에서 실행:
#   chmod +x 02_setup_ros2.sh && ./02_setup_ros2.sh
#
# 소요 시간: 약 20~30분 (인터넷 속도에 따라)
# =============================================================================

set -e
BOLD='\033[1m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; RED='\033[0;31m'; NC='\033[0m'
step() { echo -e "\n${CYAN}${BOLD}[$1]${NC}"; }
ok()   { echo -e "${GREEN}✓ $1${NC}"; }

# ── 시스템 업데이트 ────────────────────────────────────────────────────────────
step "시스템 업데이트"
sudo apt update && sudo apt upgrade -y
ok "업데이트 완료"

# ── ROS2 Humble 설치 ──────────────────────────────────────────────────────────
step "ROS2 Humble 저장소 추가"
sudo apt install -y curl gnupg lsb-release
sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
    -o /usr/share/keyrings/ros-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] \
    https://packages.ros.org/ros2/ubuntu $(. /etc/os-release && echo $UBUNTU_CODENAME) main" \
    | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null
sudo apt update

step "ROS2 Humble 설치 (ros-humble-desktop)"
sudo apt install -y ros-humble-desktop python3-colcon-common-extensions \
    python3-rosdep python3-vcstool python3-argcomplete
ok "ROS2 Humble 설치 완료"

# ── rosdep 초기화 ─────────────────────────────────────────────────────────────
step "rosdep 초기화"
if [ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]; then
    sudo rosdep init
fi
rosdep update
ok "rosdep 준비 완료"

# ── Nav2 + RTAB-Map + RealSense 패키지 ────────────────────────────────────────
step "Nav2 + RTAB-Map + RealSense ROS2 패키지 설치"
sudo apt install -y \
    ros-humble-navigation2 \
    ros-humble-nav2-bringup \
    ros-humble-rtabmap-ros \
    ros-humble-realsense2-camera \
    ros-humble-realsense2-description \
    ros-humble-tf2-tools \
    ros-humble-tf2-ros \
    ros-humble-robot-state-publisher \
    ros-humble-xacro \
    ros-humble-joint-state-publisher \
    ros-humble-rviz2 \
    ros-humble-rqt \
    ros-humble-rqt-graph \
    ros-humble-rqt-tf-tree
ok "ROS2 패키지 설치 완료"

# ── Python 의존성 ──────────────────────────────────────────────────────────────
step "Python 패키지 설치"
pip3 install websockets pyserial
ok "Python 패키지 완료"

# ── usbip 커널 모듈 (USB 장치 WSL2 연결용) ────────────────────────────────────
step "usbip 커널 모듈 설치"
sudo apt install -y linux-tools-virtual hwdata
sudo update-alternatives --install /usr/local/bin/usbip usbip \
    "$(ls /usr/lib/linux-tools/*/usbip 2>/dev/null | tail -1)" 20 2>/dev/null || true
ok "usbip 설치 완료"

# ── Intel RealSense SDK (librealsense2) ────────────────────────────────────────
step "Intel RealSense SDK 설치"
sudo mkdir -p /etc/apt/keyrings
curl -sSf https://librealsense.intel.com/Debian/librealsense.pgp \
    | sudo tee /etc/apt/keyrings/librealsense.pgp > /dev/null
echo "deb [signed-by=/etc/apt/keyrings/librealsense.pgp] \
    https://librealsense.intel.com/Debian/apt-repo \
    $(lsb_release -cs) main" \
    | sudo tee /etc/apt/sources.list.d/librealsense.list > /dev/null
sudo apt update
sudo apt install -y librealsense2-dkms librealsense2-utils librealsense2-dev
ok "RealSense SDK 설치 완료"

# ── .bashrc 설정 ───────────────────────────────────────────────────────────────
step ".bashrc ROS2 환경 설정"
BASHRC="$HOME/.bashrc"
append_if_missing() {
    grep -qF "$1" "$BASHRC" || echo "$1" >> "$BASHRC"
}
append_if_missing "source /opt/ros/humble/setup.bash"
append_if_missing "source /usr/share/colcon_argcomplete/hook/colcon-argcomplete.bash"
append_if_missing "export ROS_DOMAIN_ID=0"
append_if_missing "export RMW_IMPLEMENTATION=rmw_fastrtps_cpp"
# ros2_ws가 빌드되면 자동 source
append_if_missing "[ -f \$HOME/ros2_ws/install/setup.bash ] && source \$HOME/ros2_ws/install/setup.bash"
ok ".bashrc 설정 완료"

# ── 완료 ──────────────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}${BOLD}============================================================${NC}"
echo -e "${GREEN}${BOLD}  ROS2 Humble 환경 설치 완료!${NC}"
echo -e "${GREEN}${BOLD}============================================================${NC}"
echo ""
echo "다음 단계:"
echo "  1. 이 터미널을 닫고 새 WSL2 창 열기 (환경 변수 적용)"
echo "  2. 03_build_workspace.sh 실행 (ros2_ws 빌드)"
echo "  3. USB 공유: 윈도우에서 04_attach_usb.ps1 실행"
