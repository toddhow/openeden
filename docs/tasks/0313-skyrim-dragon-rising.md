# Task 0313: Dragon Rising (MQ104) finishes: the game's own dragon death sequence, the soul and the word

Branch `mq104-0313` from `4ac6fb8c89` (task 0308). Windows only, not pushed. Behind `[extension.skyrimse] main quest whiterun` (off by default; no new setting).
Same hand wrote the tests and the code; the tests were written first and run red against stubs (below); no fresh reviewer.

## 1. The cause (found by tracing, not by guessing)

Task 0308 stopped at: after Mirmulnir dies, MQ104 reaches stage 80 and then nothing. With `OPENEDEN_PAPYRUS_NATIVE_TRACE` and a trace line in
`GetDistance` (positions it used) the picture is plain:

- `dragonActorSCRIPT` (state `alive`, `OnDeath`) goes to `deadAndWaiting`; its `onBeginState` calls `ShouldMiraakAppear` (false) and then
  **`while getDistance(player) > deathFXrange: utility.wait(1.0)`**. The same distance test is in `MQ104DragonScript.OnUpdate` (stage 85).
- `GetDistance` read the **placement** of the reference (`PapyrusGameData::referencePosition`): in the real run the dragon was placed at z 5697, it
  fell and lay at z 3899, the player stood at z 3922. 1,774 units against a range of 1,024: the loop never ended, so `MQKillDragonScript.DeathSequence`
  (which gives `ModActorValue("dragonsouls")` and then sets MQ104 stage 90) was never called. (0308's guess was right; it was not proven.)

A first fix (a watched-place table in the cell source) still showed no live place: the table held 64 places and the scripts ask for the distance of
hundreds of objects at start-up, so the dragon was never admitted (found by the `GetDistance` trace). Only actors the core knows are watched now.

## 2. What is built

- **Live places** (`include/game_skyrimse/live_places.hpp`, header only): the first ask for an actor watches it, `SkyrimCellSource::update` reads
  `ActorRequestWriter::actorPosition` for the watched ones once an update (at most 64). `PapyrusWorldHooks::livePosition` (additive virtual) is asked by
  `PapyrusHost::positionOf` after the player, a script's own `MoveTo` and created references, before the data's placement. Used for `GetDistance`
  and anything else that reads an actor's place. Only with `main quest whiterun`; with it off `livePosition` is never asked.
- **Souls:** `Actor.ModActorValue("dragonsouls", n)` on the player goes to the shout state (`ShoutState::addSouls`, the message "Dragon soul absorbed")
  through the existing `actorValueSet` hook; the pack answers `GetActorValue("dragonsouls")` with the shout state's count, so the host keeps no
  offset of its own (`avMove`: "when the pack knows the number it is that number", like health). With the setting off the host's old behaviour is
  unchanged (the hook says "unknown").
- **Task 0253's hand-written absorption steps aside** for a dragon that has the game's `dragonactorscript` attached
  (`SkyrimCombat::setDragonSoulByScript`, `PapyrusHost::hasScript`), so the player does not get two souls. Dragons with no such script (a test
  dragon, a mod's) keep 0253's absorption.
- Two trace lines: `LIVEPLACE <ref> is at x y z` (once per watched actor), `SHOUTS the dragon's death sequence gave a soul: N souls`, and
  `SHOUTS word learned: <name>`; `GetDistance` writes the two places it measured with the native trace on.

## 3. Evidence

Tests (`SkyrimDragonRising` 6, `SkyrimLivePlaces` 4, two `CombatDragon` tests appended to `testskyrimsecombatdragon.cpp`; 12 in all).
Red run against stubs (the live branch, the `dragonsouls` branch, `hasScript` and the combat hook switched off): **5 of 12 failed**
(live place, soul to the pack, `hasScript`, the combat step-aside, and one soul test whose own wrong assertion was then corrected); the 3 that pin the
older behaviour passed, and **the 4 `SkyrimLivePlaces` tests passed in the red run (not red)**: the header-only class was restored to its real
state in the middle of the red build, which was too slow to repeat; they are tests of a new class that did not compile before it existed.
Green: the 12 pass; `*Skyrim*:*Papyrus*:*Dragon*:*Combat*:*Shout*` 4,051 pass. No existing test edited.

In game (`tools/skywhiterunmq_check`, `--start @Tamriel,7795,-37461,5800`, `combat test hostile = MQ104Dragon`, `dragons test kill at = 25`,
`story test quest = 02610C:60`, input script of shots; see the commands in the stage log):
Mirmulnir dies, `GetDistance` now reads the place it lies at, **MQ104 stage 85 is set, the game's own sequence runs
(`AddItem` of the underskin, effects, waits), `ModActorValue("dragonsouls")` gives one soul (`SHOUTS ... gave a soul: 1 souls`: 1, not 2, so 0253 stood
aside), stage 90 is set, `Game.TeachWord` and `UnlockWord` run: "Word of Power learned: Fus" and the game's message "Use the Shouts section of the
Magic Menu to equip your Unrelenting Force Shout." are on the screen** (frames `I:/Projects/OpenEden-work/mq313/keep/shot_word_learned.png` and `shot_shout_message.png`).
Three runs (t5, t6, t7, run together, ended after the sequence): the same sequence of lines in each (live place, stage 85, soul 1, stage 90, word
Fus); the first live z differs between runs (5378 against 3898: the dragon was still falling when first asked).

Morrowind unchanged: `tools/worldobjects_check` 110 objects OK (111 result lines, equal to `integ2`'s build), the fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600) is `1c5d47ad0b5dc32d` for both builds. The build of `integ2` is the reference (not a build of the exact parent commit).

## 4. Not done, not run

- **The guards' "Dragonborn" lines are not reached.** Stage 90 starts `MQ104OutroScene` (`Scene.Start 0005A6BE`) but the scene logs no start and no lines:
  its actors are the guards the quest's aliases hold, which the earlier stages (messenger, the watchtower, the guards at the fort) fill and this harness
  skips (`story test quest = 02610C:60`). Needs the stages between 10 and 60 run for real (gap 4 of task 0308).
- The sequence's art (underskin armour, `PlaySubGraphAnimation`, `ImageSpaceModifier.Apply`, `Actor.EquipItem`, `Sound.Play`, effect shaders) is
  tolerant no-ops here (logged once); the dragon's skin does not burn away. `Actor.SetGhost` is a no-op too (`actor.IsGhost` is unregistered).
- No frame of the soul message itself (the log line is the evidence; the message is shown for a few seconds and the frame timing of a loaded machine
  moved it out of the shots); the `H` shout menu did not open in the frame taken after the key (not investigated).
- Saves in the middle of the sequence (a stack parked in `Utility.Wait` is saved by the host, not tried), a dragon killed by the player's own blows
  (the test setting killed it), a second dragon, the Linux gate, GCC `-Werror` (hand check of the traps), clang-format 14 (by hand, 120 columns),
  clang-tidy, the Lua tests, `openmw-tests`, a fresh review.

## 5. Decisions for toddhow

1. **With `main quest whiterun` on, the game's own dragon death sequence takes the soul and 0253's hand-written absorption steps aside for a dragon
   that carries `dragonactorscript`.** Recorded as a decision because it changes who gives the soul (the game's script, after about 20 s of its
   waits, instead of 4.5 s after death); with the setting off nothing changes.
2. **An actor's place for a script is the world's live place** (only with the setting): a general fix for every `GetDistance` and `GetPosition` of a
   placed actor, not only the dragon. It could change other quests' behaviour that relied on the placement (none seen in the runs).
3. `Actor.ModActorValue("dragonsouls")` only ever raises the soul count (a lower value is ignored: the shout state has no way to remove one).
4. Existing tests edited: **none** (two `TEST_F` were appended to `testskyrimsecombatdragon.cpp`, one list line added to `apps/components_tests/CMakeLists.txt`).

## 6. Stage log

- Edits outside new files: `papyrus_host.{hpp,cpp}` (`livePosition`, `hasScript`, the `dragonsouls` branch, the `GetDistance` trace),
  `papyrus_world.hpp`, `combat.hpp`, `combat_dragon.cpp`, `neutral_world.hpp`, `skyrimse_game_pack.cpp`, `shouts_service.cpp` (one log line).
- Harness: `I:/Projects/OpenEden-work/mq313/run.sh <name> <input> <frames>` (a copy of 0308's), with `LOADARG`, `SETTINGS_EXTRA` as above.
