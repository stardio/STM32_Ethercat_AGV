#!/usr/bin/env bash
# =============================================================================
# 03_build_workspace.sh  —  ros2_ws를 WSL2 홈으로 복사하고 빌드
#
# WSL2 Ubuntu 터미널에서 실행:
#   chmod +x 03_build_workspace.sh && ./03_build_workspace.sh
#
# Windows D:\Ethercat_N753-6AX_AGV 가 WSL2에서
#   /mnt/d/Ethercat_N753-6AX_AGV 로 접근됩니다.
# =============================================================================

set -e
BOLD='\033[1m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[0;33m'; NC='\033[0m'
step() { echo -e "\n${CYAN}${BOLD}[$1]${NC}"; }
ok()   { echo -e "${GREEN}✓ $1${NC}"; }
warn() { echo -e "${YELLOW}⚠ $1${NC}"; }

WIN_REPO="/mnt/d/Ethercat_N753-6AX_AGV"
WS_SRC="$WIN_REPO/ros2_ws/src"
HOME_WS="$HOME/ros2_ws"

# ── ROS2 환경 확인 ─────────────────────────────────────────────────────────────
step "ROS2 환경 확인"
source /opt/ros/humble/setup.bash
ros2 --version
ok "ROS2 Humble 확인됨"

# ── Windows 프로젝트 경로 확인 ─────────────────────────────────────────────────
step "Windows 프로젝트 경로 확인"
if [ ! -d "$WS_SRC" ]; then
    echo -e "${BOLD}경고:${NC} $WS_SRC 가 없습니다."
    echo "Windows 드라이브 경로를 확인해주세요."
    echo "예: D드라이브가 아닌 경우 스크립트에서 WIN_REPO 변수를 수정하세요."
    exit 1
fi
ok "$WS_SRC 확인됨"

# ── stm32_bridge_node를 agv_bringup 패키지에 배치 ─────────────────────────────
step "stm32_bridge_node.py 복사"
BRIDGE_NODE_SRC="$WIN_REPO/PC_GUI/bridge/stm32_bridge_node.py"
BRIDGE_NODE_DST="$WS_SRC/agv_bringup/agv_bringup/stm32_bridge_node.py"
if [ -f "$BRIDGE_NODE_SRC" ]; then
    cp "$BRIDGE_NODE_SRC" "$BRIDGE_NODE_DST"
    ok "stm32_bridge_node.py 복사 완료"
else
    warn "stm32_bridge_node.py 없음 — 건너뜀"
fi

# ── WSL2 홈으로 심볼릭 링크 (편집은 Windows에서, 빌드는 WSL에서) ───────────────
step "ros2_ws 심볼릭 링크 생성 ($HOME_WS)"
if [ -L "$HOME_WS" ]; then
    warn "$HOME_WS 링크 이미 존재 — 건너뜀"
elif [ -d "$HOME_WS" ]; then
    warn "$HOME_WS 디렉토리 이미 존재 — 백업 후 링크 생성"
    mv "$HOME_WS" "${HOME_WS}_backup_$(date +%Y%m%d_%H%M%S)"
    ln -s "$WIN_REPO/ros2_ws" "$HOME_WS"
else
    ln -s "$WIN_REPO/ros2_ws" "$HOME_WS"
    ok "심볼릭 링크 생성: $HOME_WS → $WIN_REPO/ros2_ws"
fi

# ── rosdep 의존성 설치 ─────────────────────────────────────────────────────────
step "rosdep 의존성 설치"
cd "$HOME_WS"
rosdep install --from-paths src --ignore-src -r -y || warn "rosdep 일부 패키지 건너뜀 (수동 설치 필요)"
ok "rosdep 완료"

# ── colcon 빌드 ────────────────────────────────────────────────────────────────
step "colcon build"
cd "$HOME_WS"
colcon build --symlink-install \
    --cmake-args -DCMAKE_BUILD_TYPE=Release \
    2>&1 | tee /tmp/colcon_build.log

if grep -q "^Summary: .* packages finished" /tmp/colcon_build.log; then
    ok "빌드 성공"
else
    warn "빌드 로그 확인: /tmp/colcon_build.log"
fi

# ── 환경 소스 ─────────────────────────────────────────────────────────────────
step "환경 설정 적용"
source "$HOME_WS/install/setup.bash"

# ── ROS2 패키지 확인 ──────────────────────────────────────────────────────────
step "설치된 패키지 확인"
ros2 pkg list | grep -E "agv_|rtabmap|nav2|realsense" || true

echo ""
echo -e "${GREEN}${BOLD}============================================================${NC}"
echo -e "${GREEN}${BOLD}  ros2_ws 빌드 완료!${NC}"
echo -e "${GREEN}${BOLD}============================================================${NC}"
echo ""
echo "테스트 실행:"
echo "  # USB 연결 후 (04_attach_usb.ps1 먼저 실행)"
echo "  source ~/ros2_ws/install/setup.bash"
echo "  ros2 launch agv_bringup hardware.launch.py"
