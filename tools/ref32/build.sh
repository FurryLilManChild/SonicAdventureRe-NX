#!/bin/sh
# Builds the Gep32 opt plugin into build/ref32/Gep32.so (needs llvm-18-dev). SonicAdventureRe-nx.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out="$here/../../build/ref32"
mkdir -p "$out"
clang++ -shared -fPIC -O2 $(llvm-config-18 --cxxflags) -o "$out/Gep32.so" "$here/Gep32.cpp" $(llvm-config-18 --ldflags)
echo "built $out/Gep32.so"
