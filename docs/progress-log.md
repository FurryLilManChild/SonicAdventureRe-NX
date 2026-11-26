# Progress log

Newest first. One entry per session, with evidence.

## 2026-11-25: The Chao gardens, and their messages in English

- The owner in the Station Square garden (1.0): hatched a Chao, left, quit, came back, the Chao
  still there. His card: SONICADV_ALF, 28 blocks, A-LIFE header, data written; each leaving saved
  the card, read back the same; no errors in the garden sessions. The VMU transporter not tried.
- The garden's windows (CHAO_HYOUJI, 22 textures, the transporter's messages among them) were
  English already: every texture matches the US _E file in size. The other garden textures are
  the same files on both discs.
- Its Japanese text is the gardens' and the Chao race's messages: script pages as the stages'
  hints (script.c's table per garden stage; the race's by script_set_table), compiled into
  AL_GARDEN00/01/02.PRS and AL_RACE.PRS, which load at 0x0cb80000. The US modules hold each
  Japanese run, then the English after it.
- tools/english: common.module_base (0x0cb80000 for al_garden and al_race), make_english and the
  loader's english_build.c read each module at its own base; make_maps.py gardens maps the
  Japanese modules' own pages: 42 of 93 (garden00 24, garden01 4, garden02 4, race 10). The rest
  the US release left in Japanese too (the same 13 short runs in each module, likely unused).
  make_english on the US modules: 42 English pages, none with Japanese bytes. Not yet on the
  Switch.

## 2026-11-25: 1.0

- The tester's run of 0.5.4-hs1 (c5ee32a): Gamma's Hot Shelter plays through the long drop and
  its containers; the tester then finished the rest of the game with nothing more to report.
- 1.0 is c5ee32a's game as tested, its version string the only change: README, the release
  README and the notes say every story has been played through.

## 2026-11-25: Gamma's Hot Shelter crash after the long drop

- Report (a tester, 0.5.4, build 1067ebd): a crash in Hot Shelter act 2 right as Gamma lands
  after the long drop past the train, every time; once after Emerald Coast (no report for that).
- Crash report: data abort at linked 0x5005264cc, target_cleared (src/object/target.c:152),
  from shelter_container_break_when_cleared (modules/stg12/container.c:189); the work pointer it
  read was 0x018c8779d0000000, nowhere in the game's memory.
- Cause: target_start lists each target and nothing takes one off. A target that unloads stays
  listed, freed; one that loads again is listed again (64 at most, the 65th written past the end).
  The containers read every listed target's work; on the console freed memory reads without a
  fault, here its pointer can be anything.
- Fix: patches/decomp/0025-native-target-records.patch: in our builds the list keeps each
  target's id and whether it is down (marked when it is shot, newest entry for the task), the
  check reads those, and the list holds 256, noting any past that. Not yet tested.

## 2026-11-25: 0.5.4: the Final Egg and Amy fixes, tested

- The owner's tests of 0.5.3-fe3 (e0cb6a8): Sonic stands on the floor around Final Egg's button
  platform, and Amy's story plays through its end (from a save made with story_save.py at her
  last section; he beat Zero and the ending played).
- For the release the Final Egg trace's Switch side is gone (nx_main.c's wrap and build.sh's
  --wrap); patch 0022's call stays and reaches bare.c's empty default. The ref32 NaN rule stays: it
  was in the tested build.

## 2026-11-25: Final Egg act 2: the real cause (collision list overrun)

- The NaN conversion (2026-11-25 entry below) was not it: the owner's Switch test of 0.5.3-fe1
  still sank Sonic. The game cuts each turn to 16 bits, so -2^31 and 0 are the same turn.
- Two runs (x86 and qemu-aarch64, both through tools/ref32, Sonic dropped at 1123.49 -655 -807.17)
  with probes on triangle 2415: at the step where they part, polygon_sweep gets the same input on
  both except the ball, 4.0 lower on ARM in player_collide_world's second pass. Nothing between the
  passes moves work->pos or normal on purpose.
- The first pass sweeps 17 objects (2026-11-25 probes: 17 "sweep in" before the reversed second
  pass). player_collide_world and its pinball twin keep candidates[16] and sorted[16] and write the
  17th past the end: on the console and x86 into something harmless, on AArch64 into the lift of
  Sonic's ball (normal, 4.0 over his feet).
- Fix: patches/decomp/0024-native-collide-entries.patch: in our builds both lists hold 64, and a
  pass stops at 64 with a note in the log. Not yet tested on the Switch.
- The ref32 NaN rule stays for now (removing it means a full rebuild); harmless either way.

## 2026-11-25: Amy's story stops at her first scene

- Report (a player, 0.5.0, build d94c0b9): after beating Zero, and twice on choosing Amy, the game
  ends during her opening monologue. Each of the three runs in the log ends the same way:
  "no file e_0072.mlt on the disc", then "the game stopped: READ e_0072.mlt", stage 29, Amy.
- Cause: events/ev0072/script.c loads e_0072.mlt, and the Japanese disc has no such file (its
  E_0071.MLT is followed by none for 0072). On the console the reads fail and the scene plays on;
  under NONMATCHING, LOADER_MUST_READ stops the game.
- Fix: patches/decomp/0023-native-missing-sound-bank.patch: sound.c's four bank loaders return
  when the file won't open (gdfs.c already logs the missing name).
- The player's Atmosphere crash report (an instruction abort) needs build d94c0b9's ELF, which was
  not kept: unread.

## 2026-11-25: Final Egg act 2: the cause, and a fix to test

- Cause, from probes on each triangle polygon_sweep turns at the step where the builds part
  (Sonic dropped at 1123.49 -655 -807.17; every input to the sweeps bit for bit the same on both):
  for a flat triangle the SDK's atan2f(0, 0) is a NaN, and polygon_sweep turns by
  -(Angle)(that * ANGLE_PER_RADIAN). x86's CVTTSS2SI gives -2^31 for a NaN; AArch64's FCVTZS gives
  0. With 0 the ARM build counts gate triangle 2415 as hit, which x86 does not; Sonic is snapped
  to the floor without ground, falls about 65 steps, lands, and loses it again.
- Ruled out on the way: the NaN of vector_angle (only sonic_sway_step's, which is cosmetic), the
  pointer lowering (x86 built through tools/ref32 with domain 0x100000000 plays the spot), DAZ, round
  to nearest, and play_math's FSRRA, FSQRT, FMAC, FIPR, FTRV and FSCA (two million operands, the
  same bits on x86 and under qemu-aarch64).
- Fix: tools/ref32/ref32_int.py, on retargeting, gives each float or double converted to a 32-bit
  integer -2^31 for a NaN, as x86 does and, we expect, the console's FTRC (not yet checked against
  the SH-4 manual). Out of range FCVTZS already saturates as FTRC does. It covers each such
  conversion in the game (about 4,700), not only the sweep's. Checked on a test module under
  qemu-aarch64; not yet run in the game: the owner tests the Switch build.

## 2026-11-24: Final Egg act 2: Sonic stuck at a floor (diagnostic)

- Report (owner and a player, every time there): in Final Egg act 2, landing on one spot of the
  floor around the platform with the button (the floor folds away when the button is pressed, and
  the platform rises), Sonic sinks, is pushed up and back about once a second, and barely answers
  the stick. Not in the Dreamcast game nor any DX. The log of that run: stage 10 act 1, no heap
  damage. The video alone does not say what he stands on.
- Checked and cleared: elevator2's vars (16 bytes natively) fit union task_vars, whose void *p[3]
  makes it 24; set entry angles are Sint16, sign-extended.
- Patch 0022: player_collide calls native_hook_player_trace for player 0 in stage 10 (bare.c's does
  nothing); nx_main.c logs a "trace" line when the ground changes, on a drop or rise of more than 4
  units in a step, when the player barely moves while its speed says it moves, and every 30 steps:
  position, speed, mode, action, flags, the ground task and its step (as linked, for addr2line) and
  the ground's force.

## 2026-11-24: release 0.5.3 prepared

- 8b91d32's [gpu] lines, the tornado standing still on ACCURATE at stock clocks: 6.4 ms of device
  time a picture (pass 2.9, resolve 3.1) for the same ~760,000 fragments, 59 to 60 fps (was 11.5 ms,
  41 fps). raster: "subgroups of 32, stages 0x3f, operations 0x6ff": NVK on the GM20B takes the WAVE
  build. The bridge before the tornado still spiked on ACCURATE (up to 3 million fragments a
  picture, resolve 7.2 ms); EFFECTS FAST covers it.
- TRANSLUCENCY defaults to EFFECTS FAST (nx_translucency_fast = 2; first-run settings say
  "translucency effects"). README, README.txt and the release notes say so; updating players keep
  their setting and are told to choose it.
- audit.py counted 31 and 34 windows "on the disc" for the test strips. An exact search found none
  of the 34 in any disc file: the 64-bit hash takes some windows of mostly zero words (doubles,
  pointers) for others. audit.py now confirms each hashed hit by its bytes; the last strip
  (7a677b3) and 8b91d32's both audit 0. out/strip-cache/last.audit set to 0.
- The CPU and device still take turns each picture (fix 3, overlapping them): left for later.
- Release build d1f4557 (SARE_RELEASE 1): update strip 10 min 20 s, audit 0. Zip sha256
  d06e439d4a6ae9462017c44433d77384dd392c1b4f08848884c4eae41e8c5168, NRO sha256
  5bc1f643bb2bfd4a02a80d97f42b3d43e952fdd2e7a0c7754f93868d91f7b3c9.
- strip_update.py's step 2 matched 33 content sections whole again on every update (about 400 s
  of 600, finding 0 to 217 bytes): every section with a piece that moved, though step 1 had found
  nearly all of those near their place. Now only sections with a piece lost (found neither in place
  nor near) are matched whole: on d1f4557's build 4 sections, the strip 5 min 21 s instead of
  10 min 20 s, audit 0, fill_check the same as the full image (332,398 pieces).
- Then only the bytes around each lost piece's place (4096 each side) are matched again, not its
  whole section. The same case (7a677b3's strip to d1f4557's build, 298 pieces lost): 27 places in
  4 sections, 230 bytes from the disc (whole sections: 217), the strip 3 min 32 s, audit 0,
  fill_check the same (332,405 pieces). From 8b91d32's strip (no section lost a piece): 2 min 36 s,
  audit 0, fill_check the same.

## 2026-11-24: faster per-pixel translucency (test)

- a995d70's [gpu] lines, Windy Valley's tornado standing still: ACCURATE at stock clocks 11.5 ms of
  device time a picture (pass 6.9, resolve 4.2) for about 755,000 collected fragments, 41 fps;
  FAST 2.6 ms (pass 2.1), 60 fps; ACCURATE with the GPU at 768 MHz 6.9 ms (pass 3.4, resolve 3.2),
  57 to 58 fps. 755,000 fragments of 16 bytes is about 12 MB a way, near 1 ms of the memory's
  bandwidth, so most of the 9 ms is overhead. times.txt: the game's CPU about 5.2 ms a picture,
  and with 11.5 ms of device time every picture reached late: CPU and device take turns.
- Patch 0021 now also: draw.frag writes each fragment as one uvec4 (one store, not four), and
  under WAVE (built again with -DWAVE for SPIR-V 1.3, replay.mk) a subgroup takes its fragments'
  room with one atomicAdd by its lowest non-helper invocation, shared by a shuffle. raster.cpp picks
  the WAVE module where vkGetPhysicalDeviceProperties2 says the fragment stage has basic, ballot and
  shuffle, and logs the subgroup figures. resolve.comp reads each fragment once, whole, sorts the
  copies, and blends a pixel of one fragment with no list. The picture is meant to be the same
  bit for bit: the same order, the same MOST, the same cut.
- Next, separately: overlap the game's CPU work with the device's.

## 2026-11-24: measuring translucency's cost

- The owner's test of 7a677b3: Tails' plane cutscene held 30 fps on EFFECTS FAST where ACCURATE dipped
  to 24 (5 s heartbeats, one run each). The Windy Valley tornado stayed at 36 to 43 fps on EFFECTS
  FAST against 58 to 61 on FAST: its walls are models, kept per pixel. FAST draws them with flat
  vertical stripes, so FAST is no answer there. The owner wants every translucency slowdown solved
  before a release.
- To find where ACCURATE's time goes: four timestamps a picture (start, after the pass, after the
  resolve, end; patch 0021) and native_gpu_parts, which nx_menu.c sums into a "[gpu]" log line
  every 120 pictures (or at a change of TRANSLUCENCY): device ms in all, the pass, the resolve, the
  rest, and the fragments collected. A test build (not SARE_RELEASE) also writes times.txt.
- Leads to check against the numbers: resolve.comp keeps a 64-entry index array a thread and reads
  each fragment's depth and order from the buffer inside its insertion sort; draw.frag takes every
  fragment's slot with one atomicAdd on a single counter.

## 2026-11-24: TRANSLUCENCY EFFECTS FAST (test)

- Heavy smoke and dust cost a lot under ACCURATE (per-pixel lists and the resolve); FAST sorts every
  see-through triangle instead. The new middle option keeps models (level, water, glass, objects)
  per pixel and moves sprites and immediate polygons outside the screen's (njDrawSprite3D,
  njDrawTexture, njDrawPolygon3D: the particle pools, dust, smoke, sparks) to a late list.
- patches/decomp/0021-native-effects-late.patch: Scene::finish() takes the port's blend mode 4,
  puts those triangles last, sorted far to near, flagged BATCH_LATE (512). raster.cpp keeps the
  depth image, draws BATCH_LATE batches without collect in a second render pass (load ops, its own
  dependency on the resolve and the depth writes) after resolve.comp.
- Menu and settings: TRANSLUCENCY ACCURATE / EFFECTS FAST / FAST, saved as accurate, effects, fast.
- Known: an effect behind another see-through surface (water, glass) draws over it. Speed gain
  not measured yet.

## 2026-11-24: release 0.5.2

- The owner tested b5812a1: Sky Deck past the big cannon no longer crashes. Casinopolis not yet
  played (same fix, same kind of object).
- Built 4e9a733 with SARE_VERSION 0.5.2 and SARE_RELEASE 1 (95 s, no game file rebuilt); update
  strip 160 s from b5812a1's; audit 37; fill_check identical (332,214 pieces).
- SonicAdventureRe-nx-0.5.2-switch.zip sha256
  5c969717c1c60d2d1ae83ee3022dc2ca6b069201a87491b5b3ffb6c54d924389; NRO sha256
  50a2ddc458bdde161a31c3e480b4fd8b19d9dab0209f71e590f934df5c16b6aa. In out\release\ with
  release_notes_0.5.2.md.

## 2026-11-24: Sky Deck crash found: debris pieces outgrow their heap room

- f65e7b9 on the owner's Switch: still crashed at Sky Deck past the big cannon; no heap check
  line, no object note (under 144). Atmosphere report 01795534957: Data Abort in heap_free
  (task.c:808, merging with the block after), from task_destroy, task_free_work, with
  debris_destroy/debris_free (stg06/debris.c:489) in x23: freeing the debris's pieces met a
  damaged next block.
- Cause, proven from the build's DWARF: stg06 debris allocates count * PIECE_ROOM (60, "4 more
  than it uses", the Dreamcast's 56-byte piece), but struct debris_piece is 0x40 = 64 bytes
  natively (NJS_MODEL *model 8 bytes at 0x18). The last piece writes past the block into the
  next block's header. The free-list check missed it: it walks free blocks only.
- Sweep of the same kind: every heap_alloc sized by a constant (18 sites) against the struct's
  native size. One more: stg09 (Casinopolis) fragments, FRAGMENT_SIZE 60 for a 64-byte struct
  fragment. Shards (48 in 52) and the rest fit or use sizeof. All 836 *_vars structs against
  task_vars (12): three larger (character_vars, particle_vars, stg10 camera_vars) are the
  Dreamcast's own layouts, no pointers grown.
- Patch 0020: PIECE_ROOM and FRAGMENT_SIZE are the struct's size plus 4 on the native build; the
  heap check also tests a freed block's neighbors (before and after linked back), which a write
  past a block's end reaches first.

## 2026-11-24: Sky Deck crash (0.5.1, community report): decomp to 004279f, heap check

- Report (Atmosphere, 0.5.1, repeated at Sky Deck act 2 after the large cannon is destroyed):
  Data Abort in heap_alloc (task.c:590, `prev->next = rest`) with prev = 0x4153, called from
  task_create through heap_calloc; spark_step and explosion_step in the registers. A free block's
  prev held 0x4153: the heap's free list was damaged earlier, by something else. Cause unknown.
- Best lead, unproven: upstream 004279f (Nov 20, after our pin e9a6f8a) fixes native collision
  mesh pool bugs (engine/native/mesh.c: an empty mesh read through null, the pool's last record,
  the terminator) and lets a full set of 192 meshes be replaced. Sky Deck is busy with moving
  solid objects. Moved the pin to 004279f; all 17 patches apply unchanged.
- Patch 0018: the heap check. After each heap_alloc and heap_free the free list is walked
  (each link in the arena, 16-byte aligned, linked back); a free of a block outside the heap or
  already free is refused. The first 8 findings go to the log with the caller, the running step,
  and the caller and step that freed the block last holding the damaged bytes (a ring of 4096
  frees), as linked addresses for addr2line. Also: the most world objects per stage, past 144
  (three quarters of 192), as a note line.
- Patch 0019 (f65e7b9): TRIALS in the port menu (ALL OPEN or AS SAVED, default; setting
  trials) opens every Trial stage and mini game through the four list checks all_open passes,
  not all_open itself (that also opens the internet item and the last story). Declared in
  trial.c alone, so only 6 files rebuilt (1 min 52 s).
- Test build f65e7b9 strip: the update strip took 15 min (the new decomp moved most data) and
  left 73 audit windows (.rodata texture names whose windows match the disc only once their
  neighbors are zeroed). The short-string pass now repeats until a round takes nothing; rerun
  from its own result: 6 min 49 s, audit 38 (0.5.1: 39), fill_check identical.

## 2026-11-23: release 0.5.1

- Built 034d683 with SARE_VERSION 0.5.1 and SARE_RELEASE 1 (2.5 min). The update strip from
  fdd22c8's strip first left 51 audit windows (fdd22c8: 41): 1,511 pieces moved more than the
  2 MB near search, 12 windows of them in sa1_data_b_egm1. Fixed (bbd0707): moved pieces are
  looked for in the whole image, and each content section holding one is matched again against
  its disc file (137 sections, 16.5 min this time; to be narrowed to the moved pieces alone).
  Result: 330,313 pieces, audit 39 windows, fill_check identical.
- History rewritten to take the owner's name out (his OK), main force-pushed; the 0.5.0 tag
  could not be moved from the cloud (proxy 403), he moves it from his PC.
- SonicAdventureRe-nx-0.5.1-switch.zip sha256
  c2b3be75e3c4c1650a11fefc64ef6d44f8024a47057e578ed24792876f140b9d; NRO sha256
  9e763136c775bb5f2930760af60a524f494645be7ea8be3cf14302dd4d032e07. In out\release\ with
  release_notes_0.5.1.md. ELFs kept as out/elfs/034d683.*.

## 2026-11-23: faster test and update builds

- Builds: a build after a change recompiles only what changed (3.4 min, 128 files, timed). The
  2,892-file builds earlier today were a one-time full recompile, not every build.
- switch/tools/strip_update.py strips an update from the last strip's results, with no stub build:
  the last fill's pieces placed again by the symbol they lay in (each checked against the disc;
  pieces whose symbol moved are found near their place), new or changed data matched without a
  stub twin, then strip.py's two whole-image scans, searched as audit.py searches (each disc
  file's windows looked up among the image's, not the other way round). Against the 0.5.0 strip
  with 351fe14's build: 2 min (strip.py: about 35, plus the 18-min stub build); 329,308 pieces;
  audit 41 windows (0.5.0: 64); fill_check identical to the full image.
- First try (no near search, no scans) left 636,348 windows: the audit caught it, which is why
  make_release.sh stops when the audit finds more than the last strip left.
- make_release.sh: SARE_STRIP update (default once out/strip-cache/ holds a last strip) or full;
  the cache was seeded from release 0.5.0 (audit 64).
- the owner (351fe14): TALK and the D-pad first-person look round work.
- CAMERA STICK (the owner's request): the game turns the camera only with L and R (the follow camera
  reads their buttons, the orbits how far they are pulled; camera/modes.c), and has no pitch
  control outside the first-person look. The right stick past a quarter presses R (right) or L
  (left), pulled in proportion. ON by default, INVERTED, OFF; setting camera_stick. Up and down
  would be a new camera feature, not done.
- Patch 0017: native_hook_idle_said logs each idle line played (voice, TALK or standing still,
  stage, act, character).

## 2026-11-23: idle lines from SADX's table, TALK back on, D-PAD LOOK (first-person view)

- Idle lines: SADX (the owner's Steam sonic.exe) plays them in its idle check (CharObj2 IdleTime
  0x708, then 0x442360): half the time the character's general line (US 390 to 395, Sonic 396 in
  Perfect Chaos), else its own line for the stage from the table at 0x915CC8 (by act, random in
  the Chao gardens), the general line where it has none. The table is in
  tools/idle/sadx_idle_table.json (numbers only); tools/idle/make_idle_lines.py turns each US
  voice into the Japanese EVENT_ADX index through tools/english/voice_map.txt read backwards (all
  162 have one; US 316 has two Japanese clips of the same length, 321 and 322, ADX sample counts
  44128 each; 321 used). VOICES English swaps in the US clip through the usual voice hook.
- Patch 0017 reworked: script_native_talk(playing) runs each step in ACTION, FIELD and TRIAL; it
  plays the line when player 0's idle_steps drops from over 1800 (the six characters'
  FIDGET_STEPS: the fidget starts) while native_hook_idle_lines is on, or on TALK. Not while
  paused, in an event, or while a message or voice plays (message_busy). The Station Square
  script-table import is gone.
- Port menu, CONTROLS page: TALK shown again, D-PAD (LOOK default, MOVE) and IDLE LINES (ON
  default); settings dpad and idle_lines.
- First-person view: the decomp has it, camera mode SURVEY (64, "the pad's look round",
  camera.c camera_start_or_end_survey): the D-pad starts it while player 0 stands still on land,
  a face button, the stick or moving ends it. The decomp's PC backend and our window_nx.cpp both
  copied the D-pad onto the stick (x1, y1), so Up walked forward and the survey could never
  start. D-PAD LOOK leaves the stick alone, as on the Dreamcast. Not yet tested on the Switch.

## 2026-11-23: TALK off until the characters' idle lines are found

- the owner (7bc647f): TALK in Station Square played Tikal's voice, not the character's. Row 18 is
  Tikal's voiced story hint for each character (the decomp's "each character's voiced hints" was
  read as the character speaking, and the speaker wasn't checked). Wrong lines, my error.
- What the request means: TCRF lists, for the Dreamcast game, each character's two idle lines per
  area after about 30 s standing still. The SADX mod (michael-fadely/sadx-idle-chatter, 20
  lines) only sets the player's IdleTime to 0x7fff on Z; SADX's own code plays the line. In the
  decomp (JP 1.007) idle_steps > FIDGET_STEPS (1800) only starts the fidget action in all six
  characters; no reader of idle_steps outside the player files, no voice in the fidget path, no
  stage/character voice table found in src or modules, none named in SADXModLoader's headers.
- TALK_READY 0 in nx_menu.c: the hook always answers no and the CONTROLS page hides TALK. READMEs
  no longer mention it. Not built yet.

## 2026-11-23: CONTROLS (button remapping) and TALK

- A community request: the characters' voiced "where am I, what now" lines on a button, as a PC
  mod does for SADX (Idle Chatter, Z). In the decomp (JP 1.007) these are Station Square's row 18
  (hint_script_18: 31 voiced branches over all six characters, picked by story flags), shown by a
  hint box; no field has another such row, and no idle timer turned up in a search of the decomp.
- Patch 0017: script_native_talk (script.c), called from main's loop in GAME_MODE_FIELD, runs row
  18 of station_square_script_table (exported to 1st_read through config/1st_read/imports.txt)
  when the weak native_hook_talk says TALK was pressed, in stages Station Square to the Past, not
  paused, no event in control, no message busy; script_show plays the voice and pages.
- Port: nx_menu.c's CONTROLS page (the Switch button for Dreamcast A, B, X, Y, L, R: A to R + ZR;
  TALK also MINUS TAP or NONE; DEFAULTS; BACK), live, saved as button_a ... talk; window_nx.cpp
  reads the map in window_pad and raises TALK in window_pump (Minus let go with no + meanwhile).
  Defaults are the old fixed layout. The start log lists the controls; menu changes and each
  TALK press are logged.
- English for those lines: make_maps.py now also reads a module's people/text.inc and accepts
  Japanese pages without a leading tab or bell, and picks only US pages that have English. 52 new
  hint rows (46 Station Square: the 16 story hints, hint boxes, signs; 6 Mystic Ruins), every one
  checked by eye; the old 121 rows unchanged. english_build.c output still equals
  make_english.py's (173 hints).

## 2026-11-22: release 0.5.0 (needs testing)

- the owner (build e62c854, clean SD folder with only his two disc images): it worked, first launch
  unpacked both images, made english/, built the image; then the game.
- Release changes: SARE_VERSION (0.5.0, the Homebrew Menu's version; the loader's and the game's
  first log lines say it); SARE_RELEASE=1: no per-picture timing file (--times), the frame counter
  off in a first run's settings. First-run settings now list "translucency accurate". COPYING
  (GPL-2.0, as SA-nx's code it carries). README.md and the zip's README.txt rewritten for players,
  with the English layer's coverage (done and not done). make_release.sh also zips
  SonicAdventureRe-nx-0.5.0-switch.zip (switch/SonicAdventureRe-nx/: .nro, .img, .fill, .elf,
  README.txt, COPYING.txt, disc/ and us_disc/ notes) and keeps the full and loader ELFs.
- Packaged (release build d94c0b9; strip a637561 skips the port's own objects, after the first
  run stopped on default_settings, which had grown and so had no stub twin): 254 disc files,
  329,422 pieces, fill table 5,287,032 bytes; audit 64 windows (file names and labels in the
  decomp's source); fill_check identical. SonicAdventureRe-nx-0.5.0-switch.zip 15,446,187 bytes,
  sha256 1c2d93b98ed843dd096b21359102fb68399c5d94e6f7680ccef70546b4e63ad3; NRO sha256
  222461b56363738085c837e5e5c39a346185da2ffd976156885664cbc3924f85. In the owner's out\release\ with
  release_notes_0.5.0.md. Not booted on a Switch as built: differs from the tested e62c854 only by
  the version strings, no --times, and the first-run settings text.

## 2026-11-22: TRANSLUCENCY in the port menu

- the owner sees big slowdowns in smoke, fog and dust, as in SA-nx. The decomp's renderer composes the
  blend list per pixel (SA_PC_BLEND_PIXEL: each pixel's fragments go to lists in GPU memory, a
  compute pass sorts and blends them), the PowerVR's contract; it also has host orders
  (SA_PC_BLEND_ORDER, DRAW, TRIANGLE: the triangles sorted farthest first on the CPU and blended
  by the GPU as they draw). play.c fixes PIXEL.
- Patch 0016: Scene::finish asks a weak native_blend_mode() each picture (-1 keeps open()'s).
  nx_menu.c answers PIXEL or TRIANGLE from the new TRANSLUCENCY item (ACCURATE or FAST), at once,
  saved as "translucency accurate|fast"; the start log says which. Default ACCURATE.

## 2026-11-22: Disc images on the Switch

- switch/loader/disc_extract.c: the loader unpacks a .gdi or a Redump .cue (raw 2352 or 2048
  sectors) found in disc/ or us_disc/, once (extracted.txt last): the ISO9660 filesystem of the
  first data track at LBA 45000 or later, absolute or track-relative LBAs. Redump cues: each FILE
  at the running LBA, from 45000 after "REM HIGH-DENSITY AREA". Tested on the owner's PC (gcc in the
  bridge's shell) against his own images: the Japanese Redump cue gives 2,106 files (967 MB, 23 s),
  the US GDI 2,832 files (972 MB, 11 s), every file the same as the earlier extractions (the old
  Japanese one had named ten PDATA files with a trailing dot).
- The loader unpacks disc/ (stops if that fails) and us_disc/ (Japanese only if that fails), then
  makes english/, then builds the image. README and the folder notes now ask for disc images.

## 2026-11-22: First-run builder (no game data in the release)

- The released image must hold no disc data, so the Switch rebuilds it from the tester's
  Japanese disc files on first launch. switch/tools/stub_build.sh builds a second image in
  ../sare-stub with every generated body (build/gen/**/*.inc) swapped for the decomp's zero stub
  (float bodies zeroed in their own shape by stub_floats.py, written by rename so the hard-linked
  main tree is never touched). Its layout differs from the real image (zero tables land in .bss,
  the compiler folds constants differently, 62651 relocations against 80544), so the two are
  compared symbol by symbol, not byte by byte.
- switch/tools/strip.py, three passes: (1) the content rows (sa1_data_*) against their disc files,
  runs of 16 bytes, gaps filled where runs line up, relocated words left alone; (2) each data
  symbol against its stub twin, the differing bytes found in the symbol's program's file,
  1ST_READ.BIN or any disc image's file (merged string literals lose their program); the game's
  32-bit pointers into the image are the port's and stay; (3) every 16-byte window of the image
  found anywhere on the disc (numpy hashes), grown byte by byte. It writes the image with those
  bytes zeroed (header flags 1, pad = table CRC) and SonicAdventureRe-nx.fill, and checks the fill
  gives the original image back. switch/tools/audit.py looks for any 16-byte window of a stripped
  image on the disc: the first strip (passes 1 and 2) left 819,810 windows (about 3 MB, mostly
  .data tables), passes 1 to 3 left 757. A fourth pass matches the image with the taken bytes
  zeroed (short disc strings beside them), and a pointer whose stub twin is also a pointer counts
  as the port's. Final, on d5d346c: 63,735,048 bytes taken from 254 disc files (328,894 pieces,
  fill table 5.3 MB), every differing body byte found on the disc, 147,611 bytes of 32-bit
  pointers and 5.5 MB of 64-bit pointers kept; the audit finds 65 windows, all short file names and
  labels in the decomp's own source (CHAOHALL.ADX, tgs_sousa_t4). The stripped image is 5.9 MB as
  xz. switch/tools/fill_check.py rebuilds it from decomp/orig as the loader does: identical to
  the full image.
- Test delivered to the owner (build d5d346c, the first strip): release layout in his SD folder, the
  old english folder and image moved to _to_delete/sd_0e130fd, his US disc's SONICADV in
  us_disc/. Not yet run on the Switch.
- Loader: with flags 1 it reads the fill table, loads each source from disc/, PRS-decodes it,
  checks size and SHA-1, copies the pieces, and saves SonicAdventureRe-nx.built.img (flags 2),
  used on later launches while its header matches.
- English on the Switch: switch/loader/english_build.c is make_english.py in C, the maps compiled
  in (switch/tools/maps2c.py). If us_disc/ sits beside the .nro and english/made.txt doesn't
  hold this build's map CRC, the loader makes english/. Host test on the owner's discs' files: every
  output file byte for byte equal to make_english.py's (1054 subtitles, 121 hints, 124 text
  files with 805 of 834 pieces, 17 voice units with 94 of 94 banks), 5 s on the PC.

## 2026-11-22: Tikal's hints in English

- The stages' hint pages are compiled into each stage module (modules/stgNN/scripts.c, the
  Japanese text in build/gen/modules/*/scripts/text.inc). The US stage modules (load base
  0x0c900000) hold the same page arrays per language, back to back: Japanese, then English,
  French, Spanish, German (Emerald Coast: runs at 68696 to 68752 Japanese, 68768 on English).
  common.hint_blocks finds the Japanese block and the English one after it; a Japanese page is
  matched to the US Japanese page by its words (118 of 121 identical). make_maps.py writes
  hint_map.txt (module, CRC of the Japanese page, block, run, page: numbers only): 121 of 277
  pages mapped, in 12 modules (adv02, stg01 to stg10); the rest are mostly the developers' test
  pages the US dropped. stg12 (Hot Shelter): the hint block isn't found yet. make_english.py
  writes english/hints.txt from the US disc's STGnn.PRS.
- Patch 0015: run_pages (src/event/script.c) asks native_hook_hint_parts for each page's parts,
  and shows them one after the other, each with its share of the page's steps. nx_english.c
  answers from hints.txt by the CRC of the Japanese page. Host test: Emerald Coast's panel hint
  gives "Jump on panel No. 1. / It will take you to panels 2 and 3."

## 2026-11-22: In-game voice banks in English

- The sound units line up after all: listing each unit's banks (V_AMY, V_SONICTAILS, ALIFE_SONIC)
  shows the same type, number, address and room for every bank in both releases; most banks are
  rooms only (data -1). The banks holding sounds (4 to 12 a unit) all have US matches, so the
  "6 of 21" of 11-21 was every bank with sounds. common.swap_banks now counts only those, and
  make_english.py leaves a unit Japanese if any such bank lacks a match. On the owner's discs: 17 units,
  94 of 94 sound banks in English, each read back equal to the US bytes. Sound takes its banks
  from the bytes the game loads (engine/sa1/sound.c, sdBankDownload), not from the disc, so the
  file hook covers them; the VOICES setting picks them at the next load.
- The bridge served stale copies of a staged file when its path was reused; staging under a fresh
  folder name avoids it.

## 2026-11-21: The opening title screen stays Japanese

- the owner (test bbc8170): the menus are English; the title screen looked wrong. Decoding both
  releases' textures (a dumper outside the repo) showed why: the US AVA_GTITLE0 has its 8
  backgrounds with the logo cut out and 7 textures more (the logo, PressStartButton, the
  copyright, the Sonic Team mark) the US game draws on top; the Japanese game draws only the 8.
  AVA_TITLE (the menu), AVA_OPTION, AVA_FILESEL and TUTOMSG_SONIC line up texture for texture
  (the extra US textures come at the end). make_english.py keeps AVA_GTITLE0 Japanese
  (KEEP_JAPANESE); the owner agreed: it reads Sonic Adventure and its Press Start is English already.

## 2026-11-21: English textures reach the renderer

- the owner (test 213ab55): the skip crash is gone (no report), voices fine, but the title, character
  select, file select, options, sound test and the pause menu all still Japanese. The log shows
  the swap happened ("english: AVA_TITLE.PRS from the english folder", CON_REGULAR, AVA_CHSEL,
  MR_MES_S ...). Cause: the decomp's renderer never takes the texels the game loads. At start it
  indexes every texture file in its roots' SONICADV folders (engine/pc/pvr.cpp, Textures::index)
  and draws from those files by global index and file id (sa_name_id of "SONICADV/NAME"), and its
  roots were the build root and the disc.
- Patch 0014: native_play_open asks a weak native_play_texture_root() for a folder to put ahead of
  them; the first root holding a name wins. nx_english.c gives the english folder where the text
  is English at start, so the menus' pictures follow TEXT at the next start (the menu says so).
  make_english.py now writes the swapped files into english/SONICADV/, and the file hook points
  there; file ids match on both sides, as both name the file "SONICADV/NAME".

## 2026-11-21: Skip crash fixed; English menus, HUD, missions and field messages

- the owner (test 6021ce7): English cutscene voices and subtitles work. A crash in or after the cutscene
  after the Egg Hornet, as he skipped it with +. Atmosphere's report 01795287561: Data Abort at
  0x20, PC image+0x3255fc = event_mover_step (events/shared/mover.c:84, MOVER_FOLLOW) from
  event_mover_pay/event_mover_skip_step (the decomp's skip): move->base->work was null, the
  followed task gone, and its pos read at 0x20. On the Dreamcast a read near 0 lands in the boot
  ROM; on a host it faults. Upstream (004279f) has the same code. Patch 0012 (native only): a
  follow or facing move whose base, or any move whose target, has no work goes to its _END mode.
- Patch 0013: native_hook_disc_file in gdfs.c's find_nocase may give a host path in place of a
  disc file. nx_english.c answers from english/files.txt ("text NAME" or "voices NAME"), by the
  TEXT and VOICES settings.
- tools/english/make_english.py now also takes the Japanese disc, and writes, under the Japanese
  names: texture files (PRS and PVR) with each texture whose size matches the US one in the same
  place taken from the US (the Japanese GBIX stays, the PVM rewritten and stored as literal-only
  PRS, which the 2 MB work area holds: the largest is 1.4 MB), and the fields' message files
  (MR_MES, SS_MES, PAST_MES) moved to the Japanese base (Station Square's US files load at
  0x0cb46000, the Japanese at 0x0cb48000) with each script's voice renumbered to a Japanese clip
  that maps to the same US clip, or 0x2000 plus the US number (nx_english.c plays that one with
  English voices only). On the owner's discs: 125 files, 813 of 842 textures or message files in
  English; 18 texture archives match whole, the menus and tutorials keep 1 to 7 Japanese textures
  each (sizes differ).
- Sound units (V_*.MLT, ALIFE_*, CASINO_*, SKYDECK): held back. Only 4 to 12 of each unit's ~20
  banks keep the Japanese type, number, address and size; a unit mixing US and Japanese banks
  could play the wrong sounds. common.swap_banks stays for the next look.
- Not covered: the stages' hint scripts (Tikal's orbs), which are compiled into 1ST_READ.

## 2026-11-21: English layer, part 1: cutscene voices and subtitles

- the owner (test 3f2a27b): the pause menu shows, resume stays resumed, quitting returns to the home
  menu, the face is fixed, and Resolution works. All five fixes confirmed.
- Patch 0011 (native only, NONMATCHING and not DREAMCAST): three hooks. native_hook_voice_file in
  engine/sa1/sound.c's ADXT_StartAfs may give a voice's bytes in the archive's place;
  native_hook_event_text in event_message_show (src/event/play.c) may give the subtitle to show,
  and NULL from event_message_close; native_hook_event_text_next, polled in event_wait's loop,
  shows a later part of a line the US release split in two. Defaults in bare.c do nothing.
- switch/runtime/nx_english.c answers them from sdmc:/switch/SonicAdventureRe-nx/english/, and
  wraps loader_read_prs to know the cutscene (ev%04x.prs): 148 Japanese lines read differently in
  English from one cutscene to another, so subtitles are keyed by cutscene and CRC-32 of the
  Japanese bytes, with the CRC alone as the fallback. A split line's next part shows after its
  share (by letters) of the English clip, else 3 s a part. Host test on the converter's output:
  ev0006's split line shows its second part 3.3 s in, clip 954 is 62,838 bytes.
- tools/english: common.py (PRS, AFS, the US cutscenes' five-language tables; its PRS matches
  SA-nx's on all 236 US cutscene modules), make_maps.py (developer side) writes subtitle_map.txt
  (1,054 subtitles: cutscene, CRC, US table, first slot, slot count) and voice_map.txt (1,869
  clips), numbers only. make_english.py (player side, Python's standard library only) makes the
  english folder from a US disc's files: EVENT_ADX_US.AFS copied, voice_map.txt, subtitles.txt.
- Settings "voices" and "text" (english or japanese, English by default), and VOICES and TEXT in
  the port menu, both live. The menu panel grew to 344 pixels for the two lines.
- Not covered yet: 83 Japanese subtitles in cutscenes whose line counts don't match the US, the
  219 clips the US cut or rewrote (they stay Japanese), and everything outside cutscenes (menus,
  HUD, in-game voice banks, town messages, hints): the next build.

## 2026-11-21: Pause menu shows, Start counted once, clean quit

Findings from the owner's d902b62 logs, then reproduced on the decomp's x86 build (Emerald Coast,
headless, Start held for 6 vsyncs by a local test hack in pads.c, not committed):

- Re-pause: in each of the 20 or so pauses, Start resumed the game (stage_mode 15 to 14) and the
  game paused again two scans later with no new Start from the window. Cause, stage_run_action and
  stage_run_field: they sum a frame's presses starting from what per_info holds as the frame
  begins, and write the sum back at its end. On the Dreamcast the pad server refills that record
  at each vsync; a host's pads fill it only at a scan. So the sum began with the frame before's sum
  (a press counted in two frames: Start ended the pause, then opened it again), and the last
  step's scan was never added (a press there was lost). On the x86 build Start at vsync 700 (step 1)
  never paused before the fix. Patch 0009, native only (NONMATCHING and not DREAMCAST): the sum
  starts empty and takes the last step's scan. After it, Start at 700 and at 701 both pause, and
  the next Start resumes play for good (x86: stage_mode 15 at 701 or 703, 14 at 823).
- Invisible pause menu: the x86 build showed no menu either, so it is the decomp's renderer, not
  the Switch. The trace had shown the draws reach the records (544 sprites, 6320 screen strips
  while paused) and pictures kept reaching the screen. The menu's tiles and words are njDrawTexture
  strips at 1/z of 9 and 10; the PowerVR takes any 1/z, but draw.vert set depth to 1/z with the
  near plane at z = 1, so Vulkan's depth range cut everything nearer, the whole menu. Patch 0010:
  depth is 1/z times 1/256 (a power of two, so every compare and the blend list's order stay
  exact in D32_SFLOAT); world draws are still cut at z = 1, now by the clip distance (with
  BATCH_NEAR's near clip as before). x86: the menu shows (banner, three choices, the tiled back and
  its shadow); 48 gameplay pictures before the pause are pixel for pixel the same as before.
- Quit crash: every quit through exit() ends the log at "exit(0) called; the C library's handlers
  run next", including one from before the port menu (399.4 s); the game's own _exit path leaves
  cleanly. The decomp registers no atexit handler, so __wrap_exit now stops the sound thread
  (window_sound_close, new nx_sound_stop), flushes, and takes the _exit end (nx_finish).
- The next build also carries the face fix (507b244) and the live Resolution setting (f6c7a7e).

## 2026-11-21: Resolution in the port menu, live

- the owner (test d902b62): the port menu works and looks right; the menu's quit crashes instead of
  returning to the home menu (logs to come); he wants "Resolution" with 1X, 1.5X and 2X, applied
  without a restart. 2X held 30 fps but not 60, which he is fine with.
- Patch 0006: Raster::rescale(halves) sets the target in halves of 640x480 (2, 3, 4: 640x480,
  960x720, 1280x960); fit, run at every submit with a window, makes the target anew at the next
  picture (make_target waits for the device first). The backend asks the host's
  native_resolution_halves at each pad read and passes a change on.
- nx_menu.c: RESOLUTION 1X, 1.5X, 2X, at once, saved as "scale 1x|1.5x|2x" in settings.txt (the
  decomp's own resolution line stays 1). A settings file without a scale line takes 2x where its
  resolution is 2, else 1x, and the log states the resolution in force.

## 2026-11-21: Sonic's face fixed (on the PC copy): split shape keys

- Cause found on the decomp's x86 build (gdb and logging in player_mouth_blend): in the talking
  close-up, the mouth blends between shapes from sonic_mouth_shapes (21 keys of 222 points). Key 7
  read as 222 points was far from shape 0 point by point (6.5 units, the head's whole size) but close
  as a set: its array in the generated data held only 59 points. config/1st_read/data.txt splits
  the key at 0x8c1fbec8 into vertices[59] and floats[489]. On the Dreamcast build the two lie
  together (content.ld places every row at its address); the native link places each row on its
  own, so the blend read 163 points from whatever followed. The scramble ramped in over 4 pictures
  and out over 6, exactly the script's 0 to 7 (steps of 1/4) and 7 to 6 (1/6).
- A scan of every NJS_MKEY_P list in all modules' generated data finds seven keys shorter than
  their lists: Sonic's mouth keys 7, 9, 10, 12 and 13, and one key each in the lists at 0x8c396e4c
  (239 points) and 0x8c3a2cec (201 points), another character's mouth or face. In each, the rows
  that follow add up to exactly the missing points.
- Patch 0008 merges each into one vertices[N] row (the bytes are the same, so the Dreamcast build
  is unchanged). The PC copy, rebuilt, renders the scene's pictures 1586 to 1595 with the mouth
  moving whole (face_fixed.png). The Switch build takes it in the next test.

## 2026-11-21: the port menu, and a second pause trace

- Port menu (nx_menu.c, drawn by nx_overlay.c with a 5x7 font): Minus and + open it in place of
  quitting. The game waits in the backend's pad read (patch 0006: sa_input_sample loops on the
  host's native_menu_step) while the raster shows the last picture again each vsync with the menu
  over it (Raster::again: the color target, or its fitted copy, blitted to a fresh swapchain image,
  then the overlay). Sound goes quiet while it shows (the feed thread writes silence and leaves the
  game's mixer alone). Items: resume, widescreen and 60 fps (the decomp's port_toggles, at once),
  picture size and captures (next start), volume (the feed scales its samples), the frame counter,
  quit (A twice). Each change rewrites settings.txt, which now carries volume and counter too.
  The game never sees the + of the menu's chord, nor a + still held as the menu closes.
- Pause trace, second form: every scan for 10 past a pause's end; every newly pressed Start the
  window reports, with its pad read's number; and the 2D draws taken while paused (the link wraps
  sa1_record_sprite, _screen_strip, _text and _triangles), summed at each pause's end.

## 2026-11-21: sound confirmed by ear

- the owner: sound has worked in builds be54505 and 4ca411a (audout, the game's 44.1 kHz resampled
  to 48 kHz), matching the logs' steady 47 buffers a second played and returned.

## 2026-11-21: test 4ca411a read: 60 fps holds with captures off; pause traced

- Captures off ("nx: capture: off"), handheld at 1020/307/1331 MHz. Emerald Coast acts 1 and 2
  (game_mode 9): 30 of 39 heartbeats at 60 or 61 frames a second on screen, dips of 43, 52, 53
  and 59 (one near the act's end, where the owner saw the dolphins), two 30s at a load and an event.
  So the per-picture readback was what held 60 back.
- Pause trace (about 20 pauses): pictures keep reaching the screen while paused (one a scan), so
  the menu is not drawn into them; pause_choice stays 0 (continue); the stick reads 0,0. A fresh
  press of + resumes (stage_mode 15 to 14), and two scans later the stage pauses again with + still
  held, so the game saw a second press in a scan the trace did not log (it stopped at the pause's
  end). Next: log every scan to 10 past a pause's end, each newly pressed + the window reports and
  from which read, and the pause menu's draws (sprites and texture) while paused.

## 2026-11-21: captures off for speed; a pause trace

- the owner: no captures this build, keep the feature. Patch 0007: play.c turns the readback on only
  where the host's native_play_capture_on gives nonzero; nx_capture.c gives settings.txt's
  "capture" line (absent or 0: off, so no picture is waited on). A stick click with captures off
  is logged. New settings files carry "capture 0".
- nx_trace.c (the link wraps native_hook_pad_scan and native_hook_pad_scanned): from a pause's
  start to its end, every change in stage_mode, pause_choice, pause_hidden, the pad's held and
  newly pressed buttons and stick, with the pictures put on the screen since the pause began
  (nx_overlay_presents), at most 400 lines a pause.
- The face, on the PC copy: no shape key weight falls outside 0 to 1 in the scene
  (action_sample_shape checked over 3,000 vsyncs), and the records copy a morphed model's points
  at the call (records.c model_hash and memcpy), so neither extrapolation nor a shared buffer read
  late explains it. Next suspect: the shape link (action_link.c), which morphs two shapes into
  parts of _nj_shape_buf_ and blends them as the mouth changes shape.

## 2026-11-21: test be54505 read: saves work, audout plays, 60 fps is not held (my readback)

- Saving works: three "storage: saved .../card.bin, 131072 bytes, read back the same", and the
  next start loaded the save (file select, then Emerald Coast).
- audout runs: buffers played and returned at about 47 a second (48000 / 1024), all run long,
  none missing; buffers in memory type 0x5 (heap), RW.
- Speed: the owner saw Emerald Coast below 60, and he was right. The heartbeat's game clock ran at
  43 to 54 vsyncs a real second for most of the level, and the on-screen counter agreed (39 to 54,
  60 only in stretches). My earlier "60.0 pictures a second" counted pictures per 2,000 game
  vsyncs, not per real second; the game's own clock had fallen behind real time, which play.c's
  late and lost counts showed (every picture late). I dismissed them; that was wrong.
- Leading cause: the captures. play.c reads each picture back through sa_pc_read, which waits
  (raster.wait) for the picture just submitted, including its wait for a swapchain image under
  FIFO. That serializes the game's thread and the GPU every frame. Per picture: give median 4.8 ms,
  end 1.5, CPU 4.5, GPU 3.9 (p99 7.4), so overlapped they fit 16.7 ms; serialized with the vsync
  wait they don't. Not yet proven: the next build reads each picture one late (its fence long
  signaled) and lets both stick clicks turn recording off and on, to compare in one session.
- The face: upstream 004279f renders the same scattered face in the same pictures (1560 to 1575 of
  the Chaos 0 run). It is unfixed upstream; ours to find in the shape motion code.
- Pause on PC by key script is not a valid test: scripted keys count as a recording, and Start
  then ends the act (stage.c) rather than pausing. Pause stays to be traced on the Switch.

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
