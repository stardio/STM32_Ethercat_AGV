@echo off
setlocal

set ARMGCC=C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin
set CMAKE=C:\Program Files\CMake\bin
set NINJA=D:\EtherCat_STM32\Tools\ninja
set PATH=%ARMGCC%;%CMAKE%;%NINJA%;%PATH%

echo [BUILD] ARM GCC check:
arm-none-eabi-gcc --version
if errorlevel 1 (
    echo [ERROR] arm-none-eabi-gcc not found
    exit /b 1
)

echo [BUILD] Configuring CMake...
cd /d D:\Ethercat_STM32SP
cmake --preset Debug 2>&1
if errorlevel 1 (
    echo [ERROR] CMake configure failed
    exit /b 1
)

echo [BUILD] Building...
cmake --build --preset Debug 2>&1
if errorlevel 1 (
    echo [ERROR] Build failed
    exit /b 1
)

echo [BUILD] SUCCESS
dir build\Debug\*.elf 2>&1
