# SonicAdventureRe-nx: source this to build the decomp's native slice for the Switch (Horizon):
#   . tools/a64/env-nx.sh && cd decomp && make -j2 <objects>
# The domain's number given to the decomp's tools, 0x5a5a00000000, is a marker no ordinary constant
# comes near: Gep32 swaps every constant near it for native_domain_base plus an offset (code) or
# the link window's number (32-bit references in static data), and C reaches the symbol
# (nx/domain.h). The image links for window 5 (NATIVE_LINK_DOMAIN).
# A64_HERE names this folder where the shell can't tell a sourced file's place (sh)
here=${A64_HERE:-$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)}
export PATH="$here:$PATH"
export CXX="$here/clang++"
export A64_SYSTEM=nx
export NATIVE_DOMAIN=0x5a5a00000000
export NATIVE_LINK_DOMAIN=0x500000000
export NATIVE_LOWER="env RETARGET=aarch64-none-elf GEP32_DOMAIN=0x5a5a00000000 GEP32_LINK_WINDOW=0x500000000 sh $here/../ref32/lower.sh"
