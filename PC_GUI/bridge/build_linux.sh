#!/bin/bash
# build_linux.sh — Linux에서 robot-bridge 실행파일 빌드
#
# 사용법 (Linux 머신 또는 WSL에서):
#   chmod +x build_linux.sh
#   ./build_linux.sh

set -e   # 오류 발생 시 즉시 중단

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================"
echo "  Robot Bridge Linux 빌드"
echo "  $(python3 --version)"
echo "  $(uname -srm)"
echo "========================================"

# ── 1. 가상환경 생성 (선택) ──────────────────────────────────────────
if [ ! -d ".venv" ]; then
    echo "[1/4] 가상환경 생성..."
    python3 -m venv .venv
fi
source .venv/bin/activate
echo "[1/4] 가상환경: $(which python3)"

# ── 2. 의존성 설치 ───────────────────────────────────────────────────
echo "[2/4] 패키지 설치..."
pip install --quiet --upgrade pip
pip install --quiet -r requirements.txt

# ── 3. PyInstaller 빌드 ──────────────────────────────────────────────
echo "[3/4] PyInstaller 빌드..."
rm -rf build/ dist/
pyinstaller robot_bridge.spec

# ── 4. 실행파일 확인 및 권한 설정 ────────────────────────────────────
echo "[4/4] 빌드 완료 확인..."
OUTFILE="dist/robot-bridge"

if [ -f "$OUTFILE" ]; then
    chmod +x "$OUTFILE"
    SIZE=$(du -sh "$OUTFILE" | cut -f1)
    echo ""
    echo "✅ 빌드 성공!"
    echo "   파일: $OUTFILE"
    echo "   크기: $SIZE"
    echo ""
    echo "실행 방법:"
    echo "   $OUTFILE"
    echo "   $OUTFILE --port /dev/ttyACM0"
    echo "   $OUTFILE --help"
else
    echo "❌ 빌드 실패 — dist/robot-bridge 파일 없음"
    exit 1
fi
