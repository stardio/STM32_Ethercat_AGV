#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[BUILD] GCC check:"
arm-none-eabi-gcc --version || { echo "GCC NOT FOUND"; exit 1; }

cd "$SCRIPT_DIR"

echo "[BUILD] cmake configure..."
cmake --preset Debug 2>&1 || { echo "CONFIGURE FAILED"; exit 1; }

echo "[BUILD] cmake build..."
cmake --build --preset Debug 2>&1 || { echo "BUILD FAILED"; exit 1; }

echo "[BUILD] DONE"
ls build/Debug/Appli/*.elf 2>/dev/null && echo "ELF OK" || echo "ELF NOT FOUND"
