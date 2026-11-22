#!/bin/sh
# SonicAdventureRe-nx: the release folder, with no game data in it.
#
#   sh switch/tools/make_release.sh
#
# Needs both builds: switch/build (switch/build.sh) and switch/build-stub (switch/tools/stub_build.sh),
# made from the same commit. Needs python3 with numpy and pyelftools. Writes out/release/switch/SonicAdventureRe-nx/:
#
#   SonicAdventureRe-nx.nro    the loader (it builds the game from your disc on first launch)
#   SonicAdventureRe-nx.img    the game with every byte from the disc zeroed (strip.py)
#   SonicAdventureRe-nx.fill   where each of those bytes is on the Japanese disc
#   SonicAdventureRe-nx.elf    the symbol table the game reads (relocations dropped)
#   disc/                      the player's Japanese disc files go here
#   us_disc/                   the player's US disc files go here (optional: English)
#   README.txt
set -e
cd "$(dirname "$0")/../.."
B=switch/build
S=switch/build-stub
R=out/release/switch/SonicAdventureRe-nx
for f in "$B/SonicAdventureRe-nx.img" "$B/SonicAdventureRe-nx.full.elf" "$B/SonicAdventureRe-nx.map" \
    "$B/SonicAdventureRe-nx.nro" "$B/SonicAdventureRe-nx.elf" "$S/SonicAdventureRe-nx.full.elf"; do
    [ -f "$f" ] || { echo "make_release.sh: missing $f" >&2; exit 1; }
done
rm -rf out/release
mkdir -p "$R/disc" "$R/us_disc"
python3 switch/tools/strip.py "$B/SonicAdventureRe-nx.img" "$B/SonicAdventureRe-nx.full.elf" "$B/SonicAdventureRe-nx.map" \
    "$S/SonicAdventureRe-nx.full.elf" decomp "$R/SonicAdventureRe-nx.img" "$R/SonicAdventureRe-nx.fill"
mv "$R/SonicAdventureRe-nx.fill.left.txt" out/release/ 2>/dev/null || true
python3 switch/tools/audit.py "$R/SonicAdventureRe-nx.img" decomp "$B/SonicAdventureRe-nx.full.elf"
cp "$B/SonicAdventureRe-nx.nro" "$R/"
aarch64-linux-gnu-objcopy --remove-relocations='*' "$B/SonicAdventureRe-nx.elf" "$R/SonicAdventureRe-nx.elf"
cat > "$R/disc/PUT_JAPANESE_DISC_FILES_HERE.txt" <<'EOF'
Copy every file of your Japanese Sonic Adventure disc (HDR-0001, version 1.007) into this folder:
1ST_READ.BIN beside this note, and the SONICADV folder with all its files.
The first launch builds the game from them (a few minutes) and saves it as
SonicAdventureRe-nx.built.img; later launches start straight away.
EOF
cat > "$R/us_disc/PUT_US_DISC_FILES_HERE.txt" <<'EOF'
Optional, for English voices and text: copy every file of your US Sonic Adventure disc into this
folder (its SONICADV folder with all its files). The first launch makes the english folder from
them. Choose VOICES and TEXT in the port menu (+ and - together).
EOF
cp switch/release/README.txt "$R/README.txt" 2>/dev/null || true
# nothing from the disc may be left: the image's header must say "to be filled"
python3 - "$R/SonicAdventureRe-nx.img" <<'EOF'
import struct, sys
h = struct.unpack_from('<8sQQQQIIIIQ', open(sys.argv[1], 'rb').read(64), 0)
assert h[0] == b'SAREIMG1' and h[7] == 1, 'the release image is not stripped'
EOF
ls -la "$R"
(cd "$R" && sha256sum SonicAdventureRe-nx.nro SonicAdventureRe-nx.img SonicAdventureRe-nx.fill SonicAdventureRe-nx.elf)
