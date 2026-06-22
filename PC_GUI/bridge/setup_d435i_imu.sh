#!/bin/bash
# D435i IMU HID 센서 udev 권한 설정 (1회 실행, sudo 필요)
#
# pyrealsense2가 D435i 가속도계·자이로를 사용하려면
# Linux IIO scan_elements 파일에 쓰기 권한이 필요합니다.

set -e

RULES_FILE=/etc/udev/rules.d/99-realsense-d435i-imu.rules

echo "[setup] Intel RealSense D435i IMU 권한 설정..."

# udev 규칙 생성 (카메라 연결 시 IIO scan_elements 에 a+rw 부여)
cat > "$RULES_FILE" << 'EOF'
# Intel RealSense D435i IMU (accel: 200073, gyro: 200076)
# pyrealsense2 가 sampling_frequency, scan_elements 등을 설정하려면 쓰기 권한이 필요합니다.
ACTION=="add", SUBSYSTEM=="iio", KERNEL=="iio:device*", ATTRS{idVendor}=="8086", ATTRS{idProduct}=="0b3a", \
  RUN+="/bin/sh -c 'chmod -R a+rw /sys%p/scan_elements 2>/dev/null; find /sys%p -maxdepth 1 -name \"in_accel_*\" -o -name \"in_anglvel_*\" 2>/dev/null | xargs chmod a+rw 2>/dev/null; for b in /sys%p/buffer*; do [ -d \"$b\" ] && chmod -R a+rw \"$b\"; done; chmod a+rw /dev/iio:device%n 2>/dev/null; true'"
EOF

echo "[setup] 규칙 파일 생성: $RULES_FILE"

# udev 규칙 재로드
udevadm control --reload-rules
echo "[setup] udev 규칙 재로드 완료"

# 현재 연결된 IIO 장치에 즉시 권한 부여 (D435i 전체 HID 서브트리)
echo "[setup] 현재 세션 즉시 적용..."
# D435i USB 장치 경로 (VID:PID = 8086:0b3a) 를 모두 찾아서 하위 전체 chmod
for usbdev in $(find /sys/bus/usb/devices -maxdepth 1 -name "*.5" -type l 2>/dev/null); do
    vid=$(cat "$usbdev/../idVendor" 2>/dev/null)
    pid=$(cat "$usbdev/../idProduct" 2>/dev/null)
    if [ "$vid" = "8086" ] && [ "$pid" = "0b3a" ]; then
        realpath=$(realpath "$usbdev/..")
        chmod -R a+rw "$realpath/0003:8086:0B3A."* 2>/dev/null || true
        echo "[setup]   $realpath — D435i HID 서브트리 권한 부여 완료"
    fi
done
# fallback: 직접 경로로도 시도
chmod -R a+rw /sys/devices/*/usb*/*/0003:8086:0B3A.*/HID-SENSOR-*.auto/ 2>/dev/null || true
for path in /sys/bus/iio/devices/iio:device*; do
    [ -d "$path" ] || continue
    dev=$(basename "$path")
    name=$(cat "$path/name" 2>/dev/null || echo "?")
    chmod -R a+rw "$path" 2>/dev/null || true
    [ -e "/dev/$dev" ] && chmod a+rw "/dev/$dev" 2>/dev/null || true
    echo "[setup]   /dev/$dev ($name) — IIO 권한 부여 완료"
done

echo ""
echo "[완료] D435i IMU 권한 설정이 완료됐습니다."
echo "       이제 bridge.py 를 재시작하면 IMU 데이터를 읽을 수 있습니다."
echo "       (재부팅 후에도 udev 규칙이 자동 적용됩니다)"
