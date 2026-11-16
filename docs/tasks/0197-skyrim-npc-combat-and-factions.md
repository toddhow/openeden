# Task 0197: Skyrim NPC combat, factions, followers and sight

Design doc section: decision 0018 (combat); follow-up of task 0190 (combat AI, its "Decisions and limits" 4 and 5) and task 0192 (many fighters, decision 2 "friendly fire").
Approval: toddhow, 2026-11-15/16, "as much Skyrim work as possible overnight".
Goal: the combat AI picks its target from relations (factions, followers, crime guards), not always the player; NPC fights NPC; allies and guards help; a hostile behind a wall does not engage; the crowd-only friendly-fire rule of task 0192 is replaced by the real relation; the determinism difference of the default-thread run is explained.
Inherited code touched: none. OpenEden: `game_packs/skyrimse` (`combat_factions.*` new, `combat_ai_run.cpp`, hook edits in `combat.cpp`/`combat.hpp`, `world_view.cpp` profile, `neutral_world.hpp` wiring, `extension.json`, `skyrimse_game_pack.cpp`). Not touched: `papyrus_host*`, crafting, skills, map and HUD files.
Constraints: new rules behind `[extension.skyrimse] combat factions` (default off, needs `combat ai`). With it off every trace equals the parent's (`ref` build of `3885a4091b`). Morrowind unchanged. No contract change planned (0.5 stays; no writer virtual added).

## 1. Survey (at `3885a4091b`)

| What | Where | Fact |
| --- | --- | --- |
| Target | `combat_ai_run.cpp` `SkyrimCombat::runAi` | One `playerState` for every engaged actor; the target is always the player (limit 4 of task 0190). `aiMove` takes the target's state. |
| Engagement | `runAi` steps 1 and 2, `aiScan`, `aiBegin(fighter, reason)` | Watchers = `FighterProfile::mAi.mHostileOnSight` (relation to the PLAYER). Sight is a distance and a height, no walls. |
| Provoke | `aiProvoke`, `aiNoteDamage` | Only the player's damage provokes (`attackerIsPlayer`). |
| Friendly fire | `combat.cpp` `mayHit`, `chooseCrowd`, `mOpponents` | Crowd test only: a hostile hits the player, a duelist only its opponent; without a crowd test everybody in the arc is hit. |
| Relations | `actor_mechanics.cpp` `relationOfFactions`, `statedRelation`, `wouldAttackOnSight`, `computeActorDisposition` | Pure over `FactionMembership`; relation to the player only (`MechanicsOptions::mPlayerFactions`); in `FighterProfile` only `mFactions` (form ids) and `mAi.mHostileOnSight`. |
| Followers | `actor_table.cpp` `SkyrimActorTable::isFollower`, `sdk follower_rules.hpp` `FollowerRules::sideOf` | The cell source (`neutral_world.hpp`) implements it; the combat rules do not see it. |
| Ray | `CombatRequestWriter::castRay` (contract 0.5), engine `neutralsight.*` | Nearest actor, player or static body along a line, answered at once inside `combatUpdate`; own body and dead actors never hit. |
| Per-fighter aim | `combat.cpp` bow release (`subjectState(ofPlayer())`), `combat_magic.cpp` `launchSpell` call and `aimRay` | Arrows and NPC spells are aimed at the player only. |

## 2. Data (Skyrim.esm, `I:\Projects\OpenEden-work\combatai\py\survey3.py`, aggregates)

* FACT `DATA` flag 0x40 ("track crime", RECALLED-VERIFY name) is set on 58 factions (`CrimeFactionWhiterun` 0x9040). It marks every crime faction, citizens' as well as guards', so it does not say "guard".
* The Whiterun street guards (`GuardWhiterunSons*`, `GuardWhiterunImperial*`) have AIDT aggression 0, confidence 2, assistance 0 (unaggressive: Skyrim guards react to crime, not to aggression). They are in `IsGuardFaction` (0x86eee), `GuardFactionWhiterun` and a crime faction. Of the NPCs named "Guard", 356 of 413 are (0, 2).
* AIDT (aggression, assistance) over all NPCs: (0,0) 1,898; (1,1) 262; (2,2) 1,456; (0,1) 378; (0,2) 212; (2,1) 384; (2,0) 49; (1,2) 405; (1,0) 72; (3,1) 2. Assistance 0 helps nobody, 1 allies, 2 friends and allies (RECALLED-VERIFY).
* Reaction codes in `XNAM` 0/1/2/3 = neutral/enemy/ally/friend (the pack's `reactionOf`). `BanditFaction` states relations to 15 factions; only three factions state Enemy toward it (`DragonFaction`, two dungeon ones). `DragonFaction` is Enemy to `IsGuardFaction`, `CaravanGuard`, `ThalmorFaction` and others. No faction states bandit against guard: guards fight bandits through crime and assistance, not through a faction relation.

## 3. Design

**Pure rules `combat_factions.*`** (standard library and the SDK relation enum only):
* `Party` = what the rule reads of one fighter: `mPlayer`, `mFollower` (side Player), `mAggression`, `mAssistance`, `mGuard`, `mRelationToPlayer`.
* `sideRelation(a, b, factionRelation)`: both on the player's side: Ally; one on the player's side and the other's relation to the player is Enemy: Enemy; else the faction relation.
* `attacksOnSight(a, b, rel)`: a follower attacks `b` when `b` is hostile to the player's side (b's relation to the player is Enemy and b is aggressive, or b is engaged against the player's side); everybody else `wouldAttackOnSight(aggression, rel)`.
* `assists(helper, victim, attacker, relHV, relHA)`: the helper is not engaged; it helps when it is a guard and the victim is not its enemy and the attacker is not its ally or friend; or its assistance is at least 1 and the victim is its ally (assistance 2: ally or friend) and the attacker is not its ally or friend.
* `mayHit(attacker, victim, relation, engagedAgainst)`: false for an ally or friend that is not the attacker's current target; true otherwise.
* A guard (OURS, named constant): a member of the faction with editor id `IsGuardFaction` (the game's own guard marker; there is no flag).
* Line of sight: `eyeToTarget(self, target)` builds the `RayRequest` (eye 120 above the feet to the target 90 above its feet); clear when the nearest hit is nothing, an actor or the player (people do not block sight; a wall behind a person closer than the wall is not seen: limit).

**Pack integration** (everything gated by `mFactionsOn`; off = old code path):
* `CombatData::factionRelation(FighterProfile&, FighterProfile&)` (non-pure, default Neutral) implemented by `SkyrimWorldView` through `relationOfFactions`; cached per base pair in the combat rules (lookups only, so order cannot change a result). `AiProfile` gains `mAssistance`, `mRelationToPlayer`, `mGuard`.
* `AiActor::mTarget` (player or actor), `mTargetLos`. `runAi` reads the target's state from the writer, not the player's. Target dead: retarget (the nearest valid candidate) else release.
* Watchers: aggression 1 or more, a guard, a follower or assistance 1 or more. A snapshot of positions of the ready fighters is made every `kAiSnapshotPeriod` (0.5 s); a watcher looks every `kAiSightPeriod`, at most `kAiScanBudget` watchers a frame (round robin), picks the nearest valid target within sight distance and height, casts one ray, and engages (`aiBegin(target)`). Assistance: when X engages T, nearby fighters Y with `assists` and sight to X engage X (one hop: an assisting fighter does not call for help).
* Provoke: any damage an NPC or the player does to an NPC makes the victim fight the attacker (`aiNoteDamage` for the player as before; `applyHit`/spell/arrow hits call `aiProvoke(attacker, victim)` for NPC attackers when factions are on); an NPC with confidence 0 still flees.
* Followers: a follower is a watcher; it engages only within `kAiFollowerLeash` (1,500) of the player and is released beyond it (the package brain takes it back).
* `mayHit` with factions on: the relation rule above; crowd tests are expressed as relations (a duelist's opponent is an enemy, the crowd's hostile ones enemies of the player).
* Aim: `Fighter::mAimTarget`; the bow release and an NPC spell aim at it (the player when unset).
* New test setting `[extension.skyrimse] combat test attacker = <editor id>`: that actor is treated as frenzied (attacks the nearest valid target). A Lua scenario places the player so that it fights guards in Whiterun's street.

## 4. Stages
1. Doc, data survey (this file). 2. Tests first (`testskyrimsecombatfactions.cpp`), run red against stubs. 3. Pure `combat_factions.*`, profile fields, `combat_ai_run.cpp` generalisation, hooks. 4. In game: NPC-vs-NPC fight in Whiterun, a follower at the player's side, three byte-identical runs, off equals parent, Morrowind. 5. Determinism experiment (the 4 differing headings).

## 5. Acceptance
- [ ] Tests red first, then green; no existing test edited.
- [ ] A forced attacker fights guards; guards of the same faction join; a follower fights hostiles at the player's side.
- [ ] A hostile in another room (a wall between) does not engage; one in the open does.
- [ ] Friendly fire follows the relation; the crowd rule stays only for factions off.
- [ ] Off equals the parent; Morrowind unchanged; three runs byte-identical.

## Stage log
**Built** (same hand wrote tests and code; no fresh reviewer; Windows only; local commits on `npcfights` from `3885a4091b`, not pushed): `combat_factions.*` (pure), `AiProfile::mAssistance / mRelationToPlayer / mGuard` (filled in `SkyrimWorldView::fighter`, the guard by the editor id `IsGuardFaction`), `CombatData::factionRelation` (default neutral; `SkyrimWorldView` implements it with `relationOfFactions`), `combat_ai_run.cpp` (snapshot, look, help, per-actor target, retarget by release, follower leash, provoke by NPC damage), hook edits in `combat.cpp` (`mayHit`, the bow release aim), `combat_magic.cpp` (two aim sites), `neutral_world.hpp` (follower query), `skyrimse_game_pack.cpp` and `extension.json` (settings `combat factions`, `combat factions delay`, `combat test attacker`), the scenario `n5npcfight` and its Lua. No writer virtual was added, so `HudWriter` and `AiDamageWriter` needed nothing and the contract stays 0.5 (no pin test to edit).

**Tests** (`testskyrimsecombatfactions.cpp`, 32 tests: 9 pure rule groups, 22 through the contract with a fake data source and a fake writer with a configurable wall). Red run against stubs of `combat_factions.cpp` and inert setters: **21 of 30 failed**, the 9 that passed test absence and controls (factions off, an unaggressive or merely aggressive NPC beside neutrals, beyond sight, a bad ray, ...). Two tests were added after: the test attacker waits for its delay, and "nothing engages before the factions delay" (found by the first real run, see below; written after the fix, so not run red). `components-tests` 11,189 (without the Slang, complement and property groups) and `openmw-tests` 2,756 pass. No existing test edited. Not run: mutation checks.

**In game** (five masters, `OPENEDEN_FIXED_FRAME_TIME`, `[Sound] master volume = 0.0`, `OPENEDEN_QUIET_WINDOW=1`; scripts `I:\Projects\OpenEden-work\combatai\mare1.sh`, `fight1.sh`):
* Bannered Mare, `combat test attacker = Hulda`, IronSword, player 1,800 away: `COMBAT ai engage hulda ... target actor529338/4` (Sinmir), swing, damage 6.427 after 17.278 percent mitigation, Sinmir `(provoked)` fights back; 20 damage lines in the run; frame viewed (`run_m5`, deleted after viewing: two figures in the room).
* The same with `actor packages = true`, `follower test recruit = Mikael` and the player 120 units from Hulda: Hulda engages the player (damage 7.77 twice), then `engage mikael ... target actor108142/1` and Mikael hits Hulda for 7.805; frame viewed (`run_f1`, the two NPCs face to face).
* Whiterun street, 59 guards: the Imperial and Sons guard factions are enemies in the data (and the world holds both populations, which the real game does not at the same time), so with factions on the street fights on sight; the test attacker, parked guards on rings and `NF_*` knobs produced about 1,100 trace lines of NPC against NPC fights and assistance. No clean frame: the group dissolves into the street fight within seconds (`n5npcfight` frames show a corpse). **A bug the run found:** `combat factions delay` skipped the new look but then fell into the old hostile-on-sight code (`else if (playerAlive)`), so guards engaged the player at game start; fixed (a test pins it).

**Determinism (item 5).** (1) The default-thread `n5ai` runs of task 0190 (Hulda against the player, Lua teleports of the player) did NOT differ in three new runs on this commit: 367 byte-identical `COMBAT`, `NCOMBAT`, `NACTGOTO`, `NACTWALK` lines; the 4 differing headings of task 0190 are not reproduced and the cause stays what 0190 guessed (the harness's player turn against the physics worker), unverified. (2) The Mare NPC fight with a follower: three runs, 680 byte-identical lines. (3) **Whiterun street with 59 guards is not deterministic:** two runs with the same settings give 1,486 trace lines each, the first 225 lines equal, then actors' mode changes land a few frames apart (token numbers of the swings differ), engagements re-roll a half second later. Evidence: in the raw trace the `NACTGOTO path status` lines of the same fight are present in one run and absent in the other (the navigator's tile availability depends on the build workers); the pack's own decisions are a function of positions and the combat clock, so positions that depend on path timing diverge. Not root-caused further (not fixed: it is the navmesh updater, inherited). A fix would be to wait for the navmesh before the fight (a scenario knob) or make path requests deterministic; neither was tried.

**Off equals the parent** (`combatai\ref`, build of `3885a4091b`): `n5trade` 250 and `n5bow` 486 lines of `COMBAT`/`NCOMBAT`/`NACT*` byte-identical. **Morrowind unchanged:** `tools/worldobjects_check` 110 lines OK; fixed Balmora frame (seed 7, frame 600) `1c5d47ad0b5dc32d` for the parent, this build, and this build with `neutral world`, `neutral actors`, `neutral combat`, `combat ai` on.

**Decisions and limits for toddhow.**
1. Guards are recognised by the faction `IsGuardFaction`; a guard helps any ally or friend (a shared faction counts) and the player's side, against anyone. Crime and bounty are not modelled.
2. With the factions rules on, Whiterun's Imperial and Sons guards fight each other (data) because the neutral world loads both populations; a "world state" filter of placed actors (task 0141-style conditions) would remove it. Decision for you: keep (data-driven), or treat the Civil War factions as allies until a quest says otherwise.
3. Sight is a ray: a wall behind a person closer than the wall is not seen; a ray the core cannot answer counts as clear; a target already fought is not re-checked (no losing sight).
4. `mayHit` under factions: the player's blows hit anything (as before); an NPC never hits an ally or friend unless it is its target.
5. A bow or spell NPC aims at its AI target (`mAimTarget`); not run in game against an NPC.
6. Followers leave a fight 1,500 x 1.5 units from the player; they do not follow orders and do not heal.
7. Cost: a snapshot of every ready fighter per 0.5 s and at most 4 looks and 3 rays a frame; not measured in game with hundreds of fighters.

**Not run:** the Linux gate and GCC `-Werror` (hand-checked: braces, no `-Wshadow` names, loops by reference), clang-format 14 (formatted by hand, 120 columns), clang-tidy, the Lua integration tests, a fresh review, mutation checks, a closed door in game.
