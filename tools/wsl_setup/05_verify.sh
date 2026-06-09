#!/usr/bin/env bash
# =============================================================================
# 05_verify.sh  —  환경 전체 점검
#
# WSL2 Ubuntu 터미널에서 실행:
#   chmod +x 05_verify.sh && ./05_verify.sh
# =============================================================================

PASS='\033[0;32m✓\033[0m'; FAIL='\033[0;31m✗\033[0m'; WARN='\033[0;33m⚠\033[0m'
ok=0; fail=0

check() {
    local label="$1"; shift
    if "$@" &>/dev/null; then
        echo -e "$PASS $label"
        ((ok++))
    else
        echo -e "$FAIL $label"
        ((fail++))
    fi
}

warn_check() {
    local label="$1"; shift
    if "$@" &>/dev/null; then
        echo -e "$PASS $label"
        ((ok++))
    else
        echo -e "$WARN $label (연결 필요)"
    fi
}

echo "=============================="
echo " AGV WSL2 환경 점검"
echo "=============================="

# ROS2
check "ROS2 Humble sourced"         bash -c "source /opt/ros/humble/setup.bash && ros2 --version"
check "Nav2 bringup 설치됨"          bash -c "source /opt/ros/humble/setup.bash && ros2 pkg list | grep nav2_bringup"
check "rtabmap_ros 설치됨"           bash -c "source /opt/ros/humble/setup.bash && ros2 pkg list | grep rtabmap_slam"
check "realsense2_camera 설치됨"     bash -c "source /opt/ros/humble/setup.bash && ros2 pkg list | grep realsense2_camera"
check "xacro 설치됨"                 bash -c "source /opt/ros/humble/setup.bash && ros2 pkg list | grep xacro"

# ros2_ws
check "ros2_ws 존재"                 test -d "$HOME/ros2_ws/src"
check "agv_bringup 빌드됨"           test -f "$HOME/ros2_ws/install/agv_bringup/share/agv_bringup/package.xml"
check "agv_description 빌드됨"       test -f "$HOME/ros2_ws/install/agv_description/share/agv_description/package.xml"

# Python
check "websockets 설치됨"            python3 -c "import websockets"
check "rclpy 접근 가능"              bash -c "source /opt/ros/humble/setup.bash && python3 -c 'import rclpy'"

# USB 장치 (선택)
warn_check "STM32 VCP (/dev/ttyACM0)" test -e /dev/ttyACM0
warn_check "RealSense (/dev/video0)"  test -e /dev/video0

# 결과
echo ""
echo "=============================="
echo -e " 통과: $ok  /  실패: $fail"
if [ $fail -eq 0 ]; then
    echo -e "\033[0;32m 모든 검사 통과!\033[0m"
    echo ""
    echo "실행:"
    echo "  source ~/ros2_ws/install/setup.bash"
    echo "  ros2 launch agv_bringup hardware.launch.py"
else
    echo -e "\033[0;31m 위 항목을 확인 후 재실행하세요.\033[0m"
    echo "설치 스크립트 재실행: ./02_setup_ros2.sh"
fi
echo "=============================="
