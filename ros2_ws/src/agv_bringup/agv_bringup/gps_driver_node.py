"""
gps_driver_node.py — U-blox GPS 드라이버 (NMEA 필터링 내장)

nmea_navsat_driver 대신 사용. UBX 바이너리를 자동으로 무시하고
NMEA $GxGGA / $GxRMC 문장만 파싱하여 ROS2 토픽으로 발행.

발행:
  /gps/fix  (sensor_msgs/NavSatFix)  — 10Hz
  /gps/vel  (geometry_msgs/TwistWithCovarianceStamped) — 10Hz (RMC 기반)
"""

import math
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import NavSatFix, NavSatStatus
from geometry_msgs.msg import TwistWithCovarianceStamped
import serial


def _nmea_checksum_ok(sentence: str) -> bool:
    """$....*XX 형식 체크섬 검증"""
    try:
        body, cs = sentence[1:].rsplit('*', 1)
        calc = 0
        for c in body:
            calc ^= ord(c)
        return calc == int(cs.strip(), 16)
    except Exception:
        return False


def _parse_lat(val: str, hemi: str) -> float:
    if not val:
        return float('nan')
    deg = float(val[:2])
    mins = float(val[2:])
    lat = deg + mins / 60.0
    return -lat if hemi == 'S' else lat


def _parse_lon(val: str, hemi: str) -> float:
    if not val:
        return float('nan')
    deg = float(val[:3])
    mins = float(val[3:])
    lon = deg + mins / 60.0
    return -lon if hemi == 'W' else lon


GGA_FIX_MAP = {
    '0': NavSatStatus.STATUS_NO_FIX,
    '1': NavSatStatus.STATUS_FIX,
    '2': NavSatStatus.STATUS_SBAS_FIX,
    '4': NavSatStatus.STATUS_GBAS_FIX,
    '5': NavSatStatus.STATUS_GBAS_FIX,
}

# HDOP → 위치 공분산 추정 (m²)
# covariance = (HDOP × base_σ)²,  base_σ ≈ 3m (표준 GNSS)
_BASE_SIGMA = 3.0


class GpsDriverNode(Node):
    def __init__(self):
        super().__init__('gps_driver')
        self.declare_parameter('port',     '/dev/gps')
        self.declare_parameter('baud',     38400)
        self.declare_parameter('frame_id', 'gps_link')

        self._port     = self.get_parameter('port').value
        self._baud     = self.get_parameter('baud').value
        self._frame_id = self.get_parameter('frame_id').value

        self._fix_pub = self.create_publisher(NavSatFix, '/gps/fix', 10)
        self._vel_pub = self.create_publisher(TwistWithCovarianceStamped, '/gps/vel', 10)

        self._ser = None
        self._open_serial()

        self._buf = b''
        self.create_timer(0.01, self._read_loop)  # 100Hz 폴링 (10Hz GPS에 충분)

        self.get_logger().info(f'GPS 드라이버 시작: {self._port} @ {self._baud}')

    def _open_serial(self):
        try:
            self._ser = serial.Serial(self._port, self._baud, timeout=0)
            self.get_logger().info(f'시리얼 포트 열림: {self._port}')
        except Exception as e:
            self.get_logger().error(f'시리얼 포트 열기 실패: {e}')

    def _read_loop(self):
        if self._ser is None or not self._ser.is_open:
            return
        try:
            raw = self._ser.read(4096)
        except Exception as e:
            self.get_logger().warn(f'읽기 오류: {e}')
            return

        if not raw:
            return

        # UBX 바이너리 제거: 0xB5 0x62 로 시작하는 패킷 건너뜀
        # 나머지는 버퍼에 누적하여 \r\n 기준으로 NMEA 파싱
        self._buf += raw
        lines = []
        while True:
            # UBX 헤더 제거
            ubx_idx = self._buf.find(b'\xb5\x62')
            if ubx_idx != -1:
                # UBX 패킷 앞부분 NMEA 처리
                self._buf = self._buf[:ubx_idx] + self._buf[ubx_idx+2:]
                continue
            # NMEA 라인 추출
            nl = self._buf.find(b'\r\n')
            if nl == -1:
                break
            line = self._buf[:nl]
            self._buf = self._buf[nl+2:]
            try:
                s = line.decode('ascii', errors='ignore').strip()
                if s.startswith('$'):
                    lines.append(s)
            except Exception:
                pass

        for sentence in lines:
            self._process_nmea(sentence)

    def _process_nmea(self, sentence: str):
        if '*' in sentence and not _nmea_checksum_ok(sentence):
            return  # 체크섬 불일치 무시
        parts = sentence.split(',')
        talker = parts[0][1:]   # e.g. "GBGGA" → talker="GBGGA"
        msg_type = talker[-3:]  # "GGA" or "RMC"

        if msg_type == 'GGA':
            self._handle_gga(parts)
        elif msg_type == 'RMC':
            self._handle_rmc(parts)

    def _handle_gga(self, parts):
        # $xxGGA,hhmmss,lat,N,lon,E,quality,numSV,HDOP,alt,M,sep,M,,*cs
        try:
            if len(parts) < 10:
                return
            quality = parts[6]
            if quality == '0':
                # Fix 없음 — STATUS_NO_FIX로 발행
                fix = NavSatFix()
                fix.header.stamp = self.get_clock().now().to_msg()
                fix.header.frame_id = self._frame_id
                fix.status.status = NavSatStatus.STATUS_NO_FIX
                fix.status.service = NavSatStatus.SERVICE_GPS
                fix.latitude  = float('nan')
                fix.longitude = float('nan')
                fix.altitude  = float('nan')
                fix.position_covariance_type = NavSatFix.COVARIANCE_TYPE_UNKNOWN
                self._fix_pub.publish(fix)
                return

            lat = _parse_lat(parts[2], parts[3])
            lon = _parse_lon(parts[4], parts[5])
            alt = float(parts[9]) if parts[9] else 0.0
            hdop = float(parts[8]) if parts[8] else 99.0

            sigma = (_BASE_SIGMA * hdop) ** 2  # 위치 분산 (m²)

            fix = NavSatFix()
            fix.header.stamp = self.get_clock().now().to_msg()
            fix.header.frame_id = self._frame_id
            fix.status.status  = GGA_FIX_MAP.get(quality, NavSatStatus.STATUS_FIX)
            fix.status.service = NavSatStatus.SERVICE_GPS | NavSatStatus.SERVICE_GLONASS
            fix.latitude  = lat
            fix.longitude = lon
            fix.altitude  = alt
            fix.position_covariance = [
                sigma,    0.0,   0.0,
                0.0,   sigma,   0.0,
                0.0,     0.0, sigma * 4,  # 고도 분산은 수평의 4배
            ]
            fix.position_covariance_type = NavSatFix.COVARIANCE_TYPE_APPROXIMATED
            self._fix_pub.publish(fix)
        except Exception as e:
            self.get_logger().debug(f'GGA 파싱 오류: {e} | {",".join(parts)}')

    def _handle_rmc(self, parts):
        # $xxRMC,hhmmss,A,lat,N,lon,E,speed_kn,course,ddmmyy,...
        try:
            if len(parts) < 9 or parts[2] != 'A':
                return  # 유효 데이터 아님
            speed_kn = float(parts[7]) if parts[7] else 0.0
            course_deg = float(parts[8]) if parts[8] else 0.0
            speed_ms = speed_kn * 0.514444

            vx = speed_ms * math.cos(math.radians(course_deg))
            vy = speed_ms * math.sin(math.radians(course_deg))

            vel = TwistWithCovarianceStamped()
            vel.header.stamp = self.get_clock().now().to_msg()
            vel.header.frame_id = self._frame_id
            vel.twist.twist.linear.x = vx
            vel.twist.twist.linear.y = vy
            # 속도 공분산: 대각 0.04 m²/s² (±0.2 m/s)
            vel.twist.covariance[0]  = 0.04
            vel.twist.covariance[7]  = 0.04
            vel.twist.covariance[14] = 0.04
            self._vel_pub.publish(vel)
        except Exception as e:
            self.get_logger().debug(f'RMC 파싱 오류: {e}')


def main(args=None):
    rclpy.init(args=args)
    node = GpsDriverNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node._ser and node._ser.is_open:
            node._ser.close()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
