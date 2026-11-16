# Task 0227: sneaking and detection in a Skyrim session

Status: BUILT on branch `sneak` (Windows only, local commit, not pushed), behind the pack setting
`[extension.skyrimse] sneak` (default false). Same hand wrote tests and code (see the stage log for what was run red).

Design doc section: `docs/design/game-packs.md`, M-S5 (combat) and M-S7 (systems); builds on task 0179 (perks), 0180
(combat HUD), 0197 (sight rays), 0200 (progression), 0204 (crime) and 0208 (pickpocketing).

Goal: a sneaking player is noticed (or not) by nearby NPCs, the HUD shows a sneak eye, an unnoticed blow multiplies
damage, Sneak trains, and crime witnesses and pickpocketing ask who noticed the player.

Inherited code touched: `SubjectState` (`sdk/include/openeden/game/combat_rules.hpp`), `PhysicsSystem::NeutralCombatWorldImpl::subjectState`
(`apps/openmw/mwphysics/physicssystem.cpp`), the player hook in `World::World` (`apps/openmw/mwworld/worldimp.cpp`).

Constraints: off by default; with the setting off nothing of it exists (no system, no namespace, the Lua script is not
started); no virtual added to `CombatRequestWriter`; determinism (no clock, no random numbers; the system's time is the
`seconds` of `combatUpdate`); no allocation in steady state (vectors are reused; the log line is built only when an
observer changes state).

## 1. Survey

**What was there.** The engine has the player's `Stance_Sneak` (`MWMechanics::CreatureStats`, used by the pickpocket
decision in `MWClass::Neutral::activate`, task 0208). The pack had NO notion of sneaking: `SkyrimConditionWorld::isSneaking`
answers false and `PerkParticipant::mSneaking` was never set. Sight rules exist (`Factions::sightRay`, `sightClear`,
task 0197) and crime witnesses use a distance rule plus that ray (`CrimeSystem::evaluate`).

**Skyrim.esm GMSTs read** (all five masters scanned; only Skyrim.esm holds any of them; Update, Dawnguard, HearthFires and
Dragonborn add none). VERIFIED values:

| GMST | value | used for |
| --- | --- | --- |
| fSneakMaxDistance | 2500 | the largest stealth range (before our scale) |
| fSneakSkillMult | 0.5 | each skill point removes this much percent of the range |
| fSneakRunningMult | 2.0 | running |
| fSneakActionMult | 2.0 | standing up, not sneaking (RECALLED-VERIFY that this is its meaning) |
| fSneakSoundLosMult | 0.3 | heard but not seen |
| fSkillUsageSneakPerSecond | 0.625 | Sneak experience a second while hidden |
| fSneakAttackSkillUsageMelee | 30 | Sneak experience of a sneak attack |
| fCombatSneak1HSwordMult, 1HDaggerMult, 1HAxeMult, 1HMaceMult | 3.0 | sneak attack multipliers |
| fCombatSneak2HSwordMult, 2HAxeMult | 2.0 | (a two-handed hammer has no GMST: it reads 2HAxe) |
| fCombatSneakHandMult | 2.0 | bare hands |
| fSneakBaseValue -15, fSneakLightMult 0.33, fDetectionSneakLightMod 15, fSneakExteriorDistanceMult 2.1, fSneakSleepMod/BonusMod 0, fPlayerDetectionSneakBase 10, fPlayerDetectionSneakMult 0.4, fCombatSneakAttackBonusMult 100, fCombatDamageBonusSneakingMult 2.0 | | read, NOT used (light and the exterior are not modelled; the others have no known meaning) |

There is NO bow multiplier GMST: a sneak shot counts `Settings::mMultBow` = 2 (ours).

**The formula is a model of ours.** Skyrim's detection is a stealth-point meter whose arithmetic is not in the data. Every
combining rule is marked RECALLED-VERIFY and every number of ours is a named field of `sneak::Settings`.

## 2. Design

`game_packs/skyrimse/include/game_skyrimse/sneak.hpp` and `src/sneak.cpp` (standard library, the SDK combat types and the crime
roster):

* **Stealth range** `stealthRange` = fSneakMaxDistance x `mRangeScale` (0.25, ours) x (1 - fSneakSkillMult x skill / 100)
  x motion x (1 + armour weight x 0.01) x stance. Motion: still (under 10 units a second) 0.5 (ours), running (200 and
  over) fSneakRunningMult, else 1. Stance: sneaking 1, standing fSneakActionMult. A new character (Sneak 15) sneaking and
  walking: 578 units; still: 289.
* **Notice** per observer, judged every 0.5 s of `combatUpdate` time, nearest first, at most 6 sight rays a judgement:
  an observer within the range, inside its view cone (70 degrees each side, ours), with a clear ray SEES the player and
  gains `rate x (1 - distance / range)` a second; without sight only a third of the range (fSneakSoundLosMult) counts and
  the notice stops at 0.9; otherwise it decays 0.25 a second; clamped to [0, 1.5]. A fighter already engaged against the
  player is detected at once. Followers, the dead and observers over 3,000 units away are not looked at.
* **Alert** with hysteresis: caution at 0.3 (leaves below 0.2), detected at 1.0 (leaves below 0.75). The eye: closed =
  hidden, half = caution, open = detected; none while not sneaking.
* **The player's speed** is the slower of the last two judgement intervals, and an interval over 1,000 units a second
  is a teleport (counts 0): one fast interval is a teleport or a stumble, not a run.
* **Sneak attack.** `SkyrimCombat::setSneakAttackHooks` (two `std::function`s, empty by default): asked once per blow or arrow of
  the PLAYER on an actor, not a bash. The multiplier is 1 unless the player sneaks and the victim has not noticed him
  (a victim nobody judged is unaware); then the weapon type's GMST multiplier, then the perk entry point
  ModSneakAttackMult (18) through `sneak::applySneakAttackPerks` with the owner counted as sneaking. The damage before
  and after armour are both multiplied; `done` is told after the damage report: the message "Sneak attack for Nx damage!",
  30 Sneak experience, and the victim has now noticed him.
* **Experience.** One judgement of staying hidden (sneaking, moving at 10 units a second or more, at least one
  observer within 3,000 units that has not noticed him) gives 0.625 x the interval.
* **Crime and pickpocketing.** `CrimeSystem::setDetectedLookup`: with the sneak system on, a witness counts only when it
  has noticed the player (a crime nobody noticed costs nothing); a caught pickpocket try marks the victim as having noticed him
  before the crime is judged. `pickup::Hooks::mAware`: an aware pickpocket victim always catches the hand (the seeded roll is
  still drawn, so the sequence does not move).
* **The eye** (`files/data/openeden/hud/skyrimsneak.lua`, player script `scripts/openeden/skyrim_sneak_hud.lua`, started
  only when the pack declares `core.skyrimse.sneak` >= 1.0): two small RmlUi documents in the style of task 0180's combat HUD: a
  lid shape that closes, half opens or opens, with a pupil and a word (Hidden, Caution, Detected), and the sneak attack
  message for 3 seconds. Needs `[GUI] rmlui`.
* **Bindings** `skyrimse.sneak` 1.0 (Read): `eye()`, `message()`, `message_id()`, `message_seconds()`. Declared only with the setting on.

## 3. Edits outside the new files (for review)

New files: `game_packs/skyrimse/include/game_skyrimse/sneak.hpp`, `src/sneak.cpp`, `src/combat_sneak.cpp`,
`apps/components_tests/skyrimse/testskyrimsesneak.cpp`, `testskyrimsesneakwiring.cpp`, `files/data/openeden/hud/skyrimsneak.lua`,
`files/data/scripts/openeden/skyrim_sneak_hud.lua`, `tools/sneak_check/*`, this file.

Edited:

* `sdk/include/openeden/game/combat_rules.hpp`: `SubjectState::sneaking` (a trailing field with a default; **contract version NOT bumped**).
* `apps/openmw/mwphysics/physicssystem.hpp`, `.cpp`: `NeutralPlayerState::mSneaking` and the copy into `SubjectState`.
* `apps/openmw/mwworld/worldimp.cpp`: the player hook fills `mSneaking` from `Stance_Sneak`.
* `game_packs/skyrimse/include/game_skyrimse/combat.hpp`: the two hook types, `setSneakAttackHooks`, two members, one private function.
* `game_packs/skyrimse/src/combat.cpp`: two small blocks in `applyHit` (the multiplier after `applyPerksToHit`, the `done` call after `reportDamage`). No virtual added anywhere.
* `game_packs/skyrimse/include/game_skyrimse/crime.hpp`, `src/crime.cpp`: `setDetectedLookup` and one `continue` in `evaluate`.
* `game_packs/skyrimse/include/game_skyrimse/pickup_service.hpp`, `src/pickup_service.cpp`: `Hooks::mAware`, one line in `take`.
* `game_packs/skyrimse/src/perks.cpp`: one row in the tab role table: entry point 18 = owner, weapon, target (RECALLED-VERIFY).
* `game_packs/skyrimse/include/game_skyrimse/neutral_world.hpp`: `setSneak` and the `update` call in `combatUpdate`.
* `game_packs/skyrimse/src/skyrimse_game_pack.cpp`: `startSneak`, the pickpocket hooks `mAware` and `mCaught`.
* `game_packs/skyrimse/extension.json` (the setting), `CMakeLists.txt` (three lists), `CLAUDE.md`.
* `apps/components_tests/CMakeLists.txt`, `files/data/CMakeLists.txt`, `files/data/builtin.omwscripts`, `tools/testprofile/profile.cfg` and `README.md`.

No existing test was edited or deleted.

## 4. Stage log

**Tests (full loop in form only: the same hand wrote tests and code; no fresh reviewer).**

* `testskyrimsesneak.cpp` (rules 15, system 24) was written first and run red against a stub `sneak.cpp` whose functions answer the zero of
  their type: **12 of 15 rules tests and 17 of 23 system tests failed (29 of 38)**; the 9 that passed against the stub test absence or
  clamping (a bad number is harmless, nothing within range, no eye while not sneaking). Then all 38 passed on the first run of the real code.
  After the first in-game runs the speed rule changed (the slower of two intervals, see 2.) and one test was replaced and one added; those were
  written after the code and not run red.
* `testskyrimsesneakwiring.cpp` (15: combat hooks 5, perk entry point 3, crime 3, pickpocket 4) was written before the hooks existed: it did not
  COMPILE against the old headers (no `setSneakAttackHooks`, `setDetectedLookup`, `mAware`, `applySneakAttackPerks`), so its red evidence is a compile
  failure, not a failing run.
* **54 `SkyrimSneak*` tests pass.** `components-tests` with `*Skyrim*:*Extension*:*Frozen*:*Neutral*:*Papyrus*:Ui*:*Lua*:*Perk*`:
  3,162 ran, 3,091 passed, 71 skipped (gated real-data tests), 0 failed. `openmw-tests` with `*Neutral*:*Combat*:*Physics*:*Extension*:*Lua*:*Skyrim*`: 922 ran, 920 passed, 2
  skipped, 0 failed. No existing test needed an edit. The perk tab role row for entry point 18 is not pinned by an existing test (`testskyrimseperks.cpp`
  lists rows, not the table).
* `CI/check_layer_rules.sh` 104 exceptions (unchanged), `check_game_conventions`, `tools/testprofile/coverage.sh` pass.
  A bug found by the openmw-tests link: `sneak.cpp` was missing from the `game_skyrimse_world` list in `game_packs/skyrimse/CMakeLists.txt` (it was in the
  shared library list only); fixed.

**In game** (`tools/sneak_check/check.sh` with `scenario.sh`; Bannered Mare, `OPENEDEN_FIXED_FRAME_TIME`, `[Sound] master volume = 0`, `OPENEDEN_QUIET_WINDOW`,
the actors' packages off so nobody walks). Keys are real: the sneak stance is the controller's left stick button (the input script's `key` command takes
one-word key names, and "Left Ctrl" has a space) as an SDL controller event through the real input path, walking is the key W and the blows are the left mouse button.
The player is PLACED by Lua at the scene's spots (teleports); the stance, the walk and the blows are not.

* Walk south past Lucia and Brenuin: the eye halves when Saadia and Brenuin become cautious, Brenuin detects him at 204 units (the log lines carry the
  distance, range and speed).
* Closed eye (frame viewed, "Hidden"): still, 300 units or more from everybody, shot at t=18 s, eye 0.
* Half eye (frame viewed, "Caution"): 150 units in front of Hulda, 1.3 s after arriving, eye 1. Open eye (frame viewed, "Detected", red): 4 s after arriving, eye 2.
* Sneak attack on Sinmir from behind (60 units, he faces away; he is "caution" at 0.42): the trace has `COMBAT sneak attack x3`, damage 22.575 before armour
  (7 x 1.075 x 3), 18.674 after armour (mitigation 17.278 percent), `SNEAK attack on 529338 x3.00`; the next blow, 2 s later, is 7.525 before armour (he has noticed
  him: x1). The message "Sneak attack for 3x damage!" appears above the eye (frame viewed). Bindings: `msgid=1`.
* **Determinism: three runs of the scenario give a byte-identical 46-line trace** (the `SNEAK`, `COMBAT damage`, `COMBAT sneak` and `SNEAKCHECK` shot and result lines without timestamps;
  md5 `55d5c39a5923` for all three).
* **Setting off equals the parent:** the `n5trade` scenario (Hulda against the player, 268 `COMBAT` lines) with the setting unset gives md5 `0d448e8c8f7f` in two runs of this build
  and in the run of the build before the sneak code (`SubjectState` field only), byte for byte. The Lua script is reported as not started ("does not provide core.skyrimse.sneak"), no `SNEAK` line.
* **Morrowind unchanged** (`tools/worldobjects_check` against the integ2 executable, another agent's build, not a build of the exact parent): 111 lines, OK, and the fixed Balmora frame
  (`--start Balmora --random-seed 7`, frame 600, 1280 x 720) has SHA-256 prefix `1c5d47ad0b5dc32d` for both.

**Not run:** the Linux gate and GCC `-Werror`, clang-format 14 (formatted by hand at 120 columns), clang-tidy, the Lua integration tests, a fresh review, the
perk path in game (a unit test covers it; the player has no sneak perk in a new game), a bow or crossbow sneak shot in game, a sleeping NPC, a sneak
attack on a follower or in a crowd, light, the exterior, the real keyboard key (Ctrl) for sneaking, Eden Studio's link (the `game_skyrimse_world` list was fixed and
`openmw-tests` links it, but the editor was not built), the Papyrus `GetDetected` and `IsSneaking` conditions (still answer false).

## 5. Decisions for toddhow

1. **`SubjectState::sneaking` is a trailing field with the contract version unchanged** (0.5). It changes the struct's size; the engine and the pack are built together. Alternatives: bump the contract (the pinned version tests would need your approval) or a Mutate binding fed by a player script.
2. **Sneak needs `[Game] neutral combat`**: the player hook is only installed under it (the pack setting is silent without it: the eye shows -1).
3. **No light.** The pack has no light query. Skyrim's GMSTs for light (fSneakLightMult 0.33, fDetectionSneakLightMod 15) are not used, so a lit tavern and a dark cellar are the same; `mRangeScale` 0.25 and the still factor 0.5 are OURS and set the whole feel (578 units walking, 289 still at skill 15; 312 and 156 at skill 100). Tell me how it should feel and I tune them.
4. **Sleeping NPCs notice like everybody else** (the package brain has no cheap "asleep" query).
5. **A crime nobody noticed costs nothing** (witnesses must have noticed the player). With sneaking on and the player standing, an NPC in view notices him within a few seconds (range doubles): a theft in the same instant he enters its view is not witnessed.
6. **Perk entry point 18 tab roles (owner, weapon, target) are RECALLED-VERIFY**; the data's real perks (Backstab, Assassin's Blade) were not checked against them.
7. **The Morrowind sneak icon** (the small MyGUI box next to the stat bars) still draws beside the new eye; `replacedHudParts` was not changed.
8. **A bow sneak shot counts x2 (ours)**, a two-handed hammer reads the two-handed axe GMST, a staff and anything unknown read the bare hands GMST.

