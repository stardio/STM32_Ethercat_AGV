# =============================================================================
# 04_attach_usb.ps1  —  STM32 VCP + RealSense 카메라를 WSL2에 연결
#
# 실행 방법 (관리자 PowerShell):
#   .\04_attach_usb.ps1
#
# USB 장치를 연결/재연결할 때마다 실행
# =============================================================================

$ErrorActionPreference = "Stop"
function Write-Step($msg) { Write-Host "`n[$msg]" -ForegroundColor Cyan }

# ── 관리자 권한 확인 ──────────────────────────────────────────────────────────
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "관리자 권한으로 PowerShell을 실행해주세요."
    exit 1
}

# ── usbipd 설치 확인 ──────────────────────────────────────────────────────────
Write-Step "usbipd 버전 확인"
try {
    $ver = & usbipd version 2>&1
    Write-Host "usbipd: $ver"
} catch {
    Write-Error "usbipd가 설치되지 않았습니다. 01_install_wsl2.ps1 먼저 실행하세요."
    exit 1
}

# ── 연결된 USB 장치 목록 ──────────────────────────────────────────────────────
Write-Step "연결된 USB 장치 목록"
$devices = & usbipd list
Write-Host $devices

# ── STM32 VCP 자동 탐색 및 연결 ──────────────────────────────────────────────
Write-Step "STM32 VCP 탐색 (VID 0483)"
$stmLine = $devices | Where-Object {
    $_ -match "0483:" -or $_ -match "STM32" -or $_ -match "STLink" -or $_ -match "Virtual COM"
}

if ($stmLine) {
    $busid = ($stmLine[0] -split '\s+')[0]
    Write-Host "STM32 발견: BUSID=$busid"
    Write-Host "  → WSL2에 연결 중..."
    & usbipd bind   --busid $busid 2>&1 | Out-Null
    & usbipd attach --wsl --busid $busid
    Write-Host "  ✓ STM32 VCP WSL2 연결 완료" -ForegroundColor Green
} else {
    Write-Host "  ⚠ STM32를 찾을 수 없습니다. USB 케이블을 확인하세요." -ForegroundColor Yellow
}

# ── Intel RealSense 자동 탐색 및 연결 ────────────────────────────────────────
Write-Step "Intel RealSense 탐색 (VID 8086)"
$rsLines = $devices | Where-Object {
    $_ -match "8086:" -or $_ -match "RealSense" -or $_ -match "Intel.*Depth"
}

if ($rsLines) {
    foreach ($line in $rsLines) {
        $busid = ($line -split '\s+')[0]
        Write-Host "RealSense 발견: BUSID=$busid  ($line)"
        Write-Host "  → WSL2에 연결 중..."
        & usbipd bind   --busid $busid 2>&1 | Out-Null
        & usbipd attach --wsl --busid $busid
        Write-Host "  ✓ RealSense WSL2 연결 완료" -ForegroundColor Green
    }
} else {
    Write-Host "  ⚠ RealSense를 찾을 수 없습니다." -ForegroundColor Yellow
}

# ── WSL2에서 장치 확인 안내 ───────────────────────────────────────────────────
Write-Host ""
Write-Host "WSL2 터미널에서 확인:" -ForegroundColor Cyan
Write-Host "  ls /dev/ttyACM*     # STM32 VCP"
Write-Host "  ls /dev/video*      # RealSense"
Write-Host "  rs-enumerate-devices  # RealSense SDK 확인"
