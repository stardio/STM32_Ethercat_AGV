#!/bin/bash
# ROS2 Jazzy + AGV 스택 설치 스크립트 (Ubuntu 24.04)
set -e

echo "=== [1/6] ROS2 Jazzy apt 저장소 추가 ==="
sudo apt install -y software-properties-common curl
sudo add-apt-repository universe -y
sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
     -o /usr/share/keyrings/ros-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] \
     http://packages.ros.org/ros2/ubuntu $(. /etc/os-release && echo $UBUNTU_CODENAME) main" \
     | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null
sudo apt update

echo "=== [2/6] ROS2 Jazzy 설치 (약 10~15분) ==="
sudo apt install -y ros-jazzy-desktop

echo "=== [3/6] AGV 필수 ROS2 패키지 설치 ==="
sudo apt install -y \
  ros-jazzy-robot-localization \
  ros-jazzy-nav2-bringup \
  ros-jazzy-nav2-lifecycle-manager \
  ros-jazzy-realsense2-camera \
  ros-jazzy-rtabmap-ros \
  ros-jazzy-tf2-ros \
  ros-jazzy-tf2-tools \
  python3-colcon-common-extensions \
  python3-rosdep

echo "=== [4/6] Python 패키지 설치 ==="
pip3 install --break-system-packages \
  websockets \
  pyserial \
  pyserial-asyncio \
  pyrealsense2 \
  numpy \
  Pillow \
  ultralytics \
  anthropic

echo "=== [5/6] ROS2 워크스페이스 빌드 ==="
source /opt/ros/jazzy/setup.bash
cd /home/bs/Ethercat_N753-6AX_AGV/ros2_ws
rosdep init 2>/dev/null || true
rosdep update
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install
echo "빌드 완료"

echo "=== [6/6] agv-ros.service 등록 ==="
sudo tee /etc/systemd/system/agv-ros.service > /dev/null << 'EOF'
[Unit]
Description=AGV ROS2 Stack (row_follower + obstacle + EKF)
After=network.target agv-bridge.service
Wants=agv-bridge.service

[Service]
Type=simple
User=bs
WorkingDirectory=/home/bs/Ethercat_N753-6AX_AGV/ros2_ws
ExecStart=/bin/bash -c "\
  source /opt/ros/jazzy/setup.bash && \
  source /home/bs/Ethercat_N753-6AX_AGV/ros2_ws/install/setup.bash && \
  ros2 launch agv_bringup row_follow.launch.py \
    ws_url:=ws://localhost:8765"
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable agv-ros
sudo systemctl start agv-ros

echo ""
echo "============================="
echo " 설치 완료!"
echo "============================="
echo " 상태 확인: sudo systemctl status agv-ros"
echo " 로그 보기: journalctl -u agv-ros -f"
echo "============================="
