#!/bin/sh
# SonicAdventureRe-nx: the release folder, with no game data in it.
#
#   sh switch/tools/make_release.sh
#
# Two ways to strip the disc's data out (SARE_STRIP):
#   update (the default once out/strip-cache/ holds a last strip): switch/tools/strip_update.py, from
#          the last strip's results; needs switch/build only (no stub build). The audit must find
#          no more of the disc than the last strip left, else it stops: then use full.
#   full:  switch/tools/strip.py; needs switch/build and switch/build-stub
#          (switch/tools/stub_build.sh) from the same commit.
# Either way the result is kept in out/strip-cache/ for the next update.
# Needs python3 with numpy and pyelftools. Writes out/release/switch/SonicAdventureRe-nx/:
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
C=out/strip-cache
mode=${SARE_STRIP:-}
if [ -z "$mode" ]; then
    if [ -f "$C/last.full.elf" ] && [ -f "$C/last.img" ] && [ -f "$C/last.fill" ] && [ -f "$C/last.audit" ]; then
        mode=update
    else
        mode=full
    fi
fi
need="$B/SonicAdventureRe-nx.img $B/SonicAdventureRe-nx.full.elf $B/SonicAdventureRe-nx.map $B/SonicAdventureRe-nx.nro $B/SonicAdventureRe-nx.elf"
[ "$mode" = full ] && need="$need $S/SonicAdventureRe-nx.full.elf"
for f in $need; do
    [ -f "$f" ] || { echo "make_release.sh: missing $f" >&2; exit 1; }
done
rm -rf out/release
mkdir -p "$R/disc" "$R/us_disc"
echo "== strip ($mode)"
start=$(date +%s)
if [ "$mode" = full ]; then
    python3 switch/tools/strip.py "$B/SonicAdventureRe-nx.img" "$B/SonicAdventureRe-nx.full.elf" "$B/SonicAdventureRe-nx.map" \
        "$S/SonicAdventureRe-nx.full.elf" decomp "$R/SonicAdventureRe-nx.img" "$R/SonicAdventureRe-nx.fill"
    mv "$R/SonicAdventureRe-nx.fill.left.txt" out/release/ 2>/dev/null || true
else
    python3 switch/tools/strip_update.py "$C/last.full.elf" "$C/last.img" "$C/last.fill" \
        "$B/SonicAdventureRe-nx.img" "$B/SonicAdventureRe-nx.full.elf" "$B/SonicAdventureRe-nx.map" decomp \
        "$R/SonicAdventureRe-nx.img" "$R/SonicAdventureRe-nx.fill"
fi
echo "== strip done in $(( $(date +%s) - start ))s"
python3 switch/tools/audit.py "$R/SonicAdventureRe-nx.img" decomp "$B/SonicAdventureRe-nx.full.elf" | tee out/release/audit.txt
found=$(sed -n 's/^audit: .* checked, \([0-9]*\) found on the disc$/\1/p' out/release/audit.txt)
[ -n "$found" ] || { echo "make_release.sh: no count from the audit" >&2; exit 1; }
if [ "$mode" = update ] && [ "$found" -gt "$(cat "$C/last.audit")" ]; then
    echo "make_release.sh: the audit finds $found windows of the disc, the last strip left $(cat "$C/last.audit"):" \
        "the update strip missed some; run SARE_STRIP=full" >&2
    exit 1
fi
# the next update starts from this strip
mkdir -p "$C"
cp "$B/SonicAdventureRe-nx.full.elf" "$C/last.full.elf"
cp "$R/SonicAdventureRe-nx.img" "$C/last.img"
cp "$R/SonicAdventureRe-nx.fill" "$C/last.fill"
echo "$found" > "$C/last.audit"
cp "$B/SonicAdventureRe-nx.nro" "$R/"
aarch64-linux-gnu-objcopy --remove-relocations='*' "$B/SonicAdventureRe-nx.elf" "$R/SonicAdventureRe-nx.elf"
cat > "$R/disc/PUT_JAPANESE_DISC_HERE.txt" <<'NOTE'
Put your Japanese Sonic Adventure disc image (HDR-0001, version 1.007) in this folder:
a .gdi with its track files, or a .cue with its .bin files.
The first launch unpacks it here and builds the game from it (a few minutes, once).
NOTE
cat > "$R/us_disc/PUT_US_DISC_HERE.txt" <<'NOTE'
Optional, for English voices and text: put your US Sonic Adventure disc image in this folder
(a .gdi with its track files, or a .cue with its .bin files). The first launch unpacks it and
makes the english folder. Choose VOICES and TEXT in the port menu (Minus and Plus together).
NOTE
cp switch/release/README.txt "$R/README.txt"
cp COPYING "$R/COPYING.txt"
# nothing from the disc may be left: the image's header must say "to be filled"
python3 - "$R/SonicAdventureRe-nx.img" <<'EOF'
import struct, sys
h = struct.unpack_from('<8sQQQQIIIIQ', open(sys.argv[1], 'rb').read(64), 0)
assert h[0] == b'SAREIMG1' and h[7] == 1, 'the release image is not stripped'
EOF
ls -la "$R"
(cd "$R" && sha256sum SonicAdventureRe-nx.nro SonicAdventureRe-nx.img SonicAdventureRe-nx.fill SonicAdventureRe-nx.elf)

# the zip (switch/SonicAdventureRe-nx/ at its root), the files kept for reading crash reports, and
# the release notes' hashes
V=${SARE_VERSION:-0.5.0}
Z=out/release/SonicAdventureRe-nx-$V-switch.zip
(cd out/release && rm -f "SonicAdventureRe-nx-$V-switch.zip" && zip -q -r -9 "SonicAdventureRe-nx-$V-switch.zip" switch)
cp "$B/SonicAdventureRe-nx.full.elf" "out/release/release-$V.full.elf"
cp "$B/loader.elf" "out/release/release-$V.loader.elf"
sha256sum "$Z" "$R/SonicAdventureRe-nx.nro" > out/release/sha256.txt
cat out/release/sha256.txt
