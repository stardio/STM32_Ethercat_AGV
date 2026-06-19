"""
gps_config.py — U-blox 시작 설정 노드

launch 시 nmea_navsat_driver 보다 먼저 실행하여:
  1. USB 포트를 NMEA 전용 출력으로 설정 (UBX 바이너리 출력 비활성화)
  2. 10Hz 출력 주기 설정
  3. 설정을 BBR(배터리 백업 RAM)에 저장

완료 후 ROS2 파라미터 /gps_configured 를 true로 게시하고 종료.
"""

import sys
import time
import struct
import serial
import rclpy
from rclpy.node import Node
from std_msgs.msg import Bool


def ubx_msg(cls, id_, payload=b''):
    msg = bytes([0xB5, 0x62, cls, id_,
                 len(payload) & 0xFF, (len(payload) >> 8) & 0xFF]) + payload
    ck_a = ck_b = 0
    for b in msg[2:]:
        ck_a = (ck_a + b) & 0xFF
        ck_b = (ck_b + ck_a) & 0xFF
    return msg + bytes([ck_a, ck_b])


def send_ubx_wait_ack(s, cls, id_, payload=b'', timeout=1.0):
    """UBX 메시지 전송 후 ACK(05 01) 또는 NAK(05 00) 대기"""
    msg = ubx_msg(cls, id_, payload)
    s.reset_input_buffer()
    s.write(msg)
    deadline = time.time() + timeout
    buf = b''
    while time.time() < deadline:
        buf += s.read(64)
        if b'\xb5\x62\x05\x01' in buf:
            return True   # ACK
        if b'\xb5\x62\x05\x00' in buf:
            return False  # NAK
    return None  # timeout


def configure_ublox(port, baud, logger):
    try:
        s = serial.Serial(port, baud, timeout=0.5)
    except Exception as e:
        logger.error(f'GPS 포트 열기 실패: {e}')
        return False

    try:
        # ── 1. 버퍼 비우기 ─────────────────────────────────────────────
        s.reset_input_buffer()
        time.sleep(0.3)
        s.reset_input_buffer()

        # ── 2. CFG-PRT: USB 포트(3) NMEA 전용 출력 ─────────────────────
        # inProto: UBX(0x01)+NMEA(0x02)=0x03  outProto: NMEA(0x02) only
        prt_payload = struct.pack('<BBHIIHHHH',
            3,      # portID = 3 (USB)
            0,      # reserved1
            0,      # txReady
            0,      # mode (USB는 무시)
            0,      # baudRate (USB는 무시)
            0x0003, # inProtoMask: UBX+NMEA 수신
            0x0002, # outProtoMask: NMEA 출력만
            0,      # flags
            0,      # reserved2
        )
        result = send_ubx_wait_ack(s, 0x06, 0x00, prt_payload, timeout=1.5)
        if result is True:
            logger.info('✅ CFG-PRT: NMEA 전용 출력 설정 완료')
        elif result is False:
            logger.warn('⚠ CFG-PRT: NAK 수신 — 모듈이 거부했지만 계속 진행')
        else:
            logger.warn('⚠ CFG-PRT: ACK 없음 — 타임아웃, 계속 진행')

        time.sleep(0.2)

        # ── 3. CFG-RATE: 10Hz 출력 주기 ────────────────────────────────
        rate_payload = struct.pack('<HHH',
            100,  # measRate = 100ms (10Hz)
            1,    # navRate = 1 (nav마다 메시지)
            1,    # timeRef = 1 (GPS 시간 기준)
        )
        result = send_ubx_wait_ack(s, 0x06, 0x08, rate_payload, timeout=1.5)
        if result is True:
            logger.info('✅ CFG-RATE: 10Hz 설정 완료')
        else:
            logger.warn(f'⚠ CFG-RATE: 결과={result}')

        time.sleep(0.2)

        # ── 4. CFG-CFG: BBR(배터리 백업 RAM)에 저장 ───────────────────
        # clearMask=0, saveMask=0xFFFF(모두 저장), loadMask=0, devMask=BBR(0x01)
        save_payload = struct.pack('<IIIB',
            0x00000000,  # clearMask (지우지 않음)
            0x0000FFFF,  # saveMask (모든 설정 저장)
            0x00000000,  # loadMask
            0x01,        # devMask: BBR
        )
        result = send_ubx_wait_ack(s, 0x06, 0x09, save_payload, timeout=2.0)
        if result is True:
            logger.info('✅ CFG-CFG: BBR 저장 완료 (재연결 후에도 유지)')
        else:
            logger.warn(f'⚠ CFG-CFG 저장: 결과={result}')

        # ── 5. 최종 검증: UBX 바이너리 없는지 확인 ─────────────────────
        time.sleep(0.5)
        s.reset_input_buffer()
        time.sleep(1.0)
        sample = s.read(500)
        has_nmea = any(line.startswith(b'$') for line in sample.split(b'\r\n'))
        has_ubx  = b'\xb5\x62' in sample

        if has_nmea and not has_ubx:
            logger.info('✅ GPS 출력 검증: NMEA만 수신, UBX 없음')
            return True
        elif has_ubx:
            logger.warn(f'⚠ UBX 바이너리 여전히 존재 — nmea_navsat_driver 경고 발생 가능')
            return True  # 드라이버는 경고만 내고 NMEA도 처리함
        else:
            logger.error('❌ GPS 데이터 없음 — fix 미획득 상태일 수 있음')
            return False

    finally:
        s.close()


class GpsConfigNode(Node):
    def __init__(self):
        super().__init__('gps_config')
        self.declare_parameter('port', '/dev/gps')
        self.declare_parameter('baud', 38400)

        port = self.get_parameter('port').value
        baud = self.get_parameter('baud').value

        self.pub = self.create_publisher(Bool, '/gps/configured', 1)

        self.get_logger().info(f'GPS 설정 시작: {port} @ {baud}')
        ok = configure_ublox(port, baud, self.get_logger())

        msg = Bool()
        msg.data = ok
        # 구독자가 연결될 시간을 주기 위해 잠시 대기
        time.sleep(0.5)
        self.pub.publish(msg)

        if ok:
            self.get_logger().info('GPS 설정 완료 — 노드 종료')
        else:
            self.get_logger().error('GPS 설정 실패')
            sys.exit(1)


def main(args=None):
    rclpy.init(args=args)
    node = GpsConfigNode()
    # 한 스핀만 돌리고 종료
    rclpy.spin_once(node, timeout_sec=1.0)
    node.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()
