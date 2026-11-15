# Decision 0018: Skyrim combat hooks (milestone M-S5, stage S5-0)

Status: ACCEPTED in the parts toddhow decided (2026-11-12 and 2026-11-14); the contract is DRAFT 0.1.
Task: `docs/tasks/0168-skyrim-combat-s5-0-contracts.md`. Research: `docs/research/0056-skyrim-combat-survey.md`.
Extends decision 0017 (neutral actors, D3).

## Decided (toddhow)

1. **Behaviour: option B.** Combat is driven by CreativeMotion's graph with Skyrim's clips and their sidecar events
   (`HitFrame`, `preHitFrame`, `weaponSwing`, `attackStop`, ...). Skyrim's behaviour graphs are not read; mods that
   replace behaviour graphs do not work, mods that replace clips keep working through the slot names.
2. **Firebolt's scripted damage is implemented natively** (stage S5-8) and replaced by its Papyrus script when Papyrus is
   wired (M-S6).
3. **A separate `CombatRules` interface**, not more methods on `ActorRules`, found by `dynamic_cast` on the registered
   source like `ActorMechanicsSource` and `DialogueSource`, so AI/schedule rules and combat rules version independently.
4. Items 2, 5, 6, 7, 8, 9 of note 0056 section 9 as the note recommends: the player's swing is driven from the sidecar
   times without first-person arms; damage, armour, block and stagger formulas are "plausible and tunable" with every
   RECALLED-VERIFY item listed; death ragdolls use a skeleton map baked from the user's NIF (task 0070); M-S5 covers
   humanoids only; the survey scripts are ported later; the FULL loop applies to S5-0 to S5-6.

## Contract design (`sdk/include/openeden/game/combat_rules.hpp`, DRAFT 0.1, SDK stays 4.0)

The pack owns health, stats, the slot table (slot to clip name) and the decision of damage; the core owns what needs the
engine. Information flows through plain values and `ActorHandle` (never a pointer); the player has no handle, so the
structs carry a `player` flag.

- Core to pack (`CombatRules`): `onClipEvent` (clip-time order, name plus classified kind), `onSweepResult` (hits of
  one request), `combatUpdate(seconds, writer)` once a frame after `ActorRules::update`, and `onClipFinished` (default
  ignores).
- Pack to core (`CombatRequestWriter`, valid during `combatUpdate` only, all defaults refuse): `playClip`, `stopClip`,
  `requestSweep` (a forward arc: reach, half angle, heights), `reportDamage` (HUD, hit reaction, impacts), `reportDeath`
  (starts the ragdoll).
- Why a sweep result comes in a later frame: the sweep runs in the core's physics step; the same rule as goal results.
- Not in 0.1 (added when the stage needing it exists, additive): projectiles (S5-6), magic effects (S5-8), blocking
  queries, a clip-speed multiplier, stamina HUD values.

Nothing in the engine calls the interface in S5-0; the pack's `combat.*` is a skeleton that refuses and counts. All
future behaviour is behind `[Game] neutral combat` (off by default); with it off no Morrowind or Skyrim path changes.

## Decided 2026-11-14 (toddhow, stage S5-3; task `docs/tasks/0171-skyrim-combat-s5-3-melee-hit.md`, contract 0.2)

- `CombatSubject` (an actor or the player) and `onPlayerAttack` stay as built in 0.1; the melee sweep is a forward arc (reach, half angle, a height band).
- Power attack stamina is multiplied by `fPowerAttackStaminaPenalty` (2.0); the armour formulas stay "plausible and tunable" (RECALLED-VERIFY); armour from an actor's inventory (CNTO) waits for S5-9.
- Attack clips play on the whole body for now; an upper-body clip over locomotion comes with S5-9.
- Contract 0.2 (additive, SDK stays 4.0): `SubjectState` and `CombatRequestWriter::subjectState` (where a subject is, and the player's engine health), `CombatRequestWriter::faceHeading`, and the rule that `reportDamage` with `victimIsPlayer` makes the core write `healthAfter` into the engine's player health (the player's health is the engine's, not the pack's). A clip in the `Death` slot is not stopped by the core when it ends: the body keeps its last pose.
- The sweep is cast by the core at the moment of the request and told in the next frame; it is a geometric arc against the actors' and the player's capsules, not a Jolt shape cast, and does not look for a wall (decision for toddhow in the task).


## Decided 2026-11-14 (toddhow, stage S5-3b; task `docs/tasks/0171-skyrim-combat-s5-3-melee-hit.md`)

- The edit of the existing test `CombatContract.versionIsADraftZeroOne` (the pinned minor version 1 to 2: the contract is 0.2) is approved.
- **Walls:** after the geometric arc picks its candidates, each hit needs a clear line from the attacker's weapon height to the target's capsule through the Jolt world: one ray per candidate against static WORLD geometry, terrain and closed doors, never against actors, cast by the core on the main thread at the moment of the request. A blocked candidate is not hit (counted and traced).
- **Player swing timing from data:** the player's HitFrame time and clip length come from the `1HM_AttackRight` sidecar at run time, through the same meta loader the NPC clips use. Contract 0.2 gains `ClipTiming` and `CombatRequestWriter::clipTiming(clip, perSex, out)` (a trailing virtual, default refuses; additive). Until the meta is ready the player cannot swing: there is no fallback constant, and a timing that has not arrived after five seconds of trying is reported once in the log.
- The death pose stays the held stagger-back clip until S5-5 (no change).
