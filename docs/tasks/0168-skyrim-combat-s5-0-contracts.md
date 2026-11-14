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

(filled in below)

## Decisions for toddhow to confirm

1. `ClipSlot`/`ClipEventKind` lists (14 slots, 15 kinds) are my reading of note 0056 section 6.2; names of the window events are RECALLED-VERIFY.
2. The player has no `ActorHandle`: structs carry `player` flags (`SweepTarget::Player`, `DamageReport::*IsPlayer`). Alternative: give the player a reserved handle.
3. Sweep is a forward arc (reach, half angle, heights) rather than a swept capsule along the weapon; a later minor version can add a shape kind.
