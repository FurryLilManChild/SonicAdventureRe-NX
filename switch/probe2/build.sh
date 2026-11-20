#!/bin/sh
# Builds probe 2 (SonicAdventureRe-nx): the payload linked for windows 5 and 9, made an image by
# switch/tools/mkimage.py, built into the loader, packaged as sare_probe.nro.
set -e
cd "$(dirname "$0")"
DKP=${DEVKITPRO:-/opt/devkitpro}
A64=${DEVKITA64_LIBS:-/opt/devkitpro-win/devkitA64}
GCC=$(ls -d "$A64"/lib/gcc/aarch64-none-elf/* | tail -1)
TOOLS=${SWITCH_TOOLS:-$HOME/bin}
CFLAGS="--target=aarch64-none-elf -march=armv8-a+crc+crypto -mtune=cortex-a57 -O2 -g -Wall -D__SWITCH__
    -ffunction-sections -isystem $DKP/libnx/include -isystem $A64/aarch64-none-elf/include"
LIBS="-L$DKP/libnx/lib -L$A64/aarch64-none-elf/lib -L$GCC --start-group -lnx -lc -lm -lgcc -lsysbase --end-group"
mkdir -p build
# the payload, at a fixed address: not PIE
clang $CFLAGS -fno-pic -c payload/main.c -o build/payload.o
for w in 5 9; do
    aarch64-linux-gnu-ld -T ../ld/image.ld --defsym=IMAGE_BASE=$(printf '%#x' $((w * 4294967296 + 0x400000))) \
        -no-pie --gc-sections -z now --build-id=none "$GCC/crti.o" "$GCC/crtbegin.o" build/payload.o $LIBS \
        "$GCC/crtend.o" "$GCC/crtn.o" -o build/payload-$w.elf
done
python3 ../tools/mkimage.py build/payload-5.elf build/payload-9.elf build/payload.img --symbol probe_handoff
# the loader, an ordinary NRO
clang $CFLAGS -fPIE -DPAYLOAD_PATH="\"$PWD/build/payload.img\"" -c loader/main.c -o build/loader.o
aarch64-linux-gnu-ld -T ../ld/switch.ld -pie --no-dynamic-linker --gc-sections -z text -z now --build-id=sha1 \
    --wrap=envSetup "$GCC/crti.o" "$GCC/crtbegin.o" build/loader.o $LIBS "$GCC/crtend.o" "$GCC/crtn.o" \
    -o build/sare_probe.elf
"$TOOLS/nacptool" --create "SA Re probe" "SonicAdventureRe-nx" "0.0.2" build/sare_probe.nacp
"$TOOLS/elf2nro" build/sare_probe.elf sare_probe.nro --nacp=build/sare_probe.nacp
sha256sum sare_probe.nro
