# SonicAdventureRe-nx

Version 0.5.3, needs testing.

A native Nintendo Switch port of Sonic Adventure (Dreamcast), built on reshaped's
sonic-adventure decompilation (https://github.com/reshaped/sonic-adventure): the game's own
C code, compiled for the Switch, with a Vulkan renderer. Not an emulator.

Unofficial fan project. Sonic Adventure belongs to SEGA. No game data is included: the port
rebuilds the game from your own disc dumps the first time it starts.

## Status
0.5.x means feature-ready but not yet played through from start to finish. Please play, and
report what goes wrong (see Reporting problems).

- Widescreen (16:9) and 60 fps by default, at the Switch's stock clocks.
- Resolution 1X (640x480), 1.5X or 2X, changed live in the port menu.
- English voices and text from your US disc on top of the Japanese game (see English below).
- Saves to card.bin in the port's folder.

## What you need
- A Switch running Atmosphere custom firmware.
- Your own dump of the Japanese Sonic Adventure disc (HDR-0001, version 1.007), as a .gdi with
  its track files or a Redump-style .cue with its .bin files. The decomp is built from this
  version; other versions are refused with a message saying which file differs.
- Optional, for English: your own dump of the US Sonic Adventure disc (MK-51000), the same way.
  The original release and Rev A both work: their files are identical.
- About 5 GB free on the SD card for the first launch (the disc images plus their unpacked
  files). The images can be deleted afterwards.

## Install
1. Copy the "switch" folder from this zip to the root of your SD card.
2. Copy your Japanese disc image (the .gdi or .cue and all its track files) into
   sdmc:/switch/SonicAdventureRe-nx/disc/
3. Optional: copy your US disc image the same way into
   sdmc:/switch/SonicAdventureRe-nx/us_disc/
4. Launch through title takeover: hold R while opening any game, then pick
   SonicAdventureRe-nx in the Homebrew Menu. The Album does not give it enough memory.

## Updating from an earlier version
1. Copy the `switch` folder from the zip to the root of your SD card, replacing the files
   already there. Keep `disc/`, `us_disc/`, `english/`, `settings.txt` and `card.bin` as they are.
2. Launch. The first launch rebuilds the game from your unpacked disc (a short "building the
   game from your disc" screen); it does not unpack the discs or make the english folder again.
   Later launches start straight away.

## First launch
The first launch shows its progress on screen while it:
1. unpacks the Japanese disc image (and the US one, if present) into the same folders,
2. makes the english folder from the US disc,
3. builds the game from the Japanese disc and saves it as SonicAdventureRe-nx.built.img.
This takes a few minutes, once. Later launches start straight away. Once it has worked, you can
delete the .gdi/.cue and track files from disc/ and us_disc/ to save space; keep the unpacked
files and extracted.txt.

If a disc file is missing or is not the expected version, the screen says which one.

## Port menu
Press Minus and Plus together in the game.
- WIDESCREEN: on (16:9) or off (4:3).
- 60 FPS: on, or off for the game's own 30.
- RESOLUTION: 1X, 1.5X, 2X. 2X may not hold 60 fps.
- TRANSLUCENCY: EFFECTS FAST (the default, recommended) draws see-through surfaces per pixel,
  as the Dreamcast did, but smoke, dust and sparks per triangle, which keeps them quick where
  they pile up. ACCURATE draws everything per pixel. FAST sorts everything per triangle, with
  visible mistakes possible where see-through surfaces cross (the Windy Valley tornado's
  edges, for one). It changes at once.
- VOICES and TEXT: English or Japanese, each on its own (English needs the US disc).
- CONTROLS: which Switch button presses each Dreamcast button (A, B, X, Y and the two
  triggers), and:
  - TALK (a tap of Minus by default): the character says its idle line for where it is.
  - D-PAD: LOOK (the default, as on the Dreamcast): stand still and press the D-pad to look
    round in first person; a face button or the stick ends it. MOVE: the D-pad walks.
  - CAMERA STICK: ON (the default): the right stick turns the camera left and right, as the
    Dreamcast's L and R triggers do. INVERTED swaps the directions; OFF leaves it to the triggers.
  - IDLE LINES: ON (the default): a character left standing for about 30 seconds says its
    line, as in Sonic Adventure DX.
  DEFAULTS puts back the usual layout. The menu itself always uses A, B and Minus + Plus.
- VOLUME, FPS COUNTER, CAPTURES (pictures for bug reports on stick clicks; costs speed).
- TRIALS: ALL OPEN lists every Trial stage and mini game for every character, without changing
  your save; AS SAVED (the default) shows what your save has cleared.
- QUIT GAME.
Settings are saved in settings.txt.

## English
The port runs the Japanese game (the one the decomp matches) and swaps in English from your US
disc. What is in English:
- Cutscene voices: every Japanese line that has an English recording plays it (1,869 lines).
  About 219 lines the US release cut or rewrote stay Japanese.
- Cutscene subtitles: 1,054 of 1,137.
- Character voices during play: all 94 sound banks in the 17 voice files.
- Menus, HUD, mission cards, tutorials and other on-screen pictures with text: 805 of 834.
- Town and field messages (Station Square, Mystic Ruins, the Past).
- Tikal's hints: in Mystic Ruins and the action stages (except Hot Shelter); Station Square's
  hint boxes, signs and characters' story hints (173 hint pages in all).

Not yet in English:
- 83 cutscene subtitles, where the English script splits the lines differently.
- 29 on-screen pictures that the US release redrew at a different size. They are in the dialog
  boxes, file select, options, sound test, trial act select, memory card select, the tutorials
  (a few pages for each character) and one common tutorial picture.
- Tikal's hints in Hot Shelter.
- The opening title screen stays Japanese on purpose (it reads Sonic Adventure).
- Text built into the game's program rather than its data files (for example the memory card
  messages) has not been checked and may still be Japanese.


## Known issues
- With TRANSLUCENCY ACCURATE, thick piles of smoke and dust can still drop the frame rate
  (for one, the bridge before Windy Valley's tornado). EFFECTS FAST, the default, fixes that.
- Some scenes may not hold 60 fps everywhere.
- Cutscenes run at 30 fps, as in the original.
- This build has not been played from start to finish.

## Reporting problems
Say what you were doing, and your settings (resolution, translucency, 60 fps on or off,
handheld or docked). Send:
- SonicAdventureRe-nx.log from sdmc:/switch/SonicAdventureRe-nx/
- for a crash, the newest report from sdmc:/atmosphere/crash_reports/ (say its file name).

## Building

The Switch build runs on Linux (devkitPro's devkitA64 and libnx, Mesa's NVK from the
danfromtico/switch-dev-legacy-vulkan image, clang 18). It needs the Japanese disc's files in
decomp/orig/ to build the decomp (see its README), and builds GitHub's latest commit only:

    sh switch/build.sh                      # test build, into out/sd/switch/SonicAdventureRe-nx/
    sh switch/tools/stub_build.sh           # the same game from the decomp's zero bodies
    SARE_RELEASE=1 sh switch/build.sh       # release build
    sh switch/tools/make_release.sh         # strips the disc's bytes, checks, zips (out/release/)

switch/tools/strip.py removes every byte that came from the disc and writes the fill table the
loader rebuilds it from; switch/tools/audit.py and fill_check.py check the result. The decomp is
pinned in decomp/, and our changes to it are in patches/decomp/.

## Credits and license
- reshaped and contributors: the sonic-adventure decompilation and its PC engine
  (https://github.com/reshaped/sonic-adventure).
- SA-nx (FurryLilManChild): the Switch window, input and sound code, and the port's design.
- devkitPro and libnx; Mesa's NVK Vulkan driver for the Switch.
- Sonic Adventure (c) SEGA. This port contains no SEGA game data.

This port's own code is licensed under the GNU General Public License, version 2 (COPYING).
The decompilation's code remains under its own terms.
