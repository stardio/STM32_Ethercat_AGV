# 서보 프레스 상업용 기능 구현 가이드

기준일: 2026-05-12  
기반 문서: `PROJECT_FEATURE_OVERVIEW.md`, `ADVANCED_SERVO_PRESS_MASTER_SPEC.md`  
목적: 현재 EtherCAT 단축 모션 기반 위에 상업용 서보 프레스 기능을 순차적으로 추가하기 위한 실무 구현 가이드

---

## 목차

1. [현재 상태와 목표 갭 분석](#1-현재-상태와-목표-갭-분석)
2. [Phase 1 — 공정 상태기계 + 안전 인터록](#2-phase-1--공정-상태기계--안전-인터록)
3. [Phase 2 — 레시피 관리 + 품질 판정](#3-phase-2--레시피-관리--품질-판정)
4. [Phase 3 — 사이클 이력 + 알람 체계](#4-phase-3--사이클-이력--알람-체계)
5. [Phase 4 — 운영 UI + 유지보수](#5-phase-4--운영-ui--유지보수)
6. [LCD UI 화면 구성 상세](#6-lcd-ui-화면-구성-상세)
7. [Web UI 탭 구성 상세](#7-web-ui-탭-구성-상세)
8. [CMD 명령 인터페이스 확장](#8-cmd-명령-인터페이스-확장)
9. [데이터 구조 정의](#9-데이터-구조-정의)
10. [NVM 레이아웃 설계](#10-nvm-레이아웃-설계)
11. [구현 체크리스트](#11-구현-체크리스트)

---

## 1. 현재 상태와 목표 갭 분석

### 1.1 현재 구현 완료 항목

| 영역 | 상태 | 비고 |
|---|---|---|
| EtherCAT 단축 모션 제어 | 완료 | 위치/속도/토크 명령 |
| Manual 동작 | 완료 | ABS/INC, Jog FWD/REV |
| Parameter 관리 | 완료 | 저장/로드, Write/Read All |
| Program 3단계 시퀀스 | 완료 | 고정 3단계 |
| Home/ORG 동작 | 완료 | home_hw 분리 관리 |
| Web UI (UART 브리지) | 완료 | 5탭 구조 |
| TouchGFX LCD UI | 완료 | 기본 탭 구조 |

### 1.2 상업용 대비 미구현 항목 (우선순위 순)

| 항목 | 우선순위 | Phase |
|---|---|---|
| 프레스 공정 상태기계 | **최고** | 1 |
| 안전 인터록 (E-Stop, Door, Two-Hand) | **최고** | 1 |
| 운전 모드 관리 (MANUAL/SETUP/AUTO/ALARM) | 높음 | 1 |
| 레시피 구조 (다제품 관리) | 높음 | 2 |
| 품질 판정 (OK/NG + 판정 코드) | 높음 | 2 |
| 생산 카운터 (Total/OK/NG) | 높음 | 2 |
| Dashboard LCD 화면 | 높음 | 2 |
| 사이클 이력 로그 | 중간 | 3 |
| 알람 코드 + ACK/Reset 체계 | 중간 | 3 |
| CSV 내보내기 | 중간 | 3 |
| 힘-변위 파형 저장 | 낮음 | 3 |
| 유지보수/진단 화면 | 낮음 | 4 |
| 권한 레벨 (Operator/Engineer/Admin) | 낮음 | 4 |

---

## 2. Phase 1 — 공정 상태기계 + 안전 인터록

### 2.1 프레스 공정 상태기계

#### 상태 정의

```c
// Appli/Core/Inc/press_state_machine.h

typedef enum {
    PRESS_STATE_IDLE          = 0,
    PRESS_STATE_APPROACH      = 1,  // 고속 접근
    PRESS_STATE_CONTACT       = 2,  // 저속 접촉 탐색
    PRESS_STATE_PRESS         = 3,  // 본 가압
    PRESS_STATE_DWELL         = 4,  // 힘 유지
    PRESS_STATE_RETURN        = 5,  // 복귀
    PRESS_STATE_CYCLE_END     = 6,  // 정상 완료
    PRESS_STATE_CYCLE_NG      = 7,  // 불량 완료
    PRESS_STATE_ABORT         = 8,  // 강제 중단
} PressState_t;
```

#### 상태 전이 흐름

```
IDLE
 │  cycle_start=1 + 인터록 OK + 홈 완료
 ▼
APPROACH  ────────────── 속도: approach_speed (고속, 예 200mm/s)
 │                       힘 제한: 낮게 설정
 │  접촉 감지 (토크 > contact_torque_th) 또는 approach_pos 도달
 ▼
CONTACT   ────────────── 속도: contact_speed (저속, 예 10mm/s)
 │                       토크 임계값 모니터링
 │  토크 > press_start_force 또는 press_target_pos 진입
 ▼
PRESS     ────────────── 속도: press_speed
 │                       힘 상한: press_max_force
 │  press_target_pos 도달 or press_max_force 도달
 ▼
DWELL     ────────────── 정지, 힘 유지
 │  dwell_time_ms 경과
 ▼
RETURN    ────────────── 속도: return_speed
 │  return_pos 도달
 ▼
CYCLE_END (품질 판정 수행)
 │
 ├─ 판정 OK → 카운터 OK++, 결과 로그 → IDLE
 └─ 판정 NG → 카운터 NG++, NG 코드 기록 → CYCLE_NG → IDLE

※ 어느 단계에서든:
   타임아웃 / 인터록 이탈 / cycle_abort=1 → ABORT → IDLE
```

#### 단계별 파라미터 (레시피 1개 기준)

| 단계 | 파라미터 | 단위 | 설명 |
|---|---|---|---|
| APPROACH | approach_speed | mm/s | 빠른 접근 속도 |
| APPROACH | approach_pos | mm | 저속 전환 기준 위치 |
| CONTACT | contact_speed | mm/s | 접촉 탐색 저속 |
| CONTACT | contact_torque_th | % | 접촉 판정 토크 임계값 |
| PRESS | press_speed | mm/s | 가압 속도 |
| PRESS | press_target_pos | mm | 목표 압입 위치 |
| PRESS | press_max_force | % | 힘 상한 (초과 시 NG) |
| DWELL | dwell_time_ms | ms | 힘 유지 시간 |
| RETURN | return_speed | mm/s | 복귀 속도 |
| RETURN | return_pos | mm | 복귀 대기 위치 |
| 공통 | cycle_timeout_ms | ms | 전체 사이클 타임아웃 |

#### 구현 파일

- **신규 생성:** `Appli/Core/Inc/press_state_machine.h`
- **신규 생성:** `Appli/Core/Src/press_state_machine.c`
- **수정:** `Appli/Core/Src/main.c` — FreeRTOS Task에 상태기계 tick 추가
- **수정:** `Appli/Core/Src/soem_port.c` — 상태기계로부터 모션 명령 받기

#### press_state_machine.c 핵심 구조

```c
// 상태기계 메인 tick 함수 (FreeRTOS 태스크에서 주기 호출)
void PressStateMachine_Tick(void) {
    switch (g_press_state) {
        case PRESS_STATE_IDLE:
            // cycle_start 명령 + 인터록 확인 → APPROACH 전이
            break;
        case PRESS_STATE_APPROACH:
            // 고속 이동 명령, approach_pos 또는 토크 조건 → CONTACT 전이
            break;
        case PRESS_STATE_CONTACT:
            // 저속 이동, 토크 임계 초과 → PRESS 전이
            break;
        case PRESS_STATE_PRESS:
            // 가압, 목표 위치 또는 힘 도달 → DWELL 전이
            break;
        case PRESS_STATE_DWELL:
            // 정지 유지, 시간 경과 → RETURN 전이
            break;
        case PRESS_STATE_RETURN:
            // 복귀 이동, 완료 → CYCLE_END 전이
            break;
        case PRESS_STATE_CYCLE_END:
            // 품질 판정 → IDLE 전이
            break;
        case PRESS_STATE_ABORT:
            // 즉시 정지, 원인 기록 → IDLE 전이
            break;
    }
    // 공통: 타임아웃 감시
    PressStateMachine_CheckTimeout();
}
```

---

### 2.2 안전 인터록

#### 인터록 신호 정의

```c
// Appli/Core/Inc/interlock_manager.h

typedef struct {
    uint8_t estop_ok;       // E-Stop 해제 = 1
    uint8_t door_ok;        // 도어 닫힘 = 1
    uint8_t twohand_left;   // 왼손 버튼 = 1
    uint8_t twohand_right;  // 오른손 버튼 = 1
    uint8_t drive_ready;    // 드라이브 Ready = 1
    uint8_t home_complete;  // 홈 완료 = 1
    uint8_t air_pressure;   // 공압 OK = 1 (선택)
} InterlockState_t;

typedef enum {
    SAFE_STOP  = 0,  // 정지 (인터록 불만족)
    SAFE_READY = 1,  // 운전 가능
    SAFE_FAULT = 2,  // 알람 (복구 필요)
} SafetyState_t;
```

#### 인터록 우선순위 및 정지 등급

| 신호 | 정지 등급 | 동작 |
|---|---|---|
| E-Stop | Cat.0 즉시 정지 | 모션 즉시 해제, STO 신호 |
| Door (운전 중) | Cat.1 감속 정지 | RETURN 단계로 전환 후 정지 |
| Two-Hand 해제 | Cat.1 감속 정지 | RETURN 단계로 전환 |
| Drive Fault | Cat.0 즉시 정지 | 알람 발생 |

#### AUTO 모드 진입 조건 (모두 만족 필요)

```
estop_ok == 1
door_ok == 1
drive_ready == 1
home_complete == 1
현재 알람 없음
```

#### Cycle Start 추가 조건 (AUTO 모드에서)

```
twohand_left == 1 AND twohand_right == 1  (0.5초 이내 동시)
현재 상태 == PRESS_STATE_IDLE
```

#### 구현 파일

- **신규 생성:** `Appli/Core/Inc/interlock_manager.h`
- **신규 생성:** `Appli/Core/Src/interlock_manager.c`
- **수정:** `Appli/Core/Inc/main.h` — GPIO 핀 매핑 추가
- **수정:** `Appli/Core/Src/main.c` — 인터록 매니저 초기화 + 주기 갱신

#### GPIO 핀 매핑 (실제 HW 확인 후 수정)

```c
// 예시 — STM32H7S78-DK 실제 핀으로 교체 필요
#define ESTOP_PIN           GPIO_PIN_0
#define ESTOP_PORT          GPIOA
#define DOOR_PIN            GPIO_PIN_1
#define DOOR_PORT           GPIOA
#define TWOHAND_L_PIN       GPIO_PIN_2
#define TWOHAND_L_PORT      GPIOA
#define TWOHAND_R_PIN       GPIO_PIN_3
#define TWOHAND_R_PORT      GPIOA
```

---

### 2.3 운전 모드 관리

```c
typedef enum {
    OP_MODE_MANUAL   = 0,  // 수동 조작 (현재 Manual 탭)
    OP_MODE_SETUP    = 1,  // 셋업 (저속 Jog만 허용)
    OP_MODE_AUTO     = 2,  // 자동 사이클
    OP_MODE_ALARM    = 3,  // 알람 (제한 동작만)
    OP_MODE_RECOVERY = 4,  // 복구 모드
} OperationMode_t;
```

모드 전환 규칙:

| 전환 | 조건 |
|---|---|
| → AUTO | 모든 인터록 OK + 홈 완료 + 알람 없음 |
| → MANUAL | 언제나 가능 (AUTO 중에는 IDLE 상태일 때) |
| → ALARM | 알람 발생 시 자동 전환 |
| ALARM → MANUAL | 원인 제거 + 사용자 ACK + Reset |

---

### 2.4 Phase 1 완료 기준 (DoD)

- [ ] E-Stop 입력 시 모션 즉시 정지 확인
- [ ] Door 열림 시 운전 중 RETURN 전환 후 정지 확인
- [ ] AUTO 모드에서 인터록 불만족 시 Cycle Start 거부 확인
- [ ] 정상 사이클 (IDLE→APPROACH→CONTACT→PRESS→DWELL→RETURN→END) 10회 연속 성공
- [ ] 타임아웃 발생 시 ABORT 전환 확인
- [ ] LCD Dashboard에 모드/인터록 상태 표시 확인

---

## 3. Phase 2 — 레시피 관리 + 품질 판정

### 3.1 레시피 데이터 구조

```c
// Appli/Core/Inc/recipe_manager.h

#define RECIPE_MAX_COUNT     10
#define RECIPE_NAME_LEN      32
#define RECIPE_MAGIC         0xA55A

typedef struct {
    uint16_t magic;                    // 유효성 검증
    uint16_t recipe_id;
    char     product_name[RECIPE_NAME_LEN];
    uint8_t  version;
    uint8_t  locked;                   // 1=Engineer만 수정 가능

    // 공정 파라미터
    float    approach_speed;           // mm/s
    float    approach_pos;             // mm
    float    contact_speed;            // mm/s
    float    contact_torque_th;        // %
    float    press_speed;              // mm/s
    float    press_target_pos;         // mm
    float    press_max_force;          // %
    float    dwell_time_ms;            // ms
    float    return_speed;             // mm/s
    float    return_pos;               // mm
    uint32_t cycle_timeout_ms;         // ms

    // 품질 판정 기준
    float    judge_force_max;          // % 상한
    float    judge_force_min;          // % 하한
    float    judge_pos_max;            // mm 상한
    float    judge_pos_min;            // mm 하한
    uint32_t judge_cycle_time_max_ms;  // ms 상한

    uint32_t saved_timestamp;
    uint16_t crc;                      // CRC16 검증
} RecipeData_t;

typedef struct {
    RecipeData_t recipes[RECIPE_MAX_COUNT];
    uint8_t      active_recipe_idx;
    uint16_t     total_count;
    uint16_t     crc;
} RecipeStore_t;
```

#### 구현 파일

- **신규 생성:** `Appli/Core/Inc/recipe_manager.h`
- **신규 생성:** `Appli/Core/Src/recipe_manager.c`
- **수정:** NVM 레이아웃 (섹션 10 참조)

#### recipe_manager.c 주요 함수

```c
HAL_StatusTypeDef Recipe_Init(void);
HAL_StatusTypeDef Recipe_Load(uint8_t idx, RecipeData_t *out);
HAL_StatusTypeDef Recipe_Save(uint8_t idx, const RecipeData_t *in);
HAL_StatusTypeDef Recipe_Activate(uint8_t idx);
HAL_StatusTypeDef Recipe_Delete(uint8_t idx);
HAL_StatusTypeDef Recipe_Clone(uint8_t src_idx, uint8_t dst_idx);
RecipeData_t*     Recipe_GetActive(void);
uint8_t           Recipe_GetActiveIdx(void);
```

---

### 3.2 품질 판정 엔진

#### 판정 결과 코드

```c
// Appli/Core/Inc/quality_judge.h

typedef enum {
    JUDGE_OK              = 0,
    JUDGE_NG_FORCE_HIGH   = 1,  // 힘 초과 (과압입, 간섭)
    JUDGE_NG_FORCE_LOW    = 2,  // 힘 부족 (부품 없음, 체결 불량)
    JUDGE_NG_POS_HIGH     = 3,  // 위치 초과 (과압입)
    JUDGE_NG_POS_LOW      = 4,  // 위치 부족 (압입 부족)
    JUDGE_NG_TIME_OVER    = 5,  // 사이클 타임아웃
    JUDGE_NG_INTERLOCK    = 6,  // 운전 중 인터록 이탈
    JUDGE_NG_ABORT        = 7,  // 강제 중단
} JudgeResult_t;
```

#### 판정 로직 (최소 구현 — 4개 항목)

```c
// Appli/Core/Src/quality_judge.c

JudgeResult_t QualityJudge_Evaluate(
    float peak_force_pct,      // PRESS 단계 최대 토크 %
    float end_position_mm,     // DWELL 완료 시 위치
    uint32_t cycle_time_ms,    // 총 사이클 시간
    const RecipeData_t *recipe
) {
    if (peak_force_pct > recipe->judge_force_max)
        return JUDGE_NG_FORCE_HIGH;
    if (peak_force_pct < recipe->judge_force_min)
        return JUDGE_NG_FORCE_LOW;
    if (end_position_mm > recipe->judge_pos_max)
        return JUDGE_NG_POS_HIGH;
    if (end_position_mm < recipe->judge_pos_min)
        return JUDGE_NG_POS_LOW;
    if (cycle_time_ms > recipe->judge_cycle_time_max_ms)
        return JUDGE_NG_TIME_OVER;
    return JUDGE_OK;
}
```

#### 힘-변위 윈도우 판정 (고도화 — Phase 3+)

```
각 변위 포인트마다 상한/하한 힘 값을 레시피에 저장
→ 실측 곡선이 윈도우를 벗어나면 NG
구현 시 레시피에 force_window[N][3] 배열 추가 (pos, min, max)
```

---

### 3.3 생산 카운터

```c
typedef struct {
    uint32_t total;
    uint32_t ok;
    uint32_t ng;
    uint32_t ng_force_high;
    uint32_t ng_force_low;
    uint32_t ng_pos_high;
    uint32_t ng_pos_low;
    uint32_t ng_time_over;
    uint32_t ng_interlock;
    uint32_t consecutive_ng;   // 연속 NG 카운트
    float    ng_rate_pct;      // NG / total * 100
} ProductionCounter_t;
```

- 카운터는 NVM에 주기 저장 (전원 차단 대비)
- 연속 NG가 설정값 초과 시 알람 발생

---

### 3.4 Phase 2 완료 기준 (DoD)

- [ ] 레시피 10개 저장/로드/활성화 동작 확인
- [ ] 레시피 잠금 시 파라미터 수정 불가 확인
- [ ] 판정 4항목 경계값 테스트 (상한 초과/하한 미달 각각)
- [ ] NG 코드가 사이클 결과와 함께 Web UI 로그에 표시 확인
- [ ] 카운터(Total/OK/NG/NG Rate) 정확성 확인

---

## 4. Phase 3 — 사이클 이력 + 알람 체계

### 4.1 사이클 이력 (cycle_logger)

#### 1 사이클 레코드 구조

```c
// Appli/Core/Inc/cycle_logger.h

#define CYCLE_LOG_MAX  100  // 최근 100사이클 (링 버퍼)

typedef struct {
    uint32_t      timestamp;       // Unix time 또는 부팅 후 ms
    uint16_t      cycle_number;
    uint8_t       recipe_id;
    uint8_t       recipe_version;
    uint8_t       mode;            // OperationMode_t
    JudgeResult_t result;
    float         peak_force_pct;
    float         end_position_mm;
    uint32_t      cycle_time_ms;
    uint16_t      alarm_id;        // 0이면 알람 없음
} CycleRecord_t;
```

#### 구현 파일

- **신규 생성:** `Appli/Core/Inc/cycle_logger.h`
- **신규 생성:** `Appli/Core/Src/cycle_logger.c`

#### 주요 함수

```c
void             CycleLogger_Init(void);
void             CycleLogger_Record(const CycleRecord_t *rec);
CycleRecord_t*   CycleLogger_GetLast(uint8_t offset);  // offset=0: 최신
uint16_t         CycleLogger_GetCount(void);
void             CycleLogger_Clear(void);
// Web UI 전송용
void             CycleLogger_SerializeCSV(char *buf, uint16_t max_len);
```

---

### 4.2 알람 코드 체계

```c
// Appli/Core/Inc/alarm_manager.h

typedef enum {
    ALARM_NONE              = 0,
    // 안전 인터록 (100번대)
    ALARM_ESTOP             = 101,
    ALARM_DOOR_OPEN         = 102,
    ALARM_TWOHAND_RELEASED  = 103,
    // 드라이브 (200번대)
    ALARM_DRIVE_FAULT       = 201,
    ALARM_DRIVE_NOT_READY   = 202,
    // 공정 이상 (300번대)
    ALARM_CYCLE_TIMEOUT     = 301,
    ALARM_OVERLOAD          = 302,
    ALARM_POS_ERROR         = 303,
    // 품질/생산 (400번대)
    ALARM_CONSECUTIVE_NG    = 401,  // 연속 NG N회
    // 시스템 (500번대)
    ALARM_NVM_ERROR         = 501,
    ALARM_COMM_ERROR        = 502,
} AlarmCode_t;

typedef struct {
    AlarmCode_t code;
    uint32_t    occurred_at;
    uint32_t    cleared_at;   // 0이면 미해제
    uint8_t     ack_done;
    char        message[48];
} AlarmRecord_t;

#define ALARM_LOG_MAX  50
```

#### 알람 해제 절차

```
1. 원인 신호 해제 (예: E-Stop 버튼 복구)
2. 사용자 알람 ACK (CMD,alarm_ack=1)
3. 알람 Reset (CMD,alarm_reset=1)
4. ALARM 모드 → MANUAL 모드 복귀
```

#### 구현 파일

- **신규 생성:** `Appli/Core/Inc/alarm_manager.h`
- **신규 생성:** `Appli/Core/Src/alarm_manager.c`

---

### 4.3 파형 저장 정책

| 정책 | 저장 대상 | Flash 부담 | 추천 |
|---|---|---|---|
| 전체 저장 | 최근 N사이클 전체 파형 | 높음 | N=5 |
| 이벤트 저장 | NG 사이클만 파형 저장 | 중간 | **초기 추천** |
| 미저장 | 피크값/최종값만 | 낮음 | Phase 2 기본 |

초기 구현은 피크값 + 최종값만 저장하고, Flash 여유 확인 후 파형 저장 추가.

---

### 4.4 CSV 내보내기

Web UI에서 사이클 이력을 CSV로 다운로드:

```
GET /api/history/csv?from=0&count=100
```

응답 형식:
```
cycle_no,timestamp,recipe_id,recipe_ver,mode,result,peak_force_pct,end_pos_mm,cycle_time_ms,alarm_id
1,1747000000,1,3,AUTO,OK,45.2,125.3,2800,0
2,1747000100,1,3,AUTO,NG_FORCE_HIGH,61.0,125.1,2750,0
```

---

### 4.5 Phase 3 완료 기준 (DoD)

- [ ] 사이클 결과 100개 링 버퍼 저장/조회 확인
- [ ] 알람 발생 → ACK → Reset → MANUAL 복귀 시나리오 확인
- [ ] Web UI에서 CSV 다운로드 파일 열기 확인
- [ ] E-Stop 알람 이력 기록 확인 (발생/해제 시각)
- [ ] 연속 NG N회 알람 발생 확인

---

## 5. Phase 4 — 운영 UI + 유지보수

### 5.1 유지보수 화면

| 항목 | 내용 |
|---|---|
| I/O 모니터 | 인터록 GPIO 실시간 ON/OFF 표시 |
| EtherCAT 진단 | 슬레이브 상태, 통신 오류 카운터 |
| 모션 추종 오차 | 목표-실제 위치 편차 실시간 표시 |
| 사이클 카운터 | 총 사이클, 전원 투입 후 카운터 |
| 예방보전 알림 | 사이클 수 기반 점검 알림 (예: 10만 사이클마다) |

### 5.2 권한 레벨

| 레벨 | 허용 동작 |
|---|---|
| Operator | 운전 시작/정지, 알람 ACK/Reset, 레시피 선택(조회), 카운터 조회 |
| Engineer | 레시피 생성/수정/잠금, 파라미터 수정, 판정 기준 설정 |
| Admin | 권한 관리, NVM 초기화, 시스템 설정, 감사 로그 조회 |

### 5.3 캘리브레이션 마법사 (부팅 시 or 수동 실행)

```
1단계: 홈 캘리브레이션 — ORG Reset 유도, 확인
2단계: 하중 캘리브레이션 — 알려진 하중으로 토크% ↔ 힘[N] 매핑
3단계: 소프트 리밋 확인 — +/- 리밋 이동, 위치 확인
```

### 5.4 Phase 4 완료 기준 (DoD)

- [ ] I/O 모니터에서 GPIO 실시간 반영 확인
- [ ] Engineer 잠금 레시피를 Operator가 수정 불가 확인
- [ ] 예방보전 알림 N회 도달 시 표시 확인

---

## 6. LCD UI 화면 구성 상세

TouchGFX Designer에서 추가할 화면 목록 및 각 화면의 위젯 구성.

### 6.1 화면 목록 (8개 화면)

| 화면 번호 | 이름 | 역할 |
|---|---|---|
| Screen_Dashboard | Dashboard | 전체 상태 요약, 운전자 기본 화면 |
| Screen_AutoRun | Auto Run | 사이클 제어, 공정 단계 표시 |
| Screen_Manual | Manual Setup | 수동 조작 (기존 Manual 탭 대체) |
| Screen_Recipe | Recipe | 레시피 선택/조회/편집 |
| Screen_Quality | Quality | 판정 결과, OK/NG 트렌드 |
| Screen_Alarm | Alarm | 알람 목록, ACK/Reset |
| Screen_Maintenance | Maintenance | I/O 모니터, 진단 |
| Screen_System | System | 파라미터, 권한, NVM |

---

### 6.2 Dashboard 화면 상세

```
┌────────────────────────────────────────────┐
│  MODE: [AUTO]        STEP: [IDLE]          │
│ ─────────────────────────────────────────  │
│ 인터록                                     │
│  ● E-Stop   ● Door   ● Two-Hand            │
│  ● Drive    ● Home                         │
│ ─────────────────────────────────────────  │
│ 생산 현황                                  │
│  Total: 1240   OK: 1235   NG: 5            │
│  NG Rate: 0.40%    연속NG: 0              │
│ ─────────────────────────────────────────  │
│ 마지막 사이클                              │
│  결과: [OK]   Peak Force: 45.2 %          │
│  위치: 125.3 mm    사이클: 2.8 s          │
│ ─────────────────────────────────────────  │
│   [CYCLE START]           [CYCLE STOP]     │
│ ─────────────────────────────────────────  │
│ 활성 알람: 없음                            │
└────────────────────────────────────────────┘
```

#### 필요 위젯 목록

| 위젯 이름 | 타입 | 표시 내용 |
|---|---|---|
| txt_mode | TextArea | 현재 운전 모드 |
| txt_step | TextArea | 현재 공정 단계 |
| ind_estop | Image/Box | E-Stop 인터록 (녹색/적색) |
| ind_door | Image/Box | Door 인터록 |
| ind_twohand | Image/Box | Two-Hand 인터록 |
| ind_drive | Image/Box | Drive Ready |
| ind_home | Image/Box | Home Complete |
| txt_total | TextArea | Total 카운터 |
| txt_ok | TextArea | OK 카운터 |
| txt_ng | TextArea | NG 카운터 |
| txt_ng_rate | TextArea | NG Rate % |
| txt_consec_ng | TextArea | 연속 NG |
| txt_last_result | TextArea | 마지막 판정 결과 |
| txt_peak_force | TextArea | 마지막 피크 힘 % |
| txt_end_pos | TextArea | 마지막 최종 위치 |
| txt_cycle_time | TextArea | 마지막 사이클 시간 |
| btn_cycle_start | Button | 사이클 시작 |
| btn_cycle_stop | Button | 사이클 정지 |
| txt_active_alarm | TextArea | 활성 알람 메시지 |

---

### 6.3 Auto Run 화면 상세

```
┌────────────────────────────────────────────┐
│ 레시피: [R-001 볼베어링 압입 v3]           │
│ ─────────────────────────────────────────  │
│ 공정 단계:                                 │
│  [IDLE]─●─[APPROACH]─○─[CONTACT]─○─[PRESS]│
│          ─○─[DWELL]─○─[RETURN]─○─[END]   │
│ ─────────────────────────────────────────  │
│ 실시간 모니터                              │
│  위치:   125.3 mm     속도:   45 mm/s     │
│  토크:    32.1 %      피크힘: 45.2 %      │
│ ─────────────────────────────────────────  │
│ [         힘-변위 실시간 그래프          ] │
│ [                                        ] │
│ ─────────────────────────────────────────  │
│  [▶ CYCLE START]         [■ CYCLE STOP]   │
└────────────────────────────────────────────┘
```

---

### 6.4 Alarm 화면 상세

```
┌────────────────────────────────────────────┐
│ 알람 이력                  [ACK] [RESET]   │
│ ─────────────────────────────────────────  │
│ ● [101] E-Stop 발생        2026-05-12 10:31│
│ ○ [102] Door Open          2026-05-12 09:15│
│ ○ [301] Cycle Timeout      2026-05-12 08:02│
│ ─────────────────────────────────────────  │
│ 선택 알람 상세:                            │
│  코드: 101   E-Stop 신호 감지             │
│  발생: 10:31:22   해제: 10:31:45          │
│  ACK: 완료   조치: E-Stop 복구 후 Reset   │
└────────────────────────────────────────────┘
```

---

## 7. Web UI 탭 구성 상세

`PC_GUI/UartWeb/` 기반 확장.

### 7.1 탭 목록

| 탭 | URL | 주요 기능 |
|---|---|---|
| Live Monitor | `/monitor` | 실시간 위치/힘/단계, 힘-변위 Chart.js |
| Cycle Control | `/cycle` | 레시피 선택, Cycle Start/Stop, 인터록 상태 |
| Recipe Manager | `/recipe` | 레시피 CRUD, 파라미터 편집, 활성화 |
| Quality Trend | `/quality` | OK/NG 트렌드, 피크힘 분포 차트 |
| Alarm History | `/alarm` | 알람 이력, ACK/Reset, CSV 다운로드 |
| Maintenance | `/maintenance` | I/O 상태, EtherCAT 진단, 카운터 리셋 |

### 7.2 신규 API 엔드포인트

```
GET  /api/status          기존 유지 (확장 필요)
POST /api/cmd             기존 유지

GET  /api/recipe/list     레시피 목록
GET  /api/recipe/{id}     레시피 상세
POST /api/recipe/{id}     레시피 저장
POST /api/recipe/activate 레시피 활성화

GET  /api/cycle/last      마지막 사이클 결과
GET  /api/history?n=50    최근 N개 사이클 이력
GET  /api/history/csv     CSV 다운로드

GET  /api/alarm/list      알람 이력
GET  /api/alarm/active    현재 활성 알람
POST /api/alarm/ack       알람 ACK
POST /api/alarm/reset     알람 Reset

GET  /api/counter         생산 카운터
POST /api/counter/reset   카운터 리셋
```

### 7.3 로그 표시 정책 (기존 정책 확장)

| 로그 타입 | 표시 | 필터 가능 |
|---|---|---|
| EVENT | 항상 표시 | 아니오 |
| ALARM | 항상 표시 (강조) | 아니오 |
| RESULT | 항상 표시 | 아니오 |
| TEL,... | 숨김 (기존 정책 유지) | 예 |
| DEBUG | 숨김 기본, 옵션 표시 | 예 |

---

## 8. CMD 명령 인터페이스 확장

기존 `CMD,key=value` 체계를 유지하며 확장.

### 8.1 신규 명령 목록

#### 운전/모드

| 명령 | 값 | 설명 |
|---|---|---|
| `CMD,mode_set=manual` | manual/setup/auto | 운전 모드 전환 |
| `CMD,cycle_start=1` | 1 | 사이클 시작 |
| `CMD,cycle_stop=1` | 1 | 사이클 정지 (RETURN 후 IDLE) |
| `CMD,cycle_abort=1` | 1 | 사이클 강제 중단 |

#### 레시피

| 명령 | 값 | 설명 |
|---|---|---|
| `CMD,recipe_select=N` | 0~9 | 레시피 번호 활성화 |
| `CMD,recipe_save=N` | 0~9 | 현재 설정을 레시피 N에 저장 |
| `CMD,recipe_load=N` | 0~9 | 레시피 N 로드 |
| `CMD,recipe_lock=N` | 0~9 | 레시피 N 잠금 |

#### 알람

| 명령 | 값 | 설명 |
|---|---|---|
| `CMD,alarm_ack=1` | 1 | 활성 알람 ACK |
| `CMD,alarm_reset=1` | 1 | 알람 Reset (원인 해제 후) |

#### 데이터 조회

| 명령 | 값 | 설명 |
|---|---|---|
| `CMD,result_read_last=1` | 1 | 마지막 사이클 결과 응답 |
| `CMD,counter_read=1` | 1 | 생산 카운터 응답 |
| `CMD,counter_reset=1` | 1 | 생산 카운터 리셋 |
| `CMD,history_read=N` | 1~100 | 최근 N개 이력 응답 |

### 8.2 신규 응답 프레임

| 프레임 | 예시 | 설명 |
|---|---|---|
| `EVT,...` | `EVT,cycle=123,step=PRESS` | 공정 이벤트 |
| `ALM,...` | `ALM,id=101,msg=ESTOP` | 알람 발생 |
| `RST,...` | `RST,cycle=123,result=OK,force=45.2,pos=125.3` | 사이클 결과 |
| `CNT,...` | `CNT,total=1240,ok=1235,ng=5,rate=0.40` | 카운터 응답 |
| `CFG,...` | 기존 유지 | 설정 응답 |

---

## 9. 데이터 구조 정의

### 9.1 RuntimeState (전역 상태)

```c
// Appli/Core/Inc/runtime_state.h

typedef struct {
    OperationMode_t  op_mode;
    PressState_t     press_state;
    SafetyState_t    safety_state;
    InterlockState_t interlock;
    AlarmCode_t      active_alarm;

    // 실시간 모션 데이터
    float    current_pos_mm;
    float    current_speed_mm_s;
    float    current_torque_pct;
    float    peak_force_pct;      // 현재 사이클 피크

    // 사이클 정보
    uint32_t cycle_number;
    uint32_t cycle_start_tick;

    // 레시피
    uint8_t  active_recipe_idx;

    // 생산 카운터
    ProductionCounter_t counter;
} RuntimeState_t;

extern RuntimeState_t g_runtime;
```

---

## 10. NVM 레이아웃 설계

Flash 섹터 할당 계획 (실제 주소는 HW에 맞게 확인 필요).

| 영역 | 크기 | 내용 |
|---|---|---|
| 파라미터 영역 | 4 KB | 기존 파라미터 (jog speed, acc/dec 등) |
| 홈 영역 | 4 KB | home_hw 내부 원점 |
| 레시피 영역 | 32 KB | RecipeStore_t (레시피 10개) |
| 사이클 이력 | 32 KB | CycleRecord_t × 100개 링 버퍼 |
| 알람 이력 | 8 KB | AlarmRecord_t × 50개 |
| 카운터 영역 | 4 KB | ProductionCounter_t |
| 시스템 설정 | 4 KB | 권한/권한 설정, 시스템 파라미터 |

**NVM 규칙:**
- 모든 구조체에 magic 필드와 CRC16 포함
- 버전 필드로 호환성 관리 (`uint8_t nvm_version`)
- 호환 불가 버전 감지 시 기본값으로 초기화 후 알람

---

## 11. 구현 체크리스트

### Phase 1 — Safety + Auto Cycle ✅ 완료 (2026-05-12)

백업: `backup/phase1_20260512_1540/`

#### press_state_machine
- [x] `press_state_machine.h` / `press_state_machine.c` 파일 생성
- [x] PressState_t enum 정의 (IDLE/APPROACH/CONTACT/PRESS/DWELL/RETURN/CYCLE_END/CYCLE_NG/ABORT)
- [x] PressStateMachine_Init() 구현
- [x] PressStateMachine_Tick() 구현 — 각 상태 케이스
- [x] PressStateMachine_CycleStart() / CycleStop() / CycleAbort() 구현
- [x] 타임아웃 감시 구현 (cycle_timeout_ms)
- [x] FreeRTOS EtherCAT_Task에 Tick 연결 (1ms 주기)
- [x] SOEM 모션 명령 연결 (APPROACH/CONTACT/PRESS/RETURN 속도/위치 명령)
- [x] JudgeResult_t 품질 판정 코드 정의 (OK/NG 7종)
- [x] PressCounter_t 생산 카운터 (Total/OK/NG/연속NG/NG Rate)

#### interlock_manager
- [x] `interlock_manager.h` / `interlock_manager.c` 파일 생성
- [x] InterlockState_t 구조체 정의
- [x] GPIO 핀 매핑 정의 (INTERLOCK_SIM_DEFAULT=1, 실 HW 연결 시 0으로 변경)
- [x] InterlockManager_Init() 구현
- [x] InterlockManager_Update() — GPIO 읽기 + 상태 갱신
- [x] InterlockManager_IsAutoReady() — AUTO 진입 조건 확인
- [x] InterlockManager_IsCycleStartReady() — Cycle Start 조건 확인
- [x] Two-Hand 타이밍 검사 (500ms 이내 동시 — ILOCK_TWOHAND_WINDOW_MS)
- [x] E-Stop 감지 → 진행 중 사이클 ABORT 전환
- [x] 소프트웨어 시뮬레이션 모드 (ilock_sim, ilock_estop, ilock_door CMD)

#### 운전 모드 관리
- [x] OperationMode_t enum 정의 (MANUAL/SETUP/AUTO/ALARM/RECOVERY)
- [x] mode_set CMD 핸들러 추가
- [x] 모드 전환 조건 검증 (AUTO: 인터록 필수, 사이클 중 전환 차단)

#### CMD 확장 (main.c)
- [x] `cycle_start=1` 파서 추가
- [x] `cycle_stop=1` 파서 추가
- [x] `cycle_abort=1` 파서 추가
- [x] `mode_set=manual|setup|auto` 파서 추가
- [x] `press_approach_speed/pos`, `press_contact_speed/th` 파서 추가
- [x] `press_speed`, `press_target_pos`, `press_max_force`, `press_dwell_ms` 파서 추가
- [x] `press_return_speed/pos`, `press_timeout_ms` 파서 추가
- [x] `judge_force_max/min`, `judge_pos_max/min` 파서 추가
- [x] `press_status=1` 상태 조회 (PST/CNT/RST 응답) 추가
- [x] `counter_reset=1`, `alarm_reset=1` 파서 추가
- [x] `ilock_sim/estop/door/left/right/home` 시뮬레이션 CMD 추가

#### CMakeLists.txt
- [x] interlock_manager.c / press_state_machine.c APPLI_SOURCES 추가
- [x] 빌드 성공 확인 (Flash: 25.34%, RAM: 60.93%)

#### LCD Dashboard
- [ ] TouchGFX Designer에 Dashboard 화면 추가 ← **다음 작업**
- [ ] 인터록 인디케이터 위젯 배치
- [ ] 모드/단계 텍스트 위젯 배치
- [ ] 카운터 텍스트 위젯 배치
- [ ] CYCLE START / STOP 버튼 배치
- [ ] DashboardPresenter.cpp — Model에서 데이터 받아 위젯 갱신

---

### Phase 2 — Recipe + Quality

#### recipe_manager
- [ ] `recipe_manager.h` / `recipe_manager.c` 파일 생성
- [ ] RecipeData_t 구조체 정의 (magic + CRC 포함)
- [ ] Recipe_Init() — Flash 로드 + 검증
- [ ] Recipe_Load() / Save() 구현
- [ ] Recipe_Activate() 구현
- [ ] Recipe_Clone() 구현
- [ ] Recipe_Lock() / Unlock() 구현
- [ ] recipe_select / recipe_load CMD 핸들러 추가
- [ ] NVM 레시피 영역 섹터 정의

#### quality_judge
- [ ] `quality_judge.h` / `quality_judge.c` 파일 생성
- [ ] JudgeResult_t enum 정의
- [ ] QualityJudge_Evaluate() 구현 (4항목)
- [ ] CYCLE_END 상태에서 평가 호출
- [ ] RST,... 응답 프레임 전송

#### 생산 카운터
- [ ] ProductionCounter_t 구조체 정의
- [ ] 카운터 증감 함수 구현 (OK++, NG++, NG 종류별)
- [ ] NG Rate 계산
- [ ] 연속 NG 카운터 + 알람 임계값
- [ ] NVM 주기 저장 (5사이클마다)
- [ ] CNT,... 응답 프레임 추가

#### LCD Auto Run 화면
- [ ] TouchGFX Designer에 Auto Run 화면 추가
- [ ] 공정 단계 인디케이터 (IDLE→…→END) 위젯 배치
- [ ] 실시간 위치/속도/토크 표시 위젯 배치
- [ ] 힘-변위 그래프 위젯 (Canvas/Graph) 배치
- [ ] AutoRunPresenter.cpp 구현

#### LCD Recipe 화면
- [ ] TouchGFX Designer에 Recipe 화면 추가
- [ ] 레시피 목록 스크롤 위젯 배치
- [ ] 레시피 파라미터 편집 위젯 배치
- [ ] 활성화/저장/잠금 버튼 배치
- [ ] RecipePresenter.cpp 구현

---

### Phase 3 — Traceability + Alarm

#### cycle_logger
- [ ] `cycle_logger.h` / `cycle_logger.c` 파일 생성
- [ ] CycleRecord_t 구조체 정의
- [ ] 링 버퍼 구현 (100개)
- [ ] CycleLogger_Record() — CYCLE_END/CYCLE_NG 시 호출
- [ ] CycleLogger_GetLast() 구현
- [ ] CycleLogger_SerializeCSV() 구현
- [ ] NVM 이력 영역 저장

#### alarm_manager
- [ ] `alarm_manager.h` / `alarm_manager.c` 파일 생성
- [ ] AlarmCode_t enum 정의
- [ ] AlarmRecord_t 구조체 정의
- [ ] AlarmManager_Raise() 구현 — 알람 발생 + 모드 전환
- [ ] AlarmManager_Ack() 구현
- [ ] AlarmManager_Reset() 구현
- [ ] alarm_ack / alarm_reset CMD 핸들러 추가
- [ ] ALM,... 응답 프레임 전송
- [ ] LCD Alarm 화면 연결

#### Web UI 확장
- [ ] `/api/history` 엔드포인트 추가
- [ ] `/api/history/csv` 엔드포인트 추가
- [ ] `/api/alarm/list` 엔드포인트 추가
- [ ] `/api/alarm/ack` / `/reset` 엔드포인트 추가
- [ ] Quality Trend 탭 추가 (Chart.js)
- [ ] Alarm History 탭 추가

---

### Phase 4 — UX + Maintenance

#### 유지보수 화면
- [ ] LCD Maintenance 화면 추가 (TouchGFX)
- [ ] I/O 모니터 위젯 배치
- [ ] EtherCAT 상태 표시 위젯 배치
- [ ] Web Maintenance 탭 추가

#### 권한 체계
- [ ] UserLevel_t enum 정의 (Operator/Engineer/Admin)
- [ ] 레시피 잠금 → Engineer 레벨 확인 연결
- [ ] PIN/비밀번호 입력 UI (LCD + Web)
- [ ] 감사 로그 (설정 변경 + 사용자 기록)

#### 캘리브레이션 마법사
- [ ] 홈 캘리브레이션 시퀀스 구현
- [ ] 하중 캘리브레이션 (토크% ↔ 힘[N]) 구현
- [ ] LCD 마법사 화면 구현

---

## 변경 이력

| 버전 | 날짜 | 작성자 | 내용 |
|---|---|---|---|
| v1.0 | 2026-05-12 | 초안 | 전체 구현 가이드 작성 |

---

*기능 추가 시 본 문서의 해당 섹션과 체크리스트를 먼저 업데이트 후 구현.*
