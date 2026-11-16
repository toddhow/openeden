# Task 0250: Skyrim summons and raise dead (milestone M-S5/M-S7, follow-up of 0242, 0202, 0246, 0197)

Status: BUILT, Windows only, local commit on branch `summons` from `4f1c874c20`, not pushed (overnight 2026-11-16).
Same hand wrote tests and code (the pure rules and the combat side were run red against stubs first, see the stage log); no fresh reviewer.
Behind `[extension.skyrimse] summons = true` (off by default) AND `combat spell delivery = true` (creature summons also need
`creatures`, which gives the wolf its body and clips, and `actor packages` for the follow behaviour). No contract version moves.

Goal: a Conjure spell or scroll (Conjure Familiar, the Atronachs, Dremora Lord, the thralls) creates its creature near the caster,
an ally that fights the caster's enemies and goes when the spell's duration ends or it dies; Raise Zombie and the other reanimate
spells raise a corpse as a living ally for the duration and let it fall dead again.

## 1. Survey (the five masters, `SkyrimSummonsReal`, aggregates and editor ids; MGEF DATA offsets by CommonLibSSE-NG, checked by the data)

- **Archetypes.** MGEF DATA +64 is **18 on every Summon Creature effect (83 effects of spells and scrolls)** and **22 on Reanimate (32)**; Command Summoned
  is 10 (3 effects, not built). The numbers agree with CommonLibSSE-NG's `ArchetypeID` list and with the neighbours the pack already
  verified (Paralysis 21, Accumulate 32, Stagger 33, Peak 34). MGEF DATA +8 (the associated form) names an NPC_ for 74 of the 83 summon
  effects and a leveled NPC list for none; the other 9 are the werewolf howls (`HowlWerewolfSummonWolvesEffect1..3`, the Ice Wolves and
  the Werewolves: associated form 0, they work through other means) and are not handled.
- **Delivery and cost.** All Conjure spells and scrolls have delivery **4 (target location)**, fire and forget; the Conjure Familiar spell
  costs 107 and Raise Zombie 103 magicka (the stand-in player has 100). Raise Zombie itself is **aimed (2)**, its hand variants target
  actor (3). Every Reanimate spell has a SECOND effect of archetype 1 (Script, magnitude 0): a visual effect script that this task does
  not run (so "Raise Zombie" is counted "partly" below). Magnitudes of Reanimate are the level limit of the corpse (Raise Zombie 6,
  Reanimate Corpse 13, Revenant 21, Dread Zombie 30; the Dawnguard risen and vampire spells 8 to 99); durations are 60 s.
- **Counts of what now works completely** (every effect an archetype the magic runtime applies, fire and forget, delivery setting on; a
  spell is a SPEL of type 0): **scrolls 49 to 61 of 99 (the 12 new ones: the Conjure Familiar, Flame, Frost and Storm Atronach scrolls, the three
  thrall scrolls, the Raise Zombie, Reanimate Corpse, Revenant, Dread Zombie and Dead Thrall scrolls)**; **spells 208 to 250 of 677** (the Conjure
  spells with their hand variants and the DLC ones; the Reanimate SPELs with the Script effect stay "partly", the scroll versions have one effect).
  `SkyrimSummonsReal` prints the lines `SKYRIMSE-SUMMONS`. (Task 0242 had counted only scrolls: 49 of 99.)
- **Task 0202's `placeObject` refuses actors on purpose** (the pinned test `ObjectOpsQueue` and the design: a created actor needs handles in the actor
  tables, a body, a schedule and a save format). The engine side needed nothing else: a created placement of kind Actor goes through the same
  `ExtensionHost::applyNeutralObjectChanges` place op, `World::enable`, `Neutral::insertActorRendering` (the pack's `describeActor(base, variation)`, which
  takes the placement number as the pick of a leveled template), `announceNeutralActor` and `ActorRules::onActorAdded` as a placed ACHR, and the pack's
  tables (actor table, combat fighters, package brain) key on the placement id and the base the core hands them.

## 2. Design

**Path chosen for creating actors: one additive virtual on `WorldSourceService` (`placeActor(source, cellHint, prototype)`),**
not a version bump (the pinned `theServiceVersionIsOneThree` and the refusing `placeObject` tests stay), declared at the END of the interface with a
refusing default (found as the default by a host built before it). `WorldSourceServiceImpl::placeActor` shares the ordered queue and the id range with
`placeObject`; it requires kind Actor and accepts `cellHint = kNoWorldId` (an exterior: the host finds the grid square by position, as `findLoadedNeutralCell` does).
`placeObject` still refuses actors. Why not 0202's own path: it refuses actors in the host service (a pinned behaviour) and its `cellHint != kNoWorldId` rule
would refuse every exterior; the smaller change is a new call.

**Pack (`summons.*` pure, `combat_summons.cpp` members of `SkyrimCombat`, `makeSummonHooks` in the glue):**
- `SummonLedger` (preallocated, 64 entries): what each caster conjured or raised, in order, with the combat-clock time it ends.
- `SummonHooks` (`resolveBase`, `place`, `remove`, `follow`), set by the glue when `summons` is on: `resolveBase` is `SkyrimWorldView::summonBase` (an NPC_ is itself,
  a leveled NPC list picks an entry by the caster's level and the number of summons made: deterministic); `place` describes the base (`describeBase`), builds the
  object (`toNeutral`) and calls `placeActor` with the player's interior cell id (`SkyrimCellSource::playerInteriorCell`) or none; `remove` is the existing
  `setObjectEnabled(false)` queue (a created actor stays in its cell's list, disabled); `follow` is the actor table's `recruit` / `dismiss`.
- **Summon Creature** (`applySummonEffect`, one case in `applyOneEffect`, called for the caster itself as the target): the base of the associated form is placed
  `kSummonSpawnDistance` 150 units ahead of the caster and 8 above its feet (it falls to the floor), and a ledger entry ends at `mClock + duration` (60 when 0). A new summon
  by the same caster dismisses its oldest first (`summons max`, default 1: ours; Skyrim's perk raising it is not read). Because the data's delivery is
  "target location", `fireCast` sends a spell with a Summon Creature effect to the caster (a hook line before the delivery branches); without `combat spell delivery`
  it is not cast.
- **Reanimate** (`reanimateInSights`, a hook line in `fireCast` for delivery 2 and 3): the nearest dead non-player fighter within `kReanimateReach` 1500 and in front of the
  caster (`corpseInSights`, cosine 0.3), if its level is not above the effect's magnitude, is replaced: a NEW actor of the corpse's base is placed at the corpse's position and
  heading and the corpse placement is hidden (`remove`). No corpse, or a level above the magnitude: nothing happens (traced; the magicka is spent, as for any cast).
- **Allies.** The summon of the player is recruited as a follower on arrival (`follow(ref, true)`), so everything of task 0197 applies: it is on the player's side, follows him,
  fights what is hostile to the player's side within 1,500 of him and joins fights of his allies. A summon of an NPC takes its master's relations (`relationOf`) and is its
  friend (three edit lines in `combat_ai_run.cpp`; no NPC casts a summon in the AI, `aiPickSpell` takes aimed hostile fire and forget spells, so this path is unit
  tested only and not run in game).
- **Ends.** `tickSummons` (one line in `combatUpdate`): at the end of the duration a Summon is taken out (`remove`) and a raised corpse falls dead where it stands
  (`changeVital`, the spell damage path: death clip, `reportDeath`, a corpse that can be looted and raised again); a killed Summon lingers `kSummonDeadLinger` 2 s and is taken out;
  a killed raised corpse stays a corpse; a summon whose master (an NPC) died is dismissed. An actor the core removed (a cell unloaded) leaves the ledger.
- **Saves.** A summon is not saved (Skyrim dismisses them on a load, RECALLED-VERIFY): `WorldModel::collectNeutralSave` skips created actors, `CellStore::loadNeutralRefs` does not
  rebuild a created actor when a cell is built again, and `SkyrimActorTable::followerEntries` leaves created ids out of the saved followers. The corpse of a reanimate is a
  DISABLED pack placement, which the existing NWLD diff saves as disabled: **it stays hidden after a load** (decision 1 below; actor deaths are not saved by anything in this build,
  so a corpse that was not raised comes back alive at a load anyway).
- Test aids, declared in `extension.json`: `summons test kill = <editor id>` (the named actor dies 3 combat seconds after the data was read, so a scenario has a corpse),
  `summons test magicka = <n>` (the stand-in player's magicka maximum), `summons max`.

## 3. Stages and tests

Stage 1, stubs and the first tests (one header pass, one build, then the red run): `summons.cpp` and `combat_summons.cpp` did nothing, `SummonLedger` refused everything.
**21 tests in `testskyrimsesummons.cpp`: with the stubs 18 of 21 failed** (the 3 that passed test absence: the setting off, a living actor never raised, reanimate without
the delivery setting). Stage 2: the code; the first green run had one failure (a raised corpse that was never "ready" had no health to take: `ready()` added). Then, written
AFTER the code: the test kill (1), the conjure with target-location delivery and without the delivery setting (2), the aimed Reanimate (1), `placeActor` in
`test_place_actor.cpp` (6) and the saved-follower filter (1): these were NOT run red first (the aimed Reanimate and the target-location cases came from the real data, not from the spec).
`SkyrimSummonsReal` (gated, 1 test) prints the data numbers of section 1.

## 4. In game (Whiterun, the Bannered Mare, five masters; `tools/skysummons_check/check.sh`, a derivative of `tools/skydelivery_check`)

`OPENEDEN_QUIET_WINDOW=1`, `[Sound] master volume = 0.0` and `music volume = 0.0`, `OPENEDEN_FIXED_FRAME_TIME`. The player's real attack control (the left mouse button through
`OPENEDEN_RMLUI_INPUT_SCRIPT`) casts `ConjureFamiliar` or `RaiseZombie` (`combat test player spell`); a Lua global script places the player once and holds Hulda;
the camera pictures come from a free camera that looks at the summon. Settings: `actor packages`, `creatures`, `combat ai`, `combat factions`, `combat spell delivery` and `summons`.
- **Conjure Familiar.** The wolf `EncSummonFamiliar` is placed 150 units ahead of the player (`A game pack placed 'EncSummonFamiliar' as 4286578689 in the cell WhiterunBanneredMare`),
  arrives, follows, engages the hostile Hulda (`ai engage encsummonfamiliar (on sight) ... target actor108142`), lands hits (5, 5, 10 power ...: Hulda's health 75 to 15 over the run),
  Mikael and Saadia join against it as her allies, and after 60 combat seconds `COMBAT summon ended (its time is up)` and `A game pack disabled the placement 4286578689`.
  A second cast while the first lives dismisses the first (`summon ended (replaced)`), seen in an earlier run.
- **Raise Zombie.** Hulda is killed by the test aid, the player casts at her corpse: `COMBAT reanimate player raises actor 108142 as placement 4286578689 for 60.0 s`, the corpse placement is
  disabled and a new Hulda stands in its place, follows the player, engages the hostile Mikael (`ai engage hulda actor4286578689/7 (on sight) ... target actor108145/2`), both sides hit each
  other (the zombie lands 4 a hit, Mikael blocks and bashes), and at the end of the time `COMBAT summon ended placement 4286578689 (its time is up)` then `COMBAT death actor 4286578689 (spell)`:
  it falls dead where it stood. A second cast with no corpse left logs `found no corpse in the sights`.
- **Three runs of each** (`fw_a..c`, `fz_a..c`): the lines of the pack's own summon, reanimate, test kill and cast code are byte-identical (md5 `1df6047892c4` and `c67b6d36e01e` three times each);
  the whole fight trace is identical in two of three runs of each (the third differs in the combat AI: a third NPC assisting one update later, a block instead of a hit: the known path timing of
  many-fighter AI runs, task 0197), so the claim is the summon rules', not the AI's.
- Frames (`I:\Projects\OpenEden-work\ragdoll5\sum\keep`): `wolf_with_hulda_1..3.png` (the familiar beside Hulda from a free camera) and `zombie_hulda_rising_1.png`, `zombie_hulda_fighting_2.png`
  (the raised Hulda between Mikael and Saadia). The raised actor has no zombie look: it is the same NPC (decision 3).
- **Off unchanged:** the settings off, the pack's code paths are all behind `mSummonsOn`; the cast of a Conjure spell with `summons` off is as before (traced "is not supported" only for the
  effect). Morrowind: see the changelog row (`tools/worldobjects_check`, the Balmora frame).

## 5. Not done, limits and decisions for toddhow

0. **Files outside the new ones** (for the merge): `sdk/include/openeden/game/world_source_service.hpp` (`placeActor`, additive, version unchanged), `components/extension/game_services.*`,
   `apps/openmw/mwworld/worldmodel.cpp` (2 lines: created actors are not saved), `apps/openmw/mwworld/cellstore.cpp` (3 lines: not rebuilt), pack: `combat.hpp` (a block of members, `#include <cmath>`,
   `summons.hpp`), `combat.cpp` (hook lines: `onActorAdded`, `onActorRemoved`, `combatUpdate`, the test magicka), `combat_magic.cpp` (a `case` in `applyOneEffect`, two hook blocks in `fireCast`),
   `combat_ai_run.cpp` (`partyOf`, `relationOf`), `perks.*` (`mAssociatedItem`), `scroll.*` (overloads taking `summons`), `actor_table.cpp` (the saved-follower filter), `world_view.*` (`summonBase`),
   `neutral_world.hpp` (`playerInteriorCell`), `skyrimse_game_pack.cpp` (`makeSummonHooks`, the settings), `extension.json` (four settings), the CMake lists. No virtual was added to `CombatRequestWriter`
   and no existing test was edited. **Another agent adds effects to `applyOneEffect` and the scroll classification: a duplicate `case` for 18 or 22 would be a compile error to resolve at the merge.**
1. **A raised corpse's original stays hidden across a save and a load** (it is a saved "disabled" placement). Options: leave it (now), or save nothing for it by re-enabling it on a save (needs a pre-save hook that does not exist).
2. **`placeActor` is a new virtual without a version bump** (the pinned 1.3 stays). If you prefer a clean 1.4, three pinned tests change their minor number.
3. **No zombie look, no summon visuals or sounds:** the raised actor is the same NPC with the same skin, equipment and inventory (a corpse's looted items are not carried over: the new actor has the base's own); the
   conjure effects (the summon portal, the Script archetype companion of a Reanimate spell, the cast art of a target-location spell) are not drawn; Command Summoned (10), Banish, the werewolf howls and
   Dremora summons through perks are not built.
4. **The numbers that are ours:** the 150 unit spawn distance and 8 unit rise, the 2 s linger of a killed summon, the reach 1500 and cosine 0.3 of the corpse search, one summon per caster, the 60 s
   default; "magnitude is the highest level" for Reanimate is RECALLED-VERIFY (the data's magnitudes 6, 13, 21, 30 for the novice to master spells fit it).
5. A summon cast by an NPC (relation mapping, dismissal when its master dies) and a summon in an exterior (`cellHint` none) are unit tested or read from code only; the in-game runs are the Mare interior.
   The cell hint for a summon of an NPC standing in another cell than the player is the player's.
6. **Not run:** the Linux gate, clang-format 14 (formatted by hand to 120 columns), clang-tidy, the Lua integration tests, a fresh review, a save and load with a summon or a raised corpse (by reading only),
   the engine's unit tests for the two created-actor edits (`collectNeutralSave`, `loadNeutralRefs`: no test builds a neutral cell with a created actor; the in-game runs and the cap on follower entries are what show them).
