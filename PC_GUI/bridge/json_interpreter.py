"""
json_interpreter.py — AGV JSON 명령어 인터프리터 (Phase A + B + C + D + E + F)

Phase A: move / stop / wait / log
Phase B: set / calc / inc / dec
         if / while / repeat
         label / goto
         call / return / abort
Phase C: read_sensor / wait_until
Phase D: do_set / pwm_set / read_io         (I/O 확장)
Phase E: drive_control / move_to            (모션 보완)
         set_watchdog                        (안전 타임아웃)
         break / continue                   (루프 제어)
         pause / resume                     (일시정지)
         _comment 필드 허용 (무시)
         set/calc fmt: 키 — 문자열 포맷
Phase F: 디버그 트레이스 캡처
         enable_debug() / get_trace() / get_trace_summary()
"""

import asyncio
import json
import logging
import os
import re
import threading
import time
from typing import Optional, Callable, Awaitable

log = logging.getLogger("interp")

STATE_IDLE    = "idle"
STATE_RUNNING = "running"
STATE_PAUSED  = "paused"
STATE_DONE    = "done"
STATE_ERROR   = "error"
STATE_STOPPED = "stopped"

_MAX_WHILE_ITER = 10_000
_MAX_CALL_DEPTH = 8
_MAX_TRACE      = 1_000   # 트레이스 최대 항목 수

# 트레이스에 항상 포함할 핵심 센서 목록
_TRACE_SENSORS = (
    "dist_mm", "pos_left_hw", "pos_right_hw",
    "vel_left_hw", "vel_right_hw",
    "all_ready", "obstacle_action",
    "di_val", "do_val",
)


class _GotoSignal(Exception):
    """label/goto 점프 신호 — 내부 전용."""
    def __init__(self, label: str):
        self.label = label


class _ReturnSignal(Exception):
    """call/return 서브루틴 복귀 신호 — 내부 전용."""


class _AbortSignal(Exception):
    """abort 명령 신호 — 내부 전용."""
    def __init__(self, msg: str = ""):
        self.msg = msg


class _BreakSignal(Exception):
    """break 명령 신호 — while/repeat 루프 즉시 탈출."""


class _ContinueSignal(Exception):
    """continue 명령 신호 — while/repeat 다음 반복으로 점프."""


class JsonInterpreter:
    """
    JSON 프로그램 파일을 읽어 AGV 명령을 실행하는 인터프리터.

    send_velocity(linear, angular) 콜백을 통해 하드웨어를 제어한다.
    모든 실행은 asyncio 태스크로 비동기 동작하며,
    stop() 호출 시 50 ms 이내에 안전하게 중단된다.
    """

    def __init__(
        self,
        send_velocity:  Callable[[float, float], Awaitable[None]],
        programs_dir:   str,
        broadcast_fn:   Optional[Callable[[dict], Awaitable[None]]] = None,
        send_io_set:    Optional[Callable[[int, int, int, int], Awaitable[None]]] = None,
        send_drive_cmd: Optional[Callable[[str, int, bool], Awaitable[None]]] = None,
    ):
        self._send_vel      = send_velocity
        self._send_io_set   = send_io_set     # (do_mask, do_val, pwm_ch, pwm_duty) → None
        self._send_drive    = send_drive_cmd  # (action, axis, enable) → None  (Phase E)
        self._programs      = programs_dir
        self._broadcast     = broadcast_fn

        self._state      = STATE_IDLE
        self._program    = ""
        self._line       = 0
        self._total      = 0
        self._current_cmd: dict = {}
        self._vars: dict = {}
        self._log_buf: list[str] = []
        self._start_ts   = 0.0
        self._task: Optional[asyncio.Task] = None
        self._stop_evt   = asyncio.Event()
        self._call_depth = 0

        # ── Phase E: 일시정지 / 워치독 ──────────────────────────────────────
        # _resume_evt: set=실행 중, cleared=일시정지
        self._resume_evt:      Optional[asyncio.Event] = None
        self._watchdog_task:   Optional[asyncio.Task]  = None
        self._watchdog_ms:     float = 0.0   # 0 = 비활성

        # ── Phase F: 실행 트레이스 ──────────────────────────────────────────
        self._debug_mode:  bool       = False
        self._trace:       list[dict] = []
        self._trace_start: float      = 0.0
        # 핸들러가 세부 결과를 기록할 때 사용 (timeout 등 예외 없는 비정상 종료)
        self._cmd_result:  tuple[str, str] = ("ok", "")

        # ── Phase C: 센서 스토어 ────────────────────────────────────────────
        self._sensor_lock = threading.Lock()
        self._sensors: dict = {
            "dist_mm":      -1,      # D435i 중앙 거리 (mm), -1=유효없음
            "accel_x":       0.0,
            "accel_y":       0.0,
            "accel_z":       0.0,
            "accel_mag":     0.0,
            "pos_left_hw":   0,      # 좌측 바퀴 엔코더 카운트
            "pos_right_hw":  0,
            "vel_left_hw":   0,      # 좌측 바퀴 속도 (HW counts/s)
            "vel_right_hw":  0,
            "all_ready":     False,  # 드라이브 ready 여부
            "cia402_left":   "UNKNOWN",
            "cia402_right":  "UNKNOWN",
            "obstacle_action": "CLEAR",  # CLEAR/SLOW/WAIT/FULL_STOP
            # Phase D — I/O 확장
            "di_val":    0,
            "do_val":    0,
            "di_0": False, "di_1": False, "di_2": False, "di_3": False,
            "di_4": False, "di_5": False, "di_6": False, "di_7": False,
            "ai_val_0": 0, "ai_val_1": 0, "ai_val_2": 0, "ai_val_3": 0,
            "ai_volt_0": 0.0, "ai_volt_1": 0.0, "ai_volt_2": 0.0, "ai_volt_3": 0.0,
        }

    # ── Phase C: 센서 공개 API ───────────────────────────────────────────────

    def update_sensor(self, key: str, value) -> None:
        """bridge.py (카메라 스레드, 시리얼 루프) 에서 센서값 갱신."""
        with self._sensor_lock:
            self._sensors[key] = value

    def get_sensors(self) -> dict:
        """현재 센서 스냅샷 반환 (HTTP /sensor/data 엔드포인트용)."""
        with self._sensor_lock:
            return dict(self._sensors)

    def _get_sensor(self, name: str):
        with self._sensor_lock:
            return self._sensors.get(name, 0)

    # ── 공개 API ──────────────────────────────────────────────────────────────

    def list_programs(self) -> list[str]:
        try:
            return sorted(
                f[:-5] for f in os.listdir(self._programs)
                if f.endswith(".json")
            )
        except OSError:
            return []

    def load_program(self, name: str) -> Optional[dict]:
        path = os.path.join(self._programs, f"{name}.json")
        try:
            with open(path, encoding="utf-8") as f:
                return json.load(f)
        except (OSError, json.JSONDecodeError) as e:
            log.error("load_program %r: %s", name, e)
            return None

    def save_program(self, name: str, data: dict) -> bool:
        path = os.path.join(self._programs, f"{name}.json")
        try:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
            return True
        except OSError as e:
            log.error("save_program %r: %s", name, e)
            return False

    def delete_program(self, name: str) -> bool:
        path = os.path.join(self._programs, f"{name}.json")
        try:
            os.remove(path)
            return True
        except OSError:
            return False

    async def run(self, name: str) -> bool:
        """프로그램 실행. 이미 실행 중이면 False."""
        if self._state == STATE_RUNNING or self._state == STATE_PAUSED:
            log.warning("Already running %r", self._program)
            return False
        prog = self.load_program(name)
        if prog is None:
            self._set_state(STATE_ERROR, f"프로그램 없음: {name}")
            return False
        self._stop_evt.clear()
        self._resume_evt = asyncio.Event()
        self._resume_evt.set()   # 시작 시 실행 상태
        self._task = asyncio.create_task(self._execute(prog))
        return True

    async def stop(self):
        """실행 중단 요청 (50 ms 이내 응답)."""
        # 일시정지 중이었다면 먼저 깨워서 stop_evt를 감지하게 함
        if self._resume_evt and not self._resume_evt.is_set():
            self._resume_evt.set()
        self._cancel_watchdog()
        if self._task and not self._task.done():
            self._stop_evt.set()
            try:
                await asyncio.wait_for(asyncio.shield(self._task), timeout=2.0)
            except asyncio.TimeoutError:
                self._task.cancel()
        await self._send_vel(0.0, 0.0)
        self._set_state(STATE_STOPPED)

    async def pause(self):
        """외부에서 일시정지 (HTTP /program/pause 엔드포인트용)."""
        if self._state == STATE_RUNNING and self._resume_evt:
            self._resume_evt.clear()
            self._set_state(STATE_PAUSED)

    async def resume(self):
        """외부에서 재개 (HTTP /program/resume 엔드포인트용)."""
        if self._state == STATE_PAUSED and self._resume_evt:
            self._resume_evt.set()
            self._set_state(STATE_RUNNING)

    def get_status(self) -> dict:
        elapsed = round(time.time() - self._start_ts, 1) if self._start_ts else 0.0
        return {
            "state":       self._state,
            "program":     self._program,
            "line":        self._line,
            "total":       self._total,
            "cmd":         self._current_cmd.get("cmd", ""),
            "vars":        dict(self._vars),
            "elapsed":     elapsed,
            "log":         self._log_buf[-20:],
            "watchdog_ms": self._watchdog_ms,
            "debug_mode":  self._debug_mode,
            "trace_count": len(self._trace),
        }

    # ── Phase F: 디버그 트레이스 공개 API ────────────────────────────────────

    def enable_debug(self, on: bool = True) -> None:
        """디버그 모드 토글. 실행 전에 설정해야 하며, 실행 중 변경은 무효."""
        self._debug_mode = on
        log.info("Debug mode: %s", "ON" if on else "OFF")

    def get_trace(self) -> list[dict]:
        """최근 실행의 명령별 트레이스 전체 반환 (bridge HTTP용)."""
        return list(self._trace)

    def get_trace_summary(self) -> str:
        """
        Claude 디버그 프롬프트용 압축 텍스트 요약.

        각 명령의 실행 결과·변수 변화·소요 시간을 테이블로 출력한다.
        오류 발생 명령은 ❌ 강조되고 직전 vars/sensors 스냅샷이 추가된다.
        """
        if not self._trace:
            return "(트레이스 없음 — debug_mode=True로 실행하세요)"

        elapsed_total = (
            self._trace[-1]["elapsed_ms"] / 1000
            if self._trace else 0
        )
        done_count = len(self._trace)
        error_entry = next(
            (e for e in reversed(self._trace) if e["result"] not in ("ok", "label")),
            None,
        )

        lines = [
            f"=== 실행 트레이스: {self._program} ===",
            f"종료: {self._state}  |  프로그램 명령: {self._total}개"
            f"  |  트레이스 항목: {done_count}개 (중첩 포함)"
            f"  |  실행 시간: {elapsed_total:.1f}s",
            "",
            f" {'D':>1} {'PC':>3}  {'CMD':<14}  {'결과':<10}  {'ms':>6}  변수 변화",
            f" {'─'}  {'─'*3}  {'─'*14}  {'─'*10}  {'─'*6}  {'─'*34}",
        ]

        for e in self._trace:
            icon   = "❌" if e["result"] not in ("ok", "label", "return") else "  "
            depth  = e.get("depth", 0)
            indent = "  " * depth   # call 깊이 시각화

            delta  = e.get("vars_delta", {})
            if delta:
                delta_str = ", ".join(
                    f"{k}: {v['before']!r}→{v['after']!r}"
                    for k, v in list(delta.items())[:3]
                )
            else:
                delta_str = "-"

            # 핵심 인자 1개만 힌트로 표시 (최대 12자)
            args = e.get("args", {})
            hint = ""
            if args:
                key = next(iter(args))
                raw = f"{key}={args[key]!r}"
                hint = raw[:14]

            cmd_col = f"{indent}{e['cmd']}"
            lines.append(
                f"{icon} {depth}  {e['pc']:>3}  {cmd_col:<14}  "
                f"{e['result']:<10}  {e['duration_ms']:>6}  {delta_str}"
            )
            if hint:
                lines.append(f"             └ {hint}")

            # 오류 항목에는 추가 정보 표시
            if e["result"] not in ("ok", "label") and e.get("error_msg"):
                lines.append(f"             └─ 오류: {e['error_msg']}")

        # 오류 발생 직전 상태 스냅샷
        if error_entry:
            lines += ["", "▼ 오류 발생 시점 변수 스냅샷:"]
            vb = error_entry.get("vars_before", {})
            for k, v in vb.items():
                lines.append(f"  ${k} = {v!r}")

            lines += ["", "▼ 오류 발생 시점 센서 스냅샷:"]
            for k, v in error_entry.get("sensors", {}).items():
                lines.append(f"  {k} = {v!r}")

        return "\n".join(lines)

    # ── 최상위 실행 엔진 ──────────────────────────────────────────────────────

    async def _execute(self, prog: dict):
        """asyncio Task 진입점."""
        self._program    = prog.get("program", "unknown")
        self._vars       = dict(prog.get("vars", {}))
        commands         = prog.get("commands", [])
        self._total      = len(commands)
        self._line       = 0
        self._log_buf    = []
        self._start_ts   = time.time()
        self._call_depth = 0
        self._watchdog_ms = 0.0
        # Phase F: 트레이스 초기화
        self._trace       = []
        self._trace_start = time.monotonic()
        self._set_state(STATE_RUNNING)
        self._log(f"▶ 프로그램 시작: {self._program} ({self._total}개 명령)"
                  + (" [DEBUG]" if self._debug_mode else ""))

        try:
            await self._execute_block(commands)
        except asyncio.CancelledError:
            self._log("⏹ 태스크 취소됨")
        except _AbortSignal as e:
            self._log(f"❌ ABORT: {e.msg}")
            self._set_state(STATE_ERROR, e.msg)
            await self._send_vel(0.0, 0.0)
            return
        except Exception as exc:
            self._log(f"❌ 오류: {exc}")
            self._set_state(STATE_ERROR, str(exc))
            await self._send_vel(0.0, 0.0)
            return
        finally:
            self._cancel_watchdog()

        await self._send_vel(0.0, 0.0)
        if self._state == STATE_RUNNING:
            self._set_state(STATE_DONE)
            self._log(f"✅ 완료: {self._program}")

    async def _execute_block(self, commands: list):
        """
        명령 리스트를 PC(프로그램 카운터) 기반 루프로 실행.

        goto 신호를 수신하면 이 블록의 레이블 맵에서 탐색한다.
        해당 레이블이 없으면 상위 블록으로 재전파한다.
        """
        if not commands:
            return

        labels: dict[str, int] = {
            cmd["name"]: i
            for i, cmd in enumerate(commands)
            if cmd.get("cmd") == "label" and "name" in cmd
        }

        pc = 0
        while pc < len(commands):
            if self._stop_evt.is_set():
                self._log("⏹ 중단 요청")
                return

            # 일시정지 대기 (명령 사이에서만 정지)
            if self._resume_evt and not self._resume_evt.is_set():
                self._log("⏸ 일시정지 대기 중…")
                await self._resume_evt.wait()
                if self._stop_evt.is_set():
                    return
                self._log("▶ 재개")

            cmd = commands[pc]
            self._current_cmd = cmd
            self._line = pc + 1

            try:
                await self._dispatch(cmd)
            except _GotoSignal as g:
                if g.label in labels:
                    pc = labels[g.label]
                    self._log(f"  → {g.label} (line {pc + 1})")
                    continue
                raise
            pc += 1

    async def _dispatch(self, cmd: dict):
        name = cmd.get("cmd", "")

        # _comment 명령 또는 _로 시작하는 모든 명령 무시
        if not name or name.startswith("_"):
            return

        handlers = {
            # Phase A ─────────────────────────────────────────────────────
            "move":          self._cmd_move,
            "stop":          self._cmd_stop,
            "wait":          self._cmd_wait,
            "log":           self._cmd_log,
            # Phase B — 변수 ──────────────────────────────────────────────
            "set":           self._cmd_set,
            "calc":          self._cmd_calc,
            "inc":           self._cmd_inc,
            "dec":           self._cmd_dec,
            # Phase B — 흐름 제어 ─────────────────────────────────────────
            "if":            self._cmd_if,
            "while":         self._cmd_while,
            "repeat":        self._cmd_repeat,
            "label":         self._cmd_label,
            "goto":          self._cmd_goto,
            "call":          self._cmd_call,
            "return":        self._cmd_return,
            "abort":         self._cmd_abort,
            "break":         self._cmd_break,
            "continue":      self._cmd_continue,
            # Phase C — 센서 연동 ──────────────────────────────────────────
            "read_sensor":   self._cmd_read_sensor,
            "wait_until":    self._cmd_wait_until,
            # Phase D — I/O 확장 ───────────────────────────────────────────
            "do_set":        self._cmd_do_set,
            "pwm_set":       self._cmd_pwm_set,
            "read_io":       self._cmd_read_io,
            # Phase E — 모션 보완 / 안전 / 편의 ─────────────────────────────
            "drive_control": self._cmd_drive_control,
            "move_to":       self._cmd_move_to,
            "set_watchdog":  self._cmd_set_watchdog,
            "pause":         self._cmd_pause,
        }
        fn = handlers.get(name)

        if not self._debug_mode:
            # ── 일반 실행: 오버헤드 없는 빠른 경로 ────────────────────────
            if fn:
                await fn(cmd)
            else:
                self._log(f"⚠ 미구현 명령: {name!r}")
            return

        # ── 디버그 실행: 명령별 트레이스 캡처 ─────────────────────────────
        vars_before  = dict(self._vars)
        t_start      = time.monotonic()
        result       = "ok"
        error_msg    = ""
        captured_pc  = self._line   # 중첩 블록이 self._line을 덮어쓰기 전에 저장
        self._cmd_result = ("ok", "")   # 핸들러가 덮어쓸 수 있음

        try:
            if fn:
                await fn(cmd)
            else:
                self._log(f"⚠ 미구현 명령: {name!r}")
                result = "unknown_cmd"
        except _BreakSignal:
            result = "break"
            raise
        except _ContinueSignal:
            result = "continue"
            raise
        except _GotoSignal as g:
            result = f"goto:{g.label}"
            raise
        except _ReturnSignal:
            result = "return"
            raise
        except _AbortSignal as e:
            result    = "abort"
            error_msg = e.msg
            raise
        except asyncio.CancelledError:
            result = "cancelled"
            raise
        except Exception as exc:
            result    = "error"
            error_msg = str(exc)
            raise
        finally:
            # 핸들러가 _cmd_result 를 설정했으면 그것을 우선 사용 (timeout 등)
            if self._cmd_result[0] != "ok" and result == "ok":
                result, error_msg = self._cmd_result

            duration_ms = int((time.monotonic() - t_start) * 1000)
            elapsed_ms  = int((time.monotonic() - self._trace_start) * 1000)

            # 변경된 변수만 delta로 기록
            vars_after = dict(self._vars)
            vars_delta = {
                k: {"before": vars_before.get(k), "after": vars_after[k]}
                for k in vars_after
                if vars_after[k] != vars_before.get(k)
            }
            for k in vars_before:
                if k not in vars_after:
                    vars_delta[k] = {"before": vars_before[k], "after": None}

            # 핵심 센서 스냅샷 (오류/timeout 시 디버깅용)
            sensors = {k: self._get_sensor(k) for k in _TRACE_SENSORS}

            entry = {
                "pc":          captured_pc,
                "depth":       self._call_depth,
                "cmd":         name,
                "args":        {k: v for k, v in cmd.items()
                                if k not in ("cmd", "_comment")},
                "vars_before": vars_before,
                "vars_delta":  vars_delta,
                "sensors":     sensors,
                "result":      result,
                "error_msg":   error_msg,
                "elapsed_ms":  elapsed_ms,
                "duration_ms": duration_ms,
            }
            self._trace.append(entry)
            if len(self._trace) > _MAX_TRACE:
                self._trace = self._trace[-_MAX_TRACE:]

    # ── Phase A 명령 핸들러 ───────────────────────────────────────────────────

    async def _cmd_move(self, cmd: dict):
        linear   = float(self._resolve(cmd.get("linear",  0.0)))
        angular  = float(self._resolve(cmd.get("angular", 0.0)))
        duration = cmd.get("duration")
        until    = cmd.get("until")

        await self._send_vel(linear, angular)

        if until is not None:
            timeout_s = float(self._resolve(cmd.get("timeout_ms", 30_000))) / 1000.0
            deadline  = time.monotonic() + timeout_s
            self._log(f"  move  lin={linear:.2f} ang={angular:.2f}  until=… timeout={timeout_s:.0f}s")
            while not self._eval_cond(until):
                if self._stop_evt.is_set():
                    break
                if time.monotonic() > deadline:
                    self._log(f"  move  until-timeout ({timeout_s:.0f} s)")
                    self._cmd_result = (
                        "timeout",
                        f"until 조건 미충족: {until}  timeout={timeout_s:.0f}s",
                    )
                    break
                await asyncio.sleep(0.05)
            await self._send_vel(0.0, 0.0)
            self._log("  move  until 조건 충족 → 정지")
        elif duration is not None:
            secs = float(self._resolve(duration))
            self._log(f"  move  lin={linear:.2f} ang={angular:.2f}  {secs:.1f}s")
            await self._interruptible_sleep(secs)
            await self._send_vel(0.0, 0.0)
        else:
            self._log(f"  move  lin={linear:.2f} ang={angular:.2f}  (연속)")

    async def _cmd_stop(self, _cmd: dict):
        self._log("  stop")
        await self._send_vel(0.0, 0.0)

    async def _cmd_wait(self, cmd: dict):
        ms = float(self._resolve(cmd.get("ms", 0)))
        self._log(f"  wait  {ms:.0f} ms")
        await self._interruptible_sleep(ms / 1000.0)

    async def _cmd_log(self, cmd: dict):
        msg = self._interpolate(str(cmd.get("msg", "")))
        self._log(f"  [LOG] {msg}")

    # ── Phase B — 변수 명령 ───────────────────────────────────────────────────

    async def _cmd_set(self, cmd: dict):
        var = cmd["var"]
        if "fmt" in cmd:
            # 문자열 포맷: $var 보간만 수행, eval 없음
            val = self._interpolate(str(cmd["fmt"]))
        elif "expr" in cmd:
            val = self._resolve(cmd["expr"])
        else:
            val = self._resolve(cmd.get("val", 0))
        self._vars[var] = val
        self._log(f"  set   {var} = {val!r}")

    async def _cmd_calc(self, cmd: dict):
        """수식 또는 문자열 포맷 전용 set."""
        var = cmd["var"]
        if "fmt" in cmd:
            val = self._interpolate(str(cmd["fmt"]))
        else:
            val = self._resolve(cmd["expr"])
        self._vars[var] = val
        self._log(f"  calc  {var} = {val!r}")

    async def _cmd_inc(self, cmd: dict):
        var  = cmd["var"]
        step = float(self._resolve(cmd.get("step", 1)))
        self._vars[var] = float(self._vars.get(var, 0)) + step
        self._log(f"  inc   {var} → {self._vars[var]}")

    async def _cmd_dec(self, cmd: dict):
        var  = cmd["var"]
        step = float(self._resolve(cmd.get("step", 1)))
        self._vars[var] = float(self._vars.get(var, 0)) - step
        self._log(f"  dec   {var} → {self._vars[var]}")

    # ── Phase B — 흐름 제어 명령 ──────────────────────────────────────────────

    async def _cmd_if(self, cmd: dict):
        cond = self._eval_cond(cmd["cond"])
        self._log(f"  if    → {'true' if cond else 'false'}")
        block = cmd.get("then", []) if cond else cmd.get("else", [])
        if block:
            await self._execute_block(block)

    async def _cmd_while(self, cmd: dict):
        max_iter = int(cmd.get("max_iter", _MAX_WHILE_ITER))
        n = 0
        while self._eval_cond(cmd["cond"]):
            if self._stop_evt.is_set():
                break
            n += 1
            if n > max_iter:
                raise RuntimeError(f"while 무한루프 방지 ({max_iter}회 초과)")
            self._log(f"  while [{n}] cond=true")
            try:
                await self._execute_block(cmd.get("body", []))
            except _BreakSignal:
                self._log("  while break")
                break
            except _ContinueSignal:
                self._log("  while continue → 다음 반복")
                continue
        self._log(f"  while 종료 ({n}회 실행)")

    async def _cmd_repeat(self, cmd: dict):
        count = int(float(self._resolve(cmd.get("count", 1))))
        self._log(f"  repeat {count}회")
        for i in range(count):
            if self._stop_evt.is_set():
                break
            self._log(f"  repeat [{i + 1}/{count}]")
            try:
                await self._execute_block(cmd.get("body", []))
            except _BreakSignal:
                self._log("  repeat break")
                break
            except _ContinueSignal:
                self._log("  repeat continue → 다음 반복")
                continue

    async def _cmd_label(self, _cmd: dict):
        pass   # _execute_block 이 미리 레이블 맵을 구축

    async def _cmd_goto(self, cmd: dict):
        target = cmd.get("label", "")
        self._log(f"  goto  {target!r}")
        raise _GotoSignal(target)

    async def _cmd_call(self, cmd: dict):
        name = cmd.get("program", "")
        if self._call_depth >= _MAX_CALL_DEPTH:
            raise RuntimeError(f"call 최대 깊이 초과 ({_MAX_CALL_DEPTH})")
        prog = self.load_program(name)
        if prog is None:
            raise RuntimeError(f"call: 프로그램 없음 {name!r}")
        self._log(f"  call  → {name!r}  (depth={self._call_depth + 1})")
        pre_call_keys = set(self._vars.keys())
        for k, v in prog.get("vars", {}).items():
            if k not in self._vars:
                self._vars[k] = v
        self._call_depth += 1
        try:
            await self._execute_block(prog.get("commands", []))
        except _ReturnSignal:
            pass
        finally:
            self._call_depth -= 1
            for k in list(self._vars):
                if k not in pre_call_keys:
                    del self._vars[k]
        self._log(f"  call  ← {name!r}  복귀")

    async def _cmd_return(self, _cmd: dict):
        self._log("  return")
        raise _ReturnSignal()

    async def _cmd_abort(self, cmd: dict):
        msg = self._interpolate(str(cmd.get("msg", "ABORT")))
        self._log(f"  ABORT: {msg}")
        raise _AbortSignal(msg)

    async def _cmd_break(self, _cmd: dict):
        self._log("  break")
        raise _BreakSignal()

    async def _cmd_continue(self, _cmd: dict):
        self._log("  continue")
        raise _ContinueSignal()

    # ── Phase C — 센서 명령 ───────────────────────────────────────────────────

    async def _cmd_read_sensor(self, cmd: dict):
        sensor = cmd.get("sensor", "")
        var    = cmd.get("var", "")
        val    = self._get_sensor(sensor)
        if var:
            self._vars[var] = val
        self._log(f"  read_sensor  {sensor} = {val!r} → ${var}")

    async def _cmd_wait_until(self, cmd: dict):
        cond        = cmd["cond"]
        timeout_ms  = float(self._resolve(cmd.get("timeout_ms", 10_000)))
        on_timeout  = cmd.get("on_timeout", "continue")
        poll_s      = float(self._resolve(cmd.get("poll_ms", 50))) / 1000.0

        deadline = (time.monotonic() + timeout_ms / 1000.0) if timeout_ms > 0 else None
        self._log(f"  wait_until  timeout={timeout_ms:.0f}ms on_timeout={on_timeout}")

        while not self._eval_cond(cond):
            if self._stop_evt.is_set():
                return
            if deadline is not None and time.monotonic() > deadline:
                self._log(f"  wait_until TIMEOUT → {on_timeout}")
                self._cmd_result = (
                    "timeout",
                    f"조건 미충족: {cond}  on_timeout={on_timeout}",
                )
                if on_timeout == "abort":
                    raise _AbortSignal(f"wait_until timeout: {cond}")
                return
            await asyncio.sleep(poll_s)

        self._log("  wait_until 조건 충족")

    # ── Phase D — I/O 명령 핸들러 ────────────────────────────────────────────

    async def _cmd_do_set(self, cmd: dict):
        if self._send_io_set is None:
            self._log("⚠ do_set: send_io_set 콜백 미등록")
            return
        if "bit" in cmd:
            bit  = int(self._resolve(cmd["bit"])) & 7
            val  = int(bool(self._resolve(cmd.get("val", 0))))
            mask = 1 << bit
            dval = val << bit
        else:
            mask = int(self._resolve(cmd.get("mask", 0xFF))) & 0xFF
            dval = int(self._resolve(cmd.get("val",   0)))   & 0xFF
        self._log(f"  do_set  mask=0x{mask:02X} val=0x{dval:02X}")
        await self._send_io_set(mask, dval, 0xFF, 0)

    async def _cmd_pwm_set(self, cmd: dict):
        if self._send_io_set is None:
            self._log("⚠ pwm_set: send_io_set 콜백 미등록")
            return
        ch        = int(self._resolve(cmd.get("ch", 0))) & 3
        duty_pct  = float(self._resolve(cmd.get("duty", 0.0)))
        duty_x100 = min(max(int(duty_pct * 100), 0), 10000)
        self._log(f"  pwm_set  ch={ch}  duty={duty_pct:.2f}% ({duty_x100})")
        await self._send_io_set(0, 0, ch, duty_x100)

    async def _cmd_read_io(self, cmd: dict):
        sensor = cmd.get("sensor", "")
        var    = cmd.get("var", "")
        val    = self._get_sensor(sensor)
        if var:
            self._vars[var] = val
        self._log(f"  read_io  {sensor} = {val!r} → ${var}")

    # ── Phase E — 모션 보완 / 안전 / 편의 ────────────────────────────────────

    async def _cmd_drive_control(self, cmd: dict):
        """
        EtherCAT 드라이브 제어.

        {"cmd":"drive_control","action":"fault_reset"}
        {"cmd":"drive_control","action":"fault_reset","axis":0}
        {"cmd":"drive_control","action":"run_enable","enable":true}
        {"cmd":"drive_control","action":"run_enable","enable":false,"axis":1}

        action: "fault_reset" | "run_enable"
        axis:   0|1 또는 생략(=0xFF=전체)
        enable: true/false (run_enable 전용)
        """
        if self._send_drive is None:
            self._log("⚠ drive_control: send_drive_cmd 콜백 미등록")
            return

        action = cmd.get("action", "")
        axis   = int(self._resolve(cmd.get("axis", 0xFF))) & 0xFF
        enable = bool(self._resolve(cmd.get("enable", True)))

        if action not in ("fault_reset", "run_enable"):
            self._log(f"⚠ drive_control: 알 수 없는 action={action!r}")
            return

        self._log(f"  drive_control  action={action} axis=0x{axis:02X} enable={enable}")
        await self._send_drive(action, axis, enable)

        # fault_reset 후 드라이브 안정화 대기
        if action == "fault_reset":
            await self._interruptible_sleep(0.5)

    async def _cmd_move_to(self, cmd: dict):
        """
        엔코더 기반 정밀 이동.

        상대 이동 (기본):
          {"cmd":"move_to","dist_mm":500,"speed":0.3,"timeout_ms":15000}
          {"cmd":"move_to","counts":50000,"speed":0.3}

        절대 이동:
          {"cmd":"move_to","counts":100000,"speed":0.3,"mode":"absolute"}

        파라미터:
          dist_mm      상대 이동 거리 [mm] (양수=전진, 음수=후진)
          counts       상대/절대 엔코더 카운트
          unit_scale   counts/mm (기본: 변수 $unit_scale 또는 100)
          speed        이동 속도 [m/s] (기본 0.2)
          mode         "relative"(기본) | "absolute"
          timeout_ms   최대 대기 시간 (기본 30000)
        """
        mode       = cmd.get("mode", "relative")
        speed      = float(self._resolve(cmd.get("speed", 0.2)))
        timeout_s  = float(self._resolve(cmd.get("timeout_ms", 30_000))) / 1000.0

        pos_l = int(self._get_sensor("pos_left_hw"))
        pos_r = int(self._get_sensor("pos_right_hw"))

        if "dist_mm" in cmd:
            us    = float(self._resolve(cmd.get("unit_scale",
                          self._vars.get("unit_scale", 100))))
            delta = float(self._resolve(cmd["dist_mm"])) * us
        elif "counts" in cmd:
            raw_counts = float(self._resolve(cmd["counts"]))
            delta = raw_counts if mode == "relative" else (raw_counts - pos_l)
        else:
            self._log("⚠ move_to: dist_mm 또는 counts 필요")
            return

        target_l = pos_l + delta
        target_r = pos_r + delta
        forward  = delta >= 0
        linear   = abs(speed) * (1 if forward else -1)

        self._log(
            f"  move_to  mode={mode}  delta={delta:.0f}cnt  "
            f"target_l={target_l:.0f}  speed={linear:.2f}m/s  timeout={timeout_s:.0f}s"
        )

        await self._send_vel(linear, 0.0)
        deadline = time.monotonic() + timeout_s

        while True:
            if self._stop_evt.is_set():
                break
            if time.monotonic() > deadline:
                cur_l = int(self._get_sensor("pos_left_hw"))
                self._log("  move_to  timeout — 정지")
                self._cmd_result = (
                    "timeout",
                    f"pos_left_hw={cur_l} / 목표={target_l:.0f} (도달율: "
                    f"{abs(cur_l - pos_l) / abs(delta) * 100:.1f}%)"
                    if delta != 0 else f"pos_left_hw={cur_l}",
                )
                break
            cur_l = int(self._get_sensor("pos_left_hw"))
            reached = (cur_l >= target_l) if forward else (cur_l <= target_l)
            if reached:
                self._log(f"  move_to  도달: cur={cur_l}")
                break
            await asyncio.sleep(0.02)

        await self._send_vel(0.0, 0.0)

    async def _cmd_set_watchdog(self, cmd: dict):
        """
        실행 전체 안전 타임아웃 설정.

        {"cmd":"set_watchdog","ms":60000}   ← 60초 워치독 설정
        {"cmd":"set_watchdog","ms":0}       ← 워치독 해제

        타임아웃 시 프로그램 자동 정지 (stop_evt 신호).
        프로그램 내 어디서든 재설정 가능.
        """
        ms = float(self._resolve(cmd.get("ms", 0)))
        self._cancel_watchdog()
        self._watchdog_ms = ms
        if ms > 0:
            self._watchdog_task = asyncio.create_task(self._watchdog_coro(ms))
            self._log(f"  set_watchdog  {ms:.0f} ms")
        else:
            self._log("  set_watchdog  해제")

    async def _cmd_pause(self, cmd: dict):
        """
        프로그램 내부에서 일시정지 요청.

        {"cmd":"pause"}
        {"cmd":"pause","msg":"운전자 확인 후 resume 누르세요"}

        resume() 공개 API 또는 HTTP POST /program/resume 으로 재개.
        """
        msg = cmd.get("msg", "")
        if msg:
            self._log(f"  pause  [{msg}]")
        else:
            self._log("  pause  (외부 resume 대기)")
        if self._resume_evt:
            self._resume_evt.clear()
            self._set_state(STATE_PAUSED)
            await self._resume_evt.wait()
            if self._stop_evt.is_set():
                return
            self._set_state(STATE_RUNNING)

    # ── 조건 평가 ─────────────────────────────────────────────────────────────

    def _eval_cond(self, cond: dict) -> bool:
        if "and" in cond:
            return all(self._eval_cond(c) for c in cond["and"])
        if "or" in cond:
            return any(self._eval_cond(c) for c in cond["or"])
        if "not" in cond:
            return not self._eval_cond(cond["not"])

        if "var" in cond:
            left = self._vars.get(cond["var"], 0)
        elif "sensor" in cond:
            left = self._get_sensor(cond["sensor"])
        elif "expr" in cond:
            left = self._resolve(cond["expr"])
        else:
            left = 0

        right = self._resolve(cond.get("val", cond.get("value", 0)))
        op    = cond.get("op", "==")

        ops = {
            "==": lambda a, b: a == b,
            "!=": lambda a, b: a != b,
            "<":  lambda a, b: a <  b,
            "<=": lambda a, b: a <= b,
            ">":  lambda a, b: a >  b,
            ">=": lambda a, b: a >= b,
        }
        fn = ops.get(op)
        if fn is None:
            self._log(f"⚠ 알 수 없는 조건 연산자: {op!r}")
            return False

        try:
            return fn(float(left), float(right))
        except (TypeError, ValueError):
            return str(left) == str(right)

    # ── 유틸 ─────────────────────────────────────────────────────────────────

    # ASCII 식별자만 매칭 — 한글 등 비ASCII 문자가 \w에 포함되어
    # "$count회" → "count회"로 잘못 파싱되는 것을 방지
    _VAR_RE = re.compile(r'\$([A-Za-z_][A-Za-z0-9_]*)')

    def _resolve(self, value):
        """
        값 치환 + 수식 평가.

        - 숫자/bool 리터럴 → 그대로 반환
        - "$varname"        → vars[varname]
        - "수식 with $var"  → $var 치환 후 eval()
        """
        if not isinstance(value, str):
            return value
        if re.match(r'^\$[A-Za-z_][A-Za-z0-9_]*$', value):
            key = value[1:]
            if key in self._vars:
                return self._vars[key]
            log.warning("undefined var: %r", value)
            return 0
        def sub(m):
            k = m.group(1)
            return str(self._vars.get(k, 0))
        expr = self._VAR_RE.sub(sub, value)
        try:
            return eval(expr, {"__builtins__": {}})  # noqa: S307
        except Exception:
            return value

    def _interpolate(self, text: str) -> str:
        """문자열 내 $var 보간 → 문자열로 치환 (eval 없음). ASCII 식별자만 매칭."""
        def sub(m):
            v = self._vars.get(m.group(1), f"${m.group(1)}")
            return str(v)
        return self._VAR_RE.sub(sub, text)

    async def _interruptible_sleep(self, secs: float):
        """stop_evt 감지하며 sleep — 최대 50 ms 지연."""
        deadline = time.monotonic() + secs
        while time.monotonic() < deadline:
            if self._stop_evt.is_set():
                return
            remaining = min(0.05, deadline - time.monotonic())
            if remaining > 0:
                await asyncio.sleep(remaining)

    async def _watchdog_coro(self, ms: float):
        """워치독 코루틴 — ms 경과 후 stop_evt 신호."""
        await asyncio.sleep(ms / 1000.0)
        if self._state in (STATE_RUNNING, STATE_PAUSED):
            self._log(f"⏰ 워치독 타임아웃 ({ms:.0f} ms) — 강제 정지")
            self._stop_evt.set()
            if self._resume_evt:
                self._resume_evt.set()   # pause 중이라면 깨워서 stop_evt 감지

    def _cancel_watchdog(self):
        if self._watchdog_task and not self._watchdog_task.done():
            self._watchdog_task.cancel()
        self._watchdog_task = None
        self._watchdog_ms   = 0.0

    def _set_state(self, state: str, detail: str = ""):
        self._state = state
        msg = f"[상태] {state}" + (f" — {detail}" if detail else "")
        self._log(msg)
        if self._broadcast:
            asyncio.ensure_future(
                self._broadcast({"type": "program_status", **self.get_status()})
            )

    def _log(self, msg: str):
        ts   = time.strftime("%H:%M:%S")
        line = f"{ts}  {msg}"
        self._log_buf.append(line)
        if len(self._log_buf) > 200:
            self._log_buf = self._log_buf[-200:]
        log.info("INTERP %s", msg)
