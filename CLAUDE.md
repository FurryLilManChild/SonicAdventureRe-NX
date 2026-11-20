# SonicAdventureRe-nx: notes for Claude

Native Nintendo Switch port of **Sonic Adventure** (Dreamcast, Japanese release HDR-0001 v1.007)
built on [reshaped/sonic-adventure](https://github.com/reshaped/sonic-adventure), a complete
matching decompilation with a native PC game. Separate from SA-nx (the dream-recomp port, PAL and US
discs), which stays as it is. Owner: the owner (GitHub FurryLilManChild). Load the `switch-port-base`
skill before working here.

## Who does what

- Claude writes all code, scripts and configs, reads logs, and commits.
- the owner runs builds on his Windows PC when asked, tests on his PC and Switch, and moves Switch builds
  to the SD card himself. He writes no code.
- Claude reaches `C:\SonicAdventureRe-nx` through the device bridge. The Japanese disc is in
  `assets\Sonic Adventure (Japan)\` (Redump bin/cue).

## Hard rules

- Never use em dashes anywhere (chat, docs, commits). No decorative emoji.
- Never commit game data or anything generated from it: discs, extracted files, the decomp's
  `orig/` and `build/` (its `build/gen/` text tables come from the disc), saves, captures. Run
  `git status` before every commit.
- Commit as `FurryLilManChild <346934328+FurryLilManChild@users.noreply.github.com>`.
- The decomp stays pinned in the `decomp` submodule. Our changes to it go in
  `patches/decomp/*.patch`, applied idempotently by the build scripts. Follow its CONTRIBUTING.md
  inside it.

## Upstream and licences

| Upstream | Use | Licence |
|---|---|---|
| reshaped/sonic-adventure @ e9a6f8a | the game's C, the engine, the PC backend | Makefile, configs, `engine/`, `tools/`, `tests/`: MIT; the rest: none posted (the owner is fine building on it) |
| SA-nx / dream-recomp (ours) | Switch window, NVK, input and audio code | GPL-2.0 |

## Plan

1. AArch64: clang only supports `__ptr32` (the decomp's 32-bit game pointers) for x86, so the game's
   C is lowered to LLVM IR for x86-64, run through the decomp's passes with a domain base above 0,
   then retargeted to AArch64. Proven first as an AArch64 Linux build under qemu-aarch64, which must
   reach the same frames as the x86 build (Emerald Coast, 600 vsyncs: 396 frames).
2. Horizon runtime: the 4 GB domain (4 GB aligned, main RAM at 0x0c, 0x8c, 0xac via double
   mappings), thread switching without ucontext, disc read from the SD card.
3. Window (NVK), input and audio from SA-nx; first picture; then the usual play-test loop.
4. English layer for the Japanese game from the US disc: voices, banks, menus, town messages,
   subtitles.

## Layout

```
decomp/          submodule, pinned (reshaped/sonic-adventure)
patches/decomp/  our changes to it
tools/           our scripts (retarget, English layer)
switch/          the Horizon layer and NRO build
docs/progress-log.md   newest first, one entry per session with evidence
```
