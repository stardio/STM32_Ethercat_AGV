# AGV I/O 베이스보드 설계 참조서

**대상 MCU**: STM32H753ZI (NUCLEO-H753ZI)  
**작성일**: 2026-06-13  
**관련 파일**: `Appli/Core/Src/io_handler.c`, `Appli/Core/Inc/io_handler.h`

---

## 1. I/O 핀 배치 요약

### 1-1. Digital Output (DO) — 8채널

| 채널 | MCU 핀 | GPIO | 초기값 | 비고 |
|------|--------|------|--------|------|
| DO0 | PF0 | GPIOF bit0 | LOW | |
| DO1 | PF1 | GPIOF bit1 | LOW | |
| DO2 | PF2 | GPIOF bit2 | LOW | |
| DO3 | PF3 | GPIOF bit3 | LOW | |
| DO4 | PF4 | GPIOF bit4 | LOW | |
| DO5 | PF5 | GPIOF bit5 | LOW | |
| DO6 | PF6 | GPIOF bit6 | LOW | |
| DO7 | PF7 | GPIOF bit7 | LOW | |

- **출력 모드**: Push-Pull
- **로직 레벨**: 3.3 V
- **최대 전류**: 핀당 25 mA (포트 합계 120 mA 이하 권장)
- **부하 연결**: 직접 구동 불가 → **옵토커플러 또는 드라이버 IC 필수**

---

### 1-2. Digital Input (DI) — 8채널

| 채널 | MCU 핀 | GPIO | 활성 조건 | 비고 |
|------|--------|------|-----------|------|
| DI0 | PD0 | GPIOD bit0 | LOW 입력 시 활성 | 내부 Pull-up |
| DI1 | PD1 | GPIOD bit1 | LOW 입력 시 활성 | 내부 Pull-up |
| DI2 | PD2 | GPIOD bit2 | LOW 입력 시 활성 | 내부 Pull-up |
| DI3 | PD3 | GPIOD bit3 | LOW 입력 시 활성 | 내부 Pull-up |
| DI4 | PD4 | GPIOD bit4 | LOW 입력 시 활성 | 내부 Pull-up |
| DI5 | PD5 | GPIOD bit5 | LOW 입력 시 활성 | 내부 Pull-up |
| DI6 | PD6 | GPIOD bit6 | LOW 입력 시 활성 | 내부 Pull-up |
| DI7 | PD7 | GPIOD bit7 | LOW 입력 시 활성 | 내부 Pull-up |

- **입력 모드**: 내부 Pull-up (약 40 kΩ), **Active LOW**
- **소프트웨어 반전**: `IO_DI_Get()` 반환값은 반전되어 있음 → bit=1 이면 해당 DI 활성
- **인터페이스**: 옵토커플러 출력 컬렉터를 PD핀에, 이미터를 GND에 연결

> ⚠ PD8(UART3 TX), PD9(UART3 RX) 는 이미 사용 중. **PD8·PD9는 절대 DI로 사용 불가.**

---

### 1-3. Analog Input (AI) — 4채널

| 채널 | MCU 핀 | ADC1 채널 | 스캔 순서 | 비고 |
|------|--------|-----------|-----------|------|
| AI0 | PA3 | CH15 | Rank 1 | |
| AI1 | PA4 | CH18 | Rank 2 | |
| AI2 | PA5 | CH19 | Rank 3 | |
| AI3 | PA6 | CH3  | Rank 4 | |

- **해상도**: 12-bit (0 ~ 4095)
- **입력 범위**: 0 ~ 3.3 V (VREF+ = VDD = 3.3 V)
- **샘플링**: ADC_SAMPLETIME_64CYCLES_5, DMA Circular, 연속 변환
- **전압 환산**: `V = raw × 3.3 / 4095`
- **갱신 주기**: DMA가 연속 자동 갱신 → IO_STATUS 패킷(200ms) 기준으로 PC 전달

> ⚠ PA1(ETH RMII REF_CLK), PA2(ETH MDIO), PA7(ETH RMII CRS_DV) 는 Ethernet에 사용 중.  
> **PA3~PA6는 Ethernet과 무관하므로 AI로 안전하게 사용 가능.**

---

### 1-4. PWM Output — 4채널

| 채널 | MCU 핀 | TIM3 CH | 주파수 | 분해능 |
|------|--------|---------|--------|--------|
| PWM0 | PC6 | CH1 | 20 kHz | 0.1% (1000 steps) |
| PWM1 | PC7 | CH2 | 20 kHz | 0.1% (1000 steps) |
| PWM2 | PC8 | CH3 | 20 kHz | 0.1% (1000 steps) |
| PWM3 | PC9 | CH4 | 20 kHz | 0.1% (1000 steps) |

- **로직 레벨**: 3.3 V, Push-Pull AF (Alternate Function 2 = TIM3)
- **듀티 범위**: 0.00 ~ 100.00 % (API 단위: 0 ~ 10000)
- **클럭 체계**: SYSCLK 480MHz → APB1 120MHz → TIM3 240MHz  
  `Prescaler=11 → 20MHz → Period=999 → 20kHz`
- **부하 연결**: 직접 구동 불가 → **MOSFET 게이트 드라이버 또는 PWM 증폭기 필수**

---

## 2. 전체 핀 배치 한눈에 보기

```
STM32H753ZI (NUCLEO-H753ZI)
┌─────────────────────────────────────────────────┐
│                                                 │
│  UART (기존 사용)                               │
│    PD8  ── USART3 TX  (Jetson Orin Nano)        │
│    PD9  ── USART3 RX                            │
│                                                 │
│  Ethernet RMII (기존 사용 — 건드리지 말 것)      │
│    PA1  PA2  PA7  PB13  PC1  PC4  PC5           │
│    PG11  PG13                                   │
│                                                 │
│  EtherCAT SOEM (기존 사용)                      │
│    ETH 핀 통해 LAN8742 PHY 연결                 │
│                                                 │
│  LED (기존 사용)                                │
│    PB0  ── 녹색 LED                             │
│    PB14 ── 적색 LED                             │
│    PE1  ── 황색 LED                             │
│                                                 │
│  ┌─── I/O 확장 (베이스보드 신규) ──────────┐   │
│  │                                         │   │
│  │  DO ×8   PF0 ~ PF7   (GPIOF, 전용포트)  │   │
│  │  DI ×8   PD0 ~ PD7   (GPIOD, Pull-up)  │   │
│  │  AI ×4   PA3 ~ PA6   (ADC1 DMA)        │   │
│  │  PWM×4   PC6 ~ PC9   (TIM3 CH1~4)      │   │
│  │                                         │   │
│  └─────────────────────────────────────────┘   │
└─────────────────────────────────────────────────┘
```

---

## 3. 베이스보드 설계 주의사항

### 3-1. 전원 / 절연

| 항목 | 권장 사항 |
|------|-----------|
| MCU 전원 | 3.3 V (NUCLEO 보드 내장 LDO 사용) |
| 외부 I/O 전원 | 별도 5 V 또는 24 V 계통 사용 |
| DO/DI 절연 | **옵토커플러 필수** (TLP785, PC817, ADUM 계열 등) |
| AI 보호 | 입력단 TVS 다이오드 + 직렬 저항 100 Ω 이상 |
| 공통 GND | MCU GND와 외부 GND 옵토커플러로 분리 권장 |

> 농업 환경 특성상 전자기 노이즈(인버터, 펌프 모터)가 심함.  
> 절연 없이 직결 시 GPIO 손상 또는 MCU 리셋 위험.

---

### 3-2. DO 출력 회로

```
MCU PF0~PF7 (3.3V, 25mA max)
        │
       [100Ω]           ← 전류 제한 저항
        │
     옵토커플러 LED (A단)
        │
       GND_MCU

옵토커플러 OUT (컬렉터) ── 외부 전원(5V~24V) + 부하
옵토커플러 OUT (이미터) ── GND_EXT
```

- GPIO HIGH → 옵토커플러 ON → 외부 출력 ON
- 릴레이 모듈 사용 시 ULN2003 / ULN2803 트랜지스터 어레이 경유 권장
- **유도 부하(솔레노이드, 릴레이 코일)에는 반드시 플라이백 다이오드** 추가

---

### 3-3. DI 입력 회로

```
외부 센서/스위치 (NPN 개방 컬렉터 출력 또는 건접점)

외부 전원 + ── [옵토커플러 LED] ── 센서 NPN 컬렉터
                                    센서 이미터 ── GND_EXT

옵토커플러 컬렉터── MCU PDn (내부 Pull-up 40kΩ, 3.3V)
옵토커플러 이미터── GND_MCU
```

- 센서 활성 → 옵토커플러 ON → PDn = LOW → 펌웨어에서 활성으로 인식
- 외부 신호 전압 5 V ~ 24 V 모두 대응 가능 (옵토커플러 전류 제한 저항 조정)
- **건접점(Dry Contact) 스위치**는 MCU 3.3V ─ 스위치 ─ PDn 직결도 가능 (절연 불필요 시)

---

### 3-4. AI 아날로그 입력 회로

```
외부 센서 출력 (0~5V 또는 4~20mA)
        │
  [신호 조정 회로] ── 0~3.3V로 변환
        │
       [100Ω] ── PA3~PA6 ── ADC1
        │
      [10nF] (GND로 노이즈 필터)
```

- **입력 절대 최대값: 3.3 V** — 초과 시 ADC 손상
- 0~5 V 센서: 저항 분압 (R1=39kΩ, R2=68kΩ → 5V × 68/(68+39) = 3.18V ≈ OK)
- 4~20 mA 센서: 165 Ω 수신 저항 → 0.66V~3.30V (풀스케일 대응)
- 농업 환경에서 케이블이 길 경우 **차동 수신기(AMC1311 등) + 필터** 강력 권장
- ADC 기준 전압(VREF)은 MCU VDDA(3.3V) 사용 중 → 정밀 측정 필요 시 외부 VREF(REF5030 등) 고려

---

### 3-5. PWM 출력 회로

```
MCU PC6~PC9 (3.3V, 20kHz)
        │
     [게이트 드라이버] (TC4427, IR2104 등)
        │
     MOSFET 게이트
        │
     외부 부하 (팬, 펌프, LED 드라이버 등)
```

- 3.3 V 로직으로 외부 MOSFET 직접 구동 시 **LOGIC-LEVEL MOSFET** 사용 (Vgs(th) ≤ 2.5V)
  - 추천: IRLML6402 (P-ch), IRLZ34N (N-ch)
- 팬·펌프 제어 시 **PWM → RC 저역통과 필터 → 아날로그 0~3.3V 변환** 후 VFD 제어 가능
  - R=10kΩ, C=10μF → 차단 주파수 ≈ 1.6 Hz (20kHz PWM 완전 평활)
- 유도성 부하에는 **프리휠링 다이오드** 필수 (MOSFET 내장 바디 다이오드만으로 부족할 수 있음)

---

### 3-6. 커넥터 / 케이블

| 권장 사항 | 이유 |
|-----------|------|
| DO/DI: 스크류터미널 또는 M8 산업용 커넥터 | 진동·습기에 강함 |
| AI: 차폐 케이블 (Shield를 한쪽 GND에 접지) | 노이즈 차단 |
| PWM: 비틀림 쌍선(Twisted Pair) 또는 차폐 케이블 | 스위칭 노이즈 방사 억제 |
| 전원 라인: 페라이트 비드 + 470μF 전해 + 100nF 세라믹 | 전원 노이즈 디커플링 |

---

### 3-7. 보호 회로 체크리스트

- [ ] DO 각 채널 출력단 **TVS 다이오드** (SMBJ5.0A 또는 동급)
- [ ] DI 각 채널 입력단 **직렬 저항** 1kΩ + **TVS 다이오드**
- [ ] AI 입력단 **쇼트키 클램프 다이오드** (BAT54S 등, VCC·GND 방향)
- [ ] PWM 출력단 **게이트 저항** 10~47Ω (고주파 링잉 억제)
- [ ] 전원부 **역전압 보호 다이오드** (P6KE 계열)
- [ ] PCB 외곽 **GND 가드링** (EMI 저감)
- [ ] 방수 등급 필요 시 케이스 포팅(Potting) 또는 **IP65 인클로저**

---

### 3-8. 소프트웨어 연동 체크리스트

- [ ] DI Pull-up 방향 확인: 센서 OFF 시 PDn = HIGH (bit=0), ON 시 PDn = LOW (bit=1 반전 후)
- [ ] 부팅 시 DO 초기값 모두 LOW — 의도치 않은 출력 방지 (io_handler.c `GPIO_PIN_RESET`)
- [ ] IO_STATUS 200ms 주기 자동 수신 — bridge.py WebSocket `io_status` 메시지 확인
- [ ] PWM 채널 OFF 시 `pwm_set ch:N duty:0` 또는 `IO_PWM_Set(ch, 0)` 명시 호출
- [ ] ADC 채널 순서 고정: **AI0=PA3, AI1=PA4, AI2=PA5, AI3=PA6** (DMA rank 순서와 일치)

---

## 4. 핀 충돌 금지 목록

베이스보드 설계 시 아래 핀은 **절대 사용 금지** (이미 펌웨어에서 사용 중).

| 핀 | 용도 | 이유 |
|----|------|------|
| PA1, PA2, PA7 | ETH RMII | EtherCAT 통신 필수 |
| PB13 | ETH RMII TXD1 | EtherCAT 통신 필수 |
| PC1, PC4, PC5 | ETH RMII | EtherCAT 통신 필수 |
| PG11, PG13 | ETH RMII | EtherCAT 통신 필수 |
| PD8, PD9 | USART3 TX/RX | Jetson UART 통신 |
| PB0, PB14, PE1 | 보드 LED | 디버그 표시등 |
| PA11, PA12 | USB D−/D+ | USB 디버그 (NUCLEO) |
| TIM6 | FreeRTOS Timebase | 절대 재사용 불가 |

---

*이 문서는 `Appli/Core/Src/io_handler.c` 구현과 동기화되어 있습니다.*  
*회로 변경 시 io_handler.c 핀 정의(`DO_PORT`, `AI_PINS`, `PWM_PINS` 등)도 함께 수정하세요.*
