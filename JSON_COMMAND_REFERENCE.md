# AGV JSON 프로그램 명령어 참조서

**버전**: 1.5 (Phase E — 모션 보완 / 안전 / 루프 제어 추가)  
**최종 수정**: 2026-06-14  
**인터프리터**: `PC_GUI/bridge/json_interpreter.py`  
**편집기**: `http://<host>:5100/editor.html`

---

## 목차

1. [프로그램 구조](#1-프로그램-구조)
2. [모션 명령 (Phase A + E)](#2-모션-명령-phase-a--e)
3. [대기 명령 (Phase A / C)](#3-대기-명령-phase-a--c)
4. [변수 명령 (Phase B + E)](#4-변수-명령-phase-b--e)
5. [흐름 제어 (Phase B + E)](#5-흐름-제어-phase-b--e)
6. [서브루틴 (Phase B)](#6-서브루틴-phase-b)
7. [센서 명령 (Phase C)](#7-센서-명령-phase-c)
8. [조건 표현식](#8-조건-표현식)
9. [I/O 명령 (Phase D)](#9-io-명령-phase-d)
10. [드라이브 제어 (Phase E)](#10-드라이브-제어-phase-e)
11. [사용 가능 센서 목록](#11-사용-가능-센서-목록)
12. [완전한 예제 프로그램](#12-완전한-예제-프로그램)
13. [REST API](#13-rest-api)

---

## 1. 프로그램 구조

```json
{
  "program": "프로그램_이름",
  "version": "1.0",
  "description": "한 줄 설명",
  "vars": {
    "speed":     0.2,
    "threshold": 400
  },
  "commands": [
    { "cmd": "..." },
    { "cmd": "..." }
  ]
}
```

| 필드 | 필수 | 설명 |
|------|------|------|
| `program` | ✅ | 저장 파일명 기준 (공백 없이) |
| `version` | - | 버전 문자열 (참고용) |
| `description` | - | 한 줄 설명 (참고용) |
| `vars` | - | 초기 변수값 (실행 전 `set`과 동일) |
| `commands` | ✅ | 명령 배열 (순서대로 실행) |

> **변수 참조**: 값 자리에 `"$변수명"` 형식으로 사용. 수식은 `"$a * 2 + 1"`.

---

## 2. 모션 명령 (Phase A + E)

### `move` — 주행

```json
{ "cmd": "move", "linear": 0.3, "angular": 0.0 }
```

| 파라미터 | 타입 | 기본값 | 설명 |
|---------|------|--------|------|
| `linear` | float | 0.0 | 전진 속도 [m/s], 음수=후진 |
| `angular` | float | 0.0 | 회전 속도 [rad/s], 양수=좌회전 |
| `duration` | float | - | 지정 초 후 자동 정지 |
| `until` | 조건 | - | 조건 충족 시 자동 정지 |
| `timeout_ms` | int | 30000 | `until` 사용 시 최대 대기 (ms) |

```json
{ "cmd": "move", "linear": 0.2, "duration": 3.0 }

{ "cmd": "move", "linear": 0.2,
  "until": { "sensor": "dist_mm", "op": "<", "val": 400 },
  "timeout_ms": 10000 }
```

### `stop` — 즉시 정지

```json
{ "cmd": "stop" }
```

### `move_to` — 엔코더 기반 정밀 이동 (Phase E)

```json
{ "cmd": "move_to", "dist_mm": 1000, "speed": 0.3, "timeout_ms": 15000 }
{ "cmd": "move_to", "counts": 50000, "speed": 0.2 }
{ "cmd": "move_to", "counts": 100000, "speed": 0.3, "mode": "absolute" }
```

| 파라미터 | 타입 | 기본값 | 설명 |
|---------|------|--------|------|
| `dist_mm` | float | - | 상대 이동 거리 [mm], 음수=후진 |
| `counts` | int | - | 엔코더 카운트 (dist_mm과 택1) |
| `unit_scale` | float | `$unit_scale` 변수 또는 100 | counts/mm 변환비 |
| `mode` | str | `"relative"` | `"relative"` 또는 `"absolute"` |
| `speed` | float | 0.2 | 주행 속도 [m/s] |
| `timeout_ms` | int | 30000 | 최대 대기 시간 (ms) |

- 엔코더 카운트 기준: `pos_left_hw` 센서
- 목표 도달 시 자동 정지, timeout 초과 시에도 정지
- 변수에서 unit_scale을 미리 설정해두면 생략 가능

```json
{ "cmd": "set", "var": "unit_scale", "val": 125 }
{ "cmd": "move_to", "dist_mm": 500, "speed": 0.25 }
```

---

## 3. 대기 명령 (Phase A / C)

### `wait` — 시간 대기

```json
{ "cmd": "wait", "ms": 1000 }
```

### `wait_until` — 조건 대기

```json
{
  "cmd": "wait_until",
  "cond": { "sensor": "dist_mm", "op": ">", "val": 500 },
  "timeout_ms": 5000,
  "on_timeout": "continue"
}
```

| 파라미터 | 기본값 | 설명 |
|---------|--------|------|
| `cond` | 필수 | 조건 표현식 |
| `timeout_ms` | 10000 | 0 = 무제한 |
| `on_timeout` | `"continue"` | `"continue"` 또는 `"abort"` |
| `poll_ms` | 50 | 폴링 간격 (ms) |

### `log` — 로그 출력

```json
{ "cmd": "log", "msg": "현재 거리: $dist mm, 속도: $speed" }
```

---

## 4. 변수 명령 (Phase B + E)

### `set` — 변수 설정

```json
{ "cmd": "set",  "var": "speed", "val": 0.3 }
{ "cmd": "set",  "var": "x",     "expr": "$a * 2 + 1" }
```

**문자열 포맷 (Phase E)** — `fmt` 키를 사용하면 `$var` 보간만 수행 (eval 없음):

```json
{ "cmd": "set",  "var": "label", "fmt": "열 $row / $total 완료" }
{ "cmd": "set",  "var": "msg",   "fmt": "속도: $speed m/s  거리: $dist_mm mm" }
{ "cmd": "log",  "msg": "$label" }
```

| 키 | 설명 |
|----|------|
| `val` | 숫자 리터럴 또는 `$var` 단일 변수 참조 |
| `expr` | Python 수식 eval — 숫자 결과 |
| `fmt` | 문자열 포맷 — `$var` 치환만, 결과는 문자열 |

### `calc` — 수식 계산

```json
{ "cmd": "calc", "var": "dist_m", "expr": "$dist_mm / 1000.0" }
{ "cmd": "calc", "var": "label",  "fmt": "완료: $count/$total" }
```

### `inc` / `dec` — 증감

```json
{ "cmd": "inc", "var": "count" }
{ "cmd": "inc", "var": "count", "step": 2 }
{ "cmd": "dec", "var": "speed", "step": 0.05 }
```

### `read_sensor` — 센서값 → 변수

```json
{ "cmd": "read_sensor", "sensor": "dist_mm", "var": "d" }
```

> I/O 값은 `read_io` 명령 사용 (§9 참조)

---

## 5. 흐름 제어 (Phase B + E)

### `if` — 조건 분기

```json
{
  "cmd": "if",
  "cond": { "var": "dist", "op": "<", "val": 300 },
  "then": [
    { "cmd": "stop" },
    { "cmd": "log", "msg": "긴급 정지!" }
  ],
  "else": [
    { "cmd": "move", "linear": 0.2 }
  ]
}
```

### `while` — 조건 반복

```json
{
  "cmd": "while",
  "cond": { "sensor": "dist_mm", "op": ">", "val": 400 },
  "max_iter": 1000,
  "body": [
    { "cmd": "move", "linear": 0.2 },
    { "cmd": "wait", "ms": 100 }
  ]
}
```

### `repeat` — 횟수 반복

```json
{
  "cmd": "repeat",
  "count": 3,
  "body": [
    { "cmd": "move", "linear": 0.3, "duration": 2.0 },
    { "cmd": "wait", "ms": 500 }
  ]
}
```

### `break` — 루프 즉시 탈출 (Phase E)

```json
{ "cmd": "break" }
```

- 현재 `while` 또는 `repeat` 루프를 즉시 종료하고 루프 다음 명령으로 이동
- 중첩 루프에서는 가장 안쪽 루프만 탈출

```json
{
  "cmd": "while",
  "cond": { "val": 1, "op": "==", "val": 1 },
  "body": [
    { "cmd": "read_sensor", "sensor": "dist_mm", "var": "d" },
    { "cmd": "if",
      "cond": { "var": "d", "op": "<", "val": 200 },
      "then": [ { "cmd": "break" } ]
    },
    { "cmd": "move", "linear": 0.2, "duration": 0.5 }
  ]
}
```

### `continue` — 다음 반복으로 점프 (Phase E)

```json
{ "cmd": "continue" }
```

- 루프 body의 남은 명령을 건너뛰고 다음 반복 조건 평가로 돌아감

```json
{
  "cmd": "repeat",
  "count": 10,
  "body": [
    { "cmd": "read_io", "sensor": "di_0", "var": "door" },
    { "cmd": "if",
      "cond": { "var": "door", "op": "==", "val": 0 },
      "then": [
        { "cmd": "log", "msg": "도어 열림 — 이번 사이클 건너뜀" },
        { "cmd": "continue" }
      ]
    },
    { "cmd": "call", "program": "spray_valve" }
  ]
}
```

### `pause` — 일시정지 (Phase E)

```json
{ "cmd": "pause" }
{ "cmd": "pause", "msg": "운전자 확인 후 resume 버튼을 누르세요" }
```

- 프로그램 실행이 일시정지 상태로 전환
- HTTP `POST /program/resume` 또는 WebHMI resume 버튼으로 재개
- `stop()` 호출 시 자동 깨어나서 중단 처리

### `set_watchdog` — 안전 타임아웃 (Phase E)

```json
{ "cmd": "set_watchdog", "ms": 60000 }
{ "cmd": "set_watchdog", "ms": 0 }
```

- 프로그램 실행 최대 시간 설정. 타임아웃 시 자동 정지 (`stop_evt` 발생)
- `ms: 0` → 워치독 해제
- 프로그램 내 언제든지 재설정 가능 (이전 타이머 취소 후 새 타이머 시작)
- 정상 완료 시 자동 취소됨

```json
{ "cmd": "set_watchdog", "ms": 120000 },
{ "cmd": "call", "program": "long_mission" },
{ "cmd": "set_watchdog", "ms": 0 }
```

### `_comment` 필드 — 코드 주석 (Phase E)

명령 객체에 `"_comment"` 키를 추가하면 인터프리터가 무시합니다.  
또한 `"cmd"` 값이 `_`로 시작하는 명령은 모두 무시됩니다.

```json
{ "cmd": "move", "linear": 0.2, "_comment": "장애물 확인 전 서행" }
{ "cmd": "wait", "ms": 1000,    "_comment": "안정화 대기" }
{ "cmd": "_comment", "_comment": "이 줄 전체가 주석 — 실행 안 됨" }
```

> LLM이 생성한 코드나 협업 작업에서 맥락 설명을 추가할 때 유용합니다.

### `label` / `goto` — 레이블 점프

```json
{ "cmd": "label", "name": "loop_start" },
{ "cmd": "move",  "linear": 0.2, "duration": 1.0 },
{ "cmd": "goto",  "label": "loop_start" }
```

### `abort` — 실행 중단

```json
{ "cmd": "abort", "msg": "배터리 부족으로 중단" }
```

---

## 6. 서브루틴 (Phase B)

### `call` — 프로그램 호출

```json
{ "cmd": "call", "program": "turn_180" }
```

- 최대 중첩 깊이: 8단계
- 호출된 프로그램의 변수는 복귀 시 자동 정리 (호출자 스코프 격리)

### `return` — 호출 복귀

```json
{ "cmd": "return" }
```

---

## 7. 센서 명령 (Phase C)

### `read_sensor`

```json
{ "cmd": "read_sensor", "sensor": "dist_mm", "var": "d" }
```

센서 목록은 §11 참조.

---

## 8. 조건 표현식

모든 조건(`if`, `while`, `wait_until`, `move-until`)에서 공통 사용.

### 기본 비교

```json
{ "var":    "speed",   "op": "<",  "val": 0.5 }
{ "sensor": "dist_mm", "op": "<",  "val": 400 }
{ "expr":   "$a + $b", "op": ">=", "val": 10  }
```

| `op` 값 | 의미 |
|---------|------|
| `==` | 같음 |
| `!=` | 다름 |
| `<`  | 미만 |
| `<=` | 이하 |
| `>`  | 초과 |
| `>=` | 이상 |

### 논리 결합

```json
{ "and": [
    { "sensor": "dist_mm",       "op": "<",  "val": 400 },
    { "sensor": "obstacle_action","op": "==", "val": "CLEAR" }
  ]
}

{ "or": [
    { "sensor": "di_0", "op": "==", "val": 1 },
    { "sensor": "di_1", "op": "==", "val": 1 }
  ]
}

{ "not": { "sensor": "all_ready", "op": "==", "val": 1 } }
```

---

## 9. I/O 명령 (Phase D)


베이스보드 확장 포트 제어. STM32 IO_STATUS(0x34) 패킷이 200ms마다 수신되어 센서 스토어가 자동 갱신된다.

### `do_set` — 디지털 출력 제어

**단일 비트 제어**

```json
{ "cmd": "do_set", "bit": 3, "val": 1 }
{ "cmd": "do_set", "bit": 3, "val": 0 }
```

| 파라미터 | 설명 |
|---------|------|
| `bit` | DO 채널 번호 (0~7 = PF0~PF7) |
| `val` | 1 = ON, 0 = OFF |

**마스크 일괄 제어**

```json
{ "cmd": "do_set", "mask": 15, "val": 5 }
```

| 파라미터 | 설명 |
|---------|------|
| `mask` | 변경할 비트 마스크 (0xFF = 전체) |
| `val`  | 마스크 내 비트의 새 값 |

예: `mask=0b00001111, val=0b00000101` → DO0=1, DO1=0, DO2=1, DO3=0 (DO4~7 불변)

**모두 OFF**

```json
{ "cmd": "do_set", "mask": 255, "val": 0 }
```

---

### `pwm_set` — PWM 출력 제어

```json
{ "cmd": "pwm_set", "ch": 0, "duty": 75.5 }
```

| 파라미터 | 설명 |
|---------|------|
| `ch` | PWM 채널 (0~3 = PC6~PC9, TIM3 CH1~4) |
| `duty` | Duty cycle (0.00 ~ 100.00 %) |

```json
{ "cmd": "pwm_set", "ch": 0, "duty": 0.0   }
{ "cmd": "pwm_set", "ch": 1, "duty": 50.0  }
{ "cmd": "pwm_set", "ch": 2, "duty": "$fan_speed" }
```

하드웨어: 20kHz, 0.01% 분해능 (duty_x100 / 10 → TIM3 CCR, 1000 steps)

---

### `read_io` — I/O 값 → 변수

```json
{ "cmd": "read_io", "sensor": "di_3",     "var": "door_open" }
{ "cmd": "read_io", "sensor": "ai_volt_0","var": "battery_v"  }
{ "cmd": "read_io", "sensor": "di_val",   "var": "di_bitmap"  }
```

`read_sensor`와 동일 동작. I/O 전용 센서 이름을 사용하는 별칭.

---

## 10. 드라이브 제어 (Phase E)

### `drive_control` — EtherCAT 드라이브 명령

```json
{ "cmd": "drive_control", "action": "fault_reset" }
{ "cmd": "drive_control", "action": "fault_reset",  "axis": 0 }
{ "cmd": "drive_control", "action": "run_enable",   "enable": true  }
{ "cmd": "drive_control", "action": "run_enable",   "enable": false, "axis": 1 }
```

| 파라미터 | 타입 | 기본값 | 설명 |
|---------|------|--------|------|
| `action` | str | 필수 | `"fault_reset"` 또는 `"run_enable"` |
| `axis` | int | 0xFF | 0=좌, 1=우, 0xFF=전체 |
| `enable` | bool | true | run_enable 전용 (true=서보 ON, false=서보 OFF) |

**동작 설명:**

| action | 설명 |
|--------|------|
| `fault_reset` | CiA402 Fault → Switch On Disabled 상태 전환 (에러 리셋). 내부적으로 500ms 안정화 대기 |
| `run_enable` | Operation Enabled (서보 ON) / Switch On Disabled (서보 OFF) 전환 |

**전형적인 초기화 시퀀스:**

```json
{ "cmd": "drive_control", "action": "fault_reset",   "_comment": "에러 리셋 (안전)" },
{ "cmd": "drive_control", "action": "run_enable",    "enable": true },
{ "cmd": "wait_until",
  "cond": { "sensor": "all_ready", "op": "==", "val": 1 },
  "timeout_ms": 5000,
  "on_timeout": "abort" },
{ "cmd": "log", "msg": "드라이브 준비 완료" }
```

---

## 11. 사용 가능 센서 목록

`read_sensor`, `read_io`, 조건 `"sensor":` 에서 사용.

### 카메라 / IMU (D435i)

| 센서 이름 | 타입 | 설명 |
|-----------|------|------|
| `dist_mm` | int | D435i 중심 픽셀 depth (mm), -1=유효없음 |
| `accel_x` | float | IMU 가속도 X [m/s²] |
| `accel_y` | float | IMU 가속도 Y [m/s²] |
| `accel_z` | float | IMU 가속도 Z [m/s²] |
| `accel_mag` | float | 가속도 크기 √(x²+y²+z²) [m/s²] |

### 주행 / 드라이브

| 센서 이름 | 타입 | 설명 |
|-----------|------|------|
| `pos_left_hw` | int | 좌측 바퀴 엔코더 카운트 |
| `pos_right_hw` | int | 우측 바퀴 엔코더 카운트 |
| `vel_left_hw` | int | 좌측 바퀴 속도 [HW counts/s] |
| `vel_right_hw` | int | 우측 바퀴 속도 [HW counts/s] |
| `all_ready` | bool | 좌/우 드라이브 모두 PDO-ready |
| `cia402_left` | str | 좌측 CiA402 상태 (`"OP_ENABLED"` 등) |
| `cia402_right` | str | 우측 CiA402 상태 |

### 안전 / 장애물

| 센서 이름 | 타입 | 설명 |
|-----------|------|------|
| `obstacle_action` | str | `CLEAR` / `SLOW` / `WAIT` / `FULL_STOP` |

### I/O 확장 (Phase D, 200ms 갱신)

| 센서 이름 | 타입 | 설명 |
|-----------|------|------|
| `di_val` | int | DI 전체 비트맵 (bit n = DIn 활성) |
| `di_0` … `di_7` | bool | 개별 DI (PD0~PD7) |
| `do_val` | int | 현재 DO 출력 비트맵 |
| `ai_val_0` … `ai_val_3` | int | ADC 원시값 (0~4095) |
| `ai_volt_0` … `ai_volt_3` | float | 전압 환산 (0.000~3.300 V) |

---

## 12. 완전한 예제 프로그램

### 예제 1 — 단순 직진 후 귀환

```json
{
  "program": "simple_patrol",
  "description": "10m 직진 후 복귀",
  "vars": { "speed": 0.3 },
  "commands": [
    { "cmd": "log",  "msg": "전진 시작" },
    { "cmd": "move", "linear": "$speed", "duration": 33.3 },
    { "cmd": "log",  "msg": "180도 회전" },
    { "cmd": "move", "linear": 0.0, "angular": 0.5, "duration": 6.28 },
    { "cmd": "move", "linear": "$speed", "duration": 33.3 },
    { "cmd": "stop" },
    { "cmd": "log",  "msg": "귀환 완료" }
  ]
}
```

### 예제 2 — 장애물 회피 열 추종

```json
{
  "program": "row_patrol_safe",
  "description": "장애물 감지 시 정지 대기 후 재진행",
  "vars": { "speed": 0.25, "safe_dist": 500 },
  "commands": [
    { "cmd": "label", "name": "check_obstacle" },
    { "cmd": "if",
      "cond": { "sensor": "obstacle_action", "op": "==", "val": "FULL_STOP" },
      "then": [
        { "cmd": "stop" },
        { "cmd": "log", "msg": "장애물 감지 — 대기" },
        { "cmd": "wait_until",
          "cond": { "sensor": "obstacle_action", "op": "==", "val": "CLEAR" },
          "timeout_ms": 30000,
          "on_timeout": "abort" }
      ]
    },
    { "cmd": "move", "linear": "$speed", "duration": 1.0 },
    { "cmd": "goto", "label": "check_obstacle" }
  ]
}
```

### 예제 3 — 엔코더 기반 정밀 이동

```json
{
  "program": "move_by_encoder",
  "description": "엔코더 카운트로 1m 이동",
  "vars": { "unit_scale": 100, "target_mm": 1000 },
  "commands": [
    { "cmd": "read_sensor", "sensor": "pos_left_hw", "var": "start_pos" },
    { "cmd": "calc", "var": "target_counts", "expr": "$target_mm * $unit_scale" },
    { "cmd": "calc", "var": "target_abs",    "expr": "$start_pos + $target_counts" },
    { "cmd": "move", "linear": 0.2,
      "until": { "sensor": "pos_left_hw", "op": ">=", "val": "$target_abs" },
      "timeout_ms": 15000 },
    { "cmd": "stop" },
    { "cmd": "log", "msg": "이동 완료" }
  ]
}
```

### 예제 4 — I/O 제어: 도어 센서 + 밸브 (Phase D)

```json
{
  "program": "spray_valve",
  "description": "DI0 도어 센서 확인 후 DO2 방제 밸브 개방, PWM0 펌프 속도 제어",
  "vars": { "pump_duty": 80.0, "spray_sec": 5.0 },
  "commands": [
    { "cmd": "log", "msg": "방제 준비 — 도어 센서 확인" },
    { "cmd": "wait_until",
      "cond": { "sensor": "di_0", "op": "==", "val": 1 },
      "timeout_ms": 10000,
      "on_timeout": "abort" },
    { "cmd": "log",    "msg": "밸브 개방 + 펌프 시작 ($pump_duty%)" },
    { "cmd": "do_set", "bit": 2, "val": 1 },
    { "cmd": "pwm_set","ch": 0,  "duty": "$pump_duty" },
    { "cmd": "wait",   "ms": "$spray_sec * 1000" },
    { "cmd": "pwm_set","ch": 0,  "duty": 0.0 },
    { "cmd": "do_set", "bit": 2, "val": 0 },
    { "cmd": "log", "msg": "방제 완료" }
  ]
}
```

### 예제 5 — AI 전압 모니터링 (Phase D)

```json
{
  "program": "battery_check",
  "description": "AI0 배터리 전압 확인 후 저전압 경고",
  "vars": { "low_volt": 2.5 },
  "commands": [
    { "cmd": "read_io", "sensor": "ai_volt_0", "var": "batt_v" },
    { "cmd": "log", "msg": "배터리 전압: $batt_v V" },
    { "cmd": "if",
      "cond": { "var": "batt_v", "op": "<", "val": "$low_volt" },
      "then": [
        { "cmd": "do_set", "bit": 7, "val": 1 },
        { "cmd": "log",    "msg": "⚠ 저전압 경고 — DO7 경고등 ON" },
        { "cmd": "abort",  "msg": "배터리 부족으로 작업 중단" }
      ]
    },
    { "cmd": "log", "msg": "배터리 정상" }
  ]
}
```

### 예제 7 — Phase E 종합: 초기화 + 워치독 + 정밀 이동 + 일시정지

```json
{
  "program": "phase_e_demo",
  "description": "드라이브 초기화 → 워치독 설정 → 500mm 정밀 이동 → 중간 일시정지",
  "vars": { "unit_scale": 100, "speed": 0.2 },
  "commands": [
    { "cmd": "set_watchdog", "ms": 120000, "_comment": "2분 안전 타임아웃" },

    { "cmd": "drive_control", "action": "fault_reset" },
    { "cmd": "drive_control", "action": "run_enable", "enable": true },
    { "cmd": "wait_until",
      "cond": { "sensor": "all_ready", "op": "==", "val": 1 },
      "timeout_ms": 5000,
      "on_timeout": "abort" },
    { "cmd": "log", "msg": "드라이브 준비 완료" },

    { "cmd": "move_to", "dist_mm": 500, "speed": "$speed" },
    { "cmd": "set", "var": "label", "fmt": "500mm 이동 완료 — 위치: $pos_left_hw" },
    { "cmd": "log", "msg": "$label" },

    { "cmd": "pause", "msg": "다음 구역 진입 전 확인 후 resume을 누르세요" },

    { "cmd": "move_to", "dist_mm": 500, "speed": "$speed" },
    { "cmd": "stop" },
    { "cmd": "set_watchdog", "ms": 0 },
    { "cmd": "drive_control", "action": "run_enable", "enable": false }
  ]
}
```

### 예제 8 — break/continue를 사용한 열 스캔

```json
{
  "program": "row_scan_with_break",
  "description": "10개 열 스캔 중 장애물 발견 시 break, 빈 열은 continue",
  "vars": { "total_rows": 10, "row": 0, "found_obstacle": 0 },
  "commands": [
    { "cmd": "repeat", "count": "$total_rows",
      "body": [
        { "cmd": "inc", "var": "row" },
        { "cmd": "read_sensor", "sensor": "dist_mm", "var": "d" },

        { "cmd": "if",
          "cond": { "var": "d", "op": "==", "val": -1 },
          "then": [
            { "cmd": "log", "msg": "열 $row: 깊이 데이터 없음 — 건너뜀" },
            { "cmd": "continue" }
          ]
        },

        { "cmd": "if",
          "cond": { "var": "d", "op": "<", "val": 300 },
          "then": [
            { "cmd": "set", "var": "found_obstacle", "val": 1 },
            { "cmd": "log", "msg": "열 $row: 장애물 감지 ($d mm) — 스캔 중단" },
            { "cmd": "break" }
          ]
        },

        { "cmd": "log", "msg": "열 $row: 정상 ($d mm)" },
        { "cmd": "move_to", "dist_mm": 200, "speed": 0.15 }
      ]
    },
    { "cmd": "stop" },
    { "cmd": "calc", "var": "result",
      "fmt": "스캔 완료 $row/$total_rows열 — 장애물: $found_obstacle" },
    { "cmd": "log", "msg": "$result" }
  ]
}
```

### 예제 6 — 서브루틴 분리

```json
{
  "program": "main_mission",
  "description": "서브루틴 호출 예시",
  "commands": [
    { "cmd": "call", "program": "battery_check" },
    { "cmd": "call", "program": "row_patrol_safe" },
    { "cmd": "call", "program": "spray_valve" },
    { "cmd": "stop" }
  ]
}
```

---

## 13. REST API

`bridge.py` (port 5100) 기준.

### 프로그램 관리

| 메서드 | 엔드포인트 | 설명 |
|--------|-----------|------|
| GET | `/program/list` | 저장된 프로그램 이름 목록 |
| GET | `/program/status` | 현재 실행 상태 (state, program, line, vars) |
| GET | `/program/get/{name}` | 프로그램 JSON 로드 |
| POST | `/program/save` | 프로그램 저장 (body: JSON 전체) |
| POST | `/program/run/{name}` | 프로그램 실행 시작 |
| POST | `/program/stop` | 실행 중인 프로그램 정지 |
| POST | `/program/pause` | 실행 중인 프로그램 일시정지 (Phase E) |
| POST | `/program/resume` | 일시정지된 프로그램 재개 (Phase E) |
| POST | `/program/delete/{name}` | 프로그램 삭제 |

### I/O 제어 (Phase D)

| 메서드 | 엔드포인트 | 설명 |
|--------|-----------|------|
| GET | `/io/status` | 최근 IO_STATUS 스냅샷 |
| POST | `/io/set` | DO/PWM 즉시 설정 |

`POST /io/set` 요청 형식:
```json
{
  "do_mask":  8,
  "do_val":   8,
  "pwm_ch":   0,
  "pwm_duty": 5000
}
```

### 카메라

| 메서드 | 엔드포인트 | 설명 |
|--------|-----------|------|
| GET | `/camera/status` | D435i 연결 상태 JSON |
| GET | `/camera/stream` | MJPEG 스트림 (multipart, ~15fps) |
| GET | `/camera/data` | depth + 가속도 JSON |

### 센서 / 기타

| 메서드 | 엔드포인트 | 설명 |
|--------|-----------|------|
| GET | `/sensor/data` | 인터프리터 센서 스토어 전체 스냅샷 |
| GET | `/ports` | 시리얼 포트 목록 |
| POST | `/claude/chat` | Claude AI 어시스턴트 SSE 스트리밍 |

---

## WebSocket 메시지 타입

`ws://host:8765` 기준.

| type | 방향 | 설명 |
|------|------|------|
| `agv_odometry` | STM32→Client | 엔코더 위치/속도 (10ms) |
| `agv_status` | STM32→Client | CiA402 드라이브 상태 (10ms) |
| `io_status` | STM32→Client | DI/DO/AI/PWM 상태 (200ms) |
| `status` | STM32→Client | 6축 상태 레거시 호환 |
| `param_report` | STM32→Client | 파라미터 보고 |
| `ack` | STM32→Client | 명령 ACK |
| `log` | STM32→Client | STM32 로그 문자열 |
| `program_status` | Bridge→Client | 인터프리터 실행 상태 |
| `connected` | Bridge→Client | 시리얼 연결됨 |
| `disconnected` | Bridge→Client | 시리얼 끊김 |
| `agv_velocity` | Client→STM32 | 속도 명령 `{"cmd":"agv_velocity","linear_mps":0.3,"angular_rps":0.0}` |

---

*이 문서는 `AGV_DESIGN.md`와 함께 유지 관리됩니다.*  
*명령어 추가 시 인터프리터(`json_interpreter.py`)와 이 문서를 함께 업데이트하세요.*
