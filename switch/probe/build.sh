#!/bin/sh
# Builds sare_probe.nro: clang for the code, binutils' AArch64 ld with switch/ld/switch.ld, libnx
# and newlib from devkitPro, and switch-tools' nacptool and elf2nro (switch/tools). SonicAdventureRe-nx.
set -e
cd "$(dirname "$0")"
DKP=${DEVKITPRO:-/opt/devkitpro}
A64=${DEVKITA64_LIBS:-/opt/devkitpro-win/devkitA64}
GCC=$(ls -d "$A64"/lib/gcc/aarch64-none-elf/* | tail -1)
TOOLS=${SWITCH_TOOLS:-$HOME/bin}
mkdir -p build
clang --target=aarch64-none-elf -march=armv8-a+crc+crypto -mtune=cortex-a57 -fPIE -O2 -g -Wall \
    -D__SWITCH__ -ffunction-sections -isystem "$DKP/libnx/include" -isystem "$A64/aarch64-none-elf/include" \
    -c source/main.c -o build/main.o
aarch64-linux-gnu-ld -T ../ld/switch.ld -pie --no-dynamic-linker --gc-sections -z text -z now --build-id=sha1 \
    "$GCC/crti.o" "$GCC/crtbegin.o" build/main.o -L"$DKP/libnx/lib" -L"$A64/aarch64-none-elf/lib" -L"$GCC" \
    --start-group -lnx -lc -lm -lgcc -lsysbase --end-group "$GCC/crtend.o" "$GCC/crtn.o" -o build/sare_probe.elf
"$TOOLS/nacptool" --create "SA Re probe" "SonicAdventureRe-nx" "0.0.1" build/sare_probe.nacp
"$TOOLS/elf2nro" build/sare_probe.elf sare_probe.nro --nacp=build/sare_probe.nacp
sha256sum sare_probe.nro
