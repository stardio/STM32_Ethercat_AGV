#!/bin/bash
# AGV 열추종 스택 시작 스크립트 (row_follow.launch.py)
set -e

WS=/home/bs/Ethercat_N753-6AX_AGV/ros2_ws

source /opt/ros/jazzy/setup.bash

if [ ! -f "$WS/install/.jazzy_built" ]; then
  echo "[start_ros] 워크스페이스 클린 빌드 중..."
  cd "$WS"
  rm -rf build/ install/ log/
  colcon build --symlink-install
  touch "$WS/install/.jazzy_built"
  source "$WS/install/setup.bash"
else
  source "$WS/install/setup.bash"
fi

for pkg_dir in "$WS/install"/*/; do
  [ -d "$pkg_dir" ] || continue
  export AMENT_PREFIX_PATH="$pkg_dir:$AMENT_PREFIX_PATH"
  for pydir in "$pkg_dir/lib/python"*/site-packages; do
    [ -d "$pydir" ] && export PYTHONPATH="$pydir:$PYTHONPATH"
  done
done

# FastDDS SHM 비활성화 (SHM 포트 충돌 방지)
export FASTRTPS_DEFAULT_PROFILES_FILE="$WS/fastdds_no_shm.xml"

# 이전 실행에서 남은 stm32_bridge_node 정리 (orphan 프로세스 제거)
pkill -f "stm32_bridge_node" 2>/dev/null && echo "[start_ros] 이전 stm32_bridge_node 종료" || true
sleep 0.5

echo "[start_ros] 열추종 스택 시작 (row_follow.launch.py)"
exec ros2 launch agv_bringup row_follow.launch.py \
  ws_url:=ws://localhost:8765
