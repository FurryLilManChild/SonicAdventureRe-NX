#!/bin/sh
# Applies patches/decomp/*.patch to the decomp submodule, each only once (SonicAdventureRe-nx).
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
for p in "$root"/patches/decomp/*.patch; do
    if git -C "$root/decomp" apply --reverse --check "$p" 2>/dev/null; then
        echo "already applied: $(basename "$p")"
    else
        git -C "$root/decomp" apply "$p"
        echo "applied: $(basename "$p")"
    fi
done
