# Task 0341: Skyrim side quest "In My Time of Need" (MS08)

Status: BUILT for the kill branch (end to end to stage 200) and PARTLY for the betrayal branch (to the stable ambush); Windows only, local
branch `mytime-0341` rebased onto `979bbc98db`, not pushed. Behind the new pack setting `[extension.skyrimse] side quests` (off, declared in
`extension.json`, README line in `tools/testprofile`; needs `papyrus`, `quest alias packages`, `actor packages`).
Follow-up of task 0187 (which reached stage 25 from Saadia's "Any suggestions as to how I find them?"), with tasks 0204 (crime, jail), 0258
(guard dialogue), 0271 and 0298 (forced greetings, scenes, package order), 0277 (carried travellers), 0286 (cell moves), 0326 (dead lookup).
Same hand wrote tests and code; the new unit tests were written before the code and run red against stubs (counts below); no fresh reviewer.

## What runs (the game's own data, dialogue, scenes and Papyrus)

Kill branch, `tools/skymytime_check`:
- **leg1** (Bannered Mare): Saadia's real talk (clicks) sets stage 20 (`TIF__000215B1`); she walks to her private room
  (`MS08SaadiaConfidePlayer`, marker `MS08SaadiaConversationMarker` at (-1376, -490, 318)); a second talk there: "Tell me what's going on."
  ... "Any suggestions as to how I find them?" (`TIF__0006A050`) sets stage 25 (`QF_MS08` Fragment_0: the Alik'r prisoner is enabled).
- **leg2** (Dragonsreach basement): the prisoner's lines (FineVar 1, `TIF__000215C1`), the jail guard's "I want to pay that prisoner's fine."
  (`TIF__000D7880`: 100 gold), the prisoner again ("Let's talk about how we can help each other." ... Swindler's Den): `TIF__000215C2` sets
  stage 100 (Fragment_11: the prisoner scene quest starts, the Den is put on the map).
- **leg3** (Swindler's Den): Kematu's topics; killed by the aid, `MS08KematuScript.OnDeath` (no betrayal) sets stage 160 (Fragment_19); in the
  Mare Saadia offers "The Alik'r won't trouble you anymore." (it needs `GetDead(Kematu)`, who died in another cell) and her reward line
  (`TIF__00022E7C`) sets stage 200 (Fragment_24: 500 gold, JobInnServer, the quest stops). THE QUEST ENDS.

Betrayal branch (leg3, leg4): Kematu's "So, what do you want me to do?" ... "All right, I'll do it." (`TIF__00022E84`) sets stage 175
(Fragment_21); Saadia's "(Lie)" lines (`TIF__00057FA1`) make her follow the player by `MS08SaadiaFollowPlayer`; she is carried out of the
inn to the stables; **the stable box (`MS08AmbushTriggerAlias`) hears HER enter** and `MS08AmbushScene` begins ("What is this?", "What have you
done?"), then "Saadia Captured". NOT REACHED: Kematu's own lines (his reference is moved to the ambush marker by the game's script, but when the
Den was never loaded in that run the moved actor does not appear: the scene waits for him and ends), Kematu's stage 175 follow-up topics and
stage 201 (`Fragment_26`). A run that first loaded the Den showed the same conversation clicks drifting (radiant topics shift the buttons), so
it was not finished.

## Faults found by running it and fixed (all behind `side quests`)

1. **Saadia never went to the private room.** She is the unique-actor alias of MS08 (MS08's packages come from that alias) and also the find-matching
   "Server1" of the radiant tavern quest WITavern (priority 40, a forced fill). `packagesForReference` put the forced/specific-reference groups in
   front of the unique-actor packages, so WITavern's `WITavernServerSandbox` always won. `orderWithUniqueActorGroups` (pure, in
   `quest_priority.hpp`) puts every group in ONE order by quest priority; `SkyrimWorldView::uniqueActorAliasGroups` is the helper (task 0298's
   order inside it is unchanged).
2. **The walk to the private room cannot be done** (the west wing's stairs: the mover stops at a ledge, "blocked edge no-floor", the old S6e
   problem). A MUST COMPLETE (PKDT 0x4) Travel package that ends Blocked or NoPath now ends with the actor placed at its target
   (`PackageBrain::setSnapMustComplete`, the task 0277 rule extended from scene packages to quest alias packages). **A stand-in, named.**
3. Her private-room topics read `GetDistance(MS08SaadiaConversationMarker)` of HER live place: live places are on with the setting.
4. **Kematu stayed disabled**: he is an enable child (XESP) of the marker that `QF_MS08` Fragment_0 enables. The enable-children rule was on only
   for the main quest and Dawnguard; now also with the setting. Box triggers are accepted too.
5. **`GetDead` run on another reference** (Saadia's line asks `GetDead` of Kematu) read only the placed record's flag. It now also asks the
   session's kill lookup (task 0326's; the same one-line fix was made by task 0337 and merged: ONE mechanism, the lookup is wired for `civil war`
   and `side quests`).
6. **A follower by quest package is not a box trigger subject**: `MS08AmbushTriggerScript` is a script of the trigger's alias and waits for
   Saadia herself to enter. The followers (`SkyrimActorTable::followers()`) are subjects of the trigger tracker like the cart horses
   (`applyFollowerTriggers`), at their brain place.
7. `Follow` packages that name the player count for Saadia (`setFollowPlayerPackages`, task 0310's).

Diagnostics added by the task or by the merge: a refused `SetStage` says why (0338's wording kept); `tools/skymytime_check/luatest` has a
`ext` step (a teleport into an exterior cell by world and grid) and prints `enabled=` for watched references.

## Stand-ins (hand-written, named)

- The harness activates the actor to talk to (`act`/`activate`: the crosshair); all conversations are REAL mouse clicks.
- MS08 stage 10 (the Alik'r warriors' argue scene and forced greeting in Whiterun's street: the story manager's scene quest
  `MS08ArgueQuest`) is set by `story test quest = 01CF25:10@3`; the Alik'r in Rorikstead were not run.
- The legs are separate game runs; the earlier legs' results are set by the same aid (`...:20@4,...:25@5,...:100@6,...:175@7`), except
  where a leg's own talk does it (leg1 sets 20 and 25 for real from stage 10; leg2 reaches 100 for real from 25; leg3's kill and the following
  talk reach 160 and 200 for real from 100).
- Teleports between the cells (`cell`, `ext`, `abs`) instead of walking; Kematu's death is `summons test kill = MS08Kematu@30`.
- Saadia's walk to the private room is the placed-at-target rule above.

## Tests

`apps/components_tests/skyrimse/testskyrimsemytime.cpp`: 9 tests. Red against stubs: the order function 1 of 3 (the other 2 pin controls and
absence), the placed-at-target rule 1 of 4 (the others pin refusals), `GetDead` 1 of 2. All 9 pass after the code. The box trigger for followers,
the enable-children and the dead-lookup wiring in the pack are not unit tested (the game run is the evidence). No existing test edited.

## Game runs (final build, after the rebase onto `979bbc98db`; scenarios in `tools/skymytime_check/scenarios.sh`, three runs each in parallel)

- s1 (Saadia, stage 10 aid to 20 to 25): runs 1 and 3 byte-identical, run 2 has one more `PKCHOSEN` line (a package re-evaluation under load).
- s2 (prisoner, guard, prisoner, 25 aid to 100): 3 of 3 identical (md5 `c662b177591e`).
- s3 (Den kill and the inn, 100 aid to 160 to 200): 3 of 3 identical (md5 `d16106190e42`).
- s4 (betrayal from 175: follow, stables, ambush scene): runs 1 and 3 identical, run 2 lacks one `PKCHOSEN` line; one error line in each run is a
  Civil War script (`CWArrowVolleyParentScript`, budget) of the Whiterun area, not this task.
- Frames viewed: the private-room talk, the prisoner's and the guard's boxes, Kematu's boxes, Saadia's "The Alik'r won't trouble you anymore."
  box and reward line (`mt/*/user/screenshots`, not kept).
- Morrowind unchanged against the `integ2` build: `worldobjects_check` 110 objects OK, the fixed Balmora frame `1c5d47ad0b5dc32d` for both.
- `components-tests` (`*Skyrim*:*Papyrus*:*Dialogue*:*Quest*:*Package*:*Scene*:*Travel*:*Extension*`) 4,421 tests, 4,290 pass, the rest
  data-gated skips; `openmw-tests` (`*Neutral*:*Skyrim*:*Extension*`) 1,192 tests, 1,190 pass, 2 skips. `tools/testprofile/coverage.sh` OK.

## Not run

The Linux gate, clang-format 14, clang-tidy, the Lua tests, the whole `openmw-tests`, a fresh review; the Alik'r in Rorikstead, the Whiterun
street scene, the persuade/intimidate lines, Kematu's own talk after the ambush, stage 201, the "Put that down before you get hurt" branch,
Kematu's guards fighting (the player was attacked by the Den's bandits while talking in a few runs), a save in the middle of the quest.

## Decisions for toddhow

1. One order by quest priority for every alias package an actor has (behind `side quests`): a story quest now beats a radiant one. The real engine's
   order is RECALLED-VERIFY.
2. A MUST COMPLETE Travel that the mover cannot walk is completed by placing the actor (the Mare's west wing): general for quest alias
   packages, behind the setting.
3. A quest-package follower is a box trigger subject (only with `side quests`, and not when the main quest runs its own subjects).
