#!/bin/sh
# SonicAdventureRe-nx: switchbrew's nacptool and elf2nro built natively (devkitPro's copies on the
# cloud machine run under Wine, which broke). Needs gcc, git and liblz4-dev. Installs to $1
# (default ~/bin).
set -e
out=${1:-$HOME/bin}
work=$(mktemp -d)
git clone -q --depth 1 https://github.com/switchbrew/switch-tools.git "$work/st"
cd "$work/st/src"
mkdir -p "$out"
gcc -O2 -o "$out/nacptool" nacptool.c
gcc -O2 -o "$out/elf2nro" elf2nro.c romfs.c filepath.c sha256.c -llz4
echo "built $out/nacptool and $out/elf2nro"
