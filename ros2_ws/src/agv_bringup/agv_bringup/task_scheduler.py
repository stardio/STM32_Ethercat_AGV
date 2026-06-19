#!/usr/bin/env python3
"""
task_scheduler.py — AGV 미션 스케줄러 ROS2 노드 (Phase 5)

mission.yaml 에 정의된 일정에 따라 자동 작업을 실행한다.

작업 종류:
  survey        — 과수원 전체 열 순찰 (row_follower 활성화)
  spray         — 방제 작업 (저속 순찰 + /task/sprayer 신호)
  fruit_detect  — 착과 촬영 (저속 순찰)
  return_home   — 충전 스테이션 복귀 (후진 직진)

작업 완료 감지:
  - row_follower 상태가 ROW_END → 자동 정지
  - max_task_duration_min 초과 → 강제 정지 (무한 대기 방지)

웹 대시보드 HTTP REST API (기본 포트 8080):
  GET  /api/status         현재 상태 + 다음 스케줄
  GET  /api/schedule       전체 스케줄 목록
  POST /api/run/{name}     즉시 작업 실행 (name: 스케줄 name 또는 task 종류)
  POST /api/stop           현재 작업 강제 중단
  (CORS 헤더 포함 — 브라우저 fetch 가능)

토픽:
  Sub: /row_follow/status  std_msgs/String  행 추종 상태 JSON
       /obstacle/action    std_msgs/String  장애물 분류기 action
  Pub: /row_follow/enable  std_msgs/Bool    행 추종 활성/비활성
       /row_follow/speed   std_msgs/Float32 작업별 속도 조절
       /task/status        std_msgs/String  현재 태스크 상태 JSON
       /cmd_vel            geometry_msgs/Twist  (return_home 전용)

파라미터:
  mission_file         (str,   '~/.ros/mission.yaml')  미션 설정 파일
  http_port            (int,   8080)                   REST API 포트
  max_task_duration_min(float, 60.0)                   작업 최대 실행 시간 [분]
  schedule_check_sec   (float, 30.0)                   스케줄 체크 주기 [s]
"""

import datetime
import json
import math
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn
from typing import Optional

try:
    import yaml as _yaml
    _YAML_AVAILABLE = True
except ImportError:
    _YAML_AVAILABLE = False

try:
    import rclpy
    from rclpy.node import Node
    from geometry_msgs.msg import Twist
    from std_msgs.msg import Bool, Float32, String
    _ROS2_AVAILABLE = True
except ImportError:
    _ROS2_AVAILABLE = False
    print('[task_scheduler] WARNING: rclpy not found.')


# ── Mission loader ─────────────────────────────────────────────────────────────

def _load_mission(path: str) -> dict:
    path = os.path.expanduser(path)
    if not _YAML_AVAILABLE:
        print('[task_scheduler] pyyaml not installed — pip install pyyaml')
        return {}
    if not os.path.exists(path):
        print(f'[task_scheduler] mission file not found: {path}')
        return {}
    try:
        with open(path, 'r') as f:
            return _yaml.safe_load(f) or {}
    except Exception as exc:
        print(f'[task_scheduler] mission load error: {exc}')
        return {}


# ── HTTP REST API ─────────────────────────────────────────────────────────────

class _ThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


class _APIHandler(BaseHTTPRequestHandler):
    """Simple REST handler.  scheduler_node is injected at class level."""
    scheduler_node: 'TaskSchedulerNode' = None

    def log_message(self, fmt, *args):
        pass  # suppress default request logging

    def _send_json(self, data: dict, status: int = 200) -> None:
        body = json.dumps(data, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        n = int(self.headers.get('Content-Length', 0))
        return json.loads(self.rfile.read(n)) if n > 0 else {}

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_GET(self):
        node = self.scheduler_node
        if self.path == '/api/status':
            self._send_json(node.get_api_status())
        elif self.path == '/api/schedule':
            self._send_json({'schedule': node.get_api_schedule()})
        else:
            self._send_json({'error': 'not found'}, 404)

    def do_POST(self):
        node = self.scheduler_node
        if self.path.startswith('/api/run/'):
            name = self.path.split('/')[-1]
            ok, msg = node.run_by_name(name)
            self._send_json({'ok': ok, 'msg': msg})
        elif self.path == '/api/stop':
            node.stop_current()
            self._send_json({'ok': True, 'msg': 'stopped'})
        else:
            self._send_json({'error': 'not found'}, 404)


# ── Scheduler node ─────────────────────────────────────────────────────────────

class TaskSchedulerNode(Node):

    def __init__(self):
        super().__init__('task_scheduler')

        # ── Parameters ────────────────────────────────────────────────────
        self.declare_parameter('mission_file',
                               os.path.join(os.path.dirname(__file__),
                                            '..', '..', '..', '..', 'share',
                                            'agv_bringup', 'config', 'mission.yaml'))
        self.declare_parameter('http_port',             8080)
        self.declare_parameter('max_task_duration_min', 60.0)
        self.declare_parameter('schedule_check_sec',    30.0)

        mission_file    = self.get_parameter('mission_file').value
        http_port       = int(self.get_parameter('http_port').value)
        self._max_dur   = float(self.get_parameter('max_task_duration_min').value) * 60.0
        check_sec       = float(self.get_parameter('schedule_check_sec').value)

        # ── Mission config ────────────────────────────────────────────────
        self._config   = _load_mission(mission_file)
        self._agv_cfg  = self._config.get('agv', {})
        self._schedule = self._config.get('schedule', [])
        self.get_logger().info(
            f'mission loaded: {len(self._schedule)} tasks from {mission_file}')

        # ── State ─────────────────────────────────────────────────────────
        self._current_task:    Optional[dict] = None
        self._task_start_t:    float          = 0.0
        self._row_follow_state: str           = 'DISABLED'
        self._obstacle_action:  str           = 'CLEAR'
        self._lock = threading.Lock()

        # ── Publishers ────────────────────────────────────────────────────
        self._pub_enable = self.create_publisher(Bool,    '/row_follow/enable', 10)
        self._pub_speed  = self.create_publisher(Float32, '/row_follow/speed',  10)
        self._pub_status = self.create_publisher(String,  '/task/status',       10)
        self._pub_cmd    = self.create_publisher(Twist,   '/cmd_vel',           10)

        # ── Subscribers ───────────────────────────────────────────────────
        self.create_subscription(
            String, '/row_follow/status', self._on_row_status, 10)
        self.create_subscription(
            String, '/obstacle/action', self._on_obstacle, 10)

        # ── Timers ────────────────────────────────────────────────────────
        self.create_timer(check_sec, self._check_schedule)
        self.create_timer(1.0,       self._tick)

        # ── HTTP REST server ──────────────────────────────────────────────
        _APIHandler.scheduler_node = self
        self._http_server = _ThreadingHTTPServer(('', http_port), _APIHandler)
        t = threading.Thread(target=self._http_server.serve_forever, daemon=True)
        t.start()
        self.get_logger().info(
            f'task_scheduler HTTP API listening on port {http_port}')

    # ── ROS2 callbacks ────────────────────────────────────────────────────

    def _on_row_status(self, msg: String) -> None:
        try:
            data = json.loads(msg.data)
        except Exception:
            return
        self._row_follow_state = data.get('state', 'UNKNOWN')
        # Task complete when row follower reaches end of row
        if self._row_follow_state == 'ROW_END' and self._current_task:
            self.get_logger().info('ROW_END detected — task complete')
            self._finish_task('ROW_END')

    def _on_obstacle(self, msg: String) -> None:
        self._obstacle_action = msg.data

    # ── Schedule check ────────────────────────────────────────────────────

    def _check_schedule(self) -> None:
        if self._current_task is not None:
            return

        now     = datetime.datetime.now()
        hhmm    = now.strftime('%H:%M')
        weekday = now.weekday()  # 0=월 … 6=일

        for job in self._schedule:
            if job.get('time') != hhmm:
                continue
            days = job.get('days', list(range(7)))
            if weekday not in days:
                continue
            ok, msg = self._start_task(job)
            if ok:
                self.get_logger().info(f'[schedule] {msg}')
            break

    # ── Tick: timeout 체크 및 상태 발행 ──────────────────────────────────

    def _tick(self) -> None:
        with self._lock:
            task = self._current_task
        if task:
            elapsed = time.monotonic() - self._task_start_t
            if elapsed > self._max_dur:
                self.get_logger().warning(
                    f'Task timeout after {elapsed/60:.1f} min — forcing stop')
                self._finish_task('TIMEOUT')

        # Publish /task/status for ROS2 monitoring
        self._pub_status.publish(String(data=json.dumps(self.get_api_status())))

    # ── Task execution ────────────────────────────────────────────────────

    def _start_task(self, job: dict) -> tuple:
        with self._lock:
            if self._current_task is not None:
                return False, f'busy: {self._current_task.get("name","?")}'
            self._current_task = job
            self._task_start_t = time.monotonic()

        task = job.get('task', 'survey')
        speed = min(float(job.get('speed', 0.30)),
                    float(self._agv_cfg.get('max_speed', 0.40)))

        self.get_logger().info(
            f"Task START: [{job.get('name', task)}] {job.get('description', task)} "
            f"speed={speed:.2f}m/s")

        if task in ('survey', 'spray', 'fruit_detect'):
            self._pub_speed.publish(Float32(data=speed))
            self._pub_enable.publish(Bool(data=True))
        elif task == 'return_home':
            self._exec_return_home(speed)
        else:
            self.get_logger().warning(f'Unknown task type: {task}')

        return True, f"started [{job.get('name', task)}]"

    def _finish_task(self, reason: str) -> None:
        with self._lock:
            task = self._current_task
            self._current_task = None

        if task is None:
            return

        self._pub_enable.publish(Bool(data=False))

        # Stop any ongoing velocity
        stop = Twist()
        self._pub_cmd.publish(stop)

        elapsed = time.monotonic() - self._task_start_t
        self.get_logger().info(
            f"Task DONE: [{task.get('name','?')}]  reason={reason}  "
            f"elapsed={elapsed:.0f}s")

    def _exec_return_home(self, speed: float) -> None:
        """간단한 귀환: 후진 직진 (Nav2 없이 시간 기반)."""
        def _go():
            cmd = Twist()
            cmd.linear.x = -abs(speed)
            duration = 5.0  # 5초 후진 — 실제 환경에 맞게 조정
            t0 = time.monotonic()
            while time.monotonic() - t0 < duration:
                self._pub_cmd.publish(cmd)
                time.sleep(0.1)
            self._finish_task('RETURNED_HOME')

        threading.Thread(target=_go, daemon=True).start()

    # ── Public API (HTTP handler 에서 호출) ───────────────────────────────

    def run_by_name(self, name: str) -> tuple:
        """name = 스케줄 name 또는 task 종류로 즉시 실행."""
        # Find by schedule name first
        job = next((j for j in self._schedule if j.get('name') == name), None)
        if job is None:
            # Fallback: treat name as task type with defaults
            job = {'name': name, 'task': name, 'speed': 0.30,
                   'description': f'manual:{name}'}
        return self._start_task(job)

    def stop_current(self) -> None:
        self._finish_task('MANUAL_STOP')

    def get_api_status(self) -> dict:
        with self._lock:
            task = self._current_task
        elapsed = int(time.monotonic() - self._task_start_t) if task else 0
        return {
            'current_task':    task.get('name')        if task else None,
            'task_type':       task.get('task')        if task else None,
            'task_desc':       task.get('description') if task else None,
            'elapsed_sec':     elapsed,
            'row_follow_state': self._row_follow_state,
            'obstacle_action': self._obstacle_action,
            'next_tasks':      self._get_next_tasks(3),
        }

    def get_api_schedule(self) -> list:
        now     = datetime.datetime.now()
        weekday = now.weekday()
        result  = []
        for job in self._schedule:
            days = job.get('days', list(range(7)))
            result.append({
                'name':        job.get('name'),
                'time':        job.get('time'),
                'task':        job.get('task'),
                'description': job.get('description', ''),
                'speed':       job.get('speed', 0.3),
                'active_today': weekday in days,
            })
        return result

    def _get_next_tasks(self, n: int) -> list:
        now     = datetime.datetime.now()
        weekday = now.weekday()
        hhmm    = now.strftime('%H:%M')
        upcoming = []
        for job in self._schedule:
            days = job.get('days', list(range(7)))
            t    = job.get('time', '00:00')
            active = weekday in days and t >= hhmm
            if active:
                upcoming.append({
                    'name': job.get('name'),
                    'time': t,
                    'task': job.get('task'),
                })
        return upcoming[:n]

    def destroy_node(self):
        self._http_server.shutdown()
        super().destroy_node()


# ── Entry point ────────────────────────────────────────────────────────────────

def main(args=None):
    if not _ROS2_AVAILABLE:
        print('rclpy not available — cannot run.')
        return
    rclpy.init(args=args)
    node = TaskSchedulerNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
