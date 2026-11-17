# Task 0267: Skyrim paired animations, first slice (horse mount and dismount)

Design doc section: `docs/design/game-packs.md` (the Skyrim pack), task 0251 and 0255 (riding; 0255 section 1 item 6 found the clips and
left them), research note `docs/research/0054-provenance.md` (the Havok reader's rules: no outside Havok reader source, every
format fact recorded with how it was established; this task adds F54 to F57).
Branch `paired`, from `457bb898b5` (local `main`). Full loop in form only: the same hand wrote the tests and the code; the
tests of the new pure parts were written first and run red against stubs (counts below), the engine tests after the code (a
mutation check stands in for the red run, see section 4). No fresh reviewer.

Goal: a general paired-clip player (two actors, one clip file), used for the player mounting a horse and getting off it.

Inherited code touched: `components/havokpack/animation.{hpp,cpp}` (`Clip::mTrackNames`, one more field read by
`readClips`), `apps/openmw/mwrender/skyrimidle.{hpp,cpp}` (the loader gives a clip name that starts with `~` to the paired
loader), `apps/openmw/mwrender/{renderinginterface.hpp,scene.hpp,scene.cpp,playerbody.cpp}`,
`apps/openmw/mwphysics/{neutralactorbridge.hpp,neutralactorbridge.cpp,physicssystem.hpp,physicssystem.cpp}`,
`apps/openmw/mwworld/worldimp.cpp` (the riding hooks), `game_packs/skyrimse/{include/game_skyrimse/riding.hpp,src/riding.cpp,
src/neutral_world.cpp,src/skyrimse_game_pack.cpp,extension.json}`, `components/CMakeLists.txt`. New: `components/havokpack/paired.{hpp,cpp}`,
`sdk/include/openeden/game/paired_animation.hpp`.

Constraints: off by default (`[extension.skyrimse] paired animations`, needs `riding`); with it off riding is exactly task
0255's (the pack never asks the core about a paired clip). No virtual on `CombatRequestWriter`; the SDK change is one new
optional header found by `dynamic_cast` on the writer, `player_body.hpp` stays 0.3 (the body's clip name carries the paired
clip, no field added).

## 1. What a paired clip is (the survey)

Done with the opt-in tests `SkyrimPairedSurvey.*` (they print `PAIRED` lines; `OPENEDEN_PAIRED_ROOT` names the folder with the
files extracted by `bsatool`) and `HavokPairedRealData`. Everything below is DATA unless marked; the Havok rules of
`0054-provenance.md` held (no outside reader source; F54 to F57 are new rows there).

**The clips of a mount.** `meshes/actors/sharedkillmoves/human&horse/paired_mount.hkx` (27,584 bytes) and `paired_dismount.hkx`
(32,704) are ordinary packfiles with ONE `hkaSplineCompressedAnimation` (and one binding) each: 159 transform tracks, no float
tracks, 51 frames (1.66667 s) and 65 frames (2.13333 s). The binding's skeleton name is `PairedRoot`, a skeleton that is in
no file, and the track-to-bone map is empty. What names the tracks is the animation's annotation tracks (F54: an array at
+40, 24 bytes an entry, the name pointer first, one entry per track).
`meshes/actors/character/_1stperson/animations/paired_mount.hkx` is the first-person version (155 tracks, 4 float tracks).

**One file, two skeletons (F55).** Track 0 is `PairedRoot`. The second actor's tracks are named `2_<bone>` (`2_Horse_COM`,
`2_HorsePelvis`, ...): 57 of them, 51 are bones of `actors/horse/character assets/skeleton.hkx` once the prefix is cut; the
rest are `2_Horse` (the NIF's top node), `2_` (the horse's root, an empty name) and four `2_Horse...Platform` nodes. The first
actor's 101 tracks have no prefix: 99 are bones of the humanoid skeleton (`NPC Root [Root]`, `NPC COM [COM ]`, ...), the other two
are `NPC` (the NIF's top node, which the track list has as its own track) and `SaddleBone` (a horse bone, unprefixed).
Each actor plays the tracks whose names are its own bones and ignores the others, which is what the loader does:
`Paired::bindSide(clip, side)` gives a `BoundClip` whose `mTrackBones[i]` is the lower-case bone name for the tracks of that side
and `""` for the rest; the existing pose controller already skips a track whose name is not one of its bones.

**The frame the clips are in (F56).** `PairedRoot` and the horse's `2_` root track are the identity for the whole clip (the horse
does not move), so the clips are in the HORSE's frame (x to its right, y forward, z up). The first actor's root is the composition
of his `NPC` track and his `NPC Root [Root]` track. Mount: from (-63.479, 0, 0) turned -90 degrees about z (a quarter turn
clockwise: at the horse's LEFT flank, facing it) to (1.678, 4.136, 130.422) turned 0 (on the saddle, facing forward). Dismount: from
(1.674, 4.417, 130.440) turned 0 to (-111.604, 10.089, 0.223) turned 0 (112 units to the left of the horse, on the ground). The
saddle measured in task 0251 is (1.67, -0.98, 134.92): the mount ends 0.01 away in x, 5.1 in y and 4.5 in z, which is the proof
of the frame. (My first reading, from the root track alone, was that the clips are relative to the rider's start and the rider
ends 66 units in front of the saddle turned 90 degrees: the survey printed `NPC Root [Root]` and not the `NPC` track above it.)
`Paired::describe(clip)` composes the two and gives the start pose, the end pose and the duration.

**The "horse's side under `auxbones/tail`" of task 0255 was wrong (F57).** `auxbones/tail/animations/horse_paired_mount.hkx` and
`horse_paired_dismount.hkx` (2,880 and 3,040 bytes) animate five tail bones (`TailExport01` to `05`, the tail skeleton
`auxbones/tail/character assets/tailskeleton.hkx`, 5 bones): the tail of a beast-race rider. The horse's side is in the rider's file.

**The rider's idle at the saddle.** `horse_rider/idle.hkx` (the clip the riding slice loops) has its `NPC COM [COM ]` and
`NPC Pelvis [Pelv]` at (0, 0, 0): the rider's root is the hips, which is what the mount clip ends with (COM at 0), so the loop and
the mount clip meet; the existing carry point (the saddle, 134.92 up) differs from the mount's last pose by (0, 5.1, -4.5) units,
a step of 7 units when the clip hands over to the loop (decision 2).

## 2. Kill moves: what they need (not built)

Surveyed on two files only (`human&bear/paired_1hmkillmovebeara.hkx`, `human&draugr/paired_1hmkillmovedraugra.hkx`; the list of
the archive has about 60 more in `sharedkillmoves/<victim>/` and as many first-person ones under `sharedkillmoves/1stperson/`):

* Same container, same naming: `PairedRoot` + `2_` tracks + the first actor's (177 tracks: 99 + 77 + 1 for the bear, 231: 100 +
  130 + 1 for the draugr, 2 s and 4 s). The `2_` names are the VICTIM's skeleton bones, so a clip is bound to the victim's own
  skeleton by name (the bear's `2_NPC Root [Root]`, `2_NPC Pelvis`, ... here; a creature skeleton the pack does not draw yet needs
  its profile row first).
* **The frame is not the second actor's.** `2_` (the victim's node) is at (0, 156.98, 0) for the whole bear clip and at
  (5.27, 115.33, 0.83) for the draugr: the victim stands 157 or 115 units in FRONT of `PairedRoot`, and the draugr's root
  (`2_NPC Root [Root]`) walks to (-4.1, -200.7, -3.2) over the clip, while the first actor's own root moves little. So for a
  kill move `PairedRoot` is the killer's frame (the mount's was the horse's), and a general player has to take the anchor from
  the clip: place the killer at `anchor`, the victim at `anchor` composed with the clip's `2_` track, or the victim where he stands
  and the anchor from his place and the inverse of that track. The mount happens to need no such step because the horse's
  track is the identity.
* What the pack must add: the choice (weapon type, victim race, a low-health victim, the game's own conditions: they are in the
  behaviour graphs, not in the data this repository reads; the combat has no rule to start one), both actors out of the AI and the
  combat while it plays (a `PairedWriter` call to take and give back an actor), the victim ends dead (the death and ragdoll hand-over:
  task 0174's ragdoll starts from the pose at the end; a one-frame step if the last pose is held until the ragdoll takes over),
  invulnerability while it plays, the camera (the `meshes/cameras/killcam_*.nif` files, a camera animation), and the first-person
  variants (a different clip with the arms only, the body is not drawn).
* The loader of this task already binds any `~1/` or `~2/` clip and reports `describe()`; what is new is the anchor rule and the
  victim's skeleton name map.

## 3. Design

**Library** (`components/havokpack/paired.*`, formats layer, standard library only): `classifyTrack` (the naming rule),
`bindSide`, `describe`, and the clip-name convention `~1/<path under meshes>` / `~2/<path>` (`clipName`, `parseClipName`; no `..`,
no drive letter, no leading slash, at most 240 bytes). `readClips` keeps the annotation names in `Clip::mTrackNames`.

**SDK** (`sdk/include/openeden/game/paired_animation.hpp`, DRAFT 0.1, additive): `PairedWriter`, implemented by the core's writer and found by
`dynamic_cast` (as `RidingWriter` is): `pairedClipInfo(clipName, info)` (NotReady / Ready / Failed, never blocks; the first ask starts
the read on a worker) and `playPairedClip(actor, clipName)` (once, the last pose held; an empty name gives the actor its own
animation back). The player's own side is the body's clip name (`PlayerBodyState::ridingClip` starts with `~`: the player body plays
it once instead of looping it).

**Pack** (`game_packs/skyrimse/src/riding.cpp`; pure rules `horseFramePoint`, `headingForLeftFlank`): with `paired animations`
on, a mount asks for the mount clip; Ready: the horse stops, is turned to have the player on its left (the clip starts there),
plays its side, the player's body is carried to the horse's origin with the horse's heading (the clip is in that frame) and plays his
side, the jump control does nothing for the clip's length; then the horse's clip is cleared and the ride is task 0255's. The dismount
asks for the dismount clip (it was asked for at the mount, so it is read by then): Ready: the horse stops and plays its side, the
body is at the horse's origin for the clip's length, then the player is released at the end pose turned into the world and the
horse's brain takes over again. NotReady waits up to 3 s (OURS) and then does it instantly, Failed does it instantly at once, and
a core without the interface is never asked. A load door, a load and a lost horse end it and give the horse its animation back.

**Engine**: `SkyrimIdle::loadPairedAsync` (the worker load, cached by file and side), `pairedClipInfo`; the hooks of the world's
riding struct (`mPairedInfo`, `mPairedPlay`); `PhysicsSystem::queueNeutralPairedClip` (the horse's clip goes through the clip ops the
combat uses); `Scene::playNeutralObjectPaired` (the player's body, no weapon drawn).

## 4. Tests

| File | New tests | Red against stubs |
| --- | --- | --- |
| `apps/components_tests/havokpack/testpaired.cpp` | 19 (18 + the gated real-data one) | 12 of 19 (the three read tests were written after the reader and pass on the real code only; the others failed against the stub: classify, name, bind, describe, real data) |
| `apps/components_tests/skyrimse/testskyrimseriding3.cpp` | 19 | 11 of 19 (the 8 that passed test the setting off, a core without the interface, the fallback of a failed clip and the maths of an unreachable fallback) |
| `apps/openmw_tests/mwphysics/testneutralactorpaired.cpp` | 6 | written after the code (mutations below) |
| `apps/openmw_tests/mwrender/testskyrimpaired.cpp` | 5 | written after the code (mutations below) |
| `apps/components_tests/havokpack/testpairedsurvey.cpp` | 4 survey tests (gated, print) | n/a |

Mutation checks (one-line mutations, each failed named tests): the library: the `2_` prefix test (4 tests), the order of the
`NPC` and root composition (2), the sign of the yaw (3), the shared-root name (3); the pack: skipping the horse's clip clear
(7), the wait comparison (1), the flank heading sign (2), the body offset (2), the release point's y (1); the engine:
the key handed to the play hook (1), the writer closed outside `update()` (1), not giving the bones back when a clip ends
(1), not recording the driven bones (3), binding the wrong side in the loader (1). One engine mutation I first wrote
changed nothing (a `sed` range that ended at the function signature) and was redone by line number.

Wider runs on the final build: `components-tests` `-*Slang*:*Complement*:*Property*` 13,661 tests, 13,522 pass, 139 skipped (gated by data or built libraries), 0 fail; `openmw-tests` whole 3,110 tests, 3,108 pass, 2 skipped, 0 fail; the filtered `Havok*:*Skyrim*:Extension*:Frozen*:*Creature*:*Settings*:*Neutral*:Sdk*` 3,242 (3,133 pass, the rest skipped). `CI/check_extension_sdk.sh`, `check_tests_registered.sh`, `check_layer_rules.sh` (104 exceptions, none added), `check_game_conventions.sh`, `check_cmake_format.sh`, `check_no_osg.sh` and `tools/testprofile/coverage.sh` pass (`check_file_names.sh` lists two files of other work, as on `main`).

## 5. In game

`tools/ride_check/check.sh` with a new driver `I:\Projects\OpenEden-work\bow6\paired\run.sh` (not committed: the folder holds
generated runs; the scenario is the existing plan with `paired animations = true`): the Whiterun plains start `@Tamriel,17900,-12300`,
the test horse `skyrimse:b469512`, the player put 170 units west of it facing it, the activate step at 5 s, the jump
control (E, a synthetic SDL key through `OPENEDEN_RMLUI_INPUT_SCRIPT`) at frame 900, fixed frame time, sound off. Frames viewed
in `I:\Projects\OpenEden-work\bow6\paired\runs\p2\user\screenshots` (mount) and `p3` (dismount; `dismount_sheet.png` is a
crop of the six):

* **Mount** (`m1` to `m5`): the player's body stands at the horse's left flank and climbs: a hand on the horse, the leg swung over,
  seated on the saddle facing forward, from the third person camera behind. The log: `RIDING mounted horse 636841`, `RIDING paired
  mount, 1.666667 s`, `PLAYERBODY riding clip '' -> '~1/actors/sharedkillmoves/human&horse/paired_mount'`, 1.67 s later `RIDING
  paired mount finished` and `PLAYERBODY riding clip '~1/...paired_mount' -> 'horse_rider/idle'`; the player is at the saddle
  point (horse z + 135) and rides as in task 0255.
* **Dismount** (`d1` to `d6`): the jump control starts `RIDING paired dismount, 2.133333 s`, the body swings the leg over the horse,
  steps down to the left (the horse's head turns toward him in the frames: its own side plays), and after 2.13 s the player is
  released at the clip's end: the log's player position moves from (17931, -12703, saddle) to (17823, -12669, ground), 112 units
  to the horse's left (the clip's -111.6, 10.1 turned by the horse's heading), standing, the camera back at 192.
* **Three runs** (`r1` to `r3`): the `RIDING`, `PLAYERBODY` and `RIDE t=` lines (positions of the player and the horse at every second)
  are identical in all three once the order of the two worker-thread "decoded" lines is left out (md5 `e758f6ecd697`, 42 lines
  each, before the filter the three differ only in that order); a fourth run on the final binaries differs in ONE line: the
  `RIDE t=17.0` sample came after the release instead of before it (the injected key and the Lua clock are a frame apart in some
  runs: the harness, not the clips), everything else equal. The frames are not compared pixel for pixel.
* **Off** (`paired animations = false`): `RIDING mounted horse` and `PLAYERBODY riding clip '' -> 'horse_rider/idle'` at once, no
  `Skyrim paired clip` line (no clip is read), the dismount instant, as task 0255.
* **A first run found two faults, fixed**: (1) the player stub at the horse's origin put the third person camera inside the horse
  (the camera's ray to the player starts in the horse's collision): the player now stays on the saddle during the clip and the body
  is drawn at the origin by `PlayerBodyState::bodyOffsetX/Y/Z` (a trailing field, the version stays 0.3); (2) the `NPC` track of the
  clip is written by no other clip, so after the mount the body stood 60 units left of the horse: the pose controller now keeps the
  values of the bones a paired clip drove and gives them back (`SkyrimPose::Controller::captureDriven/restoreDriven`, only for clips
  that have track names).

Morrowind unchanged: `tools/worldobjects_check` (a copy `wochk.sh` of it that starts the new executable from the build folder and a
parent executable beside it) printed 111 result lines for the new build and they are byte-identical to the recorded `reference`
run of an earlier agent's build (`bow6/fc/wo/reference/result.txt`); **the parent executable itself would not start with this
build's `defaults.cfg`** ("Default setting [Game] neutral image space effects is not initialized"), so the reference is not a build
of `457bb898b5`. The fixed Balmora frame (`--skip-menu --start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`,
`--auto-screenshot 600`, 1280 x 720) has SHA-256 prefix `0d6dd39995b927fa` in two runs of the new build, the value recorded for this
base config in the rows of tasks 0207, 0219 and 0215 for the reference, the new build and the new build with settings on.

## 6. Decisions for toddhow, limits, not run

1. **The body is drawn away from the player during the clip.** The clips are in the horse's frame, so the body is drawn at the horse's
   origin and the player (camera, collision off) stays on the saddle; the alternative (the player at the horse's origin) puts the
   camera in the horse. A trailing `PlayerBodyState` field carries the offset.
2. **A small step at the hand-over.** The mount clip ends 7 units from where the riding loop carries the body (0, 5.1, -4.5), and the
   dismount starts from the loop's pose by the same step. Visible as a small shift in the frames; matching them means moving the
   riding saddle point by that amount, which also changes task 0255's frames (not done).
3. **The player does not walk to the flank.** The game walks the player to the mounting spot; here the player is put at the clip's
   start (the horse's left flank) at once (a pop of up to the activation distance) and the horse is turned to have him on its left
   at once. A walk to the spot is a navigation request for the player and was not built.
4. **The clip's time is the pack's dt, not the renderer's clock.** The pack ends the phase after the clip's duration of its own
   updates, the body holds the last pose (`playOnce`) until then; fixed-time runs are identical, a play with a very uneven frame time
   can show the last pose for a frame or end a frame early.
5. **Left flank only, third person only, the test horse only** (riding is not saved: task 0255). The first-person clip (155 tracks, 4
   float tracks) is not used because riding forces third person. The dragon's own mount clips (`human&dragon/paired_dragonmount`,
   `paired_dragoncallmount`) have the same container and would be the next riders of this loader.
6. **The behaviour of the shared pose controller changed for paired clips only:** it keeps and restores the bones a paired clip drove.
   No other clip adds to the list (a test and the unchanged ride show it), but the file is `skyrimpose.*`, used by every neutral
   actor.
7. **The engine tests were written after the code** (mutation checks above stand in for the red run); the pure parts and the
   pack's rules were red first (12 of 19, 11 of 19). Existing tests edited: none.
8. **Not run:** the Linux gate and GCC `-Werror` (no container; the traps were checked by hand: no `Options = {}`, braces around gtest
   macros, `(void)` on discarded results, all aggregate members given), clang-format 14 (not installed; lines wrapped by hand to 120),
   clang-tidy, the Lua tests, the whole test binaries (filtered runs above), a fresh review, first person, saving during a clip, a horse
   that is not the test horse, an NPC rider, the horse's own mount sounds, a cell unload during the clip.

