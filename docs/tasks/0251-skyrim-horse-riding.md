# Task 0251: Skyrim horse riding, first slice

Design doc section: `docs/design/game-packs.md` (the Skyrim SE pack), decision 0017 (neutral actors), tasks 0216 and 0246
(the horse is a creature profile), 0222 (the player's body), 0245 and 0247 (first person).
Branch `riding`, from `e204c52fb1`.

Goal: in a Skyrim session the player can mount a horse he owns, ride it with the real controls (it walks, runs and turns at
its own speeds with its own clips), and get off beside it. Behind the pack setting `[extension.skyrimse] riding` (default
false; needs `creatures`, `player body` and the engine's `[Game] neutral actors` and `neutral player body`). No combat, no
saving and no first person while riding in this slice.

## 1. Survey (installed Skyrim SE data)

* **Horse skeleton** `actors/horse/character assets/skeleton.nif` has a `SaddleBone` node (`SkyrimRidingSurvey`, gated by
  `OPENEDEN_SKYRIMSE_DATA`, line `SKYRIMSE-RIDING-SADDLE`). Rest-pose world position composed from the file's node
  transforms (`NPC Root [Root]` -> `Horse_COM` (0, -33.1, 112.9) -> `HorseSpine1` -> `HorseSpine2` (0, 0, 25.5) ->
  `SaddleBone` (1.67, -26.5, 3.24)): **(1.67, -0.98, 134.92)** relative to the horse's origin (x right, y forward, z up).
* **Rider clips** are in `Skyrim - Animations.bsa` under `actors/character/animations/horse_rider/`: `idle`, `walkforward`,
  `trotforward`, `runforward`, `sprintforward`, `turncannedl/r90/180`, `turnloopingl/r`, `jumpstart`, `saddle_offset`, the
  `idle_head*` and `idle_tail*` variants and `patch/mc_*` weapon clips. The pack uses `idle`, `walkforward` and
  `runforward`. A clip name with a folder (`horse_rider/idle`) works through `HavokPack::clipHkxPath` (the name is only
  lower-cased), so the existing `SkyrimPose::Controller::setClip(clip, perSex=false)` plays it on the player's body.
* **Horse numbers** (task 0216 row): walk 137.624 units in 1.1 s = 125.1 units/s, run 212.32 in 0.466667 s = 455.0 units/s.
* **Placed horses near Whiterun's stables** (`@Tamriel,17900,-12300`, `OPENEDEN_NEUTRAL_ACTOR_TRACE`): `HorseForCarriageNew`
  (base 469512, at (17930, -12702)) and `EncHorseSaddledBlack` (base 429306). The scenario uses the first.
* **Controls.** The player's own script (`omw/input/playercontrols.lua`) writes `self.controls` (movement, side movement,
  run); the engine copies them into `LuaManager::ActorControls` (`MWBase::LuaManager::getActorControls`). **The default
  jump key is E (`MWInput::A_Jump`, `bindingsmanager.cpp`), Space is activate.** The jump is an edge in the Lua script and
  the stub's `mPosition[2]` is consumed before the physics step, so the hook also reads `actionIsActive(A_Jump)`.

## 2. Design

The smallest seam found: two new, optional SDK interfaces in `sdk/include/openeden/game/riding_rules.hpp` (0.1 DRAFT,
found by `dynamic_cast`, so the frozen `ActorRules` and `ActorRequestWriter` are untouched and the SDK stays 4.0):

* `RidingRules` (implemented by the pack's actor rules): `canMount(handle)` and `onMountRequested(handle)`.
* `RidingWriter` (implemented by the core's actor request writer, the one `update()` receives): `riderControls` (forward,
  turn, run, jump, the player's yaw), `carryPlayer(x, y, z, heading)` and `releasePlayer(x, y, z)`.

The horse itself is driven by the **existing** `ActorRequestWriter::setMovement` / `setHeading` of task 0135, so it walks and
runs with the neutral mover, the gait blend and the clips of the creature profile (no new movement code).

* **Mount.** The engine's `Neutral::activate` asks the physics system (`neutralCanMount`) whether the pack offers the
  horse; if so the action tells the pack (`requestNeutralMount`), which mounts in its next update. The same activation on the
  horse he sits on gets off. A horse is mountable when its appearance names `horseproject` (needs `creatures`) and it is
  owned: `[extension.skyrimse] horse test owned = <editor id>` or `*`. **Buying a horse at the stable is not built** (the
  stablemaster dialogue runs Papyrus fragments; no existing service sells a horse).
* **Ride.** Each update, after the package brain: the heading is the player's own yaw (mouse look) turned by the turn
  control at 1.7 rad/s (A and D turn the horse, they do not strafe; ours); forward is walk, forward with the run control is
  run, back or nothing stops it. The velocity is the horse's own speed along the heading. The player is carried to the
  saddle (the horse's position plus the measured saddle offset turned by the heading) every frame facing the heading.
* **Core side.** `NeutralActorBridge::Writer` is also the `RidingWriter` through `NeutralRidingHooks` (std::function
  hooks the world installs; empty hooks refuse). The world's carry hook switches the player's collision mode OFF (no
  gravity, no collision, as after `tcl`), zeroes the queued velocity, rotates and moves him (`World::rotateObject`,
  `moveObject`); the first carry also takes the camera out of first person. The release hook moves him 30 units above the
  place and switches collision back on, so the floor rules put him down.
* **Body.** `PlayerBodyState::ridingClip` (SDK `player_body.hpp` 0.3, a trailing field): the pack names the rider clip of the
  gait; `MWRender::PlayerBody` loops it on the player's body (`Scene::setNeutralObjectPose`) and holds the gait speed at 0.
  The body stands at the stub player's transform, which is the saddle.
* **Camera.** The third person camera follows the player (now on the saddle). The default distance (about 190 units) is too
  close for a horse: the scenario sets the Lua camera's base distance to 420. **No riding camera distance is set by the
  engine** (the distance belongs to the Lua camera script).

First person, what it would take: the arms rig needs the rider's `horsebehavior` first-person graph
(`actors/character/_1stperson/behaviors/horsebehavior.hkx` exists, with `magicmountedbehavior` and `shoutmountedbehavior`),
the camera node moved to the saddle, and the arms' idle clips for a mounted stance. About 2 to 3 days; not built (the first
carry forces third person).

## 3. Stage log (2026-11-16)

* **Tests first, red runs.** Pack (`SkyrimRiding*`, 25 tests over a fake core): against a stub `riding.cpp` **18 of 25 failed**
  (the 7 that passed test refusals and absence); all 25 pass with the code. Engine (`NeutralActorRiding*`, 5 tests on the
  real bridge): with the three riding calls forced to refuse **2 of 5 failed** (3 test refusals), all 5 pass with the code.
  The gated `SkyrimRidingSurvey` prints the saddle numbers. No existing test was edited.
* **In game** (`tools/ride_check/check.sh`, Whiterun stables, `@Tamriel,17900,-12300`, hour 13, fixed frame time, quiet
  window, `[Sound]` volumes 0, real keys W, D, A and E through `OPENEDEN_RMLUI_INPUT_SCRIPT`): the activation tooltip over the
  horse reads "Horse"; the player mounts (`RIDING mounted horse 636841`, rider 135 units above the horse), sits with the
  `horse_rider/idle` clip, walks at **125 units a second** (positions one second apart 17939, 18064, 18189, 18314) and runs at
  **about 455 units a second** (18439, 18889, 19346, 19799), turns with D and A, and **E gets off** (`RIDING dismounted by the
  jump control`: the player stands 110 units to the right of the horse). Frames viewed: before (horse with its tooltip),
  mounted, running, turning, dismounted beside the horse (`I:\Projects\OpenEden-work\bow6\ride\keep`).
* **Determinism.** Four runs of the scenario: the `RIDE`, `RIDING` and `PLAYERBODY riding` lines of three are byte-identical
  (md5 `30cf9b62...`); the first run (cold shader cache) differed by 3 units from the key presses (the keys are frame-numbered,
  the steps are simulation-time).
* **Morrowind unchanged:** the Balmora fixed frame (`--start Balmora --random-seed 7`, fixed frame time, frame 600,
  1280 x 720, base config) has the SHA-256 prefix `0d6dd39995b927fa`, the value recorded for the previous tasks with the
  same procedure. `tools/worldobjects_check` was NOT run (no parent build here). The changed paths are reached only through
  a neutral session: the activation branch needs `neutral actors`, the bridge's riding hooks are called by the pack only, the
  world hooks are never called without the pack, `PlayerBody` exists only with `neutral player body`.
* `components-tests` (`*Skyrim*:Extension*:Frozen*:*Creature*:*Settings*`) 2,606 pass; `openmw-tests`
  (`NeutralActor*:Neutral*:Extension*:PlayerBody*`) 787 pass.

## 4. Known limits

* The horse stops against slopes and obstacles (the neutral mover's own rules): in the stables area every straight run ends
  at a rise or a wall within 6 to 12 seconds.
* The body is at the stub's transform (the rest-pose saddle), not attached to the animated `SaddleBone`: the gait's bob is
  not followed. The rider's own clips run on top. There is no mount or dismount animation (an instant change).
* A load puts the player on his feet (nothing is saved); a door or a cell change while riding is not handled (the horse
  stays behind if the core removes it: the pack dismounts where it was last seen).
* The jump key (E) reads an edge poorly: the world hook ORs the Lua jump, the stub's jump setting and the C++ action.
* No combat while riding; the activate prompt of the horse he sits on is the horse's name.

## 5. Edits outside the new files

SDK: `sdk/include/openeden/game/player_body.hpp` (a trailing `ridingClip`, version 0.2 to 0.3; no pinned test). Engine:
`apps/openmw/mwphysics/neutralactorbridge.{hpp,cpp}` (the writer is also a `RidingWriter`), `physicssystem.{hpp,cpp}`
(`setNeutralRidingHooks`, `neutralCanMount`, `requestNeutralMount`, `setPlayerCollisionMode`), `apps/openmw/mwworld/worldimp.cpp`
(the three hooks and one include), `apps/openmw/mwclass/neutral.cpp` (`MountAction` and one activation branch),
`apps/openmw/mwrender/scene.{hpp,cpp}` (`setNeutralObjectPose`), `playerbody.{hpp,cpp}` (the riding clip). Pack:
`neutral_world.{hpp,cpp}` (the base class, forwarding, the horse lookup, `ridingClip`), `skyrimse_game_pack.cpp` (the settings),
`extension.json`, `CMakeLists.txt` (`src/riding.cpp` twice). Tests: `apps/components_tests/CMakeLists.txt`,
`apps/openmw_tests/CMakeLists.txt`. Tools: `tools/ride_check/`, `tools/testprofile/profile.cfg` and `README.md`. `CLAUDE.md`
(changelog row).

## 6. Decisions for toddhow

1. The seam: optional `RidingRules` / `RidingWriter` interfaces (above) rather than a new virtual on `ActorRequestWriter`.
2. A and D turn the horse (they do not strafe); the heading follows the view, which gives the mouse look the horse's turn.
3. Collision mode off holds the player at the saddle (as `tcl`); a stronger design makes the player a rider body with no
   capsule at all.
4. Buying a horse: the stablemaster's lines run Papyrus fragments that sell nothing today; `horse test owned` stands in.
5. The riding camera distance belongs to the Lua camera script: a built-in rule needs a small script with the pack's
   bindings (not built).
