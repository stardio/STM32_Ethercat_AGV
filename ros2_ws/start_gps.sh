#!/bin/bash
# GPS 스택 시작 스크립트
#   gps_config → gps_driver_node → ekf_gps_node → navsat_transform_node
#
# 사용법:
#   ./start_gps.sh
#   GPS_PORT=/dev/gps GPS_BAUD=38400 ./start_gps.sh
#
# 전제 조건:
#   - bridge.py 실행 중 (STM32 /odom 토픽 필요)
#   - /dev/gps → /dev/ttyUSB0 심볼릭 링크 (99-ublox-gps.rules)
#
# 토픽 확인:
#   ros2 topic echo /gps/fix
#   ros2 topic echo /odometry/gps
#   ros2 topic hz /odometry/gps

set -e

WS=/home/bs/Ethercat_N753-6AX_AGV/ros2_ws
GPS_PORT=${GPS_PORT:-/dev/gps}
GPS_BAUD=${GPS_BAUD:-38400}

source /opt/ros/jazzy/setup.bash
source "$WS/install/setup.bash"

for pkg_dir in "$WS/install"/*/; do
  [ -d "$pkg_dir" ] || continue
  export AMENT_PREFIX_PATH="$pkg_dir:$AMENT_PREFIX_PATH"
  for pydir in "$pkg_dir/lib/python"*/site-packages; do
    [ -d "$pydir" ] && export PYTHONPATH="$pydir:$PYTHONPATH"
  done
done

# Nav2/RTAB-Map 실행 중일 때 SHM 포트 충돌 방지 — UDP 전용으로 실행
export FASTRTPS_DEFAULT_PROFILES_FILE="$WS/fastdds_no_shm.xml"

echo "[start_gps] GPS 스택 시작"
echo "[start_gps]   포트: $GPS_PORT @ $GPS_BAUD baud"
echo "[start_gps]   t=0s  gps_config      — UBX 비활성화, 10Hz 설정"
echo "[start_gps]   t=4s  gps_driver      — /gps/fix 발행"
echo "[start_gps]   t=4s  ekf_gps_node    — /odom → /odometry/filtered"
echo "[start_gps]   t=8s  navsat_transform — /gps/fix → /odometry/gps (ENU)"
echo "[start_gps] 종료: Ctrl+C"
echo ""

exec ros2 launch agv_bringup gps.launch.py \
  gps_port:="$GPS_PORT" \
  gps_baud:="$GPS_BAUD"
