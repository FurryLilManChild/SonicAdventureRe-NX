# SonicAdventureRe-nx: source this to build the decomp's native slice for AArch64 Linux:
#   . tools/a64/env.sh && cd decomp && make -j2 native-objects-or-target
here=$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)
export PATH="$here:$PATH"
export CXX="$here/clang++"
export NATIVE_DOMAIN=0x100000000
export NATIVE_LOWER="env RETARGET=aarch64-unknown-linux-gnu sh $here/../ref32/lower.sh"
