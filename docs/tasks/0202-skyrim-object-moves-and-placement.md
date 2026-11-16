# Task 0202: Skyrim object moves, fades and placement (milestone M-S6)

Status: BUILT, Windows only, local commit on branch `objects` from `7873a3301f`, not pushed (toddhow, overnight 2026-11-16).
Same hand wrote tests and code (stub pass first for the pure parts, see the stage log); no fresh reviewer.
Design doc: `docs/design/game-packs.md`, `docs/design/neutral-world-model.md`; earlier: tasks 0177 (neutral world saves,
the NWLD record), 0181 and 0198 (Papyrus). Behind `[extension.skyrimse] papyrus` (off by default) AND `[Game] neutral world`;
saving of the changes needs `[Game] neutral saves`. No new setting.

Goal: `ObjectReference.MoveTo`, `MoveToMyEditorLocation`, `SetPosition`, `SetAngle`, `Enable(abFadeIn)`, `Disable(abFadeOut)` and
`PlaceAtMe` do what they say in the engine, and the three kinds of change survive a save and a load in a new process.

## 1. Survey (read at `7873a3301f`)

- The only object call a pack has: `WorldSourceService::setObjectEnabled` (`sdk/include/openeden/game/world_source_service.hpp`,
  version 1.2). `WorldSourceServiceImpl::setObjectEnabled` (`components/extension/game_services.cpp`) coalesces it in a bounded
  queue (4096); `ExtensionHost::applyNeutralObjectChanges` (`apps/openmw/extensionhost.cpp`, called from `Game::run` after
  the game pack's `GameSimulation` phase) takes it and calls `World::enable` / `World::disable` on
  `WorldModel::findLoadedNeutralObject`. That test is pinned by `test_object_enable_queue.cpp`: the 1.2 queue stays as it is.
- The Papyrus host (`game_packs/skyrimse/src/papyrus_host.cpp`): `Enable`, `Disable` call `setEnabled` (host table
  `mEnabled`, callback to `PapyrusDriver::pushEnabled` in `skyrimse_game_pack.cpp`); `MoveTo` only writes `mPositions` (saved,
  never sent). After a restore the driver pushes every enable override again (`applyRestore`). `PlaceAtMe` is not registered.
- Engine objects: a neutral placement is a `LiveCellRef<NeutralBase>` in `CellStore::get<NeutralBase>()` (a `std::list`, stable
  addresses), built by `CellStore::loadNeutralRefs` from the pack's `WorldCell` snapshot `mNeutralCell`; its `RefNum` is
  `neutralRefNum(placement)` (reserved content file); `CellStore::neutralObject` is a `lower_bound` over the snapshot's ids
  (ascending). `World::moveObject`, `rotateObject`, `enable`, `disable` work on any `Ptr`; `Class::copyToCell` calls
  `unsetRefNum`, so it cannot be used to make an object with a chosen placement id.
- Saving: `WorldModel::collectNeutralSave` diffs each loaded neutral cell's live refs against the snapshot
  (`diffNeutralObject`); `World::write` writes the `NWLD` record (`mwworld/neutralsave.*`, version 1, canonical bytes) on the save's
  worker thread, which is a game state that runs alone (the main loop does not run during a save). `restoreNeutralSave` rebuilds
  the cells from the pack, then applies the changes, BEFORE the pack's `GPCH` chunk (the Papyrus state) is restored.
- Fade: `MWRender::setTransparency(MWAnim::Object&, alpha)` (`apps/openmw/mwrender/transparency.hpp`) sets `object.alpha` in the
  object state group; `files/shaders/default/main.frag` dithers with it (`#ifdef NORMAL`, the invisibility path). No shader
  edit is made (the `.interface` records are golden data).

## 2. Design

**SDK: `WorldSourceService` 1.3** (three virtuals at the END, default "not supported"; the host that predates 1.3 answers false):

- `moveObject(source, cellHint, placement, ObjectMove{hasPosition, position, hasRotation, rotation (a quaternion, the pack
  converts its angles through its own `eulerToQuat`)})`;
- `setObjectEnabledFaded(source, cellHint, placement, enabled, fadeSeconds)`: the default forwards to `setObjectEnabled`;
- `placeObject(source, cellHint, WorldObject prototype)` returns the new placement id at once (`0xFF800000 + n`, a range no load
  order form id can have; `kNoWorldId` when the host cannot or the queue is full: no id is minted).

All three go through ONE ordered queue (`WorldSourceServiceImpl::takeObjectOps`, 4096), so a place, a move and a disable keep
the script's order. The 1.2 enable queue is untouched. The pack uses only the 1.3 calls. The queue is cleared when the source is
withdrawn.

**Engine** (`ExtensionHost::applyNeutralObjectChanges`, main thread, before the frame's physics):

- Move: `World::moveObject(ptr, ptr.getCell(), position)` and `rotateObject` (the object stays in its home cell store: NWLD
  keys objects by cell and placement, so a move that re-homed it would lose it). A `MoveTo` that reaches another loaded cell or world
  than the object's own is refused with a counted log line and stays recorded in the pack (decision for toddhow).
- Fade (`mwworld/neutralfade.*`, pure): Disable with a fade time keeps the object enabled and drives the object alpha 1 to 0 with the
  dither of the invisibility path, then disables it and puts the alpha back to 1; Enable fades 0 to 1. A fade is not saved: the
  saved state is the target state. `advanceNeutralFades(dt)` runs once a frame.
- Place: `WorldModel::addNeutralCreated` registers the base (`Store<NeutralBase>::addFromObject`), builds the `ESM::CellRef` with
  `makeNeutralCellRef` (RefNum = the placement id), inserts the `LiveCellRef` in the cell's list, appends the prototype to the snapshot (ids
  stay ascending) and marks it created; at run time the ref is inserted disabled and then enabled through `World::enable`, which adds
  it to the scene and the physics world as any enable does. Actor, leveled list and unknown bases are refused (see section 6).

**Saving.** NWLD version 2 (version 1 bytes are written whenever there is no created object, and both are read): a created
object is an entry with the new flag `NeutralSaveCreated` that carries its prototype (base id, kind, editor id, model path,
bounds) and its CURRENT state (position, rotation, scale, enabled), and the state carries `nextCreatedIndex`. Moves and
enable changes of pack objects are the existing diff. On a load the created objects are inserted into the rebuilt cells, then
`WorldSourceServiceImpl::setNextCreatedIndex` is set so that no id is minted twice. The Papyrus host saves its table of created
references (id, base, cell) and its angle overrides (state version 3, 1 and 2 still read) and, as it did for enables, pushes its
position and angle overrides again after a restore (an object of a cell that was not loaded when it was moved).

## 3. Stages and tests

1. **SDK 1.3, the ordered queue, the pure parts, all against stubs** (one header pass, one build, then the red run):
   `WorldSourceServiceImpl` (`moveObject`, `setObjectEnabledFaded`, `placeObject`, `takeObjectOps`, the id counter),
   `MWWorld::NeutralFades` (the fade schedule), NWLD version 2 (`NeutralSavedObject::mCreated`, `saveCreatedNeutralObject`), the Papyrus
   host's accessors (`createdObjects`, `angleOverride(s)`, `positionOverrides`) and `SkyrimWorldView::describeBase`.
2. **Real code**: the queue; the schedule; the NWLD encoder and decoder; the host natives (`MoveTo`, `SetPosition`, `SetAngle`,
   `MoveToMyEditorLocation`, `Enable`/`Disable` with a fade, `PlaceAtMe`) and state version 3; `describeBase`; the engine: `CellStore::
   insertNeutralCreated`, `WorldModel::addNeutralCreated`/`findLoadedNeutralCell`, collect and restore of created objects,
   `ExtensionHost::applyNeutralObjectChanges`/`advanceNeutralFades`/`resetNeutralObjects`, `World::writeNeutralSave`/`restoreNeutralSave`;
   the pack driver (`pushEnabled`, `pushMove`, `placeObject`, `pushMoves`, the cell-build move lookup).
3. In game with a generated script, a Lua scenario and rays; save and load in a new process; determinism; Morrowind and papyrus-off
   comparisons; mutation checks.

## 4. Acceptance

- [x] A generated script moves a barrel, fades another out and places a new object; frames show all three; a Lua ray hits the
      barrel at its new place and not at the old one (and the new barrel's top where it was placed).
- [x] Save, load in a new process: the three changes persist (moved, disabled, the created barrel with the same id).
- [x] Papyrus off: the trace equals the parent's. Morrowind: `worldobjects_check`, the fixed Balmora frame and a save round trip.

## 5. Stage log

Build tree `I:\Projects\OpenEden-work\papyrus\build` (RelWithDebInfo, `-j 8`), Windows only. Evidence kept in
`I:\Projects\OpenEden-work\papyrus\keep202` (four frames, the logs of one save and one load run, the normalised comparison files).
The parent's binaries were copied to a scratch folder before the first rebuild (`openmw.exe` of `7873a3301f`, SHA-256 prefix
`e254dd7dd671abd2`) and are deleted now.

**Red runs (stub binaries, before any real code; the stubs refuse everything).** `ObjectOpsQueue` 8 of 12 failed (the 4 that passed
test absence: the version number, the default forwarding, a source that is not registered, the refused kinds); `SkyrimPapyrusObjects`
16 of 20 failed (4 pass at once: the world refusing, no hooks, an unknown position, a target with no position);
`SkyrimDescribeBase` 2 of 3; `NeutralSaveCreated` 11 of 15; `NeutralFade` 10 of 11. **Not run red** (written after the stub run or with the
code): the 2 cross-cell host tests, the `toOriginal` queue test, the `NeutralFades::cancel` test, the 5 `CreatedFixture` tests (the CellStore glue
was written in the same pass as the stubs), the `PackWorldHooks` forwarding test and the demo writer. Mutation checks instead (all caught by named
tests, then reverted): dropping `requestMergedRefsUpdate()` in `insertNeutralCreated` fails `CreatedFixture.a_created_object_is_visited_by_
forEach_like_every_other_reference`; `||` to `&&` in the cross-cell rule fails `MoveTo_into_another_cell_of_which_one_is_an_interior_is_
refused_and_not_recorded`; the minted id off by one fails 2 `ObjectOpsQueue` tests.

**Tests.** New: `ObjectOpsQueue` 13, `SkyrimPapyrusObjects` 22 (+1 gated demo writer), `SkyrimPapyrusObjectHooks` 1, `SkyrimDescribeBase` 3
(`components-tests`), `NeutralSaveCreated` 15, `NeutralFade` 12, `CreatedFixture` 5 (`openmw-tests`); all pass. Wider groups
(`Skyrim*:*Papyrus*:*Extension*:*Frozen*:*WorldSource*:*Neutral*:*GamePack*:*Compat*:*Contract*:*Lifecycle*` in `components-tests`: 2,133 pass; `*Neutral*:*Skyrim*:*Extension*:*Lua*:*Save*:*Created*` in `openmw-tests`: 904 pass).
**Two existing tests fail and are NOT edited** (they pin the service version 1.2; they need toddhow's approval, minor 2 to 3):
`apps/components_tests/extension/test_object_enable_queue.cpp` `ObjectEnableQueue.theServiceVersionIsOnePointTwo` (line 54) and
`apps/components_tests/extension/test_world_source_service.cpp` `WorldSourceService.theVersionIsOneZero` (line 130, expects minor 2).
`worldmodel/testfrozen.cpp` does not pin the service (it pins `world_model.hpp` and `world_source.hpp`, which are untouched). The CI checks
`check_extension_sdk`, `check_layer_rules` (104), `check_tests_registered`, `check_mwbase_fakes`, `check_game_conventions` and
`tools/testprofile/coverage.sh` pass.

**In game** (`tools/neutral_session_check/objectsdemo.sh`, five Skyrim masters, Bannered Mare, `[Game] neutral world`, `neutral saves`,
`[extension.skyrimse] papyrus`, `[Sound] master volume = 0.0`, quiet window, fixed frame time, seed 7; the generated plugin `oedemo.esp` has a
barrel activator placed twice (A: `0x01000801`, B: `0x01000802`) with the generated script `oedemotick.pex`, written by the gated test
`SkyrimPapyrusObjectsDemo`; the Lua scenario is `luatest/scripts/n9`): four seconds after its reference starts, A runs `MoveTo(self, 0, -100, 0)`,
`PlaceAtMe(own base)` and `MoveTo(new, A, 80, 0, 0)`; B runs `Disable(true)`. The pack logs `PAPYRUS move of 05000801 ... sent to the world`,
`PAPYRUS placed 05000800 as FF800001`, `PAPYRUS move of FF800001`, `PAPYRUS disable of 05000802 ... with a fade`; the engine logs
`A game pack moved the placement 83888129 to -188, 160, 68`, `A game pack placed 'OEDemoStatue' as 4286578689 in the cell WhiterunBanneredMare
at -188, 160, 68`, `... moved the placement 4286578689 to -108, 160, 68` and `... started to fade the placement 83888130 out over 1 s`;
Lua (`o.position`, the engine's own number) sees A at (-188, 160, 68), the created object `@0x3fffffffff800001` at (-108, 160, 68) and B disabled.
Rays straight down (against the world's collision): A's old spot hits the floor (z 68.0), A's new spot hits the barrel top (z 146.0), the created
barrel's spot hits a barrel top (146.0), B's spot hits the floor: **the collision moved with the object**. Frames: `1_before.png` (two
barrels), `2_midfade_B_dithered_A_moved.png` (B half dissolved in the object shader's ordered dither, A nearer the camera),
`3_after_A_moved_C_placed_B_gone.png` (A and the new barrel side by side, B gone). **Save and load in a NEW process** (the save holds
`Neutral save: 1 cells with changes, 3 changed objects`): `Neutral world restored: ... 3 changed objects (0 no longer in the game) ... 1 created
objects`; Lua sees the same three states and the same rays hit the same places; `4_after_load_in_new_process.png` equals frame 3. **The Papyrus host
restore was exercised in the final runs only** (an earlier load run quit before the host started, because simulation time carries over from the save: the
scenario now times its rays from the first update, 170 simulation seconds): `PAPYRUSLOAD restored: 881 script instances, script time 8 s`, then the pack
replays its overrides (`PAPYRUS disable of 05000802`, `move of 05000801`, `move of FF800001`; the engine logs the same three changes, no second placement). A
**second generation** (the loaded game saved again, 623 kB, loaded in a new process): `PAPYRUSLOAD restored: 882 script instances, script time 112 s`, one created
object, same rays; the counter survived (a script that placed again would have got `FF800002`). Three save runs and three loads of one save give byte-identical
normalised lines; **the normalisation cuts timestamps, picture numbers AND the simulation times `t=`** (the effects land at 48 to 60 simulation seconds because
the host starts when a worker is ready), so this shows the same states and the same order, not the same timing. A save with no created object is NWLD version 1 as
before (pure test): a save written by the PARENT build loads in this one (`xl_parent_save_loaded_by_new.lines`: 1 changed object, no error); a save with a
created object is version 2, which the parent refuses ("damaged" for an unknown version).

**Unchanged when off.** Papyrus off: the normalised traces of the parent and this build are equal, **but they are four lines** (the neutral start line, the cell
insert line and two object states; md5 `4a096b78...`). With Papyrus on a new Verbose line appears when a change reaches no loaded cell. Papyrus on, the old demo of
task 0181 (plain `Disable`): the placement is disabled at the same script step through the new queue.
Morrowind: `tools/worldobjects_check` 110 lines OK against the `integ2` build (a build of main, not of this exact parent) and the fixed
Balmora frame `1c5d47ad0b5dc32d` for the reference and this build (and again with `[Sound] master volume = 0.0`); a Morrowind save written by the parent
loads here and the other way round (`mwsaveload.sh`, both `MW-ROUNDTRIP-EQUAL`). **The sound rule was not followed for the Morrowind checks, the Skyrim
papyrus-off pair and the old-demo regression** (`mw.sh`, `mwsaveload.sh`, `worldobjects_check` and those runs used a config without the sound setting; the window
was quiet); the Skyrim demo runs and the Balmora rerun had it. The whole `openmw-tests` binary passed (2,788 of 2,790, 2 skipped, `keep202\openmw_tests_full.log`); the
whole `components-tests` binary was NOT run (filtered groups only).

**Actors.** A review of the first build found that `MoveTo`, `SetPosition`, `SetAngle` and the fade acted on neutral actors too, which the pack's mover and brain
own and NWLD leaves out. The engine now refuses a move of an actor placement with a counted log line (`A game pack's move of the actor 108142 is not done`) and
does an actor's fade as an immediate enable or disable (the old behaviour). In game (`OPENEDEN_DEMO_ACTOR=1` demo: `SetPosition` and `Disable(true)` on Hulda): the move
is refused, Hulda is disabled at once (`keep202\act_actor_case.lines`).

**Found on the way.** (1) The alias tag of the Papyrus host is `0xFF000000`, so the draft id base was moved to `0xFF800000` and the alias-handle
test excludes created ids. (2) Skyrim SE's `PlaceAtMe` has 4 parameters, not the 5 of Fallout 4; the first demo script passed 5 and the host said
`[argument count]`. (3) `CellStore::forEach` walks a merged list that must be refreshed after an insert: the first created barrel was in the cell
and drawn but invisible to `cell:getAll()` and, after a load, to the scene (fixed with `requestMergedRefsUpdate()`; the red test is the `forEach`
test). (4) The Papyrus host starts about 17 wall seconds after the process (its quest data is read on a worker), so a fixed-time scenario cannot time
its checks in advance; the Lua scenario is event driven.

## 6. Not done, limits and decisions for toddhow

0. **The API is not the shape the task text sketched.** `moveObject(source, cellHint, placement, ObjectMove)` takes a struct (position, rotation as a quaternion, and
   `toOriginal` for `MoveToMyEditorLocation`); `setObjectEnabledFaded(source, cellHint, placement, enabled, seconds)`; `placeObject(source, cellHint, WorldObject)` takes a
   full prototype, so the PACK works out the position (the reference's, plus nothing) and describes the base (`SkyrimWorldView::describeBase`), not an offset from a placement
   inside the host. **Files outside `papyrus_host*`, the world source service and its engine side that this task touched** (for the merge): `skyrimse_game_pack.cpp`,
   `neutral_world.hpp/.cpp` (the move lookup in `buildCell`), `world_view.hpp/.cpp` (`describeBase`), `papyrus_world.hpp`, `apps/openmw/game.hpp` (one call),
   `mwbase/extensionmanager.hpp`, `mwworld/cellstore.*`, `worldmodel.*`, `worldimp.cpp` (two calls), `neutralsave.*`.

1. **The two pinned version tests above** (minor 2 to 3): not edited.
2. **`PlaceActorAtMe` is NOT built.** A neutral actor is made at cell build from a placed ACHR: the actor index (`ActorAppearanceSource`), the
   `NeutralActor` body, the package brain, the combat fighter table and the dialogue source all key on a placement the pack knows, and NWLD saves no
   actor state (actors are rebuilt from the pack at every load, task 0177). A created actor would need handles in those tables, a body, a schedule
   and a save format of its own. `PlaceAtMe` refuses actor, leveled-list and unknown bases (a counted log line).
3. **Created objects are plain objects.** They get no scripts (Skyrim would attach the base's scripts: here OnInit never runs for them), no light
   component (a created lantern gives no light), no owner, lock or teleport; there is no `Delete` or `DeleteWhenAble`.
4. **`PlaceAtMe` near the player does nothing** (`Game.GetPlayer().PlaceAtMe(...)`): the pack does not know the player's position (the SDK has no
   player position call). It logs "has no position to place at". A small additive SDK call (or a placement relative to "the player" resolved by the
   engine) would serve it.
5. **`MoveTo` across cells is refused** when either cell is an interior (the engine keeps an object in its own cell store, because NWLD keys objects by
   cell and placement): a log line, nothing recorded. Between two exterior cells the object stays in its home store with a position outside it. A cell
   the player has not loaded: the move is baked into the cell when the pack builds it (`SkyrimCellSource::setMoveOverrideLookup`) and replayed after a load
   (`pushMoves`); `MoveToMyEditorLocation` for such an object returns it to the baked place, not the record's.
6. **Rotation:** `SetAngle` and a `MoveTo` with matched rotation send angles through the pack's `eulerToQuat`; the host knows only the angles scripts
   gave (the data's own are not kept), so matching the rotation of a target nobody turned does nothing. `GetAngleX/Y/Z` are not built.
7. **The fade** is the object shader's ordered dither driven through `MWRender::setTransparency` (no shader change); `kPapyrusFadeSeconds` = 1.0 is a
   named RECALLED-VERIFY constant (the game's own time is not known here). It works on objects that have the dither (`NORMAL` define, which the barrel
   has); an object without it would appear and vanish at the end. `Enable` and `Disable` do not block (the real ones wait for the fade). A save during a fade
   stores the target state (the object is enabled until the fade ends; the pack re-sends the disable after the load).
8. **Created ids and saves:** a created object saves its base description (kind, editor id, model path, bounds; strings cap 255, a longer one is refused
   at creation) instead of asking the pack at load, so a changed load order cannot lose it; the counter is saved explicitly. The cap is 65,536 created
   objects in a save. A cell that the engine unloads and loads again is rebuilt from its snapshot, in which a created object has its CREATION state: its later
   moves and fades would be lost (not tested; the demo stays in one loaded cell).
9. **A save with created objects is NWLD version 2 and the parent build refuses it.** Saves without them stay version 1.
10. A `PlaceAtMe` of a base the world view cannot describe (a quest, a form list, a missing id) returns None with a log line.
