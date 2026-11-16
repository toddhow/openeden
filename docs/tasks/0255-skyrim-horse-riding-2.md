# Task 0255: Skyrim horse riding, second slice

Continues `docs/tasks/0251-skyrim-horse-riding.md`. Branch `riding2`, from `1c0e94a8bd`. Everything stays behind the
pack setting `[extension.skyrimse] riding` (and the engine's `neutral actors`); nothing new is a setting.

Goal: fix the first slice's limits: the horse stopping on slopes and drops, the camera distance, the rider not
following the gait, doors and saves while riding, buying a horse, mount and dismount clips.

## 1. Findings

**Why the horse stopped (item 1).** Measured with a temporary diagnostic in `NeutralMover::step` (removed), riding the
Whiterun stables area in eight headings. Three different causes, one of them real terrain:

* **Drops of 50 to 60 units** (a bank or a terrace edge). The mover lets a capsule drop `kStepDown` = 24 in a move, the
  humanoid's number. The ground under the horse's centre was 50 to 60 below its resting rim: refused as an edge
  (`mEdge`). Four headings ended this way within 4 to 9 s.
* **Ground at 53 degrees.** `kMinFloorNormalZ` = 0.64 (50 degrees) refused the hill's road (`STEEP` in the diagnostic,
  ground normal z 0.60) and held the horse for good.
* **Real obstacles:** a 199 unit drop east of the stables (x = 20,440) and vertical rock faces and walls (normal z 0.03)
  higher than a step. These are real: a horse cannot ride off a cliff or over a wall either.

Fix: `NeutralMover::Limits` (step up, step down, steepest floor normal) with `limitsForHeight(capsuleHeight)`: the
humanoid's numbers up to the 128 unit reference height, above it step up = height x 24 / 128, step down = height / 2 and
a floor normal limit of 0.5 (60 degrees). The horse (capsule 169): up 31.7, down 84.5, 60 degrees. **They apply only to
the horse the player sits on**: `RidingRules::mountedHorse()` (SDK 0.2) names it once a frame and `walkNeutralActor` calls
`limitsFor(height, ridden)`; every other actor, bears, deer, mammoths and people included, keeps the humanoid's numbers
(`NeutralMover::limitsFor`). The numbers are OURS (estimates): the horse's legs are about half its shoulder height; a step
is a fifth of it. (An earlier version of this stage applied the limits to every actor of a capsule above 128; a review
caught that a tall NPC would have changed with riding off.)

**Ride length.** Before: every straight run ended in 4 to 12 s (east 4.4 s, the others 4 to 9 s). After, with a fixed
waypoint route along the road (`tools/ride_check`: the points of a navigator path from the stables over the hills and back,
steered by the script from simulation time; the forward control is the script's too), the horse rode **81 s of a run
(t = 7 to 81) at about 455 units/s: 76 of the 81 seconds moving, one stop at t = 19.5 and then a clean 51 s from t = 8 to
59, then 18 short unsticks while it turned round at the end of the road**. The horse's capsule (radius 32) is wider than the
navigator's agent, so it corners badly on narrow bends and at the dead end; the script unsticks it by trying a heading
beside the aim, as a rider would steer.

**Camera (item 2).** `core.skyrimse.riding` 1.0 (`RidingBindings`: `active()`, `camera_scale()`), a player script
`scripts/openeden/skyrimse/riding_player.lua` (`REQUIRES=core.skyrimse.riding>=1.0` in `builtin.omwscripts`) pulls the
third person base distance to `camera_scale()` = 2.2 times the distance he had on foot while he rides and restores it
after. In game: `RIDING camera distance 192 -> 422` at the mount; the frame after a dismount shows the distance back at
192. The test script no longer sets 420.

**Gait bob (item 3).** `PlayerBodyState::ridingMount` (the horse's placement id; SDK `player_body.hpp`, a trailing field,
version stays 0.3: a test pins 0.3). `PlayerBody::update` finds the horse's drawn object (`Scene::findNeutralObject`, a
walk over every cell) and its `SaddleBone` node path (`Scene::neutralObjectBonePath`) only when the mount or the scene's
objects changed (`Scene::changeCounter`), then each frame reads the bone's position from the cached nodes and draws the
body displaced by how far the bone has moved since the ride began (`riderBobOffset`, pure, capped at 60 units; the first
sample, with the horse idling, is the baseline). Only the DRAWN body moves: the stub player the rules and the camera read
stays at the pack's saddle point, so traces and the camera are unchanged. In game (`OPENEDEN_RIDING_TRACE=1` logs `RIDING
bob`) the bone's Z ranges over about 7.5 units across a gait cycle (134.0 to 141.5) with a smaller forward and sideways
part. (Not compared frame by frame against the rest-pose body.)

**Doors and loads (item 4).** `RidingRules::dismountNow(RidingWriter&)` (SDK `riding_rules.hpp` 0.2, a trailing virtual
with a default body; **the cell source forwards it to the rules, which a review caught missing: a unit test now stands
the real `SkyrimCellSource` behind the interface**). The teleport door action (`TeleportWithFollowersAction`) calls
`PhysicsSystem::dismountNeutralRider`, which calls `NeutralActorBridge::dismountRider`: the pack's `Riding::dismountNow`
releases the player beside the horse (the last place the ride saw) before the door takes him, and the horse's request is
cleared at the next update. The writer's riding calls are open outside `update()` only while that call runs
(`mSyncRiding`). **In game the door itself was not used**: Lua reports a neutral object with no type (`object.type` is
nil), so a door cannot be found by script, and no teleport door was near the stables route. What was run is the call the
door action makes, asked by the developer aid `OPENEDEN_RIDING_DISMOUNT_AFTER=<frames>` (900 frames into a ride):
`RIDING dismounted by a load door`, the player stands 110 units to the right of the horse and the horse stops (frame
`after_door_dismount_on_foot.png`). Unit tests cover the pack, the cell source forwarding and the bridge halves; the
activation branch in `neutral.cpp` is one call and was compiled, not exercised.
Save and load: a pack chunk `riding` v1 (`RidingChunk`: u8 mounted, u32 horse placement id, five bytes exactly).
`Riding::restoreState` ends a ride in progress (a load in a running process) and, for a state saved while riding, makes
the next update put the player on his feet under the saddle's place (`playerPosition` minus the saddle height, then
`releasePlayer`). The horse's own place is not saved (actors are not), so the horse stands where its placement puts it; the
player is where he rode to. Saving while riding works (`neutral saves`). In game (`PHASE=save` then `PHASE=load` of
`tools/ride_check`, a new process): saved at t = 24 s of a ride, the load logged `RIDING restored on foot after a load` and
the frame shows the player standing on the ground where he had been riding (`loaded_on_foot.png`).

**Buying a horse (item 5): not built; the finding is partial.** The stablemaster's line is the topic `StablesBuyHorseTopic`
(quest 0x68D73, 20 INFOs). None of those 20 INFOs has a fragment script (`SkyrimRidingStablesSurvey`, gated by
`OPENEDEN_SKYRIMSE_DATA`, prints `SKYRIMSE-STABLES` lines): their conditions are only fn_46, fn_566 (quest alias) and fn_71
(faction). The survey looked only at topics with "Horse" in the editor id; **it did not follow the INFOs' links to other
topics and did not read the quest's VMAD, so "the sale is in the stables quest's Papyrus" is a conclusion by elimination,
unverified**, and the estimate of 2 to 3 days is a guess. The test setting `horse test owned` stays the stand-in.

**Mount and dismount clips (item 6): in the data, not playable yet.** There are none in `horse_rider/`. The mount and
dismount are PAIRED animations: `meshes/actors/sharedkillmoves/human&horse/paired_mount.hkx` and `paired_dismount.hkx`
(the rider's side), `meshes/auxbones/tail/animations/horse_paired_mount.hkx` and `horse_paired_dismount.hkx` (the
horse's), and first-person `_1stperson/animations/paired_mount.hkx`. They live outside an actor's `animations/` folder
and move both actors together with root motion, so playing them needs the paired-animation machinery (synchronised start,
the rider's root motion applied relative to the horse). Not built.

## 2. Tests (first, run red against stubs)

| File | New tests | Red against stubs |
| --- | --- | --- |
| `apps/openmw_tests/mwphysics/testneutralmoverlimits.cpp` | 13 | 3 of 9 (first set), 2 of 12 (the floor normal limit), 1 of 13 (`limitsFor`) |
| `apps/components_tests/skyrimse/testskyrimseriding2.cpp` | 18 | 10 of 14 (the 4 that passed test refusals and absence), then 4 of 18 (cell source forwarding, `mountedHorse`, a load during a ride) |
| `apps/openmw_tests/mwphysics/testneutralactorriding2.cpp` | 4 | 4 of 4 |
| `apps/openmw_tests/mwrender/testriderbob.cpp` | 5 | 3 of 5 |
| `apps/components_tests/skyrimse/testskyrimseridingstables.cpp` | 1 (gated survey) | n/a (prints) |

All pass after the code. Wider runs on the final build: `components-tests`
`SkyrimRiding*:*Skyrim*:Extension*:Frozen*:*Creature*:*Settings*` 2,633 pass, `openmw-tests`
`NeutralMover*:NeutralActor*:Neutral*:Extension*:PlayerBody*:RiderBob*` 809 pass; `CI/check_tests_registered.sh`,
`check_extension_sdk.sh` and `check_layer_rules.sh` (104 exceptions) pass. No existing test was edited. The one existing
test that pins what a change here touches is `kPlayerBodyVersion` 0.3 (`testskyrimseplayerhands.cpp`): it is why the new
`PlayerBodyState` field did not bump the version; bumping it would need that line edited (toddhow's approval).

## 3. Edits outside the new files

Engine: `apps/openmw/mwphysics/neutralmover.{hpp,cpp}` (Limits, `limitsForHeight`, `limitsFor`, a `step` overload),
`physicssystem.{hpp,cpp}` (the ridden handle and limits in `walkNeutralActor`, `dismountNeutralRider`, the developer aid),
`neutralactorbridge.{hpp,cpp}` (`dismountRider`, `mSyncRiding`), `apps/openmw/mwclass/neutral.cpp` (one call in the door
action), `apps/openmw/mwrender/playerbody.{hpp,cpp}`, `scene.{hpp,cpp}` (`findNeutralObject`, `neutralObjectBonePath`),
the new `riderbob.hpp`. SDK: `riding_rules.hpp` (0.2, two trailing virtuals), `player_body.hpp` (a trailing field). Pack:
`riding.{hpp,cpp}`, `neutral_world.{hpp,cpp}` (two forwards, one field), `skyrimse_game_pack.cpp` (the chunk and bindings).
Scripts: `files/data/builtin.omwscripts`, `files/data/CMakeLists.txt`, `riding_player.lua`. Tests: both CMake lists. Tools:
`tools/ride_check` (waypoint routes, Lua-driven forward control, `PHASE=save|load`, a menu script).

## 4. Checks run (2026-11-16)

* **Ride** (`tools/ride_check`, Whiterun stables `@Tamriel,17900,-12300`, fixed frame time, quiet window, sound off, the
  waypoint route above): **three runs give byte-identical RIDE traces for the whole run** (101 lines, t = 0 to 81, md5
  `b098c9d8`, 19 stuck lines each; the ride ends at (27316, -11903)). With the earlier navigator-at-run-time autopilot and
  key-press forward control the runs differed (the key frames and the Lua timing moved the unsticks); both were replaced.
* **Frames** (`I:\Projects\OpenEden-work\bow6\ride2\keep`): `climb_hill_road.png` (riding up the hill road under the
  stockade), `road_river.png` (riding along the stream), `loaded_on_foot.png` (after a save at t = 24 s of a ride and a
  load in a new process: standing on the ground), `after_door_dismount_on_foot.png` (the dismount of the door call: on foot,
  camera back at its on-foot distance).
* **Morrowind unchanged**: the Balmora fixed frame (`--start Balmora --random-seed 7`, fixed frame time, frame 600,
  1280 x 720, the previous tasks' base config) has the SHA-256 prefix `0d6dd39995b927fa` on the final build, the value
  recorded for the first slice and the earlier tasks. `tools/worldobjects_check` was NOT run (`bow6/parent_bin` holds only an
  executable and a library of an unknown commit, not a build folder). The changed paths are reached only through a neutral
  session with riding on, except the limits path (now also gated: only the ridden horse) and the `Limits` overload.
* **Not run**: the Linux gate and GCC `-Werror` (traps checked by hand), clang-format 14, clang-tidy, the Lua tests, a load
  door in game, the horse purchase, a fresh review, a Skyrim fighter on a horse.

## 5. Decisions for toddhow

1. The horse's limits derive from its capsule height (step up a fifth, step down half, slope 60 degrees) and apply to the
   horse he sits on: tunable in `limitsForHeight`; a per-creature profile field is the alternative (three numbers per row)
   and would also let a free-roaming horse have them.
2. A drop of up to half the horse's height is taken in ONE move (the horse snaps down, the rider with it, up to 84 units
   in a frame). A fall under gravity is smoother but needs the mover to hand over to the fall state.
3. A load after riding puts the player on his feet where he was saved; the horse is NOT brought to him (its place is not
   saved). Alternatives: save the horse's place in the chunk and move it (needs a core call), or send it to him.
4. Buying a horse waits for a finished survey of the stables quest (see above).
5. Mount and dismount clips need paired animations.
6. The camera pull-back factor 2.2 is OURS.
