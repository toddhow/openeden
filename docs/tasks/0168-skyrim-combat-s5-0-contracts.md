# Task: Skyrim combat S5-0, contracts and decision
Design doc section: `docs/research/0056-skyrim-combat-survey.md` sections 7 to 9; decision `docs/decisions/0018-skyrim-combat-hooks.md` (extends decision 0017, D3).
Goal: Fix the contract between the core and the Skyrim pack for combat (a separate `CombatRules` interface), with a pack skeleton and pinned versions, without any behaviour.
Inherited code touched: none. New: `sdk/include/openeden/game/combat_rules.hpp`; `game_packs/skyrimse/{include/game_skyrimse,src}/combat.*`; `SkyrimCellSource` (`neutral_world.hpp`) gains a base class and three forwarding functions; CMake lists of the pack (twice) and `apps/components_tests`; `CI/check_extension_sdk.sh`. Base commit `c3a3047c1f`.
Inputs / outputs: plain values and `ActorHandle` only (no third-party type, no exception); core to pack: clip events, sweep results, `combatUpdate`; pack to core: play/stop clip, sweep request, damage report, death report.
Constraints: DRAFT 0.1 (major 0), additive, SDK stays 4.0; main thread only; nothing calls it from the engine (S5-2 does); behind the future `[Game] neutral combat`, off by default. Full loop applies from S5-0 to S5-6.
Acceptance criteria:
- [x] Decision record 0018 states the decisions of toddhow and the contract design
- [x] The header states its version policy; `CI/check_extension_sdk.sh` pins the policy text and major 0
- [x] Tests: version and enum pins, writer defaults refuse, `onClipFinished` default, skeleton refuses and counts and writes nothing, `SkyrimCellSource` is found as `CombatRules` by `dynamic_cast`
- [x] With the feature off nothing changes: no engine path calls the interface (compared by reading; no in-game run)
Out of scope: any behaviour, engine calls, slot table contents (S5-2), formulas (S5-1), projectiles, magic.

## Contract, and why each function (justified by S5-2 and S5-3 of note 0056 section 8)

- `CombatRequestWriter::playClip` (S5-2: play an attack clip by slot on a Skyrim NPC; the pack resolves slot to clip name, the core owns skeleton and playback); `stopClip` (block release, interrupt by stagger).
- `CombatRules::onClipEvent` (S5-2: sidecar events delivered in clip-time order: `preHitFrame, weaponSwing, HitFrame, attackStop`), `onClipFinished` (default ignore; frees the pack's state machine).
- `CombatRequestWriter::requestSweep` and `CombatRules::onSweepResult` (S5-3: on `HitFrame` the pack asks for an arc along weapon reach x race reach; the core casts a shape in Jolt and returns actors/player/world, a frame later like goal results).
- `CombatRequestWriter::reportDamage` (hit reaction, HUD, impact effects; the pack keeps the numbers), `reportDeath` (starts `RagdollHost`, S5-5).
- `CombatRules::combatUpdate` (once a frame after `ActorRules::update`).

## Stage log

- **Done:** decision 0018; header; skeleton `SkyrimCombat`/`SlotTable`; wiring in `SkyrimCellSource`; CI pin; tests (`apps/components_tests/skyrimse/testskyrimsecombat.cpp`, registered); note 0056 updated; changelog row.
- Tests were written from the header before the pack code but NOT run red against stubs (see the changelog row); results below.
- Build tree: `I:\Projects\OpenEden-work\combat0` (configured in place, components-tests only, `BUILD_OPENMW=ON` but not built).

## Results

- `components-tests` (built in `I:\Projects\OpenEden-work\combat0`, 2,513 steps; filter `Combat*:SkyrimSlotTable*:SkyrimCombat*:SkyrimCellSourceCombat*`): 11 of 11 pass; the wider `Skyrim*:*Extension*:*Frozen*:*WorldModel*:*Compat*` 1,129 pass. Pack targets `skyrimse` and `game_skyrimse_world` build (73 steps).
- **Red evidence is by mutation, not red-first** (the skeleton is itself refusals; no stub run was captured): with `++mUpdates` removed, `stopClip` defaulting to true, the run failed 4 tests (`theWriterDefaultsRefuseEverything`, `thePlayerCanBeTheSubjectOfEveryRequest`, `SkyrimCombat.theSkeletonRefusesAndCounts`, `SkyrimCellSourceCombat.theRegisteredSourceIsFoundByDynamicCast`: it counts updates), reverted. Removing `CombatRules` as a base of `SkyrimCellSource` was not built (the forwarding functions would stop overriding: a compile error).
- Checks: `CI/check_tests_registered.sh` ok; `CI/check_layer_rules.sh` 104; `CI/check_extension_sdk.sh` ok, and with `kCombatRulesVersion{ 1, 0 }` it exits 1 with the new message (reverted); `tools/testprofile/coverage.sh` ok.
- **Contract change after the first commit:** an advisor review noted that the player has no `ActorHandle`, so S5-3 (NPC against player) could not be written. `CombatSubject` (actor or player) now replaces the handle in `ClipEvent`, `playClip`, `stopClip`, `requestSweep`, `onSweepResult`, `onClipFinished`, and `CombatRules::onPlayerAttack(power)` was added (default ignores).
- Morrowind and Skyrim gameplay cannot change: no engine file changed and nothing calls the interface.
- Not run: the Linux gate, GCC, clang-format 14, clang-tidy, the Lua tests, `openmw-tests` (no `apps/openmw` file touched), a fresh review.

## Decisions for toddhow to confirm

1. `ClipSlot`/`ClipEventKind` lists (14 slots, 15 kinds) are my reading of note 0056 section 6.2; names of the window events are RECALLED-VERIFY.
2. The player has no `ActorHandle`: requests and events use `CombatSubject` (actor or player), victims carry `player` flags (`SweepTarget::Player`, `DamageReport::*IsPlayer`), and `onPlayerAttack(power)` tells the pack the player attacked. Alternative: a reserved player handle.
3. Sweep is a forward arc (reach, half angle, heights) rather than a swept capsule along the weapon; a later minor version can add a shape kind.

## Stage S5-1 log (2026-11-14): actor values and derived stats, subset for the first slice

Recorded in this task, not a new one. Branch `worktree-agent-a7d5c6f7a546b21b8` from the S5-0 branch (`73599e8f06`). Pure and pack-side only: no engine, SDK or core file changed. Another agent owns `combat.*`.

**Built:** `game_packs/skyrimse/{include/game_skyrimse,src}/combat_stats.*`. Decoders `decodeWeapon` (WEAP DATA 10 bytes, DNAM 100, CRDT 24), `decodeRaceCombat` (RACE DATA 164: regeneration at +84/+88/+92, unarmed damage +96 and reach +100) and `decodeGameSetting` (numeric GMSTs by the first letter of the editor id), `parseCombatRecords` (walker), `SettingsTable` / `CombatIndex` (later plugin wins, deleted leaves), `CombatSettings` (14 GMSTs with the vanilla values as defaults), `armourOf`, `wornArmour` / `computeActorArmour` (default outfit through `ActorIndex::wornModels`, one entry per ARMO, the NPC's skills from `computeActorStats`), `computeMeleeHit` (`MeleeHit`: damage before and after armour, stamina cost, stagger, critical extra), `meleeReachUnits`, `regenAmount`. Level, health/magicka/stamina and skills are NOT redone: task 0154's `computeActorStats` is used.

**Formulas (ALL RECALLED-VERIFY, plausible and tunable; GMST values VERIFIED by the data census):**
- skill multiplier = min(fDamagePCSkillMax, 1 + skill/200), skill clamped 0 to 100 (100 gives exactly the stored cap 1.5, which supports the form);
- melee damage before armour = WEAP damage x skill multiplier (unarmed: the race's damage, no skill), x (1 + fPowerAttackDefaultBonus) for a power attack;
- armour piece = rating x (1 + skill/200) for light and heavy (Light / Heavy Armor skill), rating x fClothingArmorScale for clothing; mitigation percent = min(fMaxArmorRating, rating x fArmorScalingFactor + 100 x fArmorBaseFactor x main pieces worn), main pieces = head, body, hands, feet; shield adds rating but is not a main piece. Cross-check: 567 rating with 4 pieces = 68.04 + 12 = 80.04, the well-known cap point;
- damage after armour = before x (1 - mitigation/100);
- stamina = fStaminaAttackWeaponBase + weight x fStaminaAttackWeaponMult (unarmed fActionPointsAttackUnarmed), x fPowerAttackStaminaPenalty for a power attack (the alternative reading, fActionPointsPowerAttackMult 1.5, is not used);
- reach in units = WEAP reach x fCombatDistance (141); unarmed = the race's stored reach;
- regeneration = max x rate/100 x seconds, x the fCombat*RegenRateMult while in combat.
Record fields: WEAP animation type +0, speed +4, reach +8, flags +12, skill actor value +76 (6 + skill slot), stagger +96 (VERIFIED sizes, offsets from note 0056; the CRDT multiplier at +4 and the actor-value-to-skill-slot rule are RECALLED-VERIFY). Not used: weapon speed (a clip-rate input for S5-2), critical chance, sneak-attack multipliers, perks, enchantments, poison, the race skin armour, inventory armour the NPC would equip.

**Tests:** `testskyrimsecombatstats.cpp` (33 tests incl. the 2 gated), hand-derived arithmetic in each comment, caps (80 percent, skill 100, GMST caps), zero/negative/NaN/infinite inputs, wrong-size and cut-short subrecords (located error naming DNAM), a generated plugin through the walker. **Red first:** against `stub.cpp` (every function present, none doing anything) 27 of the 31 ungated tests failed, 4 passed (absence/zero cases) and 2 skipped; green against the code after one fix the tests found (`SettingsTable::set` did not lower-case its key). Final: all 33 pass (31 ungated, and the 2 gated ones with the data present). Gated `testskyrimsecombatstatsreal.cpp` (`OPENEDEN_SKYRIMSE_DATA`).

**Real data, five masters, aggregates only:** 3,262 WEAP all with DATA and DNAM, 0 odd subrecords (2,792 melee, max damage 10,000), 161 RACE (146 resolved from NPCs, all values finite, max unarmed damage 100, unarmed reach 0 to 512), 666 numeric GMSTs, none of the 14 the formulas read missing, 0 decode errors. All 6,361 NPCs resolve and get a finite armour result (3,114 with armour, 12,406 worn pieces, max skill-scaled rating 144, max mitigation 29.28 percent, none at the cap, main pieces 0/1/2/3/4 = 3,313/956/731/1,138/223); an Iron Sword hit at skill 50 against each NPC's own armour is finite with 0 <= after <= before and positive stamina for all. The 80 percent cap is reached by no NPC outfit (only by tests).

**Not run:** the full CMake build of `components-tests` (the tests were compiled with the integ2 build's flags and linked against its `componentsrd.lib` and `game_skyrimse_worldrd.lib`, headers of the pack identical to integ2's; the build tree is `C:\OpenEdenWork\combat1`, not `I:`, because `I:` had 6.5 GB free), the Linux gate and GCC `-Werror` (written against the known traps, not compiled with GCC), clang-format 14, clang-tidy, the Lua tests, a fresh review, mutation checks, any in-game check (nothing in the engine calls this yet).

**Decisions for toddhow:** (1) power attack stamina: x fPowerAttackStaminaPenalty (2.0) as built, or fActionPointsPowerAttackMult (1.5)? (2) the armour formula's skill scale (1 + skill/200) and the +3 percent per main piece are from memory and only cross-checked by the 567 cap point: judge by a number from the real game if you have one; (3) NPC armour counts only the default outfit: inventory armour an NPC would equip (CNTO) is left to a later stage.
