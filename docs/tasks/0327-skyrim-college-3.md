# Task 0327: Skyrim, the College of Winterhold questline, third slice (follow-up of tasks 0292 and 0304)

Same hand wrote tests and code. Windows only. Not pushed. Local branch `college3-0327` (from `9852336cc7`, rebased onto main
`872713d15a`). Everything new sits behind the existing `[extension.skyrimse] college` (off by default; it needs `papyrus`,
`quest alias packages`, `actor packages`, `journal`, `[Game] neutral combat`, `neutral inventory` as in 0292 and 0304). No new
setting. The runs use a test aid at the start (`story test quest = 01F251:30`, MG01 stage 30 as 0304 reached it by the real chain)
and four hand-written harness steps, all named in section 3.

## 1. What runs now, through the game's own quests, scenes, dialogue and Papyrus

Scenario `tools/skycollege_check` (`env_f.sh`/`in_f.txt` of the work folder, 26,500 frames):

1. **Mirabelle.** The player is put in the Hall of Attainment (`WinterholdCollegeHallofAttainment`, a `cell` step). Her greeting
   offers "I was told to come see you." (`MG01MirabelleStage30BranchTopic`, TIF__00020029), then "I'd love to have a look
   around." and "Wonderful. Please follow me, and don't wander off." (TIF__0002002C). Real clicks on the box.
2. **Stage 40 and the tour.** After Goodbye the game's own `QF_MG01_0001F251.Fragment_15` runs and starts `MG01TourScene`
   (16 phases, 19 actions): Mirabelle says every line with her voice and subtitle, walks by the scene's seven packages
   (`MG01TourMirabelletoHallExteriorPackage` .. `MG01TourMirabelleHoldatClassroomExterior`), and each phase ends by the scene's own
   conditions (Mirabelle within 100 of a tour marker, the player within 300 of the markers 02030B, 020032, 02030D, 020313).
3. **Stage 50.** The scene's last line runs `TIF__00020308`, which sets MG01 to stage 50 (`Fragment_16`: objective 40 done, 50
   displayed, the tour scene stopped). Nothing here is hand-set.
4. **The classroom.** Entering `WinterholdCollegeHallofTheElements` fires the box trigger of `MGClassroomTriggerScript`
   (REFR 0x32D81 at (18, -1693, -23)); the game's script calls `MG01ClassroomScene.Start()`. The scene (12 phases, 39 actions)
   plays with Tolfdir, Onmund, Brelyna and J'zargo speaking; at phase 7 Tolfdir's package `MG01TolfdirClassroomGreetPlayer1` makes
   him open a forced greeting ("You've been quiet so far. What do you think we should do?", three choices). A click on "I think
   we should learn something practical." runs `TIF__00020A24`, Goodbye ends the phase.
5. **The ward lesson starts.** Phases 8 to 11 play ("In continuing with our theme of safety, we'll start with Wards"), Tolfdir's
   second forced greeting (package 0x731D3, "Are you at all familiar with ward spells?") runs on "I don't know
   any Ward spells." (`TIF__00021A6C` and `TIF__00021A6A`: the lesson's spell), and after Goodbye the classroom scene's own end
   fragment (`SF_MG01ClassroomScene_000206C5.Fragment_0`) starts `MG01WardScene` ("Over there, please. Just opposite me.").
   With the player at `MG01PlayerCastMarker` (-224, -1784, -46) phase 0 completes and phase 1 ("Now, cast the ward spell, and keep
   it up.") starts. **This is where it stops** (section 4).

## 2. What was missing and is built (all behind `college`)

Found by running; each cause was shown before the change.

1. **A scene's package toward another cell never made the actor wait.** Mirabelle's first tour package leads to a marker in the
   exterior while the player stands in the Hall. The exterior cells had been loaded earlier (the player began outside), so the
   view could still LOCATE the marker; `PackageBrain` therefore treated it as reachable, the actor walked at a point of another
   cell, was blocked and was placed at the exterior marker's coordinates INSIDE the Hall. Now a scene's NearReference target that
   `targetIsElsewhere` is not walked to even when it can be located (`PackageBrain::begin` and the location case): the actor waits and
   is listed as a traveller. `college` turns on `setCarryTravellers` (task 0277's mechanism): a Lua teleport of the player carries
   her through (`Neutral follower 115129 carried to ...`, the carry range is 1,500 units).
2. **A scene's "within N of the marker" for an actor read its placement.** `college` also sets the live actor place
   (`mWorldView.setLiveActorPlace`, as `companions` does) and the quest/scene condition world now gets the live places too
   (`SkyrimWorldView::setLiveActorPlace`, creation of the quest world).
3. **The player's distance in a scene.** `SkyrimConditionWorld::distanceTo` of the player returned "infinitely far" for any
   reference (a player has no placed row). With the player's position and place known and live places on, he is measured to a placed
   reference of his own interior cell, or, outdoors, to a placed reference at its world place; an indoor player is far from an
   exterior marker (interior coordinates are not the world's). `SkyrimWorldView::setPlayerPlace` now also tells the quest world
   (only with live places).
4. **Students and Tolfdir's classroom.** `college` turns on `host().setCellMoves(true)` (task 0286's mechanism, without its
   travel hook): stage 0's `MoveTo` of Onmund, Brelyna and J'zargo moves their real placements into the Hall of the Elements, so
   the classroom scene finds loaded speakers. **Side effect: with `college` on, every start-game quest's `MoveTo` that changes cell
   is now done by the world (about 20 in the log, other quests' actors too), where before it was recorded or made a copy near the
   player.**
5. **An explicit `Scene.Start` that cannot begin says why.** The one quiet refusal left in `SceneRunner::startImpl` (none of the
   scene's actors is loaded) is now logged with the alias references (`SCENE 000206C5 not started: none of its actors is loaded
   (alias references 0001C1A1 ...)`). A developer trace `OPENEDEN_SCENE_WAIT=1` logs every 300th wait of a scene phase with what
   blocks it (`SCENEWAIT <scene> phase 5 actions done conditions no`).

## 3. Hand-written or stood in (named)

* `story test quest = 01F251:30@20` gives stage 30 (0304 reached it by the real chain; the run here starts after it). Its fragment
  `Fragment_13` runs, so the gate and the faction are the game's.
* **`bring` (a new harness step in `tools/skycollege_check/luatest/scripts/mq/global.lua`): Tolfdir is teleported from the Hall of
  Attainment to the Hall of the Elements.** In the game his schedule (off-screen package travel) has him in the classroom; this
  pack does not simulate actors in unloaded cells, so without him the scene's Tolfdir lines wait and phase 7 never ends
  (shown: `SCENE 000206C5 line 1 waits for 0001C1A1`).
* `cell` and `back` steps move the player through the College's doors (the scenario does not walk, the approaches are cliffs and
  the doors were not walked through); a Lua teleport carries the traveller. `abs` puts the player on the ward lesson's cast marker.
* The clicks are real mouse events on the conversation box; their frame numbers are tuned to this machine's timing.

## 4. Not reached (ranked)

1. **The ward lesson beyond phase 0** (`MG01WardScene` phase 1 waits for `GetActorValue(WardPower) > 0` of the player; the quest
   script `MG01PlayerScript.OnWardHit`; Tolfdir's cast at the player, a package with a UseMagic procedure, runs as Stand).
   Needs: the player's ward power as a condition answer (the combat has `mWardPower`), the ward events, NPC "use magic" packages.
2. **The gate does not open in the world** (`TempGateRef.SetOpen(true)` of stage 30 is still a tolerant no-op), so nothing
   draws or blocks differently; Faralda's walk across the bridge was not re-run with the player following (0304).
3. Mirabelle and the tour start from a teleport, not from walking in from the bridge; the Hall's door is not used by the player.
4. Stage 200 (the lesson's end) and `MG02`.
5. Tolfdir's own off-screen walk to the classroom (section 3).

## 5. Tests

`SkyrimCollege3` (6, `apps/components_tests/skyrimse/testskyrimsecollege3.cpp`): a scene Travel target in another cell makes
the actor wait although it was located before; once the player's cell is the target's the actor walks to it; the player is measured to
a placed marker from where he stands outdoors, is infinitely far from an exterior marker while indoors, is measured to a reference
of his own interior, and keeps the old answer without his position.
Red against the code before: the wait test (1 of 2 failed; the second passes by nature: it pins the walk once the player is there) and the
two player distance tests (2 of 4; the other two pin the refusals). Not red-first: the glue (`college` turning on the carry, the live
places and the cell moves, `SkyrimWorldView::setPlayerPlace` forwarding, the quest world's live places, the scene log lines): no unit
test reaches them, they are shown by the runs. Green: `*Skyrim*:*Papyrus*:*Dialogue*:*Quest*:*Scene*:*Package*:*Travel*` 4,149 tests
(4,018 pass, 131 data-gated skips, 0 failed) and `openmw-tests` `*Neutral*:*Skyrim*:*Extension*` 1,162 (1,160 pass, 2 skips).
No existing test edited.

## 6. Decisions and notes for toddhow

1. `college` now turns on cell moves for every `MoveTo` (section 2.4) and the carry of travellers and live places (2.1, 2.2).
2. The 60 second scene package limit (`SceneRunner::Options::mPackageSeconds`) is unchanged; a tour phase that waits longer for
   the player would let Mirabelle fall back to her schedule (seen when the harness's door step came too late).
3. The scene explicit-start refusal line and `OPENEDEN_SCENE_WAIT` are logging only.
4. The player-distance rule (2.3) uses a placed reference's world place for an outdoor player: an interior marker's own coordinates
   would be compared with the world's if the player stood outdoors; no scene in the College does that.

## 7. Runs

Results of the three final runs, the Morrowind comparison and the frames are in the CLAUDE.md row and below (section 8).
