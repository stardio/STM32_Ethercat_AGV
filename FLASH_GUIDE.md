# STM32H753ZI 펌웨어 플래시 가이드

**대상 보드:** NUCLEO-H753ZI  
**도구:** STM32CubeProgrammer v2.22+  
**연결:** ST-Link SWD

---

## 1. 플래시 주소 정리

| 바이너리 | 주소 | 비고 |
|----------|------|------|
| `CartesianRobot_H753.bin` | **`0x08000000`** | 내부 플래시 Bank1 시작 — 전체 펌웨어 |
| `LCD_Test_Boot.bin` | `0x08000000` | 구 부트로더 (사용 안 함) |
| `LCD_Test_Appli.bin` | `0x70000000` | 외부 QSPI 플래시 (사용 안 함) |

> ⚠️ **주의:** `LCD_Test_Appli`의 주소(0x70000000)와 혼동하지 말 것.  
> `CartesianRobot_H753`은 외부 로더 없이 내부 플래시 0x08000000에 직접 플래시한다.

---

## 2. 플래시 전 주소 검증 방법

플래시 전 바이너리의 Reset_Handler 주소를 확인해서 올바른 플래시 주소를 검증한다.

```powershell
$bin   = [System.IO.File]::ReadAllBytes("build\Debug\Appli\CartesianRobot_H753.bin")
$sp    = [System.BitConverter]::ToUInt32($bin, 0)
$reset = [System.BitConverter]::ToUInt32($bin, 4)
Write-Host "Initial SP    : 0x$($sp.ToString('X8'))"
Write-Host "Reset_Handler : 0x$($reset.ToString('X8'))"
# Reset_Handler가 0x0800xxxx → 플래시 주소 0x08000000 이 정확
```

---

## 3. 빌드

```powershell
# 프로젝트 루트에서 실행
Set-Location "d:\Ethercat_N753-6AX"
$env:PATH = "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin;" + $env:PATH

# CMake 구성 (캐시가 없거나 오래됐을 때)
cmake --preset Debug

# 빌드
cmake --build build\Debug --target CartesianRobot_H753 --parallel 4
```

빌드 산출물 위치: `build\Debug\Appli\CartesianRobot_H753.bin`

---

## 4. 플래시 (PowerShell)

```powershell
$PROG = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
$BIN  = "build\Debug\Appli\CartesianRobot_H753.bin"

& $PROG -c port=SWD freq=4000 -d $BIN 0x08000000 -rst
```

### 주요 옵션 설명

| 옵션 | 의미 |
|------|------|
| `-c port=SWD freq=4000` | SWD 연결, 4 MHz |
| `-d $BIN 0x08000000` | 바이너리를 0x08000000에 다운로드 |
| `-rst` | 플래시 완료 후 MCU 소프트 리셋 |

---

## 5. 빌드 + 플래시 한 번에 (VSCode 태스크)

VSCode에서 `Ctrl+Shift+B` → **"Quick Build & Flash Appli"** 선택.  
또는 터미널에서:

```powershell
powershell -File tools\build_and_flash.ps1
```

> 단, `tools\build_and_flash.ps1` 내의 기본 타겟이 `LCD_Test_Appli`로 되어 있으므로  
> 아래와 같이 타겟을 지정해서 실행한다.

```powershell
powershell -File tools\build_and_flash.ps1 -Target CartesianRobot_H753 -Address 0x08000000
```

---

## 6. 플래시 후 정상 동작 확인

1. `bridge.py` 실행 및 UI 접속
2. LOG 패널에서 다음 메시지 확인:
   ```
   SOEM: init ok (6-axis articulated)
   SOEM: found N slave(s), activating N axis(es)
   SOEM: all axes OPERATIONAL
   ```
3. `All Ready` 표시가 초록으로 전환되면 정상 부팅 완료
4. **▶ Enable All** → `[AxJ1] OP_ENABLED` 확인

---

## 7. 플래시 영역 맵 (STM32H753ZI 내부 2 MB)

```
0x08000000  ┌─────────────────────────┐ ← CartesianRobot_H753.bin 시작
            │  Bank1 Sector 0  128 KB │   ISR 벡터 테이블 + 코드
0x08020000  ├─────────────────────────┤
            │  Bank1 Sector 1  128 KB │   코드 (이어짐)
0x08040000  ├─────────────────────────┤
            │  ...                    │
0x081E0000  ├─────────────────────────┤ ← Flash 파라미터 저장 영역
            │  Bank2 Sector 7  128 KB │   RobotFlashData_t (320 bytes)
0x08200000  └─────────────────────────┘
```

---

## 8. 자주 발생하는 오류

### CMakeCache 경로 불일치
```
CMake Error: The current CMakeCache.txt directory ... is different than ...
```
→ 캐시 삭제 후 재구성:
```powershell
Remove-Item build\Debug\CMakeCache.txt -Force
Remove-Item build\Debug\CMakeFiles -Recurse -Force
cmake --preset Debug
```

### 플래시 주소 오류 증상
- UART 접속은 되지만 STATUS가 파싱 안 됨 (구 3축 펌웨어 실행 중)
- UI에서 DRO 배지가 모두 N/A
- All Ready = OFF (절대 초록이 안 됨)  
→ **바이너리를 0x08000000에 다시 플래시**

### ST-Link 연결 실패
```
Error: No ST-LINK detected
```
→ USB 재연결 또는 `freq=4000` → `freq=1000`으로 낮춰 재시도
