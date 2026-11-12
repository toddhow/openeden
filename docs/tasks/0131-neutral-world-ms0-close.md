# Task: M-S0 close-out, Lua and mechanics safety over a neutral cell (N3a-6) and the acceptance pass (N3a-7)

Status: **APPROVED** under toddhow's blanket "go with your recommendations" of 2026-11-11 (decision D13: full loop for engine work). Branch `ms0close`.
Design doc section: `docs/design/neutral-world-model.md` 10.4 (R6, R10), 10.6 (rows N3a-6 and N3a-7); `docs/design/skyrim-se-implementation-plan.md` 5.2 and 8 item 1 (milestone M-S0).
Goal: (1) a neutral Skyrim session does not crash or corrupt state when Lua, mwscript, the console or the mechanics touch a `MWClass::Neutral` object or a neutral cell; every `MWWorld::Class` virtual that a generic engine path can call on a non-actor `Ptr` answers safely instead of throwing; (2) a written, repeatable acceptance list for M-S0/M-S1 that toddhow can run himself, run once here with frames viewed, with Morrowind shown unchanged.
Inherited code touched: `apps/openmw/mwclass/neutral.{hpp,cpp}` (the class; new overrides only, existing ones unchanged); `tools/neutral_session_check/` (a new scenario, `ms0sweep`); `docs/toddhow-checks.md`. Nothing under `apps/openmw/mwlua`, `mwscript` or `mwmechanics` is changed unless the sweep shows a crash there (then the finding and the fix are logged here).
Inputs / outputs: no SDK change, no new setting. New test file `apps/openmw_tests/mwworld/testneutralsafety.cpp`.
Constraints: main thread only; no allocation per frame; the neutral path stays behind `[Game] neutral world`; Morrowind behaviour unchanged (`tools/worldobjects_check` dump and the fixed-frame Balmora frame equal to a parent build).

## What N3a-6 covers (from design 10.4 and 10.6)

- **R10, Lua:** every `Class` virtual the Lua object API calls on a neutral `Ptr` must not throw; "N3a-6 runs the Lua object API over every kind".
- **R6, content files:** every reader of `ESM::RefNum::mContentFile` must be shown never to mis-handle a neutral ref (reserved file `kNeutralRefNumFile`, negative, so `hasContentFile()` is false). The audit table is in the stage log.
- **The incremental queue** (task 0060): the neutral `Ptr`s go through it unchanged; shown with `[Cells] incremental cell build` on in the exterior hop scenario.
- **Mechanics:** a neutral actor is an inert reference (`isActor()` false), so `Actors`, AI, combat and spells never see one; shown with `[Game] neutral actors` on (the objects are drawn) while the sweep and a Lua pass over `types.Actor` run.

## Method

1. Read the `Class` interface and list every virtual whose default throws (`apps/openmw/mwworld/class.cpp`); for each, find whether a generic path (not guarded by `isActor()`, `isItem()`, `isDoor()` or a Lua `objectIsInstance`) can reach it with a neutral `Ptr`. Those get a safe override in `MWClass::Neutral`.
2. A unit test (`testneutralsafety.cpp`) calls those virtuals on one neutral `Ptr` of every `ObjectKind` and expects no throw and the safe value; written first, run red against the unmodified class.
3. An in-game sweep, `tools/neutral_session_check/check.sh ... ms0sweep`: a global Lua script and a player Lua script call a broad set of object, cell, world, nearby, `types.*` and `mwscript` APIs on every object of the Bannered Mare (read-only first, then mutations: scale, teleport, scripts, activate, move into the player's inventory, remove), each in `pcall`, and print per-API ok/error counts. The run must exit 0, with no crash dump and no engine error line other than the known save refusal and refusals the sweep itself provokes.
4. mwscript/console commands on neutral references through `--script-run` (`tools/neutral_session_check/mwscript`).

## Acceptance criteria

- [ ] Each virtual on the list answers without throwing for every kind (unit test, red first).
- [ ] The sweep runs to its last line in the Bannered Mare with `neutral actors` off and on; exit 0; no crash dump; the list of errors it saw is in the stage log and each is a refusal (Lua error text) not a crash.
- [ ] The exterior hop scenario with `incremental cell build` on runs to the end.
- [ ] N3a-7: the written steps (below, and in `docs/toddhow-checks.md`) were run through the check tool; frames viewed.
- [ ] With `[Game] neutral world` off, `tools/worldobjects_check` dump equals the parent build's and the fixed-frame Balmora frame has the same SHA-256.
Out of scope: saving (N6), actors' behaviour (N5), Skyrim scripting, Papyrus, lock picking.

## Stage log

### Stage log (2026-11-12; same hand wrote tests and code, no fresh reviewer; Windows only; branch `ms0close`, not pushed)

Built in `I:\Projects\OpenEden-work\ms0close\build` (RelWithDebInfo). A parent executable (`openmw_parent.exe`, built from `main` before any edit here) was kept; `ms0close\refdir` is a copy of the build folder with that executable as `openmw.exe`.

**Entry points made safe** (`MWClass::Neutral` overrides; each default threw): `getValue`, `getWeight`, `getCapacity`, `getEncumbrance` (so `getNormalizedEncumbrance`), `isPersistent`, `getEnchantmentPoints`, `getInventoryIcon`, `getItemMaxHealth` (so `getItemHealth` and `getItemNormalizedHealth`), `getUpSoundId`, `getDownSoundId`, `getSoundIdFromSndGen`, `getBaseGold`, `setRemainingUsageTime`, `getDoorState`, `setDoorState`. They answer zero, empty, false or `Idle`. Left refusing on purpose (a runtime_error, which Lua turns into a Lua error and the console prints): `getCreatureStats`, `getNpcStats`, `getMovementSettings`, `getContainerStore`, `getInventoryStore`, `hit`, `onHit`, `evaluateHit`, `getSkill`, `getArmorRating`, `getBaseFightRating`; nothing generic reaches them because `isActor()`, `isItem()` and `isDoor()` are false for every neutral kind (pinned by a test).

**Tests first, red:** `apps/openmw_tests/mwworld/testneutralsafety.cpp` (6 tests, one neutral object of each of the 14 base kinds in a real `CellStore`). Against the unmodified class 3 failed (the item properties, the door state, the time-limited use; 14 distinct throws; log `ms0close\red_safety.txt`), 3 passed (they pin what was already safe and the refusals). After the overrides all 6 pass; `openmw-tests` 2,058 pass, 2 skipped (real-data, existing). No existing test edited. No mutation checks were run.

**The sweep** (`tools/neutral_session_check/check.sh ... ms0sweep`, scripts `luatest/scripts/ms0/{global,player}.lua`): global script: 14 object properties, `owner.*`, `getBoundingBox`, `hasScript`, `sendEvent`, every `types.*.objectIsInstance`, about 30 typed calls (`Actor`, `NPC`, `Creature`, `Item`, `Container`, `Door`, `Lockable`, `Light`, `Static`, `Activator`, `Player`), `mwscript.getLocalScript`, 25 cell, world and core calls, then mutations on every 7th object (`setScale`, `teleport` in the cell, `addScript`, `removeScript`, `activateBy` the player, `moveInto` the player's inventory) and `remove` on every 11th; player script: `nearby.*` lists, `castRay`, `findPath`, the stub player's stats, spells and inventory, `ui.showMessage`. Result in the Bannered Mare (697 objects): exit 0, 0 crash dumps, 0 engine error lines, 697 of 697 objects valid after the mutations, 634 objects left after removing 63. Every typed call on a neutral object is a Lua error (`Requires type 'Door'...`, `Bad LiveCellRef cast to STAT from Neutral`, `class does not have creature stats`, `class does not have a container store`, `Actor expected`) or returns an empty or false answer. `moveInto` the player: "Can't remove 0 of 0 items" (refused). Also run with exit 0 and no dump: with `[Game] neutral actors = true` (the NPCs are drawn; the first run after a build tripped the 5 s freeze monitor on cold caches and the second run was clean), in WhiterunDragonsreach (1,441 objects), Angarvunde01 (1,256) and DawnstarBarracks (128). The sweep run before the overrides existed was not made, so it does not show that the overrides were needed for a Lua path: they close the generic paths (`isPersistent`, `getValue`, ...) that no sweep call reaches today.

**mwscript and the console** (`--script-run` with `tools/neutral_session_check/mwscript/neutral_refs.txt`, `OPENEDEN_CONSOLE_TRACE=1`; new developer aids in `mwgui/console.cpp`: the console's output is copied to the log, and a `#select <record id>` line selects the first such object of the player's cell as a click does): a neutral object cannot be named in a command at all (`"skyrimse:b152263"->GetPos X` gives "Unexpected name": `CompilerContext::isId` knows no neutral record), so named references are a clean refusal; with the object selected, 8 objects x 40 commands (`GetPos`, `Disable`/`Enable`, `GetDistance`, `SetScale`, `SetAngle`, `SetPos`, `GetLocked`/`Lock`/`Unlock`, `AddItem`/`RemoveItem`/`Equip`/`GetItemCount`, `PlaceAtMe`, `Activate`, `GetWeight`, `GetValue`, `GetDoorState`, `SetDoorState`, `PlaySound3D`, `StopCombat`, `AIWander`, ...) ran with 0 crashes: results are numbers, console error text ("class does not have an inventory store", "unknown ID") or "Unexpected name" for commands that do not exist here. Log kept in `ms0close\evidence`.

**The content-file audit (R6).** Neutral refs have `mContentFile = kNeutralRefNumFile` (-0x40000000), `hasContentFile()` false. Readers: Lua `contentFile` (guards the index range, returns nil: swept, 697 of 697 ok); `userdataserializer` and `luaevents` (save and load only; saving is refused); `character.cpp`, `scene.cpp`, `npc.cpp`, `distantobjects.cpp`, `lodclusters.cpp`, `lodobjectbaker.cpp`, `puddlestatics.cpp`, `probebaker.cpp` (they use it as part of a hash or map key and never index a file list; by reading); `menuscripts.cpp` and `worldimp.cpp` (the list itself); `cellref.cpp` (generated numbers count down from -1; the neutral file is the reserved -2^30, so a collision needs a billion generated refs; by reading).

**The incremental queue:** `... e2terrain` with `[Cells] incremental cell build = true` (Mare, out of the front door, walking across grid cells): exit 0, no dump, no error line. That the queue itself ran on neutral cells was not separately traced.

**N3a-7 acceptance** (`tools/neutral_session_check/acceptance.sh`; the written steps are in its header and in `docs/toddhow-checks.md` section 2; all eight steps OK, frames viewed in `ms0close\acc\<step>\user\screenshots`): look (textured Bannered Mare), collide (standing at z 99.68 on a bench seat over a floor ray of 69.79; round table, bar counter and table-and-chair block the walk), stairs (the `N4S` run up and down, z 33 to 310), doors (Barracks to Jail and back, built then reused, base records 123 and resident cells 2 at every hop), key (`Angarvunde01` locked door: `NLOCK -> 2`, "You unlock it with the key." viewed, arrival 2720,-7488,-992, the way back reused, the first door again `-> 1`), chest (strongbox `NLOCK -> 3`, "It is locked." viewed), outside (front door to `WhiterunPlainsDistrict04`, ground 0.9 to 2.9 units under the drawn surface, walking; frame viewed), sweep. Steps 4 to 6 were rerun with `OPENEDEN_NEUTRAL_DOOR_TRACE=1` (now in the script) to log the lock decisions.

**Morrowind unchanged:** `tools/worldobjects_check` new build against `refdir` (the parent): 110 lines each, OK; the fixed-frame Balmora screenshot has SHA-256 prefix `1c5d47ad0b5dc32d` for the parent and the new build with `[Game] neutral world` off and on.

**Not run:** the Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua integration tests, `components-tests`, sanitizers, a fresh reviewer, mutation checks of the overrides, the incremental queue's own trace, a real mouse click in console selection, mwscript against an exterior neutral object, items and actors beyond the inert reference (N5), saving (N6). The `mwgui/console.cpp` edit was built and used.

### Decisions for toddhow

1. The safe answers are in `MWClass::Neutral`, not in `MWWorld::Class`: Morrowind and ESM4 classes keep throwing (the ESM4 classes have the same exposure and were not changed).
2. A neutral object cannot be named in a console or mwscript command (`isId` does not know it). If Skyrim scripts or a Skyrim console are wanted, that belongs to the Skyrim script stage, not N3a.
3. `OPENEDEN_CONSOLE_TRACE` and `#select` are developer aids in `mwgui/console.cpp` (off unless the variable is set); say if you would rather keep them out of the console file.

