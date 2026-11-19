# Task 0328: the Way of the Voice, third slice (Einarth's teaching scene, stage 20, stage 70 and the push trial scene)

Branch `mq105c-0328` from `733daa4081` (task 0325), rebased onto local `main` at `e400a9f4ec` (task 0327). Windows only, not pushed. Behind the existing
`[extension.skyrimse] main quest whiterun` (off by default). Same hand wrote the tests and the code; the pure tests were written first and run red against stubs
(section 4); no fresh reviewer.

## 1. Findings (what the data said, and what the runs showed)

1. **What sets MQ105 stage 20.** Not a script of the quest and not a TIF fragment: the placed trigger `MQ105SetStage20` (REFR 0x649BC, base 0x33F50) with the
   script `defaultsetStageTrigSCRIPT` (prereq stage 5, stage 20). It is a BOX turned **a quarter about X and about Y** (rotation (pi/2, pi/2, 0), bounds
   375.6 x 326.0 x 1149.8 as half extents), centred at (-198, -2395, -853) in HighHrothgar; the two exterior doors (`HighHrothgarExteriorDoor1/2`) arrive at
   (-609, -2562, -992) and (272, -2575, -992), inside the box. The pack's box volume knew only a turn about Z (tasks 0274, 0292) and was only on with
   `main quest start`, `college`, `daedric`, ...: so nothing ever set stage 20 and an aid did.
2. **Why `EinarthTeachScene` "stalled in phase 4" (task 0324).** An artefact of the aid, not of the real flow: the aid set stage 50 before the scene had run, so
   phases 1 and 2 (start condition "stage 50 not done") were skipped, and the hold package that starts in phase 1 and ends in phase 4 (`MQ105ArngeirHoldCurrentPosition`,
   action 52) never started: an action still Waiting keeps the phase it ends in from completing for ever. In the real order (stage 20 by the trigger, stage 30 by
   Arngeir's talk, the shout hitting Einarth) the scene runs through phase 4. That fault is real for any scene whose start phase is skipped, and is fixed (section 3).
3. **The real stalls, found by running the whole scene in order** (each one a missing piece, in the order they came):
   - phase 7 (and the PushTrialScene's phase 1) wait for `GetInCell(HighHrothgar)` on the **Player alias** (alias id 51): the scene's condition world was never told
     where the player stands (only the dialogue and package worlds were);
   - phase 12 waits for Einarth's walk to his teaching marker (MQ105EinarthTeachWordMarker at (-272, -1064, -984)): the path found by the navigator is PARTIAL
     (it ends at (-898, -762, -896), the top of a stair down to -984: the navmesh does not hold the stairs), so the walk never arrived and the package timed
     out after 60 s;
   - phase 23 waits for `GetDistance(Einarth alias, Player alias) < 800`: the condition world used Einarth's placement (-1586, -579), not where he stands;
   - the PushTrialScene's phase 1 waits for `GetInSameCell` run on the Player alias with the target **alias 33** (flag "use aliases"): the function read the alias id as a
     reference id and never matched.
4. **Stage 70 and the word.** `MQ105Stage70` (REFR 0xC2D4E, a sphere of radius 241 at (-252, -1161, -977), prereq stage 60, the same script) sets stage 70 when the player
   walks in; the game's own `QF_MQ105.Fragment_25` then runs `Game.TeachWord(WordRo)`, `GoAway()` on the word wall and `dunDragonMoundQST.SetStage(30)`.

## 2. In game (harness `tools/skywhiterunmq_check`, work folder `I:/Projects/OpenEden-work/mq324`; the five masters, player level 10)

Order of one run (`story test quest = 02610C:90,0242BA:5,0242BA:30@62,0242BA:50@68,0242BA:55@72`; the player is put at the door arrival of High Hrothgar by a
`cell` step, then in the stage 70 sphere by an `abs` step; everything else is the game's):

- the arrival is inside `MQ105SetStage20`: **stage 15 (the location change), then stage 20 by the trigger** (no aid), `Fragment_96` starts `EinarthTeachScene` (phase 0);
  real clicks on Arngeir's first talk set stage 30 (runs a1, b1, c1), the real shout (`=`) at Einarth sets 25 and 50 (run b1: the push trial of task 0324);
- the scene then runs, with the aids standing in for the player's talk (30, 50, 55 at fixed script seconds): phases 1 to 26 with all of Arngeir's lines (voiced, subtitled),
  stages 56, 58, 59, 60, **stage 70 (trigger) -> `SHOUTS word learned: Ro` (the game's `TeachWord`)**, 75, 76 (`UnlockWord`, `AbsorbEffect`), the scene ends, **stage 80** starts
  `MQ105PushTrialScene`, which runs to its end in the same run (phases 1 to 18, three skipped by their conditions), stage 82 (`Fragment_134`);
- **not reached:** stage 85 and later (the player must equip Unrelenting Force and shout at the push trial's targets: no scripted input for it yet), so **Fus Ro Dah is NOT
  shouted**, no Borri, no horn, no Dah; the stage 70 step puts the player in the sphere by a teleport.
- Three parallel runs (`runs/s1`..`s3`, 34,000 frames each): the 128 lines of the SCENE, MQ105 stage, SHOUTS and scene fragment events (times removed) are identical, md5 `99fa75d6a610`
  (the build before the last fix, runs `r1`..`r3`, gave 94 identical lines, md5 `b424dcbb27a0`, and stopped in the PushTrialScene's phase 1).
- Frames kept in `I:/Projects/OpenEden-work/mq324/keep328` (viewed: `s4_teach_ro.png` Arngeir's line after stage 70, `s4_pushtrial.png` the push trial's instruction line, `c1_arngeir_talk.png` Arngeir after the trial; the player stands in the teaching room, whose floor is not drawn at the test spot).

## 3. What was built

1. **Box triggers turned about X and Y** (`triggers.cpp`: `TriggerVolume::mRotationX/Y`; the point is turned back by Rz(az) Ry(ay) Rx(ax), the inverse of the placement's
   Rx(-ax) Ry(-ay) Rz(-az) of task 0151; with no turn about X and Y it is exactly the old Z rotation) and **boxes accepted with `main quest whiterun`**
   (`PapyrusHost::enableWhiterunTriggers`, one more flag of the existing acceptance rule).
2. **`SceneRunner`: an action whose start phase was skipped.** When a phase is entered, a Waiting action whose span covers it (start < phase <= end) starts if it is a
   package (a state for the span) and is left out (done) if it is a line or a timer (part of the skipped phase). Before, it stayed Waiting and held its end phase.
3. **The scene's condition world knows the player's place** (`world_view.cpp`: set before each scene condition evaluation, only with `quest alias packages`, like the
   other player facts) so `GetInCell` on the Player alias works.
4. **Scene walks and live places are ONE mechanism for every pack feature that needs them.** Task 0277 (Companions), task 0327 (College) and this task each switched on
   "a scene's Travel package that cannot be walked ends with the actor at its destination" (`setCarryTravellers`) and "conditions read where an actor stands now"
   (`setLiveActorPlace`) with a copy of the same lines; they are now `enableSceneWalks()` (one private member of the pack class), called by the three. The quest world
   (the one scenes use) gets the live places at its creation and whenever the lookup changes (task 0327 made the same change in `world_view.cpp`; the merge kept theirs).
5. **`GetInSameCell` with the alias flag** (`ConditionWorld::inSameCellAlias`, `SkyrimConditionWorld::inSameCellAlias`, `conditions.cpp`): the parameter is an alias id, the
   target the reference that alias holds (the player, or a placed reference; the cells are compared as `inSameCell` does).

## 4. Tests

- New `testskyrimsemq328.cpp`: `SkyrimTriggerBox328` 3 (a box turned a quarter about X and Y puts its bounds on the other axes: hand worked in the file; Z alone unchanged; a
  non-finite turn is refused) and `SkyrimConditionSameCell328` 1. Two tests appended to the END of `testskyrimsescenes.cpp` (`SceneRunnerSkip328`: a package whose start phase
  was skipped starts in the first phase that runs; a line does not hold the phase). **No existing test line edited.**
- Red: `SkyrimTriggerBox328` failed 2 of 3 against the old box test (the third pins the Z-only case, which the old code also passes); `SceneRunnerSkip328` failed 2 of 2 against
  the old runner (the fixed `scene_runner.cpp` was put back afterwards); `SkyrimConditionSameCell328` failed 1 of 1 against a stub `inSameCellAlias` that always answers false.
  The glue (the trigger acceptance, the player place in the scene world, `enableSceneWalks`, the quest world's live places) has no unit test: the game runs are its evidence.
- `components-tests` filter `*Skyrim*:*Papyrus*:*Scene*:*Quest*:*Dialog*:*Condition*:CombatAi*:*Follower*:*Trigger*` 4,191 ran, 4,060 passed, 131 skipped by data gates, 0 failed (before the
  last test and fix; the new tests and `SceneRunner*` were rerun after: 32 of 32 pass); `openmw-tests` filter `*Neutral*:*Skyrim*:*Extension*` 1,162 ran, 1,160 passed, 2 skipped;
  `tools/testprofile/coverage.sh` ok (no new setting).

## 5. Checks

Morrowind unchanged against `I:/Projects/OpenEden-work/integ2/build` (not a build of the exact parent): `tools/worldobjects_check` 110 objects OK, the fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600) is `1c5d47ad0b5dc32d` for both.

## 6. Gaps, ranked

1. **Stage 85 onward** (equip Unrelenting Force, shout at the targets: `MQ105ShoutTargetScript` sets stages for the push trial's targets, Borri's sprint trial, the horn, Dah and the
   outro): needs a scripted real-input run and probably more natives; **Fus Ro Dah is not shouted**.
2. **High Hrothgar's stairs are not on the navmesh**: Einarth (and Arngeir toward the player) get a partial path ending at the top of a stair; the scene walk now snaps them to the
   destination (task 0277's mechanism), nobody walks there. The cause (the agent shape against the narrow stair, as in task 0159) was not examined.
3. The harness teleports the player (the `cell` step to the door arrival, `abs` steps into the teaching room and the stage 70 sphere: a teleport to (-420, -1250, -985) dropped him
   450 units, floor or collision missing there, not examined).
4. Lydia on another floor (task 0324 gap 2) was not looked at.

## 7. Decisions and edits for toddhow

1. Boxes now fire with `main quest whiterun` (as with `college`, `daedric`, `dawnguard`, `thieves`): any placed box trigger of the Whiterun-side quests becomes live.
2. A package action whose start phase was skipped STARTS late (a package) or is dropped (line, timer): OURS; the engine's own rule is RECALLED-VERIFY.
3. `enableSceneWalks()` unifies three places (Companions, College, Whiterun main quest); the College and Companions behaviour is unchanged.
4. Existing tests edited: none; two tests appended to `testskyrimsescenes.cpp` (toddhow may prefer them in their own file).

## 8. Stage log

- Edits outside new files: `triggers.{hpp,cpp}`, `conditions.{hpp,cpp}`, `dialogue.{hpp,cpp}` (one function), `scene_runner.cpp`, `world_view.cpp`, `skyrimse_game_pack.cpp`,
  `apps/components_tests/CMakeLists.txt`, `testskyrimsescenes.cpp` (appended).
- **Not run:** the Linux gate, GCC, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, the whole test binaries, a fresh review, stages after 82, the real
  door (the arrival is a teleport to the door's arrival point), a save in the middle of the scene.

## Merge note (2026-11-18)

Merged after task 0329, which added `useLiveActorPlaces()` for `companions`, `college` and `dragonborn`. `enableSceneWalks()` now calls it, so the live place is one hook. The College block calls `enableSceneWalks()` and so still carries travellers.
