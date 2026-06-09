#!/bin/bash
# WSL2 환경 설정 스크립트 — root로 실행

BASHRC="/home/son/.bashrc"
WIN_WS="/mnt/d/Ethercat_N753-6AX_AGV/ros2_ws"

add_line() {
    grep -qF "$1" "$BASHRC" 2>/dev/null || echo "$1" >> "$BASHRC"
}

echo "=== .bashrc ROS2 환경 설정 ==="
add_line "source /opt/ros/humble/setup.bash"
add_line "source /usr/share/colcon_argcomplete/hook/colcon-argcomplete.bash"
add_line "export ROS_DOMAIN_ID=0"
add_line "[ -f \$HOME/ros2_ws/install/setup.bash ] && source \$HOME/ros2_ws/install/setup.bash"
echo "완료"

echo "=== rosdep 초기화 ==="
rosdep init 2>/dev/null || echo "이미 초기화됨"
sudo -u son rosdep update 2>&1 | tail -3

echo "=== ros2_ws 심볼릭 링크 ==="
ln -sfn "$WIN_WS" /home/son/ros2_ws
chown -h son:son /home/son/ros2_ws
echo "링크: /home/son/ros2_ws -> $WIN_WS"

echo "=== sudo 패스워드 없이 설정 (son 계정) ==="
echo "son ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/son
chmod 440 /etc/sudoers.d/son
echo "완료"

echo ""
echo "모든 환경 설정 완료!"
