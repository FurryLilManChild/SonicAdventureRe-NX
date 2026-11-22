#!/bin/sh
# SonicAdventureRe-nx: the Switch build. Builds GitHub's latest commit only (it stops otherwise),
# and leaves in out/sd/switch/SonicAdventureRe-nx/: SonicAdventureRe-nx.nro (the loader),
# SonicAdventureRe-nx.img (the game's image, switch/tools/mkimage.py) and SonicAdventureRe-nx.elf
# (its symbol table, which the game reads; debug info stripped).
#
#   switch/build.sh [--any-commit]
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
if [ "$1" != --any-commit ]; then
    git fetch -q origin
    if [ -n "$(git status --porcelain --untracked-files=no)" ] || [ "$(git rev-parse HEAD)" != "$(git rev-parse origin/main)" ]; then
        echo "switch/build.sh: builds only GitHub's latest commit, clean: commit and push first" >&2
        exit 1
    fi
fi
commit=$(git rev-parse --short HEAD)
DKP=${DEVKITPRO:-/opt/devkitpro}
A64=${DEVKITA64_LIBS:-/opt/devkitpro-win/devkitA64}
GCC=$(ls -d "$A64"/lib/gcc/aarch64-none-elf/* | tail -1)
TOOLS=${SWITCH_TOOLS:-$HOME/bin}
NVK=${NVK_SDK:-/opt/nvk-sdk}
PORTLIBS=${PORTLIBS:-$DKP/portlibs/switch}
B="$root/switch/build"
N="$root/decomp/build/engine/native"
mkdir -p "$B"

sh tools/apply_patches.sh
[ -f build/ref32/Gep32.so ] || sh tools/ref32/build.sh
[ -x "$TOOLS/elf2nro" ] || sh switch/tools/build_switch_tools.sh "$TOOLS"

echo "== the Switch runtime (build $commit)"
CF="--target=aarch64-none-elf -march=armv8-a+crc+crypto -mtune=cortex-a57 -O2 -g -Wall -Wextra -D__SWITCH__
    -ffunction-sections -isystem $DKP/libnx/include -isystem $A64/aarch64-none-elf/include -DNX_BUILD=\"$commit\""
clang $CF -fno-pic -c switch/runtime/nx_main.c -o "$B/nx_main.o"
clang $CF -fno-pic -c switch/runtime/nx_domain.c -o "$B/nx_domain.o"
clang $CF -fno-pic -c switch/runtime/nx_libc.c -o "$B/nx_libc.o"
clang $CF -fno-pic -c switch/runtime/nx_posix_stubs.c -o "$B/nx_posix_stubs.o"
clang $CF -fno-pic -c switch/runtime/nx_capture.c -o "$B/nx_capture.o"
clang $CF -fno-pic -c switch/runtime/nx_overlay.c -o "$B/nx_overlay.o"
clang $CF -fno-pic -c switch/runtime/nx_trace.c -o "$B/nx_trace.o"
clang $CF -fno-pic -c switch/runtime/nx_menu.c -o "$B/nx_menu.o"
clang $CF -fno-pic -c switch/runtime/nx_english.c -o "$B/nx_english.o"
clang --target=aarch64-none-elf -c switch/runtime/nx_base.S -o "$B/nx_base.o"

echo "== the game's objects (the decomp's native slice, for Horizon)"
# the PC backend's and SA1 client's objects lie outside build/engine/native, where make's request
# record doesn't reach: they are rebuilt when they were built for another system
if [ "$(cat decomp/build/engine/replay/.system 2>/dev/null)" != nx ]; then
    rm -f decomp/build/engine/replay/*.o decomp/build/engine/sa1/*.o decomp/build/engine/runtime/*.o
    mkdir -p decomp/build/engine/replay
    echo nx > decomp/build/engine/replay/.system
fi
A64_HERE="$root/tools/a64" . tools/a64/env-nx.sh
export NATIVE_STUB_LIBRARIES="$A64/aarch64-none-elf/lib/libc.a $A64/aarch64-none-elf/lib/libm.a \
$A64/aarch64-none-elf/lib/libsysbase.a $DKP/libnx/lib/libnx.a $GCC/libgcc.a $B/nx_main.o $B/nx_domain.o $B/nx_libc.o $B/nx_base.o"
# the objects of the decomp's link, which make -n names (its own link is Linux's, and isn't run)
link=$(cd decomp && make -n build/engine/native/play 2>/dev/null | grep -- " -o build/engine/native/play " | tail -1)
[ -n "$link" ] || { echo "switch/build.sh: no link command from make -n" >&2; exit 1; }
targets=$(printf '%s\n' $link | grep '\.o$' | grep -v -e 'Scrt1\|crti\|crtbegin\|crtend\|crtn')
if ! ( cd decomp && make -j${JOBS:-2} $targets > "$B/make.log" 2>&1 ); then
    grep -e "error" -e "\*\*\*" "$B/make.log" | head -20 >&2
    echo "switch/build.sh: the objects failed (switch/build/make.log)" >&2
    exit 1
fi
# the link line again, now that the tables it reads exist
link=$(cd decomp && make -n build/engine/native/play 2>/dev/null | grep -- " -o build/engine/native/play " | tail -1)
objects=$(printf '%s\n' $link | grep '\.o$' | grep -v -e 'Scrt1\|crti\|crtbegin\|crtend\|crtn' | sed "s|^build/|$root/decomp/build/|")
wraps=$(printf '%s\n' $link | grep -- '--wrap=' | sed 's|^-Wl,||')
aliases=$(awk '{ printf " --defsym=%s=%s", $1, $2 }' "$N/aliases.txt")
# the window is switch/runtime/window_nx.cpp's (a VI surface for NVK, libnx's pad and audio
# renderer), in place of the decomp's headless window_none
"$CXX" -std=c++17 -O2 -g -Wall -Wextra -Idecomp/engine/include -Idecomp/engine/pc -isystem "$NVK/include" \
    -c switch/runtime/window_nx.cpp -o "$B/window_nx.o"
printf '%s\n' $objects | grep -q '/window_none\.o$' || { echo "switch/build.sh: no window_none.o in the link" >&2; exit 1; }
objects=$(printf '%s\n' $objects | sed "s|^.*/window_none\.o$|$B/window_nx.o|")
# runner.c's main is the game's; switch/runtime/nx_main.c's main runs it
aarch64-linux-gnu-objcopy --redefine-sym main=native_main "$N/obj/engine/native/runner.o" "$B/runner.o"
objects=$(printf '%s\n' $objects | sed "s|$N/obj/engine/native/runner.o|$B/runner.o|")

echo "== the link, for window 5, with its relocations kept"
aarch64-linux-gnu-ld -T switch/ld/image.ld --defsym=IMAGE_BASE=0x500400000 --defsym=syCacheICI_P2_=0x5b0000000 \
    $aliases $wraps --wrap=_exit --wrap=exit --wrap=native_hook_pad_scan --wrap=native_hook_pad_scanned --wrap=sa1_record_sprite --wrap=sa1_record_screen_strip --wrap=sa1_record_text --wrap=sa1_record_triangles --wrap=native_hook_voice_file --wrap=native_hook_event_text --wrap=native_hook_event_text_next --wrap=native_hook_disc_file --wrap=native_hook_hint_parts --wrap=loader_read_prs -no-pie -z now --build-id=none --emit-relocs -Map="$B/SonicAdventureRe-nx.map" \
    "$GCC/crti.o" "$GCC/crtbegin.o" $objects "$B/nx_main.o" "$B/nx_domain.o" "$B/nx_libc.o" "$B/nx_base.o" "$B/nx_posix_stubs.o" "$B/nx_capture.o" "$B/nx_overlay.o" "$B/nx_trace.o" "$B/nx_menu.o" "$B/nx_english.o" \
    -L"$DKP/libnx/lib" -L"$A64/aarch64-none-elf/lib" -L"$GCC" \
    --start-group "$NVK/lib/libvulkan.a" "$NVK/lib/libexpat.a" "$NVK/lib/libzstd.a" "$PORTLIBS/lib/libz.a" \
    -lstdc++ -lm -lnx -lc -lgcc -lsysbase --end-group "$GCC/crtend.o" "$GCC/crtn.o" \
    -o "$B/SonicAdventureRe-nx.full.elf"
python3 switch/tools/mkimage.py "$B/SonicAdventureRe-nx.full.elf" "$B/SonicAdventureRe-nx.img"
# the game reads its symbol table from this file (symbols.c): the table alone, the rest as NOBITS
aarch64-linux-gnu-objcopy --strip-debug "$B/SonicAdventureRe-nx.full.elf" "$B/SonicAdventureRe-nx.stripped.elf"
aarch64-linux-gnu-objcopy --only-keep-debug "$B/SonicAdventureRe-nx.stripped.elf" "$B/SonicAdventureRe-nx.elf"

echo "== the loader"
clang $CF -fPIE -c switch/loader/main.c -o "$B/loader.o"
aarch64-linux-gnu-ld -T switch/ld/switch.ld -pie --no-dynamic-linker --gc-sections -z text -z now --build-id=sha1 \
    --wrap=envSetup "$GCC/crti.o" "$GCC/crtbegin.o" "$B/loader.o" -L"$DKP/libnx/lib" -L"$A64/aarch64-none-elf/lib" \
    -L"$GCC" --start-group -lnx -lc -lm -lgcc -lsysbase --end-group "$GCC/crtend.o" "$GCC/crtn.o" -o "$B/loader.elf"
"$TOOLS/nacptool" --create "SonicAdventureRe-nx" "FurryLilManChild" "0.0.1-$commit" "$B/loader.nacp"
# the icon is the owner's image, kept out of git (assets/ is ignored); a copy lies in his
# C:\SonicAdventureRe-nx\assets\icon.jpg (256x256 JPEG)
if [ -f assets/icon.jpg ]; then
    "$TOOLS/elf2nro" "$B/loader.elf" "$B/SonicAdventureRe-nx.nro" --nacp="$B/loader.nacp" --icon=assets/icon.jpg
else
    echo "switch/build.sh: no assets/icon.jpg, so the NRO has no icon (copy it from the owner's assets folder)" >&2
    "$TOOLS/elf2nro" "$B/loader.elf" "$B/SonicAdventureRe-nx.nro" --nacp="$B/loader.nacp"
fi

out="$root/out/sd/switch/SonicAdventureRe-nx"
mkdir -p "$out"
cp "$B/SonicAdventureRe-nx.nro" "$B/SonicAdventureRe-nx.img" "$B/SonicAdventureRe-nx.elf" "$out/"
echo "== the sweep"
python3 switch/tools/preflight.py "$B" "$commit"
echo "== built $commit"
( cd "$out" && sha256sum SonicAdventureRe-nx.nro SonicAdventureRe-nx.img SonicAdventureRe-nx.elf && ls -la )
