# AGV ROS2 Workspace — 빠른 시작 가이드

## 의존 패키지 설치

```bash
sudo apt install -y \
  ros-humble-nav2-bringup \
  ros-humble-rtabmap-ros \
  ros-humble-realsense2-camera \
  ros-humble-tf2-tools \
  python3-websockets
```

## 빌드

```bash
cd ros2_ws
colcon build --symlink-install
source install/setup.bash
```

## 실행 순서

### 1. STM32 브릿지 (Windows 또는 Linux 별도 터미널)
```bash
cd PC_GUI/bridge
python bridge.py --port /dev/ttyACM0     # Linux
python bridge.py --port COM32            # Windows
```

### 2. 맵핑 모드
```bash
source ros2_ws/install/setup.bash
ros2 launch agv_bringup full_slam.launch.py
# RViz2에서 환경을 돌아다니며 맵 생성
# 맵은 ~/.ros/rtabmap.db 에 자동 저장
```

### 3. 자율주행 모드 (맵핑 완료 후)
```bash
ros2 launch agv_bringup full_nav.launch.py db_path:=~/.ros/rtabmap.db
# RViz2에서 "2D Goal Pose" 버튼으로 목표점 지정
```

## 파라미터 튜닝 포인트

| 파라미터 | 파일 | 설명 |
|---------|------|------|
| `camera_x`, `camera_z` | hardware.launch.py 인자 | 카메라 마운트 위치 |
| `unit_scale_left/right` | GUI 마법사로 측정 후 자동 반영 | 엔코더 스케일 |
| `robot_radius` | nav2_params.yaml | 장애물 팽창 반경 |
| `Grid/CellSize` | rtabmap_params.yaml | 맵 해상도 |

## TF 트리 확인
```bash
ros2 run tf2_tools view_frames
# 결과: map → odom → base_link → camera_link
```
