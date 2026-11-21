# Progress log

Newest first. One entry per session, with evidence.

## 2026-11-21: Sonic's face is the decomp's own bug (PC comparison); pause under study

- The decomp at our pin (e9a6f8a), built unchanged for x86-64 Linux in a separate worktree
  (build/gen copied from the Switch build, make pc, then build/engine/native/play), rendered
  headless on lavapipe: `play --headless orig --pictures DIR --card FILE orig 4500 15 0`
  (Chaos 0 from its start, 36 s for 1,500 vsyncs). In the entry event's close-up of Sonic
  talking (pictures 1500 to 1590), most pictures move the mouth whole and some scatter the face's
  points exactly as the Switch replay_002 showed. So the shape motion bug is in the decomp, not
  in the AArch64 lowering or the Switch runtime.
- Upstream has one newer commit, 004279f (Nov 20, "Adds faster trig, clean fps counter, bug
  fixes"); its notes name no face fix. It is being built in the same worktree to render the same
  scene.
- Pause on the Switch (build be54505, the owner): no menu shows, and + again leaves (Emerald Coast to
  the field, the field to the title), so pause_choice was not PAUSE_CONTINUE at the second press.
  stage_run_paused moves it only on pad_press_repeat KU or KD (the d-pad, or the stick past 78).
  Next: a key script pausing in Emerald Coast on the PC build, to see whether the PC shows the
  menu and resumes.
- Saving appears to work on be54505 (the owner); the log's storage lines are still to be read.

## 2026-11-21: saving through the engine's storage, sound on audout, frame counter

- Patch 0005: storage.cpp (the writer a windowed run's card uses) on __SWITCH__ removes the old
  save before renaming the new file onto its name, logs each step's errno, and reads the save
  back to compare ("storage: saved NAME, N bytes, read back the same"). A rename that fails after
  the old save went leaves NAME.new whole; card.c's adopted (now on __SWITCH__ too, as on
  Windows) takes it as the card at the next start. Checked in the build: storage.o holds the new
  lines, card.ll calls adopted.
- Sound moves from audrv/audren (silent: 0 samples played) to audout at its own rate (48 kHz):
  four 0x1000-aligned buffers of 1024 frames, the game's 44.1 kHz resampled linearly, buffers
  refilled as audoutWaitPlayFinish returns them. The log gives the buffers' memory type (Horizon's
  svcQueryMemory) and the heartbeat the buffers queued and returned.
- Patch 0006: raster.cpp asks a weak native_overlay for pixels once a picture shown and copies
  them onto the swapchain image after the picture (vkCmdCopyBufferToImage, a per-frame
  host buffer). nx_overlay.c counts those calls: frames on screen a second, drawn as "NN FPS" at
  the top left, renewed every half second, and logged in the heartbeat. Captures are read back
  before it, so they never hold it.
- Chaos 0's 33 to 45 a second (test 5f3d5d3): runs of 60 a second for 7, 14 and 24 s with 30 a
  second between (39 s, 14 s). pace.c asks 60 only in stage play (stage_in_play, frame_interval
  2, two steps), so the entry event and other events stay at 30 by the decomp's design.

## 2026-11-21: test 5f3d5d3 read (16:9, 60 fps, captures)

- Settings in force logged: aspect 16:9, fps 60, vsync 1, sound 1, resolution 1. Clocks
  1020/307/1331 MHz handheld throughout.
- Saving still failed ("can't write the card", 5 times). Cause found in the code: with a window,
  card.c's store() goes through the engine's storage (sa_storage_write, engine/pc/storage.cpp),
  not filed(), which patch 0004 fixed. storage.cpp writes NAME.new and renames it onto NAME,
  which FAT refuses once the card exists (the first write of a fresh card worked in the a6b0060
  run). Pausing still showed the card error, as expected while saves fail.
- Sound: the thread runs (about 195 loops a second), rendered its first 4 pieces and queued them;
  the voice then played 0 samples and no piece ever came back (state Done), all run long. Every
  setup call returned 0. The audio renderer path is failing silently; cause unknown.
- Replays work: 3 saved (4.1 to 5.2 s each, 110 MB), the ring's copy costs 0.42 ms a picture.
  replay_002 shows Sonic's face in the Station Square close-up: some pictures whole with the mouth
  moving, others with the face's points scattered. That is the shape motion (action.c's
  action_shape_*, the mouth morph). Not yet known whether the AArch64 port or the decomp itself
  is at fault.
- Pictures a second, from the times file: Emerald Coast acts 1 and 2 at 60.0 (game thread
  median 5.8 ms, 99th 14-15; GPU median 4.0, 99th 7-9); cutscenes at 30; Chaos 0 at 33 to 45
  with CPU and GPU about 5 to 6 ms, so not limited by time. The late and lost vsync counts are
  not meaningful under FIFO vsync (play.c's own clock runs behind the blocking present).

## 2026-11-21: first-picture test read; saving, sound and captures for the next test

- Test a6b0060 on the owner's Switch (handheld; clocks CPU 1020, GPU 307, memory 1331 MHz): the
  front door ran 21,585 vsyncs: boot movies, title, Station Square, Chaos 0, Emerald Coast acts 1
  and 2; ended by Minus and + (clean exit). Pictures at 30 a second throughout play; 1,384 vsyncs
  late and 2,160 lost in all, nearly all at loads, movies and stage changes (vsyncs 12000-20000:
  139 late, 88 lost). Game thread per picture: median 5.5 ms, 99th 20.0; backend CPU median 4.6,
  99th 8.4; GPU median 3.7, 99th 14.0, most 57.9 ms. Budget at 30 fps 33 ms, at 60 fps 16.7 ms.
- No sound: "nx: sound open" logged, but the engine reports "sound refused 8177 commands, held at
  most 4096 of 4096, 0 voices". Sound::render drains that queue, so the feeding thread never
  rendered. The log can't say why (no counters in the thread). Changes: 256 KB stack, a 5 ms
  sleep in place of audrenWaitFrame, every setup result logged, and loops, pieces and samples
  played in each heartbeat.
- Saving failed every time ("can't write the card"): card.c writes card.bin.new and renames it
  onto card.bin, which FAT refuses. Pausing in a stage runs stage_run_card_error, which checks
  that the card's saves match the game's; they never did, so the card message showed in place of
  the pause menu (the "freeze, then the title" the owner saw; game_mode 11 near vsync 10800). Patch
  0004: on __SWITCH__ the old card is removed first, as on Windows (checked in card.o: remove,
  then rename).
- Captures (as in SonAdvance-nx): right stick click saves logs/shot_NNN.bmp; left stick click
  logs/replay_NNN.bin, the last 120 pictures shown with each one's vsync, game_mode, stage and
  act (110 MB, at most 10 a session); tools/replay/replay.py makes PNGs (16:9 runs stretched to
  854x480). Patch 0004 adds play.c's native_play_capture hook, which turns the readback on; its
  cost shows in the times file and the heartbeat's copy figure.
- Settings default to 16:9 at 60 fps, and the settings in force are logged at start. The
  heartbeat logs the clocks every 5 s.

## 2026-11-20: the window build (Vulkan on NVK): first-picture test prepared

- `switch/runtime/window_nx.cpp` is the decomp's window (engine/pc/window.h) on Horizon, in place
  of its headless window_none: a VI surface on the default NWindow (VK_NN_vi_surface), 1280x720;
  the pad from libnx HID, positional (Switch B/A/Y/X as Dreamcast A/B/X/Y, + Start, ZL/ZR and
  L/R the triggers at 0 or 255, the left stick squared as in SA-nx, the d-pad moving it as on
  PC); Minus and + together end the game; sound through audrv at 44.1 kHz stereo, four pieces of
  1024 frames fed from a thread on core 1 at priority 0x2a.
- The raster opens Vulkan by dlopen/dlsym: nx_libc.c now gives a handle and NVK's
  vkGetInstanceProcAddr (libvulkan.a, linked statically with libexpat, libzstd and zlib).
- The link without --gc-sections (the game finds statics by name) left 63 POSIX calls undefined,
  all named only by NAK's Rust standard library inside libvulkan.a (files, processes, signals).
  SA-nx's link drops that code with --gc-sections. `nx_posix_stubs.c` gives weak stand-ins that
  fail with ENOSYS and log the first 16 calls. NVK brings its own posix_memalign, so ours is weak.
- Preflight check 1 flagged 10 window-5 numbers in NVK's code (GPU method words such as
  0x0002000500010000, MME value pairs 0x500000001, NAK encoder tables). None carries a relocation,
  and libvulkan.a was compiled before our link chose window 5, so they are NVK's own constants.
  The check now exempts unrelocated constants inside libvulkan.a's sections (from the map) and
  64-bit constants with their top 16 bits set. Preflight passes.
- `nx_main.c` runs the front door: `play --settings DIR/settings.txt --card DIR/card.bin --window
  DIR/disc --times DIR/SonicAdventureRe-nx.times.txt`. A first run writes settings.txt with the
  game's defaults spelled out (4:3, 30 fps, vsync, sound, resolution 1). No console (it would take
  the window from Vulkan); stdout goes to SonicAdventureRe-nx.out.log; the heartbeat logs vsyncs
  a second every 5 s.

## 2026-11-20: Switch Test 2 on the owner's Switch: the game runs, 396 frames, as on PC

- Build 65f655e. Window 1 of 1 8-11 13-15. "code mapped again, executable at 0x1b0000000: 0":
  the P2 page fix held. Stack guards still refused (0xd401), the run went on.
- "sa1: done after 600 vsyncs: 396 frames, game_mode 4, stage 1 act 0, character 0", and all 33
  sa1: lines (29 stubs with their vsyncs, the reports, texture memory 720/719/1116/0) identical to
  the x86 PC run's run.txt.
- Timing (stock clocks, headless, no rendering or pacing): 0 vsyncs at 4.1 s (setup, disc check,
  symbols), 211 at 6.1 s, done at 7.6 s.
- Exit path: _exit on the end's own stack, heartbeat stopped, report, 12 domain maps undone ok,
  waited for +, left (the log ends there; no crash log). Test 1's black screen was this end screen
  never reached, as the failure came before.

## 2026-11-20: Switch Test 1 on the owner's Switch, and Test 2's fixes

- Test 1 (build 0c4589e), log: the loader chose window 4 of 4 5 6 8 9 10 14 15, moved 66,482
  addresses, mapped the image (0) and started it; the game's libnx came up; clocks CPU 1020 MHz,
  GPU 307 MHz, memory 1331 MHz. The domain's RAM, mirrors, devices and stacks mapped. Then
  "making code failed at 0x4b0000000: 0xd401" and "sa1: can't map the P2 calls": code memory made
  RW (the runner writes its RET) can't then be made RX. Probe 2 made it RX first, then RW. The
  stack guards (Perm_None) failed the same way (the run carries on without them). The screen then
  went black; nothing after the failure was logged, so the exit path's hang (or a silent end
  screen) is unplaced. the owner went Home without pressing +.
- Test 2: nx_code unmaps the code memory (what the game wrote stays in the heap pages), maps it
  again and makes it RX at once. The exit path logs each step (exit wrapped to log before the C
  library's handlers; _exit's end logs every stage). The log's "linked for" reads a number the
  loader doesn't move (nx_link_window_number).

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
