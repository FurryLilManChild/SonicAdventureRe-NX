# SonicAdventureRe-nx 0.5.0 (needs testing)

A native Nintendo Switch port of Sonic Adventure (Dreamcast), built on reshaped's
[sonic-adventure](https://github.com/reshaped/sonic-adventure) decompilation: the game's own C
code compiled for the Switch, with a Vulkan renderer. Not an emulator.

0.5.0 is feature-ready but not yet played through. Please play and report problems.

No game data is included. On its first launch the port unpacks your own disc dumps and
rebuilds the game from them.

## What's in it

- Widescreen and 60 fps by default, at stock clocks.
- Resolution 1X, 1.5X or 2X, changed live in the port menu (Minus and Plus).
- TRANSLUCENCY: ACCURATE (per pixel, as the Dreamcast) or FAST (per triangle, quicker in
  smoke, dust and fog), changed live.
- English voices and text from your US disc, chosen separately (VOICES and TEXT).
- Reads disc images directly: .gdi with its tracks, or a Redump-style .cue with its .bins.

## What you need

- Atmosphere, and title takeover (hold R on a game): the Album is not enough.
- Your Japanese Sonic Adventure dump (HDR-0001, v1.007) in `switch/SonicAdventureRe-nx/disc/`.
- Optional, for English: your US dump (MK-51000, the original release or Rev A, whose files
  are identical) in `switch/SonicAdventureRe-nx/us_disc/`.
- About 5 GB free on the SD card for the first launch.

The port checks every file it uses from the Japanese disc and names any that don't match.
For reference, the Redump-style dump this release was made from:

| File | SHA-1 |
|---|---|
| Sonic Adventure (Japan) (Track 1).bin | 3723997027355996f4e281ca373163400e900d10 |
| Sonic Adventure (Japan) (Track 2).bin | 70ff7ffbfc1e66bdf5596578a1b2fb49f7dfb592 |
| Sonic Adventure (Japan) (Track 3).bin | c98509d77792be67791b15f762a2019aa85cfbba |

## English

In English: cutscene voices (1,869 lines), cutscene subtitles (1,054 of 1,137), character
voices during play (all 94 sound banks), menus, HUD, mission cards and tutorials (805 of 834
pictures), town and field messages, Tikal's hints (121 pages).

Not yet: 83 cutscene subtitles; 29 pictures the US release redrew at another size (dialog boxes,
file select, options, sound test, trial act select, memory card select, some tutorial pages);
Tikal's hints in Hot Shelter; text built into the program itself (such as the memory card
messages) has not been checked. The opening title screen stays Japanese on purpose.

## Known issues

- Heavy smoke, dust or fog can drop the frame rate a lot with TRANSLUCENCY ACCURATE: try FAST.
- Some scenes may not hold 60 fps everywhere. Cutscenes run at 30, as in the original.

## Reporting problems

Send `SonicAdventureRe-nx.log` from `sdmc:/switch/SonicAdventureRe-nx/`, and for a crash the
newest report from `sdmc:/atmosphere/crash_reports/` (say its file name). Say what you were
doing and your settings.

## Files

| File | SHA-256 |
|---|---|
| SonicAdventureRe-nx-0.5.0-switch.zip | ZIP_SHA |
| SonicAdventureRe-nx.nro | NRO_SHA |
