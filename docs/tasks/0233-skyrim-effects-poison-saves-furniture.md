# Task: Skyrim paralysis, frenzy, fear, weakness, poison saves and pickable furniture (milestone M-S7; follow-up of tasks 0186 and 0229)

Status: BUILT, Windows only, local branch `magic2` from `45f6895307`, not pushed. See the stage log at the end.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.17; tasks 0186 (magic runtime), 0190 (combat AI), 0197 (targets from
factions), 0229 (benches and poisons), 0174 and 0195 (ragdolls).
Goal: five things task 0229 left out: (1) an NPC hit by paralysis falls and cannot act for the duration; (2) fear makes it flee its caster and
frenzy makes it attack the nearest actor, for the duration; (3) weakness to fire, shock, frost, magic and poison raises the damage a hostile effect
does while it lasts; (4) a poison on the player's weapon survives a save; (5) the cooking pot and the fire spit open with the real activate key.
Inherited code touched: none outside the pack except (5): `World::getFocusObject` (`apps/openmw/mwworld/worldimp.cpp`, one block, only with `[Game] neutral inventory`), new `mwworld/neutralpick.*`.
Constraints: pack only for 1 to 4; no virtual added to `CombatRequestWriter`; combat edits are hook calls; the magic runtime's archetype code is additive
(a new `switch` case, a new file, no change to the cases other agents edit); no new setting (the effects need `[Game] neutral combat` and, for fear and frenzy,
the existing `combat ai`); no existing test edited.

## 1. Survey (Skyrim.esm, Python, aggregates; `tools/skyrimse_status_survey/survey.py`)

VERIFIED by reading every MGEF (DATA +0 flags, +64 archetype, +68 primary actor value) and every poison ALCH of the Data folder's `Skyrim.esm`:

- **Paralysis** is archetype **21** on actor value **53** (19 effects: `AlchParalysis`, `ParalysisFFAimed`, the Deep Freeze and Grandmaster perk effects...).
  The poisons `Paralyze02..05` last 5, 7, 10 and 15 s; the spell `Paralyze` (cost 450) 10 s.
- **Frenzy** is archetype **8** (10 effects, "aggression up": `AlchInfluenceAggUp`, `InfluenceAggUpFFAimed`; spells `Fury` mag 6, 30 s, cost 67, `Frenzy`, `Mayhem`;
  poisons `Frenzy02..05` mag 8 to 23, 10 s). **Calm** is archetype 6 (13 effects), not built.
- **Fear** is archetype **7**, the existing `Demoralize` ("confidence down": `AlchInfluenceConfDown`, `InfluenceConfDownFFAimed` (spell `Fear` mag 9, 30 s, cost 153), 21 effects).
- **Weakness** is archetype **34** (Peak) with the flags 0x7 (Hostile, Recover, Detrimental) on the resistance's actor value: poison **40**, fire **41**, shock **42**,
  frost **43**, magic **44** (disease 45). `AlchWeaknessFire`, `AlchWeaknessShock`, `AlchWeaknessFrost`, `AlchWeaknessMagic`, `AlchWeaknessPoison` (the last has resist value 40 itself).
  The poisons `WeaknessFire02..05` carry 55, 70, 85 and 100 for 30 s. Magic is 44, NOT 39 (39 is the ward's strength, as 0186 found).
- **Conditions:** `AlchParalysis` carries 2 CTDA (functions 560 and 244), `AlchInfluenceAggUp` 6, `AlchInfluenceConfDown` 5, the spells' effects 3 to 7; they are immunity and state
  checks on the target. The magic runtime skips an effect with conditions unless `combat test spell conditions` is set (task 0186), so **a real Paralysis, Frenzy or Fear
  poison does nothing without that setting**. Weakness effects carry none and work as they are.
- Spell costs: `Paralyze` 450, `Fear` 153 and `Fury` 67 against the stand-in player's 100 magicka: the player cannot cast the first two; the poisons are how the effects reach an NPC in a game run.

## 2. Design

**Pure rules (`magic_status.*`).** `StatusArchetype`, `ResistValue`, `isResistValue`, `levelGateAllows(level, magnitude)` (fear and frenzy work on a target whose level is at or below the
magnitude: RECALLED-VERIFY, the game's rule; a magnitude that is not finite lets nothing through), `weaknessPercent(sum)` (the negated sum of the Peak modifiers on the resistance, 0 to
`kMaxWeaknessPercent` 100, OURS), `weakened(magnitude, percent)` (magnitude x (1 + percent / 100)), `extendStatus` and `statusActive` (a status ends at the later of its ends; a duration is cut at one hour),
and `ActorStatus` (the three end times, the next frenzy look and "the fall clip is held").

**Combat (`combat_status.cpp`, new; hook calls from the older files).** `Fighter::mStatus`.
- *Paralysis* (`applyStatusEffect`): the target is not the player (the engine owns his body: traced, nothing applied); a fresh paralysis cancels its swing, bow draw and casts, holds the package brain and any goal
  (`holdActor` = `crimeHold`: Suspend then Stop) and plays the stagger-back clip as a **Death-slot clip** (the core does not stop such a clip at its end, so the body keeps the pose; this is the "freeze" of the
  task, with the fall of the 0171 death clip; the ragdoll path of 0174 and 0195 is the engine's for dead actors and needs a core request the contract does not have, so it was not used). While held: `beginAttack`, `beginBowDraw`
  and `beginCast` refuse, `runAi` skips the actor, a blow on it plays no reaction clip. When the time is up (`runStatuses`, once an update before `runAi`) the held pose is replaced by a Stagger-slot recovery clip and the
  package brain is given back (`crimeRelease`; an AI-engaged actor only gets a Stop, the AI keeps it).
- *Frenzy*: needs `combat ai`. `mFrenzyUntil` is set; every 0.5 s (`kFrenzyRetargetPeriod`) `frenzyRetarget` finds the nearest living fighter (every ready fighter and the player, within `kAiSightDistance` and
  `kAiSightHeight`, ties by the table order) and either starts an engagement (`aiBegin` "frenzy", assisted so nobody is called for help, `AiActor::mByStatus`) or retargets the engaged one (`aiEngage` again with
  the same seed). Its blows, arrows and spells then go through the ordinary hit paths against that target. When it ends the engagement the frenzy made is released (the ordinary scan takes over).
- *Fear* (the existing Demoralize case, bookkeeping unchanged): with `combat ai` and the level gate the caster becomes what the target flees: `aiBegin` "fear" (assisted, `mByStatus`) and `AiView::mFeared`.
  `aiThink` (pure, in `combat_ai.cpp`) goes to Retreat at any health, is never cornered into fighting (a frightened actor with nowhere to run holds still), and when the fear ends returns to Duel. A flight that
  reaches `mFleeDistance` ends the fear (`statusNoteLost`); an engagement the fear made is let go.
- *Weakness*: the status branch of `applyOneEffect` (a modifier on a value the pack does not track) now stores the signed magnitude of a modifier on a resistance (40 to 45) in `ActiveEffect::mPerSecond`
  (a Timed effect is never ticked, so nothing else reads it); `weaknessOf(target, resistValue)` sums the running ones on that resistance and on magic and `weakened(...)` is applied after `resistedMagnitude` in the two
  value-effect paths (a thrown, touched or poison effect, and a beam's tick). A weakness ends with its effect (the list removes it). A positive modifier (a resistance buff) is not read: nothing here changes resistance upward.

**Poison saves (`poison.*`).** A new chunk `poisons` version 1 (`PoisonChunk`): count, then per coating the weapon item, the poison item, its name, the charges and the effects. Strict canonical reader (capped, every truncation
refused, a bad table changes nothing). The `crafting` chunk (name, version 1, bytes) is untouched, so a save made before this build has no `poisons` chunk and loads with clean weapons, and old builds
ignore the new chunk's bytes. After a load the pack re-syncs the wielded weapon's poison (`syncPoison`).

**Pickable furniture (5): the real finding.** The pot and the spit are NOT without collision, and their rendered geometry IS pickable: the Bannered Mare's `CraftingCookingFireSpit` has a mesh body (`NCT ... mesh`) and a
rendering scan from the player's eye (new `N9_SCANR` in `tools/neutral_session_check`, `asyncCastRenderingRay` over a grid) hits it (record `b428763`, at the pot's rim). What goes wrong is the aim: the activate key
picks what the camera ray hits in the RENDERED geometry (`World::getFocusObject` -> `castCameraToViewportRay`), and the spit is an iron rail, a few wires and open bowls, so the crosshair on the pot passes through
it and the ray ends on the floor behind (`ACTKEY focus: "skyrimse:b381327" name 'WRIntFloorDirtStr01' tooltip 0 distance 127..189` in five runs). Task 0229's "the ray hits the floor or the stand" was right about the
ray and wrong about the cause (not a missing collision hint).
Fix, general for neutral furniture: `World::getFocusObject` also tests the camera ray against the bounds of every enabled neutral Furniture in the active cells (the base record's bounds, in the object's frame, with the
object's own rotation, position and scale; `MWWorld::rayOrientedBox`, pure) and `boxBeatsRenderHit` decides: with no render hit, or one beyond the whole box, the furniture wins; a render hit that cannot be activated (no tooltip:
the floor, a wall, a static) loses as soon as the ray entered the box before it; a hit that can be activated (an item on the furniture, an actor in front of it) loses only when beyond the whole box, and anything in
front of the box stays. The ray is the camera's position through the render hit point (no render hit: no box test). With `[Game] neutral inventory` off nothing runs. A first version (keep the render hit when it lies
inside the box) failed in game for exactly the reason above: the spit's bounds are 287 x 287 x 278 units and contain the floor under it.

## 3. Out of scope
Calm (archetype 6), the player as a target of paralysis, fear or frenzy, evaluating the effects' conditions (they need the target's keywords and states: the perk engine's world could answer fn 560; not built), a Frenzy or Fear
spell cast by an NPC at another NPC, area spells, the ragdoll for a paralysed NPC.

## Stage log
(see the end of this file)

2026-11-16; same hand wrote tests and code; no fresh reviewer; Windows only.

**Tests first, red against stubs.** New: `testskyrimsestatuses.cpp` (7 pure + 17 combat tests: `MagicStatusPure`, `CombatStatus`), `testskyrimsepoisonsave.cpp` (11, `PoisonSave`) in
`components-tests`, and `apps/openmw_tests/mwworld/testneutralpick.cpp` (15 for the furniture pick, 12 written first and 3 added when the first rule failed in game). With no-op stubs of every new function: **23 of the first 35 `components-tests` failed**
(pure rules 6 of 7, combat 11 of 17, poison chunk 6 of 11; the 12 that passed test absence or old behaviour: the baseline Burnbolt, a frost weakness leaving fire damage alone, a weakness on another
target and one that has ended, a fear with the AI off, a refused truncated or damaged chunk, the archetype constants); and **10 of the first 12 pick tests** (the 2 that passed are the miss cases; the 3 added with the second rule, `renderHitCanBeActivated`, were written after the in-game failure and not run red).
After the code: 35 of 35. One test of mine was wrong on its own terms (a self spell on the health pool is a "plain heal" that a full-health caster skips: the control cast never began) and was
corrected before the code was finished. The existing selection (`components-tests` `*Skyrim*:*Combat*:*Poison*:*Magic*:*Craft*:*Bench*:*Inventory*:*Neutral*:*Papyrus*:*Perk*`: 3,019 pass, 0 fail, 72 skipped, the gated real-data tests; `openmw-tests` `*Neutral*:*Pick*:*Extension*:*Lua*:*Skyrim*:*Combat*`: 951 pass, 0 fail, 2 skipped) passes.
**No existing test edited.**

**In game** (`tools/skybenches_check`, a copy of the 0229 scenario with the new cfg `pullTag`, `OPENFRAME=0`; Bannered Mare, Hulda; fixed frame time; the REAL attack key through
`OPENEDEN_RMLUI_INPUT_SCRIPT`; the weapon is coated through the inventory binding `inv.use`; `combat test spell conditions = true`, see the survey; frames viewed in
`I:\Projects\OpenEden-work\combatai\evidence`):
- **Paralysis** (`Paralyze05`, 15 s): `COMBAT effect paralysis on actor 108142 for 15.000 s`, `NCOMBAT start token 3 slot 13 clip 1HM_StaggerBackMedium` (a Death-slot clip: the held pose),
  no swing of hers while held (a second real tap during the hold: no reaction clip), `COMBAT effect paralysis ended`, `NCOMBAT start token 5 slot 9 clip 1HM_StaggerBackMedium` (the recovery),
  then her AI attacks again. Frames: standing, falling back behind the bar, held, up.
- **Frenzy** (`Frenzy03`, 13 against level 4, with Saadia pulled 45 units beside Hulda): Saadia hit by the player, `effect frenzy on actor 881925 for 10.000 s`, `frenzy: actor 881925 now attacks actor 108142`,
  Saadia's power attack and two normal blows take Hulda's health 75 to 59 (Hulda staggers), later the nearest actor changes and `now attacks player`, `frenzy ended`. Frames: the two fighting.
- **Fear** (`Fear03`, 30 s): `effect fear on actor 108142`, `ai mode Advance -> Retreat`, she runs from (340, -419) to (31, -387) and stops there (`blocked edge`: a frightened actor with nowhere to run holds still).
- **Poison saved:** the weapon coated with `Paralyze05` and saved (`neutral saves`, the menu script) in one process and loaded in a NEW one: `CRAFT poisons restored: 1 coated weapons`, the inventory shows the sword
  and the poison (x2), and the first real tap after the load gives `COMBAT poison ... on the blow, 0 charges left` and `effect paralysis on actor 108142 for 15.000 s`.
- **Three runs** of the paralysis scenario: the filtered `COMBAT` and `NCOMBAT` lines (68) have the same md5 prefix (`e0f54b31da02`) in all three, unsorted.
- **The cooking pot and the fire spit open with the real key** (`tools/skybenches_check` through `N9`: `pot1.sh`, the player 75 units from the Mare's `CraftingCookingFireSpit` at pitch 1.28, yaw -0.56, Space down and up
  through `OPENEDEN_RMLUI_INPUT_SCRIPT`): before the fix `ACTKEY focus: "skyrimse:b381327" name 'WRIntFloorDirtStr01' tooltip 0 distance 127.477` (the floor, no window; also 183, 189 in four other runs);
  after it `ACTKEY focus: "skyrimse:b428763" name 'Cooking Spit' tooltip 1 distance 14.4498` and `CRAFT open Cooking Spit (cook) placement 0010c201`; the window lists the cooking recipes (frame viewed:
  `evidence/cooking_pot_window.png`: "Cooking Spit", Grilled Chicken Breast and the others, "(cannot)" for lack of ingredients).
- **Morrowind unchanged**, against `combat5`'s build (the newest executable on the machine, NOT a build of the exact parent `45f6895307`): `tools/worldobjects_check` 111 result lines identical, `OK`; the fixed Balmora frame
  (`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600) SHA-256 prefix `1c5d47ad0b5dc32d` for the reference, this build and this build with `neutral world`, `neutral actors`, `neutral combat` on.
  (The focus hook was not run with `[Game] neutral inventory` on in a Morrowind session; it finds no neutral reference there.)

**Edits outside the new files.**
- Pack, hook calls: `combat.hpp` (`mStatus`, `statusesApplied`/`paralyzed`/`feared`/`frenzied`, private declarations, `AiActor::mByStatus`), `combat.cpp` (`isParalyzed` in `beginAttack`, `beginBowDraw` and the reaction clip,
  `runStatuses` before `runAi`), `combat_magic.cpp` (`beginCast`'s gate, the Demoralize hook, the new `Paralysis` and `Frenzy` cases, the weakness status value, `weakened(...)` at the two value-effect paths),
  `combat_ai.hpp`/`combat_ai.cpp` (`AiView::mFeared`, `AiActorState::mFearFlee`, the fear block of `aiThink`), `combat_ai_run.cpp` (the paralysis skip, `view.mFeared`, `statusNoteLost`),
  `poison.hpp`/`poison.cpp` (the chunk), `crafting_service.hpp` (`poisonChunk()` and the member), `skyrimse_game_pack.cpp` (registering the chunk and its restored sink), `game_packs/skyrimse/CMakeLists.txt` (2 sources twice, 1 header).
- Engine (item 5 only): `apps/openmw/mwworld/worldimp.cpp` (`World::getFocusObject`), `apps/openmw/CMakeLists.txt` (`neutralpick`), `apps/openmw_tests/CMakeLists.txt`, `apps/components_tests/CMakeLists.txt` (2 tests, 2 pack sources).
- Tools: `tools/neutral_session_check/check.sh` and `luatest/scripts/n9/player.lua` (`N9_SCANR`, the rendering-ray scan), `tools/skybenches_check/check.sh` and `luatest/scripts/inv/place.lua`
  (`pullTag`, `pullFrame`, `pullDx`, `pullDy`: stands a second actor beside the target), `tools/skyrimse_status_survey/survey.py` (new).
- **No virtual added to `CombatRequestWriter`; no existing test edited.**

**Not run:** the Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua tests, a fresh review, **weakness in game** (it is covered by the unit tests; in game only the status itself is untried: no spell or
poison in the data does fire damage to an NPC through a coated weapon, and the 100 magicka stand-in cannot pay `Paralyze` 450 or `Fear` 153), the player as a target of any of the three effects (not applied),
a feared NPC with a way to run (the Mare's walls stopped it), Calm, area spells, an NPC casting Fear or Frenzy at another NPC, the cook pot (not the spit) by key, a furniture box test on rotated or scaled furniture in game
(unit tested only), the focus hook with `[Game] neutral inventory` in a Morrowind session.

## Decisions for toddhow
1. **Effect conditions.** The real Paralysis, Frenzy and Fear effects carry 2 to 6 CTDA (target immunities and states) that the pack cannot evaluate, so without `combat test spell conditions` a Paralysis, Frenzy or Fear
   poison or spell does nothing. The perk engine's condition world could answer fn 560 (HasMagicEffectKeyword) and the others; not built.
2. **Paralysis is a held Death-slot clip plus a Stagger-slot recovery**, not a ragdoll (no contract request for one; the ragdoll is for dead actors). The clip is the stagger-back of a sword; a paralysed actor on its
   feet falls backward and stays there. A real fall to the floor needs a contract request.
3. **The level gate** (fear and frenzy only on a target at or below the magnitude) is the game's rule from memory; `levelGateAllows` is one line to change.
4. **Weakness adds to damage after resistance** (x (1 + percent / 100), at most +100 percent, magic weakness on every hostile value effect): ours.
5. **The weakness status only changes hostile value effects** (spells, beams and poisons that do damage); it does not touch weapon damage.
6. **Furniture picking by bounds** applies to every enabled neutral Furniture with bounds (beds and chairs too), not only crafting benches, and only with `[Game] neutral inventory`. Restricting it to benches needs a
   per-base answer from the pack at focus time.
7. **The poison chunk is new (`poisons` v1)**, the crafting chunk is untouched; an older build ignores it, so the same save loaded there has clean weapons.
