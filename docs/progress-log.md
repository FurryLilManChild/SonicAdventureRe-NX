# Progress log

Newest first. One entry per session, with evidence.

## 2026-11-20: references as integers (x86 proof), English mappings

- `tools/ref32`: Gep32 (an opt plugin: getelementptr on a reference becomes i32 arithmetic with the
  x86 layout's offsets) then `ref32_int.py` (`ptr addrspace(271)` retyped as i32, casts become
  add/zext/trunc). Hooked into the decomp's native slice by `NATIVE_LOWER`
  (`patches/decomp/0001-native-lower-hook.patch`, applied by `tools/apply_patches.sh`).
- Full x86 build with `NATIVE_DOMAIN=0x100000000 NATIVE_LOWER="sh tools/ref32/lower.sh"`: 17.5 min,
  Emerald Coast 600 vsyncs, 396 frames, exit 0, the same as without it. sonic.ll keeps no
  addrspace(271).
- Bug found on the way: Gep32's check of static initializers walked shared constants as trees and
  ran out of memory (3 GB cap, sonic.c). A visited set fixed it (0.15 s).
- English voices (`tools/english/event_voices.py`): JP EVENT_ADX.AFS 2,088 clips against the US
  disc's EVENT_ADX.AFS (Japanese) and EVENT_ADX_US.AFS (English), 2,044 each in the same order.
  1,869 JP clips mapped (sample count, then sound envelope); 80 spot checks agreed. 219 clips
  (577 s) have no US counterpart: lines the US release rewrote or cut.
- English subtitles (`tools/english/event_text.py`): each US event module (base 0x0cb80000) has a
  table of five pointers (JP, EN, FR, ES, DE) to parallel arrays of line pointers; an empty JP
  slot means the English split the line. 1,054 of 1,137 JP subtitles aligned to English lines,
  1,032 byte-identical to the US Japanese. Some US event .bin files from SA-nx's extraction are
  148 to 1,224 bytes (EV0053, EV0101...): to look at.
- Still to handle for AArch64: `engine/sa1/sound` uses `llvm.x86.sse.ldmxcsr`.

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
