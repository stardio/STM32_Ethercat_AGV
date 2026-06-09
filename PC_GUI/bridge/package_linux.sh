#!/bin/bash
# package_linux.sh — 배포용 tar.gz 패키지 생성
#
# 실행 후 생성: robot-hmi-linux-x86_64.tar.gz
#   robot-hmi/
#   ├── robot-bridge          ← 실행파일 (브리지 + HTTP 서버 내장)
#   ├── start.sh              ← 실행 스크립트
#   ├── install_service.sh    ← systemd 서비스 설치
#   └── README.txt

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

OUTFILE="dist/robot-bridge"
if [ ! -f "$OUTFILE" ]; then
    echo "먼저 build_linux.sh 를 실행하세요."
    exit 1
fi

PKG_DIR="robot-hmi"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"

# 실행파일 복사
cp dist/robot-bridge "$PKG_DIR/"
chmod +x "$PKG_DIR/robot-bridge"

# start.sh 생성
cat > "$PKG_DIR/start.sh" << 'EOF'
#!/bin/bash
# Robot HMI 시작 스크립트
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

echo "Robot HMI Bridge 시작..."
echo "브라우저에서 http://localhost:5100/ 접속"
echo "종료: Ctrl+C"
echo ""

./robot-bridge "$@"
EOF
chmod +x "$PKG_DIR/start.sh"

# systemd 서비스 설치 스크립트 생성
cat > "$PKG_DIR/install_service.sh" << 'EOF'
#!/bin/bash
# systemd 서비스 설치 (부팅 시 자동 시작)
DIR="$(cd "$(dirname "$0")" && pwd)"
USER_NAME=$(whoami)

SERVICE_FILE="/etc/systemd/system/robot-hmi.service"

sudo tee "$SERVICE_FILE" > /dev/null << SVCEOF
[Unit]
Description=Robot HMI Bridge
After=network.target

[Service]
Type=simple
User=$USER_NAME
WorkingDirectory=$DIR
ExecStart=$DIR/robot-bridge
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
SVCEOF

sudo systemctl daemon-reload
sudo systemctl enable robot-hmi
sudo systemctl start robot-hmi

echo "서비스 설치 완료!"
echo "상태 확인: sudo systemctl status robot-hmi"
echo "로그 확인: journalctl -u robot-hmi -f"
EOF
chmod +x "$PKG_DIR/install_service.sh"

# README 생성
ARCH=$(uname -m)
cat > "$PKG_DIR/README.txt" << EOF
Robot HMI Bridge for Linux
===========================
아키텍처 : $ARCH
빌드 날짜: $(date '+%Y-%m-%d %H:%M')

실행 방법
---------
1. 직접 실행:
   ./start.sh
   또는
   ./robot-bridge
   또는
   ./robot-bridge --port /dev/ttyACM0

2. 시리얼 포트 권한 설정 (최초 1회):
   sudo usermod -aG dialout \$USER
   (재로그인 필요)

3. 브라우저 접속:
   http://localhost:5100/

4. 부팅 자동 시작 설정:
   ./install_service.sh

옵션
----
--port    PORT    시리얼 포트 (기본: 자동 감지)
--baud    BAUD    통신 속도   (기본: 921600)
--ws-port PORT    WebSocket  (기본: 8765)
--http-port PORT  HTTP 포트  (기본: 5100)
--verbose        상세 로그 출력
EOF

# 패키지 압축
TARFILE="robot-hmi-linux-${ARCH}.tar.gz"
tar -czf "$TARFILE" "$PKG_DIR"
rm -rf "$PKG_DIR"

SIZE=$(du -sh "$TARFILE" | cut -f1)
echo ""
echo "✅ 패키지 생성 완료!"
echo "   파일: $TARFILE  ($SIZE)"
echo ""
echo "Linux 머신에서 설치:"
echo "   tar -xzf $TARFILE"
echo "   cd robot-hmi"
echo "   ./start.sh"
