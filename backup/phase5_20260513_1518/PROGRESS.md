# 서보 프레스 시스템 개발 진행 현황

> 최종 업데이트: 2026-05-13  
> 플랫폼: STM32H7S78-DK / SOEM EtherCAT CiA402 / TouchGFX / ASP.NET Web UI  

---

## 시스템 구성

```
PC 브라우저 (http://localhost:5100)
    │  HTTP REST API
    ▼
ASP.NET UartWeb 서버 (PC_GUI/UartWeb)
    │  UART (COM15, 115200bps)
    ▼
STM32H7S78-DK 펌웨어 (Appli/)
    │  EtherCAT (SOEM)
    ▼
서보 드라이브 (CiA402 CSP 모드)
```

---

## 완료된 기능

### Phase 1 — 기본 연결 및 수동 운전
- UART 통신 프로토콜 (`CMD,key=value` / `CFGM,CFGP,CFGR,PST,CNT,RST,ALM` 응답)
- 웹UI 수동 운전: 위치/속도/토크 설정, 절대/상대 이동, Start/Stop
- 조그 운전: 버튼 단타(jog_delta) / 누름 유지(jog_velocity → 소프트 리밋까지 이동)
- Run Enable 제어

### Phase 2 — 파라미터 관리
- 파라미터 페이지: JogSpeed / Acc / Dec / Limit+/- / GearRatio / BallLead / EncRes / HomeOffset / PositionGain
- Write All → 드라이브 적용 + 내부 NVM 저장 (재부팅 후 유지)
- Read All → 드라이브 SDO 읽기
- Save / Load (플래시 개별 저장/불러오기)

### Phase 3 — 사이클 이력 및 알람
- 사이클 결과 기록 (OK/NG, 피크 토크, 도달 위치, 사이클 시간)
- CSV 다운로드 (`/api/history/csv`)
- 알람 이력 조회 / ACK / Reset
- 생산 카운터 (Total / OK / NG / 연속NG / NG율)

### Phase 4 — 사용자 인증 및 유지보수
- 사용자 레벨 (OPERATOR / TECHNICIAN / ADMIN), PIN 로그인
- 유지보수 카운터: 총 사이클 / 마지막 정비 후 사이클 / PM 임계값 알림

### Phase 5 — 단위 mm 적용 및 unit_scale 연동
- 모든 위치 필드: `(mm)` 단위 표시
- 모든 속도 필드: `(mm/s)` 단위 표시
- `unit_scale = enc × gear / lead` (counts/mm) 계산 및 SOEM 적용
- 부팅 시 `Pc_ApplyParametersToDrive()` 호출 → 플래시 저장 파라미터 즉시 적용

---

## 해결한 주요 버그

### 1. Write All 후 감속비/리드/엔코더가 1로 리셋
**원인:** `ApplyOutgoingCommandNoLock()`에서 명령 전송마다 `Settings.TimestampUtc` 갱신  
→ Write All 시퀀스 중 폴링이 중간 값(gear=1)으로 폼을 덮어씀  
**수정:** `TimestampUtc` 업데이트를 outgoing 명령 처리에서 제거. 펌웨어에서 완성된 CFGP 응답이 올 때만 폼 갱신.  
파일: `PC_GUI/UartWeb/Services/UartGateway.cs`

---

### 2. Write All 후 CiA402 OP 드롭
**원인:** `Pc_SaveParameterToFlash()` → `UiFlash_WriteLayout()` → `__disable_irq()` → EtherCAT ISR 차단 → 워치독 타임아웃 → SAFE-OP 전환  
**수정:** `__disable_irq()` 제거. 펌웨어는 외부 XSPI(0x70000000)에서 실행되므로 내부 NVM erase 중에도 instruction fetch 영향 없음.  
파일: `Appli/Core/Src/ui_flash_storage.c`

---

### 3. 조그 운전 속도 불균일
**원인:** CSP 모드에서 HTTP `setInterval(120ms)` + 가변 레이턴시 → `jog_delta` 도착 간격 불규칙 → 실효 속도 크게 변동  
**수정:** 버튼 누름 → `CMD,jog_velocity=+1/-1` 단일 전송 → 소프트 리밋까지 목표 설정 → 펌웨어 소프트 램프가 일정 속도 유지  
버튼 뗌 → `CMD,manual_stop=1` → 현재 위치로 목표 즉시 리셋  
파일: `PC_GUI/UartWeb/wwwroot/app.js`

---

### 4. 부팅 시 unit_scale = 1 (mm 변환 미적용)
**원인:** `Pc_CommandInit()`이 플래시 파라미터를 로드했지만 `SOEM_SetUnitScale()` 미호출 → `soem_unit_scale = 1` 유지  
→ 100mm 입력 = 100 counts 이동  
**수정:** `Pc_CommandInit()` 마지막에 `Pc_ApplyParametersToDrive()` 호출 추가  
파일: `Appli/Core/Src/main.c`

---

### 5. LCD 파라미터와 웹UI 파라미터 인덱스 충돌 (핵심 버그)
**원인:** 동일한 플래시 영역을 LCD 모델과 웹UI가 다른 레이아웃으로 해석

| 인덱스 | LCD 모델 (`kParam`) | 웹UI (`PC_PARAM`) |
|:---:|---|---|
| 5 | `kParamUnitScale` (precomputed) | `PC_PARAM_GEAR_RATIO` |
| 6 | `kParamHomeOffset` | `PC_PARAM_HOME_OFFSET` |
| 7 | `kParamPositionGain` | `PC_PARAM_POSITION_GAIN` |

LCD `initialize()` / `writeAllParametersToDrive()` 호출 시:  
`SOEM_SetUnitScale(flash[5])` = `SOEM_SetUnitScale(gear_ratio)` → unit_scale 오염

예: gear=5, lead=5, enc=10000 → 올바른 unit_scale=10000, LCD가 설정한 값=5

**수정:**  
1. `Model::writeAllParametersToDrive()`에서 `modelSetUnitScale()` 호출 제거  
2. `Pc_SyncTouchGfxModel()` 이후 `Pc_ApplyParametersToDrive()` 재호출  
파일: `Appli/TouchGFX/gui/src/model/Model.cpp`, `Appli/Core/Src/main.c`

---

## 현재 아키텍처 — unit_scale 적용 흐름

```
웹UI 파라미터 입력
  enc × gear / lead = unit_scale (counts/mm)
        │
        ▼ Pc_ApplyParametersToDrive()
  SOEM_SetUnitScale(unit_scale)
        │
  soem_unit_scale (volatile)
        │
  ┌─────┴──────────────────────┐
  │                            │
  ▼                            ▼
soem_saturated_user_to_hw()   soem_get_target_step_hw_per_cycle()
  pos_hw = pos_mm × scale       step = velocity_mm_s × scale / 1000
  + home_offset                 (counts per 1ms CSP cycle)
```

---

## 파라미터 플래시 레이아웃 (웹UI 기준, PC_PARAM)

| 인덱스 | 이름 | 단위 | 설명 |
|:---:|---|---|---|
| 0 | JogSpeed | mm/s | 조그/기본 이송 속도 |
| 1 | AccTime | ms | 가속 시간 (드라이브 SDO 0x2301) |
| 2 | DecTime | ms | 감속 시간 |
| 3 | LimitPlus | mm | 소프트 상한 리밋 |
| 4 | LimitMinus | mm | 소프트 하한 리밋 |
| 5 | GearRatio | - | 감속비 (e.g. 5 = 5:1) |
| 6 | HomeOffset | - | 홈 오프셋 (내부) |
| 7 | PositionGain | - | 위치 게인 |
| 8 | BallLead | mm/rev | 볼스크류 리드 |
| 9 | EncRes | counts/rev | 엔코더 분해능 |

---

## 웹서버 실행

```powershell
# 서버 시작
cd D:\Ethercat_STM32SP\PC_GUI\UartWeb
dotnet run

# 접속 URL
http://localhost:5100
```

---

## 빌드 및 플래시

```powershell
# 빌드
cd D:\Ethercat_STM32SP
cmake --build build/Debug

# BIN 변환 (objcopy가 PATH에 없을 경우)
& "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin\arm-none-eabi-objcopy.exe" `
  -O binary build/Debug/Appli/LCD_Test_Appli.elf build/Debug/Appli/LCD_Test_Appli.bin

# 플래시
.\tools\flash.ps1
```

---

## 향후 개선 필요 사항

- [ ] LCD 파라미터 배열과 웹UI 파라미터 배열 인덱스 통일 (현재 LCD는 웹UI 우선 정책으로 우회)
- [ ] 홈 설정(Set Home) 후 웹UI 위치 표시 동기화 확인
- [ ] 프레스 사이클 (Approach → Contact → Press → Dwell → Return) 실기 검증
- [ ] NG 판정 조건 (force_max/min, pos_max/min) 현장 튜닝
- [ ] 유지보수 PM 임계값 알림 동작 확인
