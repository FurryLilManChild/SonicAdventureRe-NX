#!/bin/sh
# SonicAdventureRe-nx: NATIVE_LOWER for the decomp's native slice. Lowers the references in one
# module's IR to 32-bit integers (Gep32, then ref32_int.py), and with RETARGET set, retargets it.
#   NATIVE_LOWER="sh /path/to/lower.sh" [RETARGET=aarch64-unknown-linux-gnu]
set -e
here=$(dirname "$0")
plugin=${GEP32_PLUGIN:-$here/../../build/ref32/Gep32.so}
opt -load-pass-plugin="$plugin" -passes=gep32 "$1" -S -o "$1.tmp"
mv "$1.tmp" "$1"
if [ -n "$RETARGET" ]; then
    python3 "$here/ref32_int.py" "$1" --retarget "$RETARGET"
else
    python3 "$here/ref32_int.py" "$1"
fi
