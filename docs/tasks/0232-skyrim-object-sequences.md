# Task 0232: Skyrim doors swing and chests open

Design doc section: `docs/design/neutral-world-model.md` (neutral objects), task 0157 (`mwphysics/neutraldoorswing.hpp`: the
sinking door this replaces when it can), task 0135 stage S6c (found that door models animate in a NIF sequence).
Goal: a Skyrim SE door plays its model's `Open` and `Close` NiControllerSequence when a walking NPC or the player opens it, and
a chest or cupboard lifts its lid while its inventory window is open, behind `[Game] neutral object animations` (off).
Inherited code touched: none of the Morrowind paths; see "Edits outside the new files" below.
Branch `nifanim` from `daa8d117e5`, Windows only, local commits, not pushed.

## Survey (five official masters, `SkyrimSequencesReal`, aggregates only)

`apps/components_tests/nifscene/testsequencereal.cpp` (gated by `OPENEDEN_SKYRIMSE_DATA`; `OPENEDEN_SEQ_DUMP_MODEL=<path>` prints
one model).

- 22,091 NIFs read (1 fails in the inherited reader), **1,280 have controller sequences**; **249 have an `Open`, 248 of them also
  a `Close`** (the names are `Open` and `Close`, verified). Other names are effects and idles (`autoplay`, `idle`, `fade`, ...).
- Controlled-block interpolators in all sequences: `NiTransformInterpolator` 13,181, `NiFloatInterpolator` 29,861,
  `NiBoolInterpolator` 6,980, `NiBoolTimelineInterpolator` 7,478, `NiPoint3Interpolator` 2,586, `NiPathInterpolator` 45. **No
  B-spline interpolator occurs**, so none is read.
- Of the 249 `Open` sequences, **241 contain only transform interpolators** (the other 8 also have a float, bool or path block, which
  is counted and ignored); every track names a node of its file (0 missing). Channels in `Open`: rotation 239, scale 81,
  translation 56. **Rotation is stored as three Euler angle tracks (type 4, "XYZ"), 397 tracks against 13 with quaternion keys**;
  key interpolation is linear (1) or quadratic (2) with tangents. Durations 0.23 to 9.7 s (a door 0.6 to 0.83 s).
- By path words: door or gate 177, container-like 41, other 31. How far `Open` moves its parts at its end: doors turn under 10
  degrees 34, 10 to 45 degrees 52, 45 to 120 degrees 84, over 120 degrees 7 (162 of 177 also slide 5 units or more); other
  objects turn under 10 degrees 35, 10 to 45 22, 45 to 120 12, over 120 3, 59 of 72 slide 5 units or more.
- **Ordinary chests open very little in the file**: `chest01` lid 5 degrees (latch 15), `strongbox01` lid 10, `wardrobe01` doors 15
  (both), `dresser01` drawer slides 10 units, `wrdoor02` (a double door) +-83 degrees. The dump shows exactly these numbers; they are
  what the animation is, not a reading fault (a door's 1.45 rad confirms the angle units and the Euler order).

## Design

1. **Reading** (`components/nifscene`, formats layer). `Scene::mSequences` (`scene.hpp`: `Sequence`, `Track`, `KeyVec3`, `KeyQuat`,
   `KeyFloat`) is filled by `build.cpp` `buildSequences()` from every `Nif::NiControllerSequence` record (caps 64 sequences, 256
   tracks, 4,096 keys a channel). The existing `components/nif` reader already parses `NiControllerSequence`, `ControlledBlock`,
   `NiTransformInterpolator` and `NiKeyframeData`; nothing there changed. Only transform interpolators with a node name are read;
   anything else is counted in `Sequence::mUnsupportedBlocks`. Morrowind's reader is untouched (`SceneOptions` does not read Morrowind
   files; the 7,343-file hash tests and `NifMorrowindUnchanged` still pass).
2. **Maths** (`components/nifscene/sequence.*`, pure, no allocation in `sampleLocal`/`delta`): `find`, `duration`, `timeAt`,
   `sampleLocal` (linear, Hermite, constant, shortest-way quaternion blend, Euler as R = Rz Ry Rx, the NIF's "no value" marker and
   non-finite keys ignored), `animatedNodes`, `restWorld`, `animatedWorld`, `delta` (= animated world * inverse(rest world): a point
   of the node's baked shape in model space moved to where the sequence puts it; nested animated nodes work because the whole parent
   chain is used), `poseFor` (Open forward, or Close from its first pose when closing), `stepProgress`.
3. **Splitting the model** (`flatten.*`): `Limits::mAnimatedNodes` and `Shape::mAnimNode`. A shape under an animated node is never
   merged with another and says which node moves it; vertices stay baked at the rest pose.
4. **Drawing** (`apps/openmw/mwrender/skyrimsequence.*`). `Model` (cached per path, null when there is no usable `Open`): the still
   shapes and one flat model per moving part. `build()` makes ONE node per reference: the still shapes plus one `Anim::Transform` per
   part around that part's shapes (the cull nodes stay inside the moving transform, so culling follows the part). `Instance` is an
   auto-play `Anim::Controller` that writes each part's transform from `delta`. It sits in `MWAnim::Object::keepAlive`. Not
   the actors' bone path (`MWAnim::Play`, `SkyrimPose`): an actor's bones are a separate skeleton file, here the moving parts are
   rigid pieces of the model's own tree, so the delta in model space is both simpler and exactly what a collision shape would follow.
   Objects are per reference (a door's moving part must not be shared), not through VSG's per-path cache.
5. **Who moves it** (all main thread, all behind the setting):
   - a door: `PhysicsSystem::updateNeutralDoors` already steps open/hold/close from walkers (task 0157); an animated door
     (`addNeutralDoor(ptr, animated)`) now publishes its progress and direction (`NeutralDoorMove`), and `World::doPhysics` calls
     `RenderingInterface::setNeutralObjectOpen`. Collision: **it opens at half the swing** (`NeutralDoor::kCollisionOpenProgress`):
     the body and the navigator shape sink as before, only once, and the drawn door is not moved. The collision does NOT follow the
     leaf. Closing uses the Close sequence when there is one.
   - the player: activating a door that goes nowhere (`NeutralDoorOutcome::Inert`, which includes a locked door opened by its key)
     calls `PhysicsSystem::pokeNeutralDoor`: it counts as approached for 6 s (`kPokeSeconds`).
   - a container: `OpenInventoryAction` (kind `container`) calls `PhysicsSystem::openNeutralLid`; `World::doPhysics` sends
     `setNeutralObjectTarget(open)`, and closes it when the GUI mode has been seen and is gone (or after 5 s if it never showed;
     `NeutralDoor::lidShouldClose`). The lid moves by the sequence's own time.
   - Fallback: a model with no usable `Open`, or the setting off, keeps today's behaviour (the door sinks, the container has no lid).
6. **Settings**: `[Game] neutral object animations` (default false; `files/settings-default.cfg`, in `tools/testprofile/profile.cfg`
   with its README line). Developer aid `OPENEDEN_NEUTRAL_SEQUENCE_TRACE=1` logs `NSEQ lid ...`; a model that gets a sequence logs
   one Info line ("Skyrim sequence model ...: N moving part(s), Open takes S s").

## Stage log

Tests (written first, run red against stubs where the code could be stubbed; the rest are noted):

- `NifSequence*` (`components_tests`, 32 tests, `testsequence.cpp`, `testsequenceflatten.cpp`, generated NIFs from
  `seqbuilder.hpp`): the pure maths and the sequence maths were run red against a stub `sequence.cpp` first: **15 of 23 failed**
  (the 8 that passed were the 3 read tests, the 3 flatten tests, and two tests of absence). The read tests and flatten tests were
  written after their code (not red first). The Euler, `poseFor` and `stepProgress` tests (13) were written with their code, not run
  red.
- `SkyrimSequence*` (`openmw_tests`, 12, `testskyrimsequence.cpp`) and `NeutralDoorSequence` (6, `testneutraldoorsequence.cpp`):
  written with the code, not run red against stubs (the header functions are inline); instead each rule was mutation-checked by
  reasoning only, **not by running mutants**.
- With the data (`SkyrimSequencesReal`, gated): the survey above.
- Whole suites after the change: `openmw-tests` 2,887 pass (2 data-gated skips), `components-tests` without the Slang,
  complement, property and real-data groups 12,377 pass. `CI/check_tests_registered.sh`, `check_layer_rules.sh` (104 exceptions,
  unchanged), `check_game_conventions.sh`, `check_no_osg.sh`, `check_cmake_format.sh` and `tools/testprofile/coverage.sh` pass
  (`check_file_names.sh` lists two files that are not mine).
- **Morrowind unchanged:** `tools/worldobjects_check` against the integ2 build (not the exact parent): 111 lines equal, OK; the
  fixed Balmora frame (`--start Balmora --random-seed 7`, fixed frame time, frame 600) is `1c5d47ad0b5dc32d` with the setting off
  and with `neutral world` plus `neutral object animations` on, equal to the value recorded for the parent; the Morrowind NIF
  hash tests are untouched and pass. Nothing Morrowind reads changed: every new code path is behind the setting and a Neutral
  object.
- **Determinism:** three runs of `seqdoor` give identical `NACTDOOR` and model lines (md5 `0c557dd38c11`). Screenshots are NOT
  byte-identical (as for every scene in this engine; 2,000 to 12,000 of 921,600 pixels differ per frame, one mid-swing frame
  26,000 when the swing was a frame apart).

In game (`tools/neutral_session_check/check.sh ... seqdoor|seqwalk`, `tools/inventory_check/check.sh`; five Skyrim masters, the
Bannered Mare, fixed frame time, quiet window, sound off; 11 models of the cell get a sequence):

- **A door the player opens with the real key** (`seqdoor`, Space through `OPENEDEN_RMLUI_INPUT_SCRIPT`): `wrdoor02` (a double door)
  is closed, swings open (two frames mid-swing), stays open showing the room, and closes again 6 s after the press; frames viewed
  (`sq_d1_sheet.png`). `NACTDOOR` logs opens and closed.
- **An NPC walks through the upstairs door** (`seqwalk`, midnight, `actor packages`, `actor sleep snap = false`): the door swings
  with the NPC beside it (frames viewed, `sq_w2_sheet.png`); the log has `NACTDOOR` opens/closed for both doors.
- **A container with the inventory window open** (`wardrobe01`, 2560 x 1440 so the window leaves the object visible): the doors
  turn 15 degrees (the file's own angle) while the window is open (3,650 to 4,000 pixels differ from the closed frame outside the
  window, `sq_wd2_lid.png` shows the amplified difference), and the frame after the window closes is identical to the closed one (0
  pixels). A chest01 lid would turn 5 degrees: barely visible by design.

## Edits outside the new files

`components/nifscene/{scene.hpp,build.cpp,flatten.hpp,flatten.cpp}` (additive fields and one function), `components/CMakeLists.txt`,
`apps/openmw/CMakeLists.txt`, `apps/components_tests/CMakeLists.txt`, `apps/openmw_tests/CMakeLists.txt`,
`apps/openmw/mwrender/{scene.cpp,scene.hpp,renderinginterface.hpp}` (three defaulted virtuals at the end of `RenderingInterface`),
`apps/openmw/mwphysics/{neutraldoorswing.hpp,physicssystem.hpp,physicssystem.cpp}`, `apps/openmw/mwworld/worldimp.cpp`,
`apps/openmw/mwclass/neutral.cpp`, `components/settings/categories/game.hpp`, `files/settings-default.cfg`,
`tools/testprofile/{profile.cfg,README.md}`, `tools/neutral_session_check/check.sh` (+ `luatest/seqdoor.omwscripts`,
`seqwalk.omwscripts`, `scripts/seq/`), `tools/inventory_check/{check.sh,luatest/scripts/inv/place.lua}` (a `YAW_OFFSET`).
No existing test edited. No combat contract change.

## Not done / limits

- Collision does not follow the leaf (opens at half swing for everyone); a body in the doorway when it closes is not pushed.
- Only transform interpolators; no Euler `XYZ` with another axis order than Rz Ry Rx (RECALLED-VERIFY; the doors' pure Z turns
  and the survey's 397 three-axis tracks do not disambiguate it), no B-spline (none exists in the data), no NiFloat/NiBool tracks
  (sounds, effects, visibility).
- Only Door and Container kinds; furniture, activators and levers are not moved.
- Lid and door sounds are not played.
- Fixed number of parts (32); a model above it is not animated.
- Containers: a locked container opened with its key lifts its lid like any other; opening by Lua `activateBy` does the same only
  through the action.

## Decisions for toddhow

1. Collision at half the swing (rather than following the leaf, or only at the end) is a stand-in: say if a swung door should block
   until it is fully open or follow a swept shape.
2. Ordinary chests animate only 5 to 15 degrees in the vanilla files; do you want a minimum visible opening (a stand-in angle) for
   lids, or keep the file's own?
3. Whether the setting should be on by default once the Linux gate has run.
