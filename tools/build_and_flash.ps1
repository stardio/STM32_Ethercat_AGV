param(
    [string]$Preset = "Debug",
    [string]$Target = "LCD_Test_Appli",
    [string]$BootTarget = "LCD_Test_Boot",
    [int]$Jobs = 4,
    [string]$Address = "0x70000000"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")
Set-Location $repoRoot

& (Join-Path $scriptDir "build.ps1") -Preset $Preset -Target $BootTarget -Jobs $Jobs
if ($LASTEXITCODE -ne 0) {
    throw "Boot build step failed."
}

$bootBinPath = "build\$Preset\Boot\$BootTarget.bin"
if (-not (Test-Path $bootBinPath)) {
    throw "Boot binary not found: $bootBinPath"
}

& 'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe' -c port=SWD -d $bootBinPath 0x08000000 -rst
if ($LASTEXITCODE -ne 0) {
    throw "Boot flash step failed."
}

& (Join-Path $scriptDir "build.ps1") -Preset $Preset -Target $Target -Jobs $Jobs
if ($LASTEXITCODE -ne 0) {
    throw "Build step failed."
}

$binPath = "build\$Preset\Appli\$Target.bin"
& (Join-Path $scriptDir "flash.ps1") -Preset $Preset -BinPath $binPath -Address $Address
if ($LASTEXITCODE -ne 0) {
    throw "Flash step failed."
}

Write-Host "[build+flash] done"
