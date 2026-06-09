#!/bin/bash
export PATH="/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/12.2 mpacbti-rel1/bin:/c/Program Files/CMake/bin:/d/EtherCat_STM32/Tools/ninja:$PATH"

echo "[BUILD] GCC check:"
arm-none-eabi-gcc --version || { echo "GCC NOT FOUND"; exit 1; }

cd /d/Ethercat_STM32SP

echo "[BUILD] cmake configure..."
cmake --preset Debug 2>&1 || { echo "CONFIGURE FAILED"; exit 1; }

echo "[BUILD] cmake build..."
cmake --build --preset Debug 2>&1 || { echo "BUILD FAILED"; exit 1; }

echo "[BUILD] DONE"
ls build/Debug/*.elf 2>/dev/null && echo "ELF OK" || echo "ELF NOT FOUND"
