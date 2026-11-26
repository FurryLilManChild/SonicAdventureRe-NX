# Super Sonic in trial mode: plan

Goal for the next update: Super Sonic playable in trial mode, without changing story mode.
Our own code, built on the decomp. The SADX fan mods are read only for how they solved
problems; none of their code is used, and neither repository posts a licence.

## What Mark decided (2026-11-26)

1. A **Super Sonic** slot on the trial wheel (the last story's slot), with Sonic's trial
   stages and the Perfect Chaos fight.
2. In Sonic's stages he starts as Sonic. With 50 rings, a transform button turns him Super.
3. Once Super, his rings drain.
4. Super Sonic's trial times are kept apart from Sonic's.

## What the code shows

- **Trial mode skips the last story on purpose.** When the character wheel opens in trial mode
  on the last story's slot, it moves to Sonic (modules/advertise/character_select.c). The trial
  stage and mini game lists hold six stories; the last story has none (modules/advertise/trial.c).
- **Only the Perfect Chaos fight turns Super Sonic on.** The B_CHAOS7 module asks for
  PLAYER_MODE_SUPER (modules/b_chaos7/chaos7.c). The change itself is Sonic's own code in
  1ST_READ (src/player/sonic.c): the super flag, the aura task, and his constants (less
  slowdown, more air steering, a higher center). Any stage could make the same request.
- **Much of Super Sonic lives in the Perfect Chaos module, not in Sonic's code:**
  - his animations and model, which sonic.c reaches in the module's head section;
  - his aura, wake, spin balls and dash (modules/b_chaos7/super.c);
  - the ring drain: a ring every so many steps, and when none are left he turns back
    (modules/b_chaos7/fight.c).

  His textures are a disc file of their own, SUPERSONIC.PVM (73 textures), loaded with the
  fight's list. On the Dreamcast only one module sits at 0x0c900000 at a time, and in an action
  stage that is the stage's own module. So Super Sonic in an action stage needs this data
  available alongside the stage. This is the main technical question.
- **His own move set is small:** stand, walk, path, fall, launch, spring, homing attack and
  jump. He has no spin dash, rolling, light dash, boards or pinball.
- **Known original bug:** when the super form ends, two of Sonic's constants keep their Super
  values (sonic.c's sonic_super_destroy, marked @bug logic in the decomp). Story mode never
  shows it, because Perfect Chaos ends the game; turning back in a stage would.
- **Ground angle:** a player.c function that sets him upright skips its speed check for Super
  Sonic (player_turn_upright), a Perfect Chaos assumption.
- Getting knocked down already ends the super form.

## What the SADX mods show (kellsnc/sadx-super-sonic, after sonicretro/sadx-super-sonic)

Their ways of solving things, to check against our code:

- **Transform:** in the air after a jump, the action button with 50 rings (and the last story
  cleared). The same button in the air turns him back. While Super he is also invincible.
- **Drain:** one ring a second; at 0 he turns back into Sonic, with no life lost.
- **All of Sonic's moves while Super:** the biggest idea. Instead of Super Sonic's own small
  move set, he keeps Sonic's normal modes (spin dash, light dash, rolling, everything) with
  Super Sonic's animations and model swapped in. Where Sonic's normal mode has a Super twin,
  they map it back to the normal one. In their simpler mode, he turns back instead on the acts
  where Super doesn't work: Casinopolis' pinball acts and one Speed Highway act.
- **Look:** Super Sonic's texture list swapped in when Sonic is drawn while Super. They also
  add his own life icon.
- **Physics:** the original Super values fly him over whole levels on a jump. They offer softer
  values, and fix springs, jump panels, downward slopes and the upright check above. They
  also stop him passing through water surfaces.
- **Music:** the Super theme while he is Super, and the stage music back when he turns back,
  plus the transform sound and voice.

## Our design

1. **Trial wheel:** the last story's slot opens in trial mode once the last story is cleared,
   or with TRIALS ALL OPEN. It shows as Super Sonic. Its action stages are Sonic's trial
   stages; its mini game is the Perfect Chaos fight, as in the story.
2. **Transform:** in Sonic's stages, from this slot only, with 50 or more rings. In the air,
   the button (to choose: the action button, or a port-menu control) turns him Super:
   - he becomes invincible, with the aura, the transform sound, and the Super music;
   - the same button in the air turns him back.

   Story mode and Sonic's own trial slot don't change.
3. **Drain:** one ring a second while Super. At 0 he turns back and plays on as Sonic.
4. **Moves:** Sonic's full move set while Super, with Super Sonic's animations, model and
   textures, as the mod found works best. Where a part of a stage can't work as Super (pinball,
   boards), he turns back for it.
5. **Physics:** start from the original Super values; soften them where play shows he flies
   over a stage. Fix the constants bug and the upright check in port builds.
6. **Times:** trial records for this slot are kept apart from Sonic's. Where the save has no
   room for them, they go in a file of the port's own next to card.bin.

## Phases

1. **Groundwork (no test for Mark).**
   - Find where the native build keeps the Perfect Chaos module's Super Sonic data
     (animations, model, effects), and how to have it present in an action stage.
   - Load SUPERSONIC.PVM there too.
   - List everything that tests the super flag or Super Sonic's modes.
   - Find where trial times are saved.
2. **Proof test (one Switch build).** Sonic in Emerald Coast act 1 from the trial slot,
   with the transform button. Mark checks:
   - he turns Super at 50 rings, with the aura, sound and music;
   - he moves, jumps, homes in and spin dashes as Super;
   - the rings drain and he turns back at 0;
   - the act can be finished.

   This is the go or no-go for the whole feature.
3. **The trial menu.** The Super Sonic slot, its stage and mini game lists, act select, its
   text in English and Japanese, and its separate times.
4. **Stage by stage.** Each of Sonic's acts as Super: gimmicks, springs, water, physics
   tuning. Mark plays each act. This is likely the longest phase.
5. **Docs and release.**

## Phase 1 findings (2026-11-26)

- **The data is there.** The native build links every program at once, each with its own data
  and bss (engine/native/loads.c); reading a module only puts its starting data back. So
  B_CHAOS7's Super Sonic model, actions and effects (super_aura_start and the rest) are in the
  image in every stage, and sonic.c already reaches them by name. The Dreamcast's one-module
  limit doesn't apply on the Switch.
- **Textures:** super_sonic_texlist, filled from SUPERSONIC.PVM by chaos7_act_textures
  (texture.c's list for Chaos 7's act). In another stage it must be loaded by hand (and
  released when the stage ends).
- **Drawing:** sonic_draw picks super_sonic_texlist for actions ACTION_SUPER_STAND to
  ACTION_SUPER_BALL, and each action row names its model (super_sonic_object for Super's
  rows). Super's rows play Sonic's own motions for spring, fall and ball on Super's model, so
  the two share a skeleton (SONIC_NODES). For Sonic's full move set while Super: a second copy
  of the action table with Super's model in Sonic's rows, and the texture choice by the super
  flag instead of the action number.
- **Transform button:** Sonic's jump (SONIC_JUMP) never reads the action button, so the action
  button in the air is free, as the mod uses it.
- **Music:** MUSIC_SPRSONIC is the Super Sonic theme (src/sound/stream.h).
- **Trial times:** the save keeps fixed records per character (src/save/save.h:
  SAVE_STAGES 32, Sonic's ten first), with no room for an eighth. Super Sonic's go in a file
  of the port's own.

## Still open (for later phases)

- Which button transforms. Sonic's action button is already used in the air (homing attack is
  jump again, so the action button may be free there; to check).
- Whether Super's animations need fixes for Sonic's moves that never had Super versions.
