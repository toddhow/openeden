# Task 0289: Skyrim dungeon mechanics, first slice (Bleak Falls Barrow)

Design doc section: `docs/tasks/0288-skyrim-world-sweep.md` item 12 (traps, levers, puzzle doors and pillars load as statics).
Goal: the vanilla scripts of `BleakFallsBarrow01` and `BleakFallsBarrow02` run through the Papyrus host and do what they do in the real game; nothing is hand-written as a puzzle rule.
Inherited code touched: none under `apps/openmw/mwphysics`. Engine: `mwrender/skyrimsequence.*` (doors and lids, task 0232), `scene.cpp` (`makeNeutralSequenceObject`), `extensionhost.*` (object ops, task 0202). Pack: `papyrus_host.*`, `mining_service.*`, `skyrimse_game_pack.cpp`.
Settings: `[extension.skyrimse] dungeon mechanics` (off; needs `papyrus`, `inventory`, `[Game] neutral inventory`, and `[Game] neutral object animations` for the movement).

## 1. The survey (data, five masters; a Python walker of Skyrim.esm and Update.esm, nothing copied)

Scripts of the references of the two cells (REFR/ACHR with VMAD, XLKR or XTEL; `tools` scratch, aggregates only):

BleakFallsBarrow01 (70 such references):
- Pillar puzzle: three `NorDefaultPuzzlePillar01` (ACTI, `clutter/ruins/pillar/ruinspuzzlepillar01.nif`) with `defaultPuzzlePillarScript` (initial/solve states: A 1/2, B 3/2, C 1/3), each linked (XLKR, no keyword) to the lever `LeverPushAnimatingNoPlayer` (FURN, `furniture/clutter/leveranimating.nif`, scripts `defaultPillarPuzzleLever` pillarCount 3 and `TrapLever`). The lever's properties: `puzzleDoorActivator` 0x3e728 (an XMarker), `refActOnSuccess01` 0x3b9f1 (`NorPortcullisLarge01`, an ACTI with NO script, `dungeons/nordic/doors/animated/portcullislarge/portcullislarge01.nif`), `refActOnFailure01..04` the four `DartTrap` ports (`TrapDartPortSingle02`, each a chain of eight `TrapDartPortSingle01Child` through XLKR).
- Traps: 4 `DartTrap` (extends `TrapBase`), `TrapSwingingWall` (`TrapHitBase` base, wooden wall trap), `TrapOilPool`, `TrapTriggerBox` x1 (`TrapTriggerBase`).
- Ambushes: `masterAmbushScript` on 3 ACHR (draugr, each linked to a coffin marker `DragonPriestCoffinMarker` / creature alcove marker).
- Others: MineOreScript x2 (task 0224), defaultSetStageTRIG, defaultEnableEncLinkedRef x2, spider web scripts (MS13), WICommentTrigger, defaultAddMusicTrigger.

BleakFallsBarrow02:
- The Hall of Stories door: keyhole `PuzzleDoorKeyHole01` with `HallofStoriesKeyholeScript` (needs the golden claw `myMiscObject` 0x39647, sets quest MS13 stage 50 on success), three rings `PuzzleDoorSmallWheel02`, `MediumWheel02`, `LargeWheel02` with `HallofStoriesDiskScript` (initial/solve states 2/1, 1/3, 3/2), all linked to the keyhole; the door base 0x4e2b8 and dust FX 0x5256c.
- `WordWallTriggerScript` (task 0256), `TrapTriggerBox`, `masterAmbushScript` x2, `FXCameraAttachScript`.

Why these objects did nothing: they are ACTI/FURN statics to the engine. Their motion is NOT in the NIFs' NiControllerSequences: the NIF has a `NiTransformController` with no data per bone and a `BSBehaviorGraphExtraData` naming a Havok project. The clips are `.hkx` files named by `meshes/animationdata/<model>.txt` (pillar: behaviour graph, character, skeleton, three clips `ruinspillartrigger01..03`; ring: `largewheel01..03`, `largewheeldown`; portcullis: `open`, `close`; keyhole: `keyhole01`, `keyholedown`; lever: `leverpush`). The behaviour graph's events (strings of `behavior00.hkx`: `Trigger01..03`, `Turned01..03`, `StartState02/03`) are what the scripts call.

Natives the scripts call that the host did not answer: `PlayAnimation` (a no-op), `PlayAnimationAndWait` (not registered), `GetOpenState`, `SetOpen` (no-op), `SetNoFavorAllowed`, the default activation of an object with no script (`refActOnSuccess01.Activate(marker)`), `Weapon.Fire` (the darts; still unanswered), `OnCellAttach` (the dart traps fire only after it: unraised), `GetCurrentRealTime`, `countLinks`/`GetNthLinkedRef` (TrapBase's own custom functions: unchecked).

## 2. What was built

Pack (`game_packs/skyrimse`, behind `dungeon mechanics`):
- `PapyrusHost`: `ObjectReference.PlayAnimation` (to the hook `PapyrusWorldHooks::playObjectAnimation`), `PlayAnimationAndWait` (registered LATENT: the script parks for the seconds the hook answers, the clip's own length, which stands for the animation event it names), `GetOpenState` (1 open, 3 closed, kept for objects a script opened or closed), `SetOpen`, `SetNoFavorAllowed`, the default activation (`Activate` of a reference with no script plays `Open` or `Close`), `OnCellAttach` raised with `OnCellLoad`, `Weapon.Fire` (counted, logged once per port), `hasInstances`. `reg()` gained a `latent` argument.
- `skyrimse_game_pack.cpp`: `PapyrusDriver::pushAnimation` asks the host's `ObjectAnimationService` (found by `dynamic_cast` on the world source service) to queue the animation and for its length; the mining service's `mOther` hook runs a non-resource activator's or furniture's own `OnActivate` scripts (a standing stone is skipped: its own hook does it); the tooltip name of a scripted placement is its base's name or "Activate".
SDK/core: `openeden/game/object_animation.hpp` 0.1 (DRAFT, optional interface), `WorldSourceServiceImpl` implements it (`ObjectOp::Kind::Animate`, one queue with moves and fades); `WorldSourceService` stays 1.3 (its two pin tests are untouched).
Engine: `HavokPack::parseObjectProject` and friends (`components/havokpack/objectproject.*`); `mwrender/skyrimhavok.*` (clips to `NifScene::Sequence`s, with an Info line per sequence that says how far its first pose is from the node's rest pose: 0.05 units and 0 rotation for the pillar's `Trigger01`, 1.51 for `Trigger02` which starts a turn on); `SkyrimSequence` (`play`, `secondsOf`, `mHavok`, models with only named sequences, Activators and Furniture also get a model when `neutral object animations` is on); `ExtensionHost` applies `Animate` ops (an object not drawn yet is retried for 1,800 calls: the start states are asked while the cell is built) and lowers the body of a gate that plays `open`.

Decisions for toddhow:
1. The event-to-clip mapping is INFERRED from the clip file names (`trigger0N` for a trailing number, `open` for a stem ending in `down`, a start state holds its clip's first pose). The behaviour graph (`hkbStateMachine`, strings read only) is not decoded. It matches the pillar's three clips and the ring's three, and the data check: `trigger02`'s first pose is a quarter turn from the rest pose (rotation difference 1.51 in the matrix) and the frames show the right symbols (eagle, snake, whale in the order the script's numbers say).
2. A gate or door that plays `open` loses its collision at once, by lowering its body 4,000 units (not restored by a load or saved; the navmesh is not updated). A better rule needs the physics shape to follow the animation (task 0232 did that for doors).
3. `dungeon mechanics` creates the mining service even with `harvest` off (its resource hook then answers nothing): the activation path of placed activators and furniture is the mining service's.

## 3. Not built, ranked

1. **Dart traps do not fire.** The lever's wrong-answer path activates the four `DartTrap` ports and their child ports play `PlayAnim0N` (visible in the log), but `Weapon.Fire` is never reached: the cause was not found (`fireTrap` needs `isLoaded` from the now raised `OnCellAttach`, `CalculateEncounterLevel` and `WindupSound.play` answer tolerantly; `TrapBase`'s trigger cast was not traced). There is also no projectile or damage in the engine for a script's shot (the combat bridge's `launchProjectile` is the way, not wired).
2. **Draugr ambushes** (`masterAmbushScript`, 3 in BleakFallsBarrow01, 2 in 02): not run; they need the actor package scripts and the coffin and alcove markers' animations (`SnapClosed`, `FullPull` were asked and are not animations of the models).
3. Swinging blades (`TrapSwingingWall`, `TrapBladeSwinging`), pressure plates (`trapstonepressureplate`), the oil pool, `TrapTriggerBox`es: the scripts attach, no hit damage exists.
4. Levers and pull chains: the lever's own pull animation is not played (the project of `leveranimating.nif` names files that are not under the model's folder: `Skyrim Havok object ... is not animated: Resource 'meshes/furniture/clutter/characterassets/lever_character.hkx' not found`); `norlever01` and `norpullchain01` convert (3 parts) but no script of the two cells uses them.
5. The other dungeons of the sweep (Saarthal, Ustengrav, Folgunthur, Forelhost, Ysgramor's Tomb, Mzinchaleft): not run; they use the same pillar, door and ring scripts and variants (`dunKarthspirePillarPuzzle`, `dunSkuldafnPuzzlePillar*`, `dunHalldirPuzzlePillar`, `DLC2dunKolbjornPillarPuzzleLever`): each is a script class the host runs the same way, untested.
6. Saves: the host's open states, the sunk gates and the pillar poses are not saved (a load shows the start states).
7. Sarcophagus lids (`sarcophagus_topvert01` converts: `trigger01`), word wall glow, the door dust FX `FXDoorOpen`.

## 4. Tests

- `HavokObjectProject` (8, `apps/components_tests/havokpack/testobjectproject.cpp`), `SkyrimDungeonNatives` (8, `.../skyrimse/testskyrimsedungeon.cpp`), `SkyrimHavokSequence` and `SkyrimHavokInstance` (10, `apps/openmw_tests/mwrender/testskyrimhavok.cpp`): all pass. **They were written together with the code and not run red against stubs; the red counts are by mutation, see the stage log.**
- No existing test edited.

## 5. Stage log

- **Red counts (by mutation, not against stubs):** four one-line mutations at once (`eventNamesOfClip` without the `trigger0N` alias and `startStateClipEvent` empty; `Instance::play` returns false; the `PlayAnimation` natives never ask the hook; `sequenceFromSampler` without the last key) failed 4 of 16 `components-tests` (`HavokObjectProject` x2, `SkyrimDungeonNatives` x2) and 6 of the 24 `SkyrimHavok*` `openmw-tests` (the other 4 pass: they test other paths); all restored, 488 and 103 selected tests pass.
- **Game checks** (`tools/skydungeon_check/check.sh`, five masters, fixed frame time, quiet window, volumes 0; the plan starts 8 real seconds after the first update so that the pack's scripts are attached, then runs on simulation time): pillar scenario 3 runs, 78 trace lines each (DNG, DUNGEON, NANIM, `PAPYRUS animation`), md5 identical; claw scenario once with its final plan (an earlier plan with 4 s gaps turned a ring while it was busy: a ring turn takes 4 s). Real keys: `OPENEDEN_RMLUI_INPUT_SCRIPT` Space presses at a pillar logged `ACTKEY focus: "skyrimse:b698574" name 'Pillar' tooltip 1` and `DUNGEON activated 000AA8CC` four times in five presses. Frames kept in `I:\Projects\OpenEden-work\combat3\d1\keep` (pillars at the start, solved, the gate before and after; the Hall of Stories door before and after).
- **Morrowind:** `worldobjects_check` against the `n6saves` build (main `19d7dd9d38`, NOT an exact parent: no build of `c53b0003ae` was made) 110 objects OK; the fixed Balmora frame `1ae72bec04593e1b` for that build, this build and this build with `neutral object animations`, `neutral inventory` and `dungeon mechanics` on.
- **CI scripts run:** `check_extension_sdk`, `check_tests_registered`, `check_layer_rules` (104 exceptions), `check_cmake_format`, `check_no_osg`, `tools/testprofile/coverage.sh`: ok. **Not run:** the Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, a fresh review.
- **Faults found by running:** the first build failed on a `string_view` to `std::string` conversion; the harness started its plan during loading (acts before the scripts were attached: "act none" and a first ring turn lost); the first `act` hit the pillar's STATIC base at the same position (now filtered by record id); the ring base ids were miscomputed as decimal (329043 to 329045).
