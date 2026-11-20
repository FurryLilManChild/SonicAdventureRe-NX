# Progress log

Newest first. One entry per session, with evidence.

## 2026-11-20: start, feasibility

- reshaped/sonic-adventure e9a6f8a builds its PC game (`make pc`) from the owner's Japanese disc
  (track03 sha256 4ac0e256cf91520f794d7f831c193847b16397da3bde5c890e04e5330499c76c) in the cloud
  in 16 min on 2 cores, clang 18. `make native`: Emerald Coast, 600 vsyncs, 396 game frames, exit
  0. `play --headless build/dc --pictures DIR` on lavapipe renders the picture correctly.
- The same with `NATIVE_DOMAIN=0x100000000` (the game's 4 GB away from address 0, as the Switch
  will need): 396 frames, exit 0.
- AArch64: clang supports `__ptr32` only for x86 (a test struct is 8 bytes on x86-64, wrong on
  aarch64-linux-gnu, aarch64-none-elf, aarch64-windows-msvc). Lowering 120 random game sources to
  x86-64 IR, running refs.py with base 4 GB, then swapping the triple and datalayout to AArch64:
  96 compile. The other 24 all fail on one form, a static initializer holding a reference
  (`inttoptr (trunc (ptrtoint @sym - 4 GB))`), which a load-time relocation can answer. One of the
  120 uses varargs.
- The PC backend asks Vulkan for 1.1, the swapchain and fragmentStoresAndAtomics. SDL3 is used in
  window.cpp, input.cpp and sound.cpp.
- US disc (v1.005) against the Japanese code: of about 3,000 game functions, 10% appear byte for
  byte in the US 1ST_READ.BIN and 35% share their first 16 bytes (PAL against US: 97% by the same
  instruction-shape test). The SDK members differ too. A US build of this decomp is out of reach.
- English for the Japanese game, from the US disc:
  - Cutscene voices: EVENT_ADX.AFS (JP 2,088 clips); the US disc has EVENT_ADX.AFS (Japanese, 2,044)
    and EVENT_ADX_US.AFS (English, 2,044) in the same order. 1,862 JP clips have an exact-length
    match in the US Japanese archive (1,523 unique), so JP index to US index to English clip.
  - Voice banks: JP V_SONICTAILS.MLT is byte-identical to the US one; the US has V_*_E.MLT.
  - Field messages: JP MR_MES_A.BIN and US MR_MES_A_J.BIN/_E.BIN share the layout; the table
    pointer differs (0x0cb4a328 against 0x0cb4a314), so a converter rebases them.
  - Menus and HUD: `_E` texture files on the US disc.
  - The font (font_ascii.inc, 96 glyphs, with widths) has proper Latin letters.
  - Cutscene subtitles: about 2,000 strings in the events' text tables, to map from the US events.
