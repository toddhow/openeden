# Task 0192: Skyrim combat with many fighters

Design doc section: decision 0018 (combat); tasks 0154 (process levels, in `docs/tasks/0135-neutral-actors-ms3-bodies.md` stage S7), 0171 (melee, with its review fixes), 0172, 0174 (death and ragdoll), 0178 (bow), 0183 (determinism), 0186 (spells).
Approval: toddhow, 2026-11-15, "Combat on more NPCs": real fights in Whiterun's street with several actors and guards; check process levels, frame cost and determinism with many fighters.
Goal: find out what breaks when 8, 16, 32 and more actors fight at once (until now every fight was one or two actors in the Bannered Mare), and fix what the measurements show, with tests. No speculative optimisation.
Inherited code touched: none. OpenEden code: `game_packs/skyrimse/src/combat.cpp` (small edits, no hook changes), new `combat_crowd.*`, `apps/openmw/mwphysics/neutralcombatbridge.*` and `physicssystem.*` (cost split only). Not touched: `combat_ai*`, `combat_magic*`, `papyrus_host*`, `quest*`, `dialogue*`, `inventory*`, barter.
Constraints: with the new settings unset nothing changes (the traces of `n5trade` and `n5bow` equal the parent's); Morrowind unchanged; the new test rules are off by default.

## Survey (what exists, with file and function)

- **Who fights.** `SkyrimCombat::startHostiles` (`combat.cpp`) makes the actors named by `combat test hostile` attack the PLAYER when it is in reach; `mSwingers` swing in the air; the player swings by `combat test player swing`. Every fight so far had one hostile. Nothing made two actors fight each other, and nothing selected actors by place.
- **Per frame, engine side.** `PhysicsSystem::updateNeutralActors` (`physicssystem.cpp`) calls `NeutralCombatBridge::update` (clip clocks, sidecar events, sweep results, then the pack's `combatUpdate`), then steps each grounded neutral actor on its process level (`NeutralProcess`, task 0154: High to 4,096 units from the player, Middle to 8,192 on a 1/20 s period, Low beyond on 1/5 s, behind `[Game] neutral actor process levels`, off by default). `OPENEDEN_NEUTRAL_ACTOR_COST=1` logs `NACTCOST` every 600 frames.
- **Per swing.** HitFrame event, `onClipEvent` pushes a `PendingHit`, `combatUpdate` turns it into `requestSweep`, `NeutralCombatBridge::Writer::doRequestSweep` collects EVERY neutral actor and the player as candidates (`NeutralCombatWorldImpl::collectCandidates`), `NeutralSweep::arcSweep` keeps the 16 nearest in the arc (`kMaxSweepHits`), a ray per hit checks the line, the result is told the next update, `resolveSweep` damages the nearest valid one.
- **Tables with fixed sizes.** Bridge: `kMaxPlaying` 64 clips, `kMaxPendingResults` 64 sweeps, 64 projectiles, 1,024 corpses. Pack: `mPendingHits`, `mAwaiting`, `mDeliveries`, `mPendingBow` reserved to 32 and refusing a push beyond that. Ragdolls: 8 (`RagdollHost`).
- **Lookups.** `SkyrimCombat::find` is a linear scan over every actor of the world (up to 4,096); `NeutralActorBridge::handleOf` is linear, `keyOfHandle` binary.

## What was built (test rules and tools; all off by default)

- Pack settings (declared in `extension.json`): `combat test crowd = N` (the N actors nearest to the player, within 3,000 units, become hostile to it), `combat test duels = M` (the next 2M nearest fight each other in pairs: each unpaired one is paired with the nearest unpaired other, so two actors side by side fight each other), `combat test crowd delay = <seconds>` (when the choice is made, once, on the combat clock; default 4). The choice is `game_skyrimse::Crowd::choose` (`combat_crowd.*`, pure: distance, then placement id, then session break every tie, so the order of the input never matters). A duelist's opponent comes from `mOpponents`; `startHostiles` aims at it (bows stay player-only, duels are melee).
- `tools/neutral_session_check` scenario `n5many` (`luatest/scripts/n5/manyglobal.lua`, `manyplayer.lua`, `check.sh` variables `MANY_HOSTILE`, `MANY_DUELS`, `MANY_PARK`, `MANY_AWAY`, `MANY_AWAY_AT`, `MANY_EVERY`, `MANY_CAM_BACK`, `MANY_CAM_UP`): Whiterun's street (`CELL='@WhiterunWorld,24620,-8083'`, the widest level ground within 1,800 units of the first spot, found by `n5manyscan`, `manyscan.lua`: a ray scan), the 59 guards and soldiers of the 25 cells round the player gathered (Lua cannot tell an actor from a static in a neutral world, so the base records are listed in the script), every one teleported once (a second teleport in the same frame is refused): the hostile ones on rings of 12 round the player (radius 60 to 140, all inside the 159 unit reach), the duelists in pairs on a circle of 300, the rest parked at 2,200. The player's health is raised to 100,000 so a fight can run for minutes. Times are SIMULATION seconds (a fixed frame time makes the run a function of the frame count).
- Cost split in `NACTCOST` (`OPENEDEN_NEUTRAL_ACTOR_COST=1`): the combat bridge's whole update, the pack's `combatUpdate` inside it (`NeutralCombatBridge::Stats::mRulesNs`), the longest frame of each, clips playing, corpses remembered.

## Measurements

Machine: this one (shared with other agents' builds, so a figure is good to a few microseconds), fixed frame time 1/60 s, Whiterun's street with 69 actors, `[Game] neutral actors`, idles and combat on, IronSword, `combat trace` OFF, `OPENEDEN_NEUTRAL_ACTOR_COST=1`, 4,200 frames, mean of the windows after the second (steady fighting). Everybody within reach of the player; the player swings every 2 s.

| fighters | combat bridge, us a frame | of which pack `combatUpdate` | longest frame, us (bridge / pack) | whole neutral actor update, us |
| --- | --- | --- | --- | --- |
| 0 (setting present, nobody chosen) | 10.1 | 8.4 | 99 / 97 | 19.1 |
| 1 | 10.2 | 8.3 | 72 / 71 | 18.5 |
| 4 | 15.5 | 12.1 | 148 / 137 | 27.1 |
| 8 | 13.2 | 10.3 | 203 / 201 | 21.9 |
| 16 | 16.2 | 12.4 | 235 / 229 | 25.7 |
| 16 (8 duels) | 16.5 | 12.7 | 226 / 215 | 27.3 |
| 32 | 23.3 | 20.0 | 315 / 310 | 30.7 |
| 32 (16 hostile + 8 duels) | 20.4 | 15.4 | 746 / 734 | 30.4 |

About 0.4 us a fighter, 0.2 to 0.7 ms in the frames where a whole crowd swings together (their clips start on the same frame and keep the same 2.5 s period, so they hit together): a 60 fps frame is 16,700 us, so combat is about 0.14 percent of it at 32 fighters. The 4 fighter row is noise on a shared machine. Population matters little: the same bridge and the REAL pack rules in a unit test (`NeutralCombatMany`, fake world, no engine) cost 0.75 us a frame with 16 fighters and no other actor, 0.88 with 70, 1.34 with 400 and 2.26 with 1,000 other actors (`SkyrimCombat::find` and `handleOf` are linear; 1,000 actors add 1.5 us). The bridge reserves 262,656 bytes (fixed). The whole frame time is NOT a combat measure: the arena of 40 actors in a ring round the player costs 9.3 ms a frame with combat OFF and 4 to 7 ms with it on in the same scene (the rendering and animation of a crowd next to the camera, and a shared machine); `NACTCOST` is the figure that belongs to combat.

- **Allocations.** Before the fix, a steady fight allocated about 7 times a swing, all in the same few frames of each 2.5 s period: 112 / 896 / 1,792 / 3,584 allocations in 1,200 frames (20 s) for 1 / 8 / 16 / 32 fighters, the worst frame 7 / 56 / 112 / 224. After: 0 in all four, and 0 with 16 fighters and up to 1,000 other actors (`NeutralCombatMany`, the shared `operator new` counter of `openmw-tests`).
- **Memory.** Working set of the whole game in a street run (peak at start-up 5.0 GB from reading the plugins, steady 2.4 to 2.5 GB), 0 fighters 2,543 MB, 32 hostile + 8 duelists 2,359 MB at the last sample: no difference beyond noise. The combat tables are fixed size (262 KiB in the bridge); the pack keeps one small record per known actor.
- **Determinism.** 24 hostile + 8 duels, DaedricSword, power attack every 2nd, `[Physics] creativemotion ragdolls`, trace on, 9,000 frames, three runs started at the same time so the workers compete: all 14,217 trace lines (`COMBAT`, `NCOMBAT` and the `Ragdoll` lines, timestamps cut, not sorted) are byte-identical (md5 `e5214401c3f0...`). (A fourth run with a static camera and the UI on has another player position, so another trace: the scenario, not combat.)
- **Process levels.** 8 duelists 6,000 units from the player (the player teleported away after the choice; `NACTCOST` shows high / middle / low as 13 / 53 / 3 actors, the duelists in Middle), IronSword so the fight outlasts the walk: with `[Game] neutral actor process levels` off and on the combat trace is the same 6,378 lines in the same order but for the "Corpse ... stays where it died" lines, which move by a few lines (the rest check runs on the actor's own step; the sorted traces are identical, md5 `703453b7...`). Combat reads the actors' places (a position does not change in a fight: attack clips have no root motion in the sidecar) and the process level only decides how often the walk and the corpse check run, so a far fight is the same fight. Low (beyond 8,192 units) cannot be reached in a game: the cells of the active grid are 4,096 units and the radius is 2, so an actor farther than that is unloaded.
- **Deaths and ragdolls.** 24 hostile + 8 duels, DaedricSword with a power attack every 2nd: 20 deaths within a few frames of each other (symmetric duels kill both at once): the first 8 become ragdolls, the other 12 log "No ragdoll for ...: all actor ragdoll slots are in use" at Verbose and keep the death clip's last pose (the designed fallback of task 0174). A ragdoll slot is held until the actor is removed, so after 8 deaths no corpse of the session falls any more.
- **HUD.** The enemy bar names and shows the last actor the player hit (frame below), unchanged by a crowd.

## Faults found and fixed

1. **Silent loss of blows in a big volley (pack).** The tables for swings in one frame were 32 entries and refused the rest without a word. A crowd that starts its swings together reaches its HitFrames together. Run: 56 hostile actors within reach, 1,344 swings, only 736 reached the sweep (45 percent lost). Now `kMaxConcurrentAttacks` = 64 (the bridge plays at most 64 clips, so no more events can come in a frame), all 1,344 land, and a drop beyond 64 is warned once (`COMBAT more hit events in one frame than the table holds`). Tests: 3 new, all red first (40 fighters landed 32, 64 landed 32, 80 gave no warning).
2. **Friendly fire (pack).** A swing hits the nearest body in its arc and an arrow ends at the first capsule on its way, so the members of a crowd hurt each other: 173 of the 519 hits of a 24 archer crowd landed on another archer (35 percent of the arrows), and a hostile at the back of a ring hit the ones in front. With a crowd test on (`SkyrimCombat::mayHit`) an NPC's blow or arrow only hurts its enemies: the player for a crowd member, its opponent for a duelist; the sweep then takes the next body in its list. Without a crowd test the nearest body in the arc is still hit (control test). The real fix is a notion of enemy and ally: task 0190's combat AI. Tests: 2 + 2 new, red first (the friend took damage).
3. **Allocation in steady fighting (pack).** `trace(...)` builds its string (name, `std::to_string`, `fmt`) even when no trace is set; eleven hot sites (swing, hitframe, damage, reaction, death, miss, bow draw and release, arrow launch and end) now read `if (mTrace) trace(...)`. Test: `NeutralCombatMany` expects 0 allocations over 1,200 frames, red first (numbers above), green.
4. **Not fixed, limits.** (a) The sweep keeps the 16 nearest bodies (`kMaxSweepHits`) friends included, so with more than 16 bodies in the arc the player is not in the list and the blow misses: 192 of 1,344 swings in the 56-in-reach case (a density no real crowd has: 56 capsules of 22 units in a circle of 140). An ignore list in `SweepRequest` is an additive contract change. (b) 8 ragdolls for the session: a settled corpse could give its slot back (it would keep its last drawn pose). (c) Spells among several actors were not run (`combat_magic*` is another task). (d) The loot window with many corpses was not run.

## Tests

- `components-tests` `CombatCrowdChoose*` (8) and `CombatCrowd*` (13, `apps/components_tests/skyrimse/testskyrimsecombatcrowd.cpp`): the pure choice on hand-placed actors, and the rules through the contract with a fake data source and writer. Red first against stubs: 13 of the first 16 failed (a `choose` that returns nothing, a `mayHit` that always says yes, a `startHostiles` that ignores opponents; the 3 that passed test absence and a control); the table tests 3 of 3; the arrow tests 1 of 2 (the other is the control). 21 pass.
- `openmw-tests` `NeutralCombatMany*` (3, `apps/openmw_tests/mwphysics/testneutralcombatmany.cpp`): the real bridge with the REAL `SkyrimCombat` and a fake world, N fighters on a ring; a steady fight does real work (128 sweeps, 384 hits told, 128 damage reports, 0 refused), allocations, and the cost with a population. The allocation test was red against the unfixed trace sites (counts above), the other two were written with the code.
- Whole runs: `openmw-tests` 2,697 pass, `components-tests` (without the Slang, complement and property groups) 10,869 pass; ONE existing test fails, listed below.
- **Existing test that needs toddhow's approval to edit (not edited):** `CombatReview.aFullSweepTableIsReportedOnceAndNothingIsSpent` (`apps/components_tests/skyrimse/testskyrimsecombatreview.cpp`, lines 238 to 250) pins the old 32 entry table: "fill the table in one update: 32 hits", `pendingSweeps() == 32`, then 2 more hits refused, `mSweeps.size() == 32`, one warning. With 64 entries it fails at its first `ASSERT_EQ(..., 32u)`. The fix is to use `kMaxConcurrentAttacks` for the three 32s (two lines); the test's idea (a full table is reported once and nothing is spent) is unchanged.

## Evidence in game

Frames (viewed): `I:\Projects\OpenEden-work\combat5\m4_003.png` (12 hostile + 3 duels in Whiterun's street from above, fighting; the crowd stands round the player, the duelists further out) and `st1_004.png` (24 + 8 with the HUD: the enemy bar names `GuardWhiterunSonsPatrolDay1`, the player's bars at the bottom, corpses on the ground). Traces and logs: `I:\Projects\OpenEden-work\combat5\run_*` (`tr_*.txt` are the trace files).

Off means unchanged: with the new settings unset the combat traces of `n5trade` (366 lines, md5 `74bad715e8ce`) and `n5bow` (176 lines, md5 `dc31f01db67c`) are byte-identical to the parent commit's (`ee63db9771`, built in the same tree and run the same way). Morrowind: `tools/worldobjects_check` 110 lines, equal to the parent's; the fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600) has SHA-256 `1c5d47ad0b5dc32d...` for the parent, for this build with defaults, and for this build with `neutral world`, `neutral actors`, `neutral combat` and a crowd on.

## Not run

The Linux gate and GCC `-Werror` (hand-checked against the pitfalls in CLAUDE.md), clang-format 14 (not installed: formatted by hand to 120 columns, new lines only), clang-tidy, the Lua integration tests, a fresh review, spells and the loot window among many actors, the Low process level in a game (not reachable), a crowd of more than the 59 guards of the street, `components-tests` Slang/complement/property groups, frame time against a build with the old tables (the whole frame is dominated by drawing the crowd and by other agents' load on the machine).

## Decisions for toddhow

1. Approve editing the one pinned test above (two lines).
2. Friendly fire in the real game: the pack now has a crowd-only `mayHit`; a real rule needs the enemy relation of task 0190. Add an ignore list to `SweepRequest` so a crowd's blow does not lose the player among friends (additive)?
3. Ragdoll slots: let a settled corpse give its slot to a new death (keeps its last pose), or raise the 8?
4. Is `kMaxConcurrentAttacks` = 64 (the bridge's clip table) the right ceiling, or should the bridge's own tables grow with the world?

## Decisions of toddhow (2026-11-15, "Yes, approve")

1. The pinned test `CombatReview.aFullSweepTableIsReportedOnceAndNothingIsSpent` now uses `kMaxConcurrentAttacks` in place of its three `32` values.
2. Friendly fire: the real enemy and ally relation comes from task 0190's combat AI. An additive ignore list on `SweepRequest`, so that a crowd's blow cannot lose the player among friends, is a later follow-up.
3. Ragdoll slots: a settled corpse gives its slot to a new death and keeps its last pose (follow-up). The cap of 8 stays.
4. 64 swings a frame (the bridge's clip table) is the ceiling.
