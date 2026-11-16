# Task 0198: Papyrus natives, second tranche (milestone M-S6)

Design doc: `docs/design/game-packs.md`; earlier slices: tasks 0176, 0181, 0187, 0194. Behind `[extension.skyrimse] papyrus`
(off by default); the inventory natives also need `[extension.skyrimse] inventory`, the combat and actor value natives need
the combat rules (`[Game] neutral combat`). No new setting.

Goal: real effects for the natives real scripts call most that an existing system can serve, and the events inventory and
combat can raise; a measured before and after of the tolerant (no-op) calls.

Sources for meanings: the Creation Kit wiki function pages (recalled, **RECALLED-VERIFY**; no page was opened this session),
nothing from Havok. CommonLibSSE and xEdit were not needed.

## 1. Survey (file and function, at `3885a4091b`)

- Natives are registered in `PapyrusHost::Impl::registerWorldNatives` (`game_packs/skyrimse/src/papyrus_host.cpp`) through `reg`;
  an unregistered declared native is tolerant in `Runtime::callNative` (`components/script_papyrus/papyrusinterp.cpp`): zero
  returned, logged once, `Stats::unregisteredNativeCalls` counted. That counter had no per-name split, so a ranking was
  impossible: **new `Stats::unregisteredByName`** (a map filled on that path, which already built the name string for its
  log-once key) and `PapyrusHost::tolerantRanking()`.
- Inventory: `inventory::InventoryModel` (`inventory.*`): owners are the placement id, the player is `kPlayerOwner` (0);
  `transfer` and `consume` existed, nothing could add an item from nowhere and nothing told anyone of a change.
- Combat: damage and death reach the core through `CombatRequestWriter::reportDamage` / `reportDeath`; `neutral_world.hpp`
  `SkyrimCellSource::combatUpdate` wraps the writer in `HudWriter` and `AiDamageWriter`. `SkyrimCombat::fighterView` gives a
  fighter's health, magicka, stamina and death; `setFighterHealth` (a test aid) writes health.
- Actor values for skills: `ActorMechanicsSource::actorStats` is the skills and levelling work of another agent: **not used**.
- `ObjectReference` moves: the only object call a pack has is `WorldSourceService::setObjectEnabled`. There is no SDK call
  to move an object or fade it, so `MoveTo` stays recorded only and `Enable`/`Disable` have no fade (decision for toddhow).
- HUD messages: nothing in the SDK shows text; the pack declares a Read-only script namespace instead (as the journal does).

## 2. The gap (Bannered Mare and Whiterun street, five masters, `papyrus`, `inventory`, `actor packages`, `neutral actors`,
fixed frame time, 14,000 frames)

Baseline executable (the parent), tolerant natives by name (the parent had no per-name counts; calls are the total):

| Scenario | tolerant natives | tolerant calls | native calls answered |
| --- | --- | --- | --- |
| Mare (183 s of script time) | 14 | 68 | 217 |
| Whiterun street (216 s) | 19 | 102 | 217 |

Ranked by calls, new executable before its natives (Mare `m1`; Whiterun `w1` after, see section 5): `debug.openuserlog` 11,
`actor.addperk` 7, `actor.isinfaction` 6, `actor.setfactionrank` 6, `alias.registerforupdate` 6, then 3 and below. Whiterun adds
`form.registerforanimationevent` 12, `objectreference.getlinkedref` 10, `objectreference.getcurrentdestructionstage` 9.

**Comparison with note 0046's top 106:** the survey of Bethesda's scripts ranks by call SITES, not calls in a run. The natives
this task implements (AddItem, RemoveItem, GetItemCount, RemoveAllItems, GetActorValue, DamageActorValue, RestoreActorValue,
SetActorValue, ModActorValue, IsDead, Kill, GetDistance, GetFormFromFile, GetLinkedRef, Debug.Notification kept) are in that
list, but real scripts in the two scenarios almost never CALL them: the Mare's scripts hit `GetActorValuePercentage` once
(now answered) and `AddInventoryEventFilter` (now answered). The calls that remain tolerant belong to other systems
(perks, factions, packages, destruction, animation events), which other tasks own. The natives were built to the task's
list, not to what these two cells measure.

## 3. Design

- `PapyrusWorldHooks` (new, `papyrus_host.hpp`): an additive interface the host calls for effects (inventory, actor values,
  death, notifications, item test). Every default means "cannot", so the natives stay tolerant without it (a test pins that).
  `PapyrusGameData` gains `formFromFile`, `linkedRef`, `initiallyDead` (defaults 0 and false).
- `papyrus_world.*` (new): `PackWorldHooks` over the inventory model and function objects; `ScriptCombatBridge` and the
  wrapper writer `ScriptEventWriter` (forwards every `CombatRequestWriter` virtual, including contract 0.5) telling the host of
  each damage and death report that went through, and starting the deaths `Actor.Kill` asked for inside the next combat update
  (where the writer is valid): `reportDeath` for the core, `setFighterHealth(0)` for the pack's own state.
  `PapyrusNotificationBindings`: the Read-only namespace `skyrimse.papyrus` 1.0 (`notification_count`, `notification(n)`),
  shown by `files/data/scripts/openeden/skyrimse/papyrus_player.lua` with `ui.showMessage`.
- Natives (CK wiki signatures, RECALLED-VERIFY): `ObjectReference.AddItem`, `RemoveItem` (also to another container),
  `GetItemCount`, `RemoveAllItems`, `GetDistance`, `GetLinkedRef`, `AddInventoryEventFilter`, `RemoveInventoryEventFilter`
  (on `ObjectReference` and `ReferenceAlias`); `Actor.GetActorValue`, `GetBaseActorValue`, `GetActorValuePercentage`,
  `SetActorValue`, `ModActorValue`, `DamageActorValue`, `RestoreActorValue`, `IsDead`, `Kill`; `Game.GetFormFromFile`;
  `Debug.Notification` (the pure native's text, now kept and shown).
- Actor values: health, magicka and stamina only (the player from the combat HUD's snapshot, an actor from `fighterView`);
  anything else reads 0 as before. An actor's health is written through `setFighterHealth` (a drop to 0 is a Kill); every
  other change is kept by the host as an offset on the pack's number (clamped to [0, maximum]) and saved.
- Events, all queued and run by the next tick: `OnItemAdded(akBaseItem, aiItemCount, akItemReference = None,
  akSourceContainer)` to the new owner and to reference aliases that hold it, `OnItemRemoved(..., akDestContainer)` to the old
  owner, `OnContainerChanged(akNewContainer, akOldContainer)` to scripts on the item form, `OnHit(akAggressor, akSource = None,
  akProjectile = None, abPowerAttack = false, abSneakAttack = false, abBashAttack, abHitBlocked)`, `OnDying(akKiller)` then
  `OnDeath(akKiller)` once per actor. The inventory model has ONE hook (`setChangeSink`, plus `add`), so the inventory windows,
  barter and a script's `AddItem` all raise the same events.
- Save: the host state is version 2 (actor value offsets, deaths, event filters); version 1 is still read.

## 4. Stages, tests

Same hand wrote tests and code; the tests were written after the code in one pass (a deviation from "tests first"); the red
evidence is by mutation: three one-line mutations (the sign of `DamageActorValue`, no once-only guard on a death, the bridge
dropping the death sink) failed 3, 1 and 2 named tests and were reverted. `testskyrimsepapyrusnatives2.cpp`: 32 tests (inventory
natives to the hook, tolerant without hooks, health through the pack, offsets for what the pack cannot write, bounds, IsDead and
Kill, GetFormFromFile, GetDistance, GetLinkedRef, notifications ring, every event and the filter, a form-list filter that
restricts nothing, save round trip, the tolerant ranking, the hooks over a REAL `InventoryModel`, the combat bridge and the
wrapper forwarding), plus a gated demo-script writer. No existing test edited.

## 5. Results

Tolerant calls (the no-ops), same scenarios and settings, parent against this branch:

| Scenario | tolerant natives | tolerant calls | native calls answered |
| --- | --- | --- | --- |
| Mare, before | 14 | 68 | 217 |
| Mare, after | 12 | 46 | 239 |
| Whiterun street, before | 19 | 102 | 217 |
| Whiterun street, after | 16 | 70 | 251 |

The calls that disappeared are `GetActorValuePercentage`, `AddInventoryEventFilter` and `GetLinkedRef` (10 in Whiterun). What is
left is other systems' (the lists are in the `PAPYRUS tolerantcalls` lines of the logs kept in
`I:\Projects\OpenEden-work\papyrus\keep198`).

In game (`tools/neutral_session_check/papyrusdemo.sh` with a generated `oedemotick.pex` written by the gated
`SkyrimPapyrusNatives2Demo`, five masters, Bannered Mare, `inventory`, `combat ai`, `neutral combat`): a script gives the
player 50 gold (the player's own 100 goes to 150), takes 20 (130), reads his health 100 and stamina 100, damages the stamina
to 90, says `Debug.Notification` (shown on the HUD: `keep198\notification_hud.png`), and kills Hulda by placement id (`IsDead`
false then true); the pack logs `PAPYRUS item 0000000F x50 from 00000000 to 00000014` for each change, `PAPYRUS death of
0001A66E killer 00000000`, and `PAPYRUS hit on ... by ...` for every blow of a Hulda fight (12 hits both ways in 26 s). 40 item
events reached real quest scripts in the Mare. Three runs (fixed frame time): the item, hit, death and OEDEMO198 lines are
identical; only the last summary line and wall-clock build times differ with the run's end. Papyrus on against off, same combat
scenario (`n5ai`, `OPENEDEN_NEUTRAL_COMBAT_TRACE`): the 107 NCOMBAT lines are identical (md5 68aa1d2f...). Morrowind:
`tools/worldobjects_check` 110 lines OK against the `integ2` build (a build of main, not of this exact parent) and the fixed
Balmora frame is `1c5d47ad0b5dc32d`, the parent's recorded hash.

Tests: `Skyrim*:*Papyrus*:*Inventory*:*Combat*:*Extension*:*Frozen*` in `components-tests` 2,094 pass; `openmw-tests`
`*Neutral*:*Skyrim*:*Extension*:*Lua*` 812 pass. `CI/check_tests_registered`, `check_layer_rules` (104), `check_extension_sdk`
and `tools/testprofile/coverage.sh` pass.

**Edits outside `papyrus_host*`** (small and additive): `inventory.hpp/cpp` (the one hook: `add` and `setChangeSink`);
`neutral_world.hpp` (the wrapper writer in `combatUpdate` and a bridge member: the only combat-side edit; `combat.*` untouched);
`world_view.*` (`formFromFile`); `components/script_papyrus` (`Stats::unregisteredByName`); `skyrimse_game_pack.cpp` (the
wiring); `files/data` (one Lua script and its builtin line).

## Not done, and decisions for toddhow

1. **`MoveTo` for real and `Enable`/`Disable` with fade need SDK work**: `WorldSourceService` has only `setObjectEnabled`.
   A `setObjectPosition` (and an enable with a fade time) at the end of the service, plus the engine side, is a small task of
   its own; today `MoveTo` is recorded and saved but the engine is not moved.
2. **Kill has no death animation of its own**: it takes the core's `reportDeath` path (ragdoll or corpse as combat's deaths do).
3. **`PlaceAtMe`, skills as actor values, health of the player (the engine owns it), the `Is3DLoaded`,
   `GetCurrentDestructionStage`, `RegisterForAnimationEvent` and `Debug.OpenUserLog` natives are not done** (no system serves
   them or the meaning is unsure). `OpenUserLog` is the biggest remaining tolerant call in both cells (11).
4. A filter that names a form list restricts nothing (the host cannot expand a list): scripts with such a filter receive every
   item event.
5. `OnItemAdded` to a script on the PLAYER reference only reaches reference aliases that hold the player (nothing attaches
   scripts to 0x14 directly).
