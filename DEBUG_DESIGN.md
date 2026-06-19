# AGV JSON 프로그램 AI 디버깅 시스템 설계서

**버전**: 1.0  
**작성일**: 2026-06-15  
**관련 파일**: `PC_GUI/bridge/json_interpreter.py`, `PC_GUI/bridge/bridge.py`, `PC_GUI/bridge/editor.html`

---

## 1. 왜 AI 디버깅이 필요한가

현재 JSON 프로그램은 Claude AI가 자동 생성하거나 사용자가 직접 작성한다.  
프로그램이 오작동할 때 사용자는 오류 로그만 보고 원인을 찾아야 한다.

**문제 상황:**
- `move_to` 명령이 timeout으로 정지했을 때 — unit_scale 오설정인지, 엔코더 미응답인지 구분 불가
- `wait_until` 조건이 영원히 충족되지 않을 때 — 센서 이름 오타인지, 조건식 논리 오류인지 불명
- Claude가 생성한 코드의 변수명 불일치 — 직접 JSON 읽지 않으면 파악 불가

**목표:**  
프로그램을 생성한 AI가 실행 트레이스를 보고 스스로 원인을 찾아 수정안을 제시할 수 있도록 한다.

--- 

## 2. 전체 아키텍처

```
사용자 / Claude AI
      │
      │ JSON 프로그램 작성
      ▼
  editor.html
      │
      │ POST /program/debug_run/{name}
      ▼
  bridge.py  ──────────────────────────────────────────────────────────┐
      │                                                                 │
      │ debug_mode=True 로 실행                                         │
      ▼                                                                 │
  JsonInterpreter._execute()                                           │
      │                                                                 │
      │ 각 명령 실행 시마다 _dispatch()에서 트레이스 항목 기록           │
      │ { pc, cmd, args, vars_before, vars_delta,                       │
      │   sensors, result, error_msg, elapsed_ms, duration_ms }        │
      │                                                                 │
      ▼                                                                 │
  오류/비정상 종료 감지                                                  │
      │                                                                 │
      │ GET /program/trace/summary                                      │
      ▼                                                                 │
  텍스트 트레이스 요약 (Claude 프롬프트용)                              │
      │                                                                 │
      │ POST /claude/debug                                              │
      ▼                                                                 │
  Anthropic Claude API                                                  │
      │                                                                 │
      │ 응답: 원인 분석 + 수정된 JSON 프로그램                          │
      ▼                                                                 │
  editor.html                                                           │
      │                                                                 │
      │ 사용자: diff 확인 → [적용 후 재실행]                            │
      ▼                                                                 │
  자동 재실행 루프 (최대 3회) ──────────────────────────────────────────┘
```

---

## 3. 구현 단계 (Phase F)

### Phase F-1: 인터프리터 트레이스 캡처 ✅ 완료

인터프리터가 각 명령의 실행 결과를 자동으로 기록한다.

**핵심 원칙:**
- `debug_mode=False`(기본)일 때는 완전히 동일한 빠른 경로 — 오버헤드 없음
- `debug_mode=True`일 때만 트레이스 캡처 시작
- 트레이스는 최대 1,000항목, 초과 시 오래된 것 자동 삭제

**트레이스 항목 구조:**
```json
{
  "pc":          4,
  "depth":       0,
  "cmd":         "move_to",
  "args":        { "dist_mm": 500, "speed": 0.2 },
  "vars_before": { "count": 2.0, "speed": 0.2, "unit_scale": 100 },
  "vars_delta":  {},
  "sensors": {
    "dist_mm":        -1,
    "pos_left_hw":    120,
    "pos_right_hw":   118,
    "vel_left_hw":    0,
    "vel_right_hw":   0,
    "all_ready":      true,
    "obstacle_action":"CLEAR",
    "di_val":         0,
    "do_val":         0
  },
  "result":      "timeout",
  "error_msg":   "pos_left_hw=120 / 목표=50000 (도달율: 0.2%)",
  "elapsed_ms":  15041,
  "duration_ms": 15002
}
```

**`result` 가능한 값:**

| 값 | 의미 |
|----|------|
| `ok` | 정상 완료 |
| `timeout` | 시간 초과 (move_to, wait_until, move until) |
| `abort` | abort 명령으로 강제 종료 |
| `error` | 파이썬 예외 발생 |
| `break` | break 신호 (while/repeat 탈출) |
| `continue` | continue 신호 (루프 다음 반복) |
| `goto:레이블` | goto 점프 발생 |
| `return` | 서브루틴 복귀 |
| `cancelled` | asyncio 태스크 취소 |
| `unknown_cmd` | 미등록 명령어 |

---

### Phase F-2: bridge.py 디버그 HTTP 엔드포인트 ✅ 완료

| 메서드 | 엔드포인트 | 설명 |
|--------|-----------|------|
| `POST` | `/program/debug_run/{name}` | debug_mode=True 로 실행 (트레이스 수집) |
| `GET`  | `/program/trace` | 전체 트레이스 JSON 반환 |
| `GET`  | `/program/trace/summary` | Claude 프롬프트용 텍스트 요약 반환 |

**참고:** `POST /program/run/{name}` 은 기존과 동일 — debug_mode=False 유지

---

### Phase F-3: Claude 디버그 API 엔드포인트 🔲 미구현

bridge.py에 새 엔드포인트 추가:

```
POST /claude/debug
```

**요청 형식:**
```json
{
  "program_name": "move_test",
  "user_note":    "500mm 이동 중 timeout이 발생함"
}
```

**처리 흐름:**
1. `_get_interp().load_program(name)` → 원본 JSON 로드
2. `_get_interp().get_trace_summary()` → 트레이스 요약 텍스트 생성
3. Anthropic API 호출 (SSE 스트리밍)
4. 응답 중 수정된 JSON이 있으면 파싱해서 포함

**Claude 시스템 프롬프트 (디버그 전용):**
```
당신은 AGV JSON 프로그램 디버거입니다.
실행 트레이스를 분석하여 다음 형식으로 응답하세요:

1. 원인: (1~3줄)
2. 수정된 프로그램:
   ```json
   { ...완전한 JSON... }
   ```
3. 변경 사항:
   - 항목 1
   - 항목 2

JSON은 반드시 완전한 프로그램 전체를 출력하세요.
부분 수정 제안은 하지 마세요.
```

**응답 형식:**
```json
{
  "analysis":   "move_to에서 unit_scale이 설정되지 않아...",
  "fixed":      { ...수정된 program JSON... },
  "changes":    ["vars에 unit_scale:125 추가", "timeout_ms 5000→20000"],
  "confidence": 0.92,
  "iterations": 1
}
```

---

### Phase F-4: editor.html 디버그 UI 패널 🔲 미구현

기존 에디터에 디버그 패널 추가:

```
┌─────────────────────────────────────────────────────────────────────┐
│ 현재 프로그램: move_test                                              │
│ [▶ 실행]  [🐛 디버그 실행]  [🤖 AI 자동수정 (최대3회)]  [⏹ 정지]   │
└─────────────────────────────────────────────────────────────────────┘

[실행 트레이스] 탭
┌──────────────────────────────────────────────────────────────────────┐
│ D  PC  명령            결과        소요시간   변수 변화              │
│ ─  ──  ──────────────  ──────────  ────────   ──────────────────     │
│    1   log             ok               0ms   -                      │
│    2   set             ok               0ms   speed: None→0.3        │
│    3   move_to         timeout      15002ms   pos_left: 0→120        │ ← 빨간
│    4   abort           abort            0ms   오류: "timeout"        │ ← 빨간
└──────────────────────────────────────────────────────────────────────┘

[AI 분석] 탭  (디버그 실행 + 오류 후 자동 열림)
┌──────────────────────────────────────────────────────────────────────┐
│ 🤖 Claude 분석 결과:                                                 │
│                                                                      │
│ 원인: move_to 명령에서 unit_scale이 설정되지 않아                    │
│       기본값(100)이 사용되었으나 실제 하드웨어의 단위비는 125임.     │
│       목표 카운트가 62500이어야 하는데 50000으로 설정됨.             │
│                                                                      │
│ 변경 사항:                                                           │
│   • vars에 unit_scale: 125 추가                                      │
│   • move_to timeout_ms: 5000 → 20000으로 연장                       │
│                                                                      │
│ [diff 보기] [✅ 적용 후 재실행]                                      │
└──────────────────────────────────────────────────────────────────────┘
```

**자동 디버그 루프 흐름:**
```
[🤖 AI 자동수정] 클릭
      │
      ▼
debug_run → 오류 감지
      │
      ▼
/claude/debug 호출 → 수정안 수신
      │
      ▼
수정 프로그램 저장 → debug_run 재실행
      │
      ▼
성공 또는 최대 3회 초과 → 결과 표시
```

---

### Phase F-5: 브레이크포인트 + 스텝 실행 🔲 미구현 (선택)

기존 `pause`/`resume` 메커니즘을 재활용.

**브레이크포인트 설정 방법:**

```json
{ "cmd": "move_to", "dist_mm": 500, "_breakpoint": true }
```

프리프로세서가 `_breakpoint: true` 항목 앞에 `pause` 명령을 자동 삽입.

**스텝 실행 API:**

| 메서드 | 엔드포인트 | 설명 |
|--------|-----------|------|
| `POST` | `/program/step` | 명령 1개 실행 후 자동 pause |
| `GET`  | `/program/vars` | 현재 변수 스냅샷 (실시간) |

editor.html에서 줄 번호 클릭 → 브레이크포인트 토글.  
스텝 실행 중 변수 패널 실시간 갱신.

---

## 4. 데이터 흐름 상세

### 정상 실행 흐름

```
사용자 [▶ 실행]
  → POST /program/run/{name}
  → debug_mode = False
  → _execute() 시작 (트레이스 없음)
  → 완료
```

### 디버그 실행 흐름

```
사용자 [🐛 디버그 실행]
  → POST /program/debug_run/{name}
  → debug_mode = True
  → _execute() 시작
      → _dispatch() 호출마다:
          vars_before 스냅샷
          핸들러 실행
          예외 종류 분류 (ok/timeout/abort/error/...)
          vars_delta 계산 (변경된 변수만)
          sensors 스냅샷 (9개 핵심 센서)
          트레이스 항목 append
  → 완료 (정상 또는 오류)
  → 클라이언트: GET /program/trace/summary
  → 오류 있으면 AI 분석 패널 자동 열기
```

### AI 자동 수정 흐름

```
사용자 [🤖 AI 자동수정]
  ┌─ 루프 최대 3회 ─────────────────────────────────────────────────┐
  │                                                                  │
  │  debug_run → 트레이스 수집                                       │
  │     ↓                                                            │
  │  result == "done" → 성공, 루프 종료                              │
  │     ↓                                                            │
  │  POST /claude/debug                                              │
  │     ↓                                                            │
  │  수정된 JSON 수신 → save → debug_run                            │
  │                                                                  │
  └──────────────────────────────────────────────────────────────────┘
  최대 3회 초과 또는 성공 → 최종 결과 표시
```

---

## 5. 트레이스 요약 텍스트 예시

`GET /program/trace/summary` 가 반환하는 텍스트 (Claude 프롬프트에 삽입).

```
=== 실행 트레이스: move_test ===
종료: error  |  프로그램 명령: 6개  |  트레이스 항목: 6개  |  실행 시간: 15.1s

 D  PC  CMD             결과          ms  변수 변화
 ─  ───  ──────────────  ──────────  ──────  ─────────────────────────────────
   0    1  set_watchdog    ok               0  -
   0    2  drive_control   ok             512  -
   0    3  wait_until      ok            2341  -
   0    4  set             ok               0  count: None→1.0
   0    5  move_to         timeout      15002  -
             └ dist_mm=500
             └─ 오류: pos_left_hw=120 / 목표=50000 (도달율: 0.2%)
❌ 0    6  abort           abort            0  -
             └ msg='엔코더 미응답'
             └─ 오류: 엔코더 미응답

▼ 오류 발생 시점 변수 스냅샷:
  $speed = 0.3
  $count = 1.0
  $unit_scale = 100

▼ 오류 발생 시점 센서 스냅샷:
  dist_mm = -1
  pos_left_hw = 120
  pos_right_hw = 118
  vel_left_hw = 0
  vel_right_hw = 0
  all_ready = True
  obstacle_action = 'CLEAR'
  di_val = 0
  do_val = 0
```

---

## 6. 구현 우선순위 및 일정

| 단계 | 내용 | 상태 | 난이도 |
|------|------|------|--------|
| F-1 | 인터프리터 트레이스 캡처 | ✅ 완료 | 중 |
| F-2 | bridge.py HTTP 엔드포인트 | ✅ 완료 | 하 |
| F-3 | Claude 디버그 API `/claude/debug` | 🔲 대기 | 중 |
| F-4 | editor.html 디버그 UI | 🔲 대기 | 상 |
| F-5 | 브레이크포인트/스텝 실행 | 🔲 선택 | 중 |

**F-3이 핵심 의존성**: F-4는 F-3 완료 후 구현 가능.  
F-5는 독립적이며 필요 시 언제든 추가 가능.

---

## 7. 기술적 결정 사항

### debug_mode 기본값이 False인 이유

디버그 모드는 각 명령마다 `dict()` 복사(vars_before, vars_delta 계산),  
sensors 9개 읽기, list append 등 작업을 추가한다.  
1ms 주기 EtherCAT 루프와 분리된 PC 측 asyncio 작업이므로  
영향은 없지만, 불필요한 오버헤드를 피하기 위해 기본값을 False로 설정.

### 트레이스 최대 1,000항목 이유

`while` 루프 100회 × 명령 5개 = 500항목.  
충분한 여유를 두어 1,000으로 설정.  
초과 시 오래된 항목 자동 삭제(FIFO).

### timeout을 예외가 아닌 `_cmd_result`로 처리한 이유

`move_to`/`wait_until`의 timeout은 프로그램 흐름상 정상적인 경로일 수 있다  
(`on_timeout: "continue"` 설정 시). 예외로 처리하면 상위 try/catch에 의해  
프로그램이 강제 종료된다. 대신 `_cmd_result` 슬롯에 결과를 기록하고  
`_dispatch`의 finally에서 트레이스에 포함.

### 센서 스냅샷을 모든 명령에 찍는 이유

오류 발생 명령이 어느 것인지 사전에 알 수 없다.  
모든 명령에 센서 스냅샷을 포함해야 어떤 명령에서 문제가 생겨도  
그 시점의 하드웨어 상태를 확인할 수 있다.  
9개 센서 × 1,000항목 = 메모리 영향 미미.

---

*이 문서는 `DEBUG_STATUS.md` 와 함께 유지 관리됩니다.*
