#!/bin/sh
# SonicAdventureRe-nx: the same Switch build from the decomp's zero bodies, for switch/tools/strip.py.
#
# The decomp writes each body of data the game's C includes from the disc under build/gen/, and a
# zero one of the same size under build/stub/ (its stub builds, which need no disc). This makes a
# copy of decomp/ beside it (../sare-stub), its files hard links to decomp/'s, except: build/gen's
# bodies are build/stub's, and its objects are its own (compiled anew, never written over
# decomp/'s). switch/build.sh then builds that copy into switch/build-stub/. The two images differ
# exactly where a byte came from the disc's bodies (the content rows, build/gen/*/content.c, are
# the same in both: strip.py finds those in the disc's files).
#
#   switch/tools/stub_build.sh      (after switch/build.sh, from the same commit)
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
S="$root/../sare-stub"
cd "$root"
if [ ! -d "$S" ]; then
    echo "== the copy"
    mkdir -p "$S"
    # the sources and tables linked; build/engine copied whole (make writes there), less the objects
    for d in decomp/* decomp/.[!.]*; do
        [ -e "$d" ] || continue
        [ "$(basename "$d")" = build ] && continue
        cp -al "$d" "$S/"
    done
    mkdir -p "$S/build"
    for d in decomp/build/*; do
        n=$(basename "$d")
        [ "$n" = engine ] && continue
        cp -al "$d" "$S/build/$n"
    done
    mkdir -p "$S/build/engine"
    for d in decomp/build/engine/*; do
        n=$(basename "$d")
        if [ "$n" = native ]; then
            mkdir -p "$S/build/engine/native"
            for f in decomp/build/engine/native/*; do
                case "$(basename "$f")" in
                    obj|play|play.lacking|play.stripped|*.o|*.r) ;;
                    *) cp -a "$f" "$S/build/engine/native/" ;;
                esac
            done
        else
            cp -a "$d" "$S/build/engine/$n"
        fi
    done
fi
echo "== the zero bodies in place of the disc's"
n=0
(cd decomp/build/stub && find . -name '*.inc') | while read -r f; do
    if [ -f "$S/build/gen/$f" ]; then
        if ! cmp -s "decomp/build/stub/$f" "$S/build/gen/$f"; then
            rm -f "$S/build/gen/$f"
            cp -p "decomp/build/stub/$f" "$S/build/gen/$f"
            touch "$S/build/gen/$f"
        fi
    fi
done
echo "  $(cd decomp/build/stub && find . -name '*.inc' | wc -l) bodies"
# the float tables: the native build's float pass reads a body's numbers, and the decomp's stub
# for them is a bare 0
python3 switch/tools/stub_floats.py decomp/build/gen decomp/build/stub "$S/build/gen"
SARE_DECOMP=../sare-stub SARE_BUILD="$root/switch/build-stub" sh switch/build.sh --any-commit
