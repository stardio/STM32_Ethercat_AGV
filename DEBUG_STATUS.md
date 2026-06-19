# AI 디버깅 시스템 구현 현황

**최종 업데이트**: 2026-06-15  
**설계 문서**: `DEBUG_DESIGN.md`  
**인터프리터**: `PC_GUI/bridge/json_interpreter.py`

---

## 전체 진행 현황

| Phase | 내용 | 상태 |
|-------|------|------|
| F-1 | 인터프리터 트레이스 캡처 | ✅ 완료 |
| F-2 | bridge.py HTTP 엔드포인트 | ✅ 완료 |
| F-3 | Claude 디버그 API | ✅ 완료 |
| F-4 | editor.html 디버그 UI | ✅ 완료 (2026-06-15) |
| F-5 | 브레이크포인트/스텝 실행 | 🔲 미구현 (선택) |

---

## Phase F-1 + F-2 완료 상세

### 추가된 코드

#### `json_interpreter.py`

**새 상수 (파일 상단):**
```python
_MAX_TRACE   = 1_000   # 트레이스 최대 항목 수

_TRACE_SENSORS = (     # 트레이스에 항상 포함할 핵심 센서 9개
    "dist_mm", "pos_left_hw", "pos_right_hw",
    "vel_left_hw", "vel_right_hw",
    "all_ready", "obstacle_action",
    "di_val", "do_val",
)
```

**`__init__`에 추가된 필드:**
```python
self._debug_mode:  bool       = False   # False = 빠른 경로 (오버헤드 없음)
self._trace:       list[dict] = []      # 명령별 실행 기록
self._trace_start: float      = 0.0    # 프로그램 시작 시각 (monotonic)
self._cmd_result:  tuple[str, str] = ("ok", "")  # 핸들러 → timeout 기록용
```

**신규 공개 메서드 3개:**

| 메서드 | 반환 타입 | 설명 |
|--------|----------|------|
| `enable_debug(on: bool)` | `None` | 디버그 모드 ON/OFF |
| `get_trace()` | `list[dict]` | 트레이스 항목 전체 |
| `get_trace_summary()` | `str` | Claude 프롬프트용 텍스트 요약 |

**`get_status()` 추가 필드:**
```python
"debug_mode":  self._debug_mode,   # 현재 디버그 모드 여부
"trace_count": len(self._trace),   # 현재 트레이스 항목 수
```

**수정된 메서드:**

`_execute()` — 실행 시작 시 트레이스 초기화:
```python
self._trace       = []
self._trace_start = time.monotonic()
```

`_dispatch()` — 디버그 모드일 때 try/finally 래핑:
```
debug_mode=False → 기존 동일 경로 (변경 없음)
debug_mode=True  → vars_before 저장 → 핸들러 실행 → finally에서 트레이스 항목 append
```

timeout 결과 기록 (`_cmd_result` 사용):
- `_cmd_move_to`: timeout 시 `self._cmd_result = ("timeout", "pos_left_hw=... / 목표=...")`
- `_cmd_wait_until`: timeout 시 `self._cmd_result = ("timeout", "조건 미충족: ...")`
- `_cmd_move` (until): timeout 시 `self._cmd_result = ("timeout", "until 조건 미충족: ...")`

`_cmd_abort()` — 메시지 변수 보간 추가:
```python
msg = self._interpolate(str(cmd.get("msg", "ABORT")))  # $var 치환됨
```

---

#### `bridge.py`

**신규 GET 엔드포인트:**
```python
GET /program/trace         → {"trace": [...]}          # 트레이스 JSON
GET /program/trace/summary → 텍스트 (text/plain)       # Claude용 요약
```

**신규 POST 엔드포인트:**
```python
POST /program/debug_run/{name}
  → enable_debug(True) 후 run(name)
  → 응답: {"ok": true, "program": "...", "debug": true}
```

**기존 POST 수정:**
```python
POST /program/run/{name}
  → enable_debug(False) 명시적으로 설정 (이전: 아무것도 안 함)
  → 응답에 "debug": false 추가
```

---

### 트레이스 항목 구조 (1개 항목)

```json
{
  "pc":    4,
  "depth": 0,
  "cmd":   "move_to",
  "args":  { "dist_mm": 500, "speed": 0.2 },

  "vars_before": {
    "speed": 0.2,
    "count": 1.0,
    "unit_scale": 100
  },

  "vars_delta": {
    "count": { "before": 0, "after": 1.0 }
  },

  "sensors": {
    "dist_mm":         -1,
    "pos_left_hw":     120,
    "pos_right_hw":    118,
    "vel_left_hw":     0,
    "vel_right_hw":    0,
    "all_ready":       true,
    "obstacle_action": "CLEAR",
    "di_val":          0,
    "do_val":          0
  },

  "result":      "timeout",
  "error_msg":   "pos_left_hw=120 / 목표=50000 (도달율: 0.2%)",
  "elapsed_ms":  15041,
  "duration_ms": 15002
}
```

**필드 설명:**

| 필드 | 설명 |
|------|------|
| `pc` | 명령의 블록 내 위치 (1부터 시작). 중첩 블록은 해당 블록 내 위치. |
| `depth` | call 깊이. 0=최상위, 1=서브루틴 1단계. while/if/repeat 내부도 0. |
| `cmd` | 명령 이름 |
| `args` | `cmd`와 `_comment` 제외한 나머지 인자 전체 |
| `vars_before` | 명령 실행 직전 전체 변수 스냅샷 |
| `vars_delta` | 실행 후 변경된 변수만 `{이름: {before, after}}` 형태 |
| `sensors` | 명령 실행 직후 9개 핵심 센서 스냅샷 |
| `result` | 실행 결과 (아래 표 참조) |
| `error_msg` | 오류/timeout 상세 메시지 |
| `elapsed_ms` | 프로그램 시작부터 이 명령 종료까지 누적 시간 |
| `duration_ms` | 이 명령만의 실행 시간 |

**`result` 값:**

| 값 | 발생 조건 |
|----|-----------|
| `ok` | 정상 완료 |
| `timeout` | move_to / wait_until / move-until 시간 초과 |
| `abort` | abort 명령 실행 |
| `error` | Python 예외 발생 |
| `break` | break 신호 (while/repeat에서 즉시 탈출) |
| `continue` | continue 신호 (루프 다음 반복) |
| `goto:레이블` | goto 점프 발생 |
| `return` | 서브루틴 복귀 |
| `cancelled` | asyncio 태스크 취소 |
| `unknown_cmd` | 등록되지 않은 명령어 |

---

### `get_trace_summary()` 출력 예시

```
=== 실행 트레이스: debug_test ===
종료: error  |  프로그램 명령: 5개  |  트레이스 항목: 11개 (중첩 포함)  |  실행 시간: 0.0s

 D  PC  CMD             결과          ms  변수 변화
 ─  ───  ──────────────  ──────────  ──────  ─────────────────────────────────
   0    1  log             ok               0  -
             └ msg='시작'
   0    2  set             ok               0  speed: 0.2→0.3
             └ var='speed'
   0    1  inc             ok               0  count: 0→1.0
   0    2  log             ok               0  -
   0    1  inc             ok               0  count: 1.0→2.0
   0    2  log             ok               0  -
   0    1  inc             ok               0  count: 2.0→3.0
   0    2  log             ok               0  -
   0    3  repeat          ok               0  count: 0→3.0
             └ count=3
❌ 0    1  abort           abort            0  -
             └─ 오류: count가 5가 아님 (실제: 3.0)
❌ 0    4  if              abort            0  -
             └─ 오류: count가 5가 아님 (실제: 3.0)

▼ 오류 발생 시점 변수 스냅샷:
  $speed = 0.3
  $count = 3.0
  $limit = 5

▼ 오류 발생 시점 센서 스냅샷:
  dist_mm = -1
  pos_left_hw = 0
  ...
```

**포맷 읽는 법:**
- `D` 열: call 깊이 (0=최상위, 1+=서브루틴 내부)
- `PC` 열: 해당 블록 내 위치 번호 (중첩 블록은 1부터 다시 시작)
- `❌` 표시: 비정상 종료 명령
- `└` 인자 힌트: 핵심 인자 1개 미리보기
- `└─ 오류`: 구체적인 오류 메시지

---

### 현재 사용 방법 (커맨드라인 테스트)

```bash
# bridge.py 실행 중 상태에서:

# 1. 디버그 모드로 실행
curl -X POST http://localhost:5100/program/debug_run/my_program

# 2. 트레이스 JSON 확인
curl http://localhost:5100/program/trace | python3 -m json.tool

# 3. 텍스트 요약 확인 (Claude 프롬프트에 넣을 내용)
curl http://localhost:5100/program/trace/summary

# 4. 실행 상태 확인 (debug_mode, trace_count 포함)
curl http://localhost:5100/program/status
```

---

---

## Phase F-3 완료 상세 (2026-06-15)

### 추가된 코드 — `bridge.py`

**새 상수:**
```python
_DEBUG_SYSTEM_PROMPT = "..."   # 디버그 전용 시스템 프롬프트
                                # (일반 chat과 완전 분리)
```

**응답 파싱 헬퍼 3개:**
```python
_extract_json_block(text)      # ```json 블록 → dict (없으면 None)
_extract_section(text, heading)  # ### 섹션 텍스트 추출 (json 블록 제외)
_extract_bullet_list(text, heading)  # bullet 항목 list[str]
```

**`_handle_claude_debug(handler)` 함수:**
- Anthropic API를 non-streaming으로 호출 (전체 응답을 받아 파싱)
- 프로그램 JSON + 트레이스 요약을 합쳐 단일 사용자 메시지 구성
- 트레이스가 없으면 즉시 오류 반환 (debug_run 먼저 실행 안내)

**`do_POST` 분기 추가:**
```python
if self.path == '/claude/debug':
    _handle_claude_debug(self)
    return
```

### 신규 HTTP 엔드포인트

**`POST /claude/debug`**

요청:
```json
{ "program_name": "move_test", "user_note": "선택적 메모" }
```

응답 (성공):
```json
{
  "ok":       true,
  "analysis": "move_to에서 timeout_ms가 200ms로 너무 짧아...",
  "fixed": {
    "program": "move_test",
    "vars": { "speed": 0.2, "unit_scale": 100 },
    "commands": [...]
  },
  "changes": [
    "timeout_ms: 200 → 30000 (30초로 연장)",
    "vars에 unit_scale: 100 명시적 추가"
  ],
  "raw": "Claude 전체 응답 텍스트"
}
```

응답 (오류):
```json
{ "ok": false, "error": "트레이스 없음 — debug_run 먼저 실행하세요" }
```

### Claude 시스템 프롬프트 전략

- `_CLAUDE_SYSTEM_PROMPT` (일반 chat): JSON 작성 도우미, 다양한 대화 맥락
- `_DEBUG_SYSTEM_PROMPT` (디버그 전용): 구조화된 출력 강제, 섹션 형식 명시
  - `### 원인` 섹션: 1~3줄 설명
  - `### 수정된 프로그램` 섹션: 완전한 JSON
  - `### 변경 사항` 섹션: bullet list

### 완성된 HTTP API 사용 흐름

```bash
# 1단계: 디버그 실행 (트레이스 수집)
curl -X POST http://localhost:5100/program/debug_run/move_test

# 2단계: 트레이스 확인 (선택)
curl http://localhost:5100/program/trace/summary

# 3단계: Claude 분석 요청 (핵심)
curl -X POST http://localhost:5100/claude/debug \
  -H 'Content-Type: application/json' \
  -d '{"program_name":"move_test","user_note":"500mm 이동 중 timeout 발생"}'

# 4단계: 수정된 프로그램 저장 (응답의 fixed 필드를 body로)
curl -X POST http://localhost:5100/program/save \
  -H 'Content-Type: application/json' \
  -d '{ ...fixed 필드 내용... }'

# 5단계: 재실행 및 반복
curl -X POST http://localhost:5100/program/debug_run/move_test
```

---

## Phase F-4 완료 상세 (2026-06-15)

### 추가된 코드 — `PC_GUI/bridge/editor.html`

**새 탭**: `#tab-debug` (기존 ▶실행 / 🤖Claude 탭에 🐛디버그 탭 추가)

**`#sp-debug` 패널 구성:**

| 영역 | 내용 |
|------|------|
| 버튼 행 | `[🐛 디버그 실행]` `[🤖 AI 분석]` `[🔄 AI 자동수정 (최대 3회)]` |
| 메모 입력 | AI에게 전달할 오류 메모 (선택, `#debug-user-note`) |
| 트레이스 헤더 | 항목 수 + 오류 수 |
| 트레이스 테이블 | D:PC / CMD / 결과(색상) / ms / Δ변수 5열, 오류 행은 빨간 배경 |
| AI 분석 결과 | 분석 텍스트 / 변경 사항 / 수정된 코드 + 에디터 적용 버튼 |

**새 JavaScript 함수:**

| 함수 | 설명 |
|------|------|
| `debugRun()` | 저장 → `POST /program/debug_run/{name}` → 완료 대기 → 트레이스 로드 |
| `_waitProgramDone(sec)` | `/program/status` 600ms 폴링, 완료 시 resolve |
| `_loadTrace()` | `GET /program/trace` → `_renderTrace()` 호출 |
| `_renderTrace(trace)` | 트레이스 테이블 HTML 생성 (결과별 색상, 오류 행 강조) |
| `aiDebug()` | `POST /claude/debug` → `_renderDebugResult()` |
| `_renderDebugResult(d)` | 분석/변경/수정 코드 렌더링, 적용 버튼 onclick 연결 |
| `_applyFixed(json)` | 수정된 JSON → 에디터 적용 후 실행 탭으로 전환 |
| `aiAutoFix(maxIter)` | debug_run → 완료 대기 → AI분석 → save → 반복 (최대 3회) |

**`switchTab()` 수정:** 3탭 핸들링 (`exec` / `claude` / `debug`)

### 완성된 디버그 워크플로

```
[🐛 디버그 실행] 클릭
  → debug_run (트레이스 수집)
  → 트레이스 테이블 표시 (오류 위치 빨간색 강조)

[🤖 AI 분석] 클릭 (트레이스 있을 때)
  → /claude/debug → 분석 / 변경사항 / 수정 코드 표시
  → [📋 에디터에 적용] 클릭 → 수정 반영

[🔄 AI 자동수정 (최대 3회)]
  → debug_run → AI분석 → save → debug_run → ... (완료 or 3회)
```

## 다음 단계: Phase F-5 (선택)

브레이크포인트 / 스텝 실행 — 아직 요청 없음.

---

*이 문서는 Phase F 구현 완료 시마다 업데이트됩니다.*
