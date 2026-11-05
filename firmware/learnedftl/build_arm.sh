#!/bin/bash
# Compile run-lftl with the same compiler and flags SDK's generated makefile uses,
# without opening the SDK workspace. For checking the code builds; flash from SDK.
#
# Usage: build_arm.sh <path to cosm-plus-sys.sdk> <output dir> [extra compiler flags]
set -e

SDK_WS="$1"
OUT="$2"
shift 2

GNU=/c/Xilinx/SDK/2019.1/gnu/aarch32/nt/gcc-arm-none-eabi/bin
BSP="$SDK_WS/run-gftl3_bsp/ps7_cortexa9_0"
SRC="$SDK_WS/run-lftl/src"
ARCH="-mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard"

mkdir -p "$OUT"
rm -f "$OUT"/*.o

for f in "$SRC"/*.c "$SRC"/nvme/*.c; do
	"$GNU/arm-none-eabi-gcc" -Wall -O0 -g3 -c -fmessage-length=0 $ARCH -I"$BSP/include" "$@" -o "$OUT/$(basename "$f" .c).o" "$f" &
done
wait

cd "$OUT"
"$GNU/arm-none-eabi-gcc" $ARCH -Wl,-build-id=none -specs="$SRC/Xilinx.spec" -Wl,-T -Wl,"$SRC/lscript.ld" -L"$BSP/lib" \
	-o run-lftl.elf ./*.o -Wl,--start-group,-lxil,-lgcc,-lc,--end-group
"$GNU/arm-none-eabi-size" run-lftl.elf
