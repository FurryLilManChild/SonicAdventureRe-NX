# Progress log

Newest first. One entry per session, with evidence.

## 2026-11-20: Switch Test 1 built (headless Emerald Coast on Horizon)

- Toolchain (devkitPro's tools run under Wine here, which now crashes): clang for all code, clang
  20 for the C++ backend (devkitPro's GCC 16 C++ headers defeat clang 18: stl_iterator.h's friend
  operator-), binutils' AArch64 ld with switch/ld/image.ld (libnx's script at a fixed address),
  switch-tools' nacptool and elf2nro built from source. tools/a64/clang, A64_SYSTEM=nx: front ends
  x86-64 against newlib and libnx headers (nx/libc.h hides newlib's strupr; nx/posix.h declares
  clock_nanosleep; nx/include has elf.h, link.h, dlfcn.h); code for aarch64-none-elf.
- The domain: refs.py gets a marker, 0x5a5a00000000; Gep32 (GEP32_DOMAIN) swaps constants near it
  for native_domain_base + offset in code, and for window 5's number inside 32-bit static
  references (GEP32_LINK_WINDOW). C reaches the symbol (nx/domain.h). 974 modules use it. The
  first attempt used window 5's own number as the marker; the sweep found an optimizer's packed
  switch table (race_rival_init, 0x500090002) in that range, proof such a constant could be
  mistaken. Built both ways, the image was byte-identical, so the old pass had swapped nothing
  wrongly; the marker removes the risk.
- The image links once for window 5 with --emit-relocs; mkimage.py's relocation-list mode takes
  every ABS64 in the window and every .got word (66,482), and agrees exactly with the two-link
  method on probe 2's payload (363 and 363). The two-link method can't do the game: trunc(sym -
  base) needs the link window's base in 32 bits.
- patches/decomp/0003-horizon.patch: runner.c (maps through switch/runtime/nx_domain.c, no
  signals or re-exec), threads.c (an AArch64 native_switch as Windows' path has), symbols.c (reads
  the ELF from the SD card), native.mk (NATIVE_STUB_LIBRARIES, NATIVE_LINK_DOMAIN).
  tools/apply_patches.sh now checks layered patches in a scratch worktree.
- switch/: loader/main.c (the NRO: picks a window clear of the heap, alias and stack regions and
  of mapped pages for all 13 of the runtime's ranges, places the image, starts it with
  loader_return as its return address, which unmaps the image before hbloader), runtime/nx_main.c
  (logs, heartbeat at priority 0x2b, crash log with the window's distance for addr2line, _exit
  moved off the domain's stack, the domain's maps undone), runtime/nx_domain.c, runtime/nx_libc.c,
  tools/preflight.py (PASS: no fixed window address in code; no marker left in 3,018 modules'
  lowered IR; the loader's ranges match runner.c, threads.c, shim.c; 92 MB image in 256 MB;
  306,668 symbols with the 9 statics; one build).
- Disc for the SD card: tools/extract_disc.py from the owner's track 3 on his PC: 2,106 files, 973 MB;
  all 258 files of the decomp's SHA256SUMS match.

## 2026-11-20: probe 2 on the owner's Switch (2 launches): the Switch design holds

- Window choice clear of the heap and alias regions: usable windows 1 2 8-15, then 1 2 6-11 15;
  window 1 chosen both times.
- Game RAM at three addresses: code memory at W+0x0c000000 made RW, svcMapProcessMemory views of
  it at W+0x8c000000 and W+0xac000000 (all 0); a write through each view read back through the
  other two (ALL MATCH, including the last word); all unmapped cleanly.
- Moved image: the payload linked for window 5, moved to window 1 (363 addresses), mapped as code
  memory with RX/R/RW segments, started with the launcher's entries less the kept heap: it ran
  its own libnx (console, SD log, a 1 MB malloc) and a moved pointer read back correctly.
- Not clean: pressing + in the payload crashed instead of returning to the menu, both launches.
  Theory (unconfirmed): the payload's image is still mapped as code memory over the loader's
  heap when it returns to hbloader, which then reuses that heap. Fix for the game build: the
  payload returns through the loader, which unmaps the image first, and logs that it got there.

## 2026-11-20: Horizon memory probe on the owner's Switch (3 launches, title takeover)

- Address space: ASLR region 0x8000000 to 0x1000000000 (36 bits), so 4 GB windows 1 to 15.
  Stack region fixed at 0x8000000 (size 0x78000000, window 0). Heap region (8 GB) and alias region
  (6 GB) move each launch: heap at 0x14d400000 then 0x3a1000000 twice; alias at 0x45ce00000 then
  0x71d000000 twice. 13 of 15 windows had every range the game maps unmapped each time.
- Window 1 (the AArch64 build's fixed window) was NOT free on launch 1 (heap mapped
  0x14d400000-0x214400000), free on launches 2 and 3. A fixed window fails some launches.
- Code memory at a chosen address works: svcMapProcessCodeMemory at 0x100400000, set RX, called it
  (returned 42), set RW, wrote and read back, unmapped (all result 0). On launch 1 the same call
  at 0x300400000 failed 0xd401: that address was inside the heap region (unmapped, so the probe's
  free test missed it). The window choice must avoid the heap and alias regions, not just mapped
  pages.
- One buffer at two addresses: svcMapProcessMemory with a heap buffer as the source failed (0xd401
  inside window 1; 0xdc01 inside the heap region). Not yet tried: a code-memory mapping as the
  source, which is how libnx's Jit makes its second view.

## 2026-11-20: AArch64 Linux build reaches the same frames under qemu

- `tools/a64/`: a `clang` wrapper (first on PATH; the decomp's NATIVE_CC is plain clang) and
  binutils wrappers. Front-end steps (-E, -M, -emit-llvm) stay x86-64 for `__ptr32`, but read
  AArch64 glibc's headers, with `-DNATIVE_A64 -mlong-double-64` (SH-4's long double is a double)
  and the Microsoft va_list in place of SysV's. Steps that make code target aarch64-linux-gnu.
  `NATIVE_LOWER` with `RETARGET=aarch64-unknown-linux-gnu` retargets every module (ref32_int now
  also drops x86's target-cpu and target-features). `. tools/a64/env.sh` sets it all.
- `patches/decomp/0002-aarch64.patch` (all under NATIVE_A64 or __aarch64__): FPCR in MXCSR's
  place (round toward zero plus FZ; FZ also flushes operands, which MXCSR's DAZ leaves clear);
  AArch64's RET for the P2 calls with an inline cache flush; the game's 14 variadic functions
  `ms_abi` (tested alone: x86 IR with win64cc varargs, retargeted, sums 11 mixed int and double
  arguments correctly on qemu-aarch64); `vsprintf` bridged to glibc's AArch64 va_list
  (engine/native/a64.c); the events' sscanf (path editor only) returns 0; stubs.py reads
  AArch64 call relocations and zeroes S0; asm2c.py's JSR into game C uses AAPCS64 (each class's
  registers in order, as SysV).
- Result: `build/engine/native/play` is an AArch64 ELF. Under `qemu-aarch64 -L
  /usr/aarch64-linux-gnu`, Emerald Coast, 600 vsyncs: 396 frames, exit 0, and the whole log
  (33 lines) identical to the x86 run's.
- Not covered by that run: the pictures (no AArch64 Vulkan here), sound output, the variadic
  functions' text on screen.

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
