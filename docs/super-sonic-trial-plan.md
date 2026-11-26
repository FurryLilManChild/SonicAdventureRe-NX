# Super Sonic in trial mode: plan (draft for review)

Goal for the next update: Super Sonic playable in trial mode, without changing story mode.

## What the code shows

- **Trial mode skips the last story on purpose.** When the character wheel opens in trial mode
  on the last story's slot, it moves to Sonic (modules/advertise/character_select.c). The trial
  stage and mini game lists hold six stories; the last story has none (modules/advertise/trial.c).
- **Only the Perfect Chaos fight turns Super Sonic on.** The B_CHAOS7 module asks for
  PLAYER_MODE_SUPER on its second step (modules/b_chaos7/chaos7.c). The change itself is
  Sonic's own code in 1ST_READ (src/player/sonic.c): the super flag, the aura task, and his
  constants (less slowdown, more air steering, a higher center). Any stage could make the same
  request.
- **Much of Super Sonic lives in the Perfect Chaos module, not in Sonic's code:**
  - his animations, model and textures, which sonic.c reaches in the module's head section;
  - his aura, wake, spin balls and dash (modules/b_chaos7/super.c);
  - the ring drain: a ring every so many steps, and when none are left he turns back
    (modules/b_chaos7/fight.c).

  On the Dreamcast only one module sits at 0x0c900000 at a time, and in an action stage that is
  the stage's own module. So Super Sonic in an action stage needs this data available
  alongside the stage. This is the main technical question.
- **His move set is small:** stand, walk, path, fall, launch, spring, homing attack and jump.
  He has no spin dash, rolling, light dash, snowboard, sand board or pinball. Stage gimmicks
  built on those (Ice Cap's board, Sand Hill, Casinopolis' pinball, rolling under things) have
  no Super version.
- **Known original bug:** when the super form ends, two of Sonic's constants keep their Super
  values (sonic.c's sonic_super_destroy, marked @bug logic in the decomp). Story mode never
  shows it, because Perfect Chaos ends the game; turning back in a stage would.
- Getting knocked down (PLAYER_MODE_DOWN) already ends the super form.

## Proposed design (for Mark to confirm)

1. The last story's slot on the trial wheel opens once the last story is cleared (or with
   TRIALS ALL OPEN), as **Super Sonic**.
2. Its action stage list is Sonic's trial stages, played as Super Sonic from the start. Its
   mini game list is the Perfect Chaos fight.
3. Rings drain as in the Perfect Chaos fight. At 0 rings he turns back into Sonic and finishes
   the act as Sonic, rather than losing a life. That needs the constants bug above fixed (port
   builds only).
4. Where a stage needs a move he lacks (boards, pinball, rolling), he turns back for that part,
   or that act is left out of his list. Each act is checked in play.
5. Trial best times and records for Super Sonic runs are kept apart from Sonic's, or not saved
   at all, so Sonic's records stay honest.

## Phases

1. **Groundwork (no test for Mark).** Find where the native build keeps the module's Super
   Sonic data, and whether it can be present in another stage. List every place that tests the
   super flag or Super Sonic's modes. Read the ring drain and the end of the form.
2. **Proof test (one Switch build).** A test switch makes Sonic super at the start of Emerald
   Coast act 1 in trial. Mark checks:
   - he looks right, with the aura and effects;
   - he moves, jumps and homes in;
   - the music and sounds play;
   - the act can be finished.

   This is the go or no-go for the whole feature.
3. **The trial menu.** The last story's slot in trial mode, its stage and mini game lists, act
   select, and its text (English and Japanese). Test: the slot appears only when it should, and
   each list starts the right stage.
4. **Stage by stage.** Ring drain, turning back, the gimmicks, and tuning if his speed breaks a
   stage. Mark plays each act. This is likely the longest phase.
5. **Records, docs and release.** README, notes, and then the next release.

## Open questions for Mark

- Is the design above what you mean by "playable in trial mode"? The other readings are a
  Perfect Chaos rematch only, or a transform button in Sonic's own trial stages.
- Ring drain and turning back, or Super Sonic for the whole act with no drain?
- Super Sonic's times: kept separately, or not saved at all?

Not checked: how Sonic Adventure DX handles Super Sonic outside the last story. I couldn't
confirm it from a source, so it isn't used as a model here.
