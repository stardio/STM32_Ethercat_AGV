# =============================================================================
# 01_install_wsl2.ps1  —  WSL2 + Ubuntu 22.04 + usbipd-win 설치
#
# 실행 방법 (관리자 PowerShell):
#   Set-ExecutionPolicy Bypass -Scope Process -Force
#   .\01_install_wsl2.ps1
#
# 완료 후 PC 재시작 → 02_setup_ubuntu.ps1 실행
# =============================================================================

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "`n[$msg]" -ForegroundColor Cyan }

# ── 관리자 권한 확인 ──────────────────────────────────────────────────────────
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "관리자 권한으로 PowerShell을 실행해주세요."
    exit 1
}

# ── WSL 기능 활성화 ───────────────────────────────────────────────────────────
Write-Step "WSL + VirtualMachinePlatform 기능 활성화"
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart

# ── WSL2 커널 업데이트 ─────────────────────────────────────────────────────────
Write-Step "WSL2 Linux 커널 업데이트 패키지 다운로드"
$kernelUrl  = "https://wslstorestorage.blob.core.windows.net/wslblob/wsl_update_x64.msi"
$kernelPath = "$env:TEMP\wsl_update_x64.msi"
Invoke-WebRequest -Uri $kernelUrl -OutFile $kernelPath -UseBasicParsing
Start-Process msiexec.exe -Wait -ArgumentList "/i `"$kernelPath`" /quiet"
Remove-Item $kernelPath

# ── WSL2 기본 버전 설정 ────────────────────────────────────────────────────────
Write-Step "WSL 기본 버전 2로 설정"
wsl --set-default-version 2

# ── Ubuntu 22.04 설치 ─────────────────────────────────────────────────────────
Write-Step "Ubuntu 22.04 LTS 설치"
wsl --install -d Ubuntu-22.04
# 설치 중 사용자명/비밀번호 입력 창이 열립니다

# ── usbipd-win 설치 (winget) ──────────────────────────────────────────────────
Write-Step "usbipd-win 설치 (USB → WSL 공유)"
$usbipdCheck = winget list --id dorssel.usbipd-win 2>&1
if ($usbipdCheck -notmatch "dorssel") {
    winget install --id dorssel.usbipd-win --accept-source-agreements --accept-package-agreements
} else {
    Write-Host "usbipd-win 이미 설치됨"
}

Write-Host "`n" -NoNewline
Write-Host "============================================================" -ForegroundColor Green
Write-Host "  설치 완료!" -ForegroundColor Green
Write-Host "  다음 단계:" -ForegroundColor Green
Write-Host "  1. PC 재시작" -ForegroundColor Yellow
Write-Host "  2. Ubuntu 22.04 창에서 사용자명/비밀번호 설정" -ForegroundColor Yellow
Write-Host "  3. 관리자 PowerShell에서 02_setup_ubuntu.ps1 실행" -ForegroundColor Yellow
Write-Host "============================================================" -ForegroundColor Green
