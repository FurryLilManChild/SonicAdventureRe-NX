# SonicAdventureRe-nx 0.5.3 (needs testing)

A native Nintendo Switch port of Sonic Adventure (Dreamcast), built on reshaped's
[sonic-adventure](https://github.com/reshaped/sonic-adventure) decompilation: the game's own C
code compiled for the Switch, with a Vulkan renderer. Not an emulator.

No game data is included. The port rebuilds the game from your own disc dumps.

## Faster see-through effects

- **Translucency costs far less.** The per-pixel way the port draws see-through surfaces (as the
  Dreamcast did) now does much less work on the Switch's GPU for the same picture. In Windy
  Valley's tornado, at stock clocks, the GPU's time for a frame went from 11.5 ms to 6.4 ms, and
  the frame rate there from about 41 to 60.
- **TRANSLUCENCY EFFECTS FAST** is a new setting, and the default for new installs: see-through
  surfaces stay per pixel, but smoke, dust and sparks are drawn the quick way, which keeps thick
  smoke and dust (such as the bridge before the tornado) at full speed. ACCURATE and FAST are
  still there.
- **If you are updating**, your settings keep the TRANSLUCENCY you had: set EFFECTS FAST in the
  port menu (Minus and Plus) for the recommended setting.

## Updating from an earlier version

1. Copy the `switch` folder from the zip to the root of your SD card, replacing the files already
   there. Keep `disc/`, `us_disc/`, `english/`, `settings.txt` and `card.bin`.
2. Launch. The first launch rebuilds the game from your unpacked disc (a short screen), without
   unpacking the discs or making the english folder again. Later launches start straight away.

New installs: see the README in the zip (your Japanese disc in `disc/`, optionally the US disc in
`us_disc/` for English).

## What you need

- Atmosphere, and title takeover (hold R on a game): the Album is not enough.
- Your Japanese Sonic Adventure dump (HDR-0001, v1.007) in `switch/SonicAdventureRe-nx/disc/`.
- Optional, for English: your US dump (MK-51000, the original release or Rev A) in
  `switch/SonicAdventureRe-nx/us_disc/`.

The port checks every file it uses from the Japanese disc and names any that don't match.
For reference, the Redump-style dump this release was made from:

| File | SHA-1 |
|---|---|
| Sonic Adventure (Japan) (Track 1).bin | 3723997027355996f4e281ca373163400e900d10 |
| Sonic Adventure (Japan) (Track 2).bin | 70ff7ffbfc1e66bdf5596578a1b2fb49f7dfb592 |
| Sonic Adventure (Japan) (Track 3).bin | c98509d77792be67791b15f762a2019aa85cfbba |

## Still not in English

83 cutscene subtitles; 29 pictures the US release redrew at another size (dialog boxes, file
select, options, sound test, trial act select, memory card select, some tutorial pages); Tikal's
hints in Hot Shelter; text built into the program itself (such as the memory card messages) has
not been checked. The opening title screen stays Japanese on purpose.

## Known issues

- Thick smoke and dust can still slow TRANSLUCENCY ACCURATE down: EFFECTS FAST fixes that.
- Some scenes may not hold 60 fps everywhere. Cutscenes run at 30, as in the original.
- With 60 FPS on, the idle line may come after about 15 seconds instead of 30.

## Reporting problems

Send `SonicAdventureRe-nx.log` from `sdmc:/switch/SonicAdventureRe-nx/`, and for a crash the
newest report from `sdmc:/atmosphere/crash_reports/` (say its file name). Say what you were
doing and your settings.

## Files

| File | SHA-256 |
|---|---|
| SonicAdventureRe-nx-0.5.3-switch.zip | ZIP_SHA |
| SonicAdventureRe-nx.nro | NRO_SHA |
