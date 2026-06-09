# Robot HMI — Linux 배포 가이드

**프로젝트**: N753-6AX 6축 로봇 HMI Bridge  
**작성일**: 2026-06-05  
**테스트 환경**: Ubuntu 24.04 LTS (Linux 6.17, x86_64)

---

## 1. 시스템 구성

```
Windows PC (개발)                  Linux 머신 (운용)
─────────────────                  ──────────────────────────────
index.html (Web UI)   →  빌드  →   dist/robot-bridge  (실행파일)
bridge.py             →  빌드  →      ├─ Python 런타임 내장
packet_defs.py        →  빌드  →      ├─ index.html 내장
slip_codec.py                         └─ 모든 패키지 내장 (9.2MB)
                                              │
                                    브라우저 ◀─┤ HTTP :5100
                                    STM32  ◀──┤ Serial /dev/ttyACM0
                                              └─ WebSocket :8765
```

---

## 2. 사전 준비 (Linux 머신)

### 2.1 시리얼 포트 권한 설정 (최초 1회)

```bash
sudo usermod -aG dialout $USER
# 재로그인 또는 재부팅 필요
```

> 재로그인 전 임시 방편:
> ```bash
> sudo chmod 666 /dev/ttyACM0
> ```

### 2.2 STM32 포트 확인

```bash
# STM32 연결 후
ls /dev/ttyACM*    # STM32 VCP → /dev/ttyACM0
ls /dev/ttyUSB*    # UART 변환기 → /dev/ttyUSB0
dmesg | tail -5    # 연결 이벤트 확인
```

---

## 3. 빌드 방법

### 방법 A — Linux 머신에서 직접 빌드

```bash
# 소스 파일 복사 후
cd ~/robot-hmi
chmod +x build_linux.sh
./build_linux.sh
# → dist/robot-bridge 생성
```

### 방법 B — Windows에서 SSH로 원격 빌드

`tools/ssh_setup.py` 를 사용하면 Windows에서 한 번에 처리됩니다.

```powershell
# Windows PowerShell
$env:PYTHONUTF8=1
pip install paramiko
python tools\ssh_setup.py
```

스크립트가 자동으로 수행하는 작업:
1. SSH 키 생성 및 Linux 머신 등록
2. 소스 파일 업로드 (SCP)
3. python3-pip, python3-venv apt 설치
4. PyInstaller 빌드
5. 실행파일 확인

### 방법 C — Windows Docker로 크로스빌드

```batch
cd PC_GUI\bridge
build_docker.bat
# → dist/robot-bridge (Linux x86_64)
```

---

## 4. 빌드 출력물

```
robot-hmi/
├── dist/
│   └── robot-bridge              ← 단일 실행파일 (~9.2MB)
│         Python 3.12 런타임 내장
│         websockets, pyserial, serial_asyncio 내장
│         index.html 내장
│
└── robot-hmi-linux-x86_64.tar.gz ← 배포 패키지
      robot-hmi/
      ├── robot-bridge
      ├── start.sh
      ├── install_service.sh
      └── README.txt
```

---

## 5. 실행 방법

### 5.1 직접 실행

```bash
cd ~/robot-hmi

# STM32 포트 자동 감지 (권장)
./dist/robot-bridge

# 포트 수동 지정
./dist/robot-bridge --port /dev/ttyACM0

# 상세 로그
./dist/robot-bridge --verbose
```

### 5.2 실행 옵션

| 옵션 | 기본값 | 설명 |
|---|---|---|
| `--port PORT` | 자동 감지 | 시리얼 포트 (`/dev/ttyACM0` 등) |
| `--baud BAUD` | 921600 | 통신 속도 |
| `--http-port PORT` | 5100 | 웹 UI HTTP 포트 |
| `--ws-port PORT` | 8765 | WebSocket 포트 |
| `--reconnect SEC` | 3.0 | 재연결 대기 시간(초) |
| `--verbose` | — | 상세 로그 출력 |

### 5.3 백그라운드 실행

```bash
nohup ~/robot-hmi/dist/robot-bridge > ~/robot-hmi/bridge.log 2>&1 &

# 로그 확인
tail -f ~/robot-hmi/bridge.log

# 프로세스 확인
pgrep -a robot-bridge

# 종료
pkill -f robot-bridge
```

---

## 6. 부팅 자동 시작 (systemd)

### 서비스 파일 생성

```bash
sudo nano /etc/systemd/system/robot-hmi.service
```

```ini
[Unit]
Description=Robot HMI Bridge
After=network.target

[Service]
Type=simple
User=bs
WorkingDirectory=/home/bs/robot-hmi
ExecStart=/home/bs/robot-hmi/dist/robot-bridge
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

### 서비스 등록 및 시작

```bash
sudo systemctl daemon-reload
sudo systemctl enable robot-hmi      # 부팅 자동시작 등록
sudo systemctl start robot-hmi       # 즉시 시작

# 상태 확인
sudo systemctl status robot-hmi

# 실시간 로그
journalctl -u robot-hmi -f
```

### 서비스 제어

```bash
sudo systemctl stop robot-hmi        # 정지
sudo systemctl restart robot-hmi     # 재시작
sudo systemctl disable robot-hmi     # 자동시작 해제
```

---

## 7. 브라우저 접속

```
http://<Linux_IP>:5100/
```

| 환경 | 접속 주소 |
|---|---|
| 같은 PC (로컬) | http://localhost:5100/ |
| 원격 PC/태블릿 | http://192.168.49.7:5100/ |
| 스마트폰 | http://192.168.49.7:5100/ |

> **주의**: `https://` 가 아닌 `http://` 로 접속해야 합니다.

---

## 8. 포트 자동 탐색 동작

bridge가 시작될 때 STM32 VCP를 자동으로 찾습니다.

```
탐색 순서:
1. USB VID=0x0483 (STMicroelectronics), PID=0x5740/0x374B 매칭
2. 포트 설명에 "STM32", "STLink" 포함 여부
3. /dev/ttyACM* → /dev/ttyUSB* 순서로 첫 번째
4. 기본값: /dev/ttyUSB0
```

시작 로그 예시:
```
STM32 VCP 자동 감지 (설명): /dev/ttyACM0 — STLINK-V3 - ST-Link VCP Ctrl
HTTP server: http://localhost:5100/
WebSocket server: ws://0.0.0.0:8765
Serial open: /dev/ttyACM0
```

---

## 9. 방화벽 설정

```bash
# ufw 방화벽 포트 개방
sudo ufw allow 5100/tcp    # HTTP (Web UI)
sudo ufw allow 8765/tcp    # WebSocket
sudo ufw status
```

---

## 10. 문제 해결

### 포트 검색 중에서 넘어가지 않음

```
원인: index.html이 localhost를 고정으로 사용하던 이전 버전
해결: 수정된 index.html 사용 (window.location.hostname 자동 감지)
```

### Permission denied: /dev/ttyACM0

```bash
# 임시 (현재 세션)
sudo chmod 666 /dev/ttyACM0

# 영구 (재로그인 필요)
sudo usermod -aG dialout $USER
```

### 포트 이미 사용 중

```bash
# 기존 프로세스 종료
pkill -f robot-bridge
# 또는
sudo fuser -k 5100/tcp 8765/tcp
```

### 빌드 시 python3-venv 오류

```bash
sudo apt-get update
sudo apt-get install -y python3-pip python3-venv
```

### STM32 연결 확인

```bash
dmesg | grep -i tty | tail -10
ls -la /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

---

## 11. 테스트 환경 접속 정보

| 항목 | 값 |
|---|---|
| Linux 머신 IP | 192.168.49.7 |
| SSH 사용자 | bs |
| SSH 키 | `~/.ssh/id_rsa_robot` (Windows) |
| 실행파일 경로 | `/home/bs/robot-hmi/dist/robot-bridge` |
| 소스 경로 | `/home/bs/robot-hmi/` |
| 브라우저 접속 | http://192.168.49.7:5100/ |

---

## 12. 재배포 절차 (소스 수정 후)

```powershell
# Windows에서 — 수정 후 재배포
$env:PYTHONUTF8=1
python tools\ssh_setup.py
# SSH 키가 이미 등록되어 있으므로 비밀번호 불필요
```

또는 Linux에서:

```bash
cd ~/robot-hmi
# 수정된 파일 복사 후
./build_linux.sh
pkill -f robot-bridge
nohup ./dist/robot-bridge > bridge.log 2>&1 &
```
