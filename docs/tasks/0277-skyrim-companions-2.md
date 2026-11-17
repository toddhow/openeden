# Task 0277: Skyrim Companions, second slice (C00 from the walk to the yard to stage 30)

Design doc section: `docs/design/game-packs.md`; follow-up of tasks 0264 (Companions start), 0272 (stage reset), 0271 (quest alias
packages and forced greetings), 0196 (followers). toddhow chose "Companions C00 to C01" on 2026-11-17.
Branch: this worktree's branch from `34beb9d459`. Same hand wrote the code and the tests (the tests after the code: see section 4);
no fresh reviewer; Windows only; local commits, not pushed. Everything stays behind `[extension.skyrimse] companions` (no new
setting); the forced-greeting and alias-package parts also need `quest alias packages`.

Goal: the whole opening of the Companions runs through the game's own quests, scenes, dialogue and Papyrus without the
`companions test training` aid: the NPCs go to the yard, the training fight starts from Vilkas' own line, Vilkas greets again after
the fight, C00 reaches stage 30.

## 1. What was found (by running the scenario, `tools/skycompanions_check`)

| # | Gap | Cause (measured) | Done |
| --- | --- | --- | --- |
| 1 | Vilkas never left the basement | his scene package `C00VilkasGetToOutside` (0xF7957) travels to the REFR 0x23E55 in the Whiterun exterior; `locate` knew no position (the cell was not loaded) so the anchor was his home and the package was Done at once | **built**: a scene's Travel package whose target stands in another cell than the player's waits and the actor is **carried through the player's doors** like a follower (`PackageBrain::travellerRefs`, listed by `SkyrimActorTable::followers()`; not a follower: `isFollower`/`sideOf` do not list it) |
| 2 | the scene's package was forgotten when a door made the core remove and add the actor again | `mForced` lived in the actor row | **built**: `mForcedRefs` by placement, restored in `onActorAdded` |
| 3 | after the door, the walk down the Jorrvaskr porch stops at "blocked edge" (the porch is 146 units above the yard, the neutral mover will not step down; the navmesh path was a straight line) | exterior navmesh near the porch, not investigated | **worked around, not fixed**: a scene Travel walk that ends Blocked or NoPath puts the actor at the anchor (`ActorRequestWriter::placeActor`, additive virtual at the end of the interface, SDK version unchanged; `PackageBrain::mTravelSnaps`) |
| 4 | Vilkas' line "So you're supposed to train me?" said "Not here. Out in the yard" every time | INFO 0xA3E98 needs `GetDistance(marker) > 1300` and the condition world measured from the placement's INITIAL position (basement cell: infinitely far) | **built**: `SkyrimConditionWorld::setLivePlaces` (the package brain's estimate of where the actor stands, and where a loaded reference stands) used by `GetDistance` |
| 5 | the training quest's alias package `C00VilkasForceGreetAfterCombat` (0xA3EC4) never reached Vilkas | `packagesFor` reads the running quests when the actor is resolved, and the quest starts later (0271 pitfall) | **built**: `PackageSource::packagesRevision` (moves when a quest stage changes with `quest alias packages` on); the brain asks again for every actor's list |
| 6 | the brain's forced greeting was never opened | `forcegreet_global.lua` searched only the player's own cell; Vilkas stays an object of the cell where the door put him (7,-2) while he stands in (7,-1) | **built**: the script also searches the eight neighbours of an exterior cell, every 30th update (the scan is nine cells) |

## 2. Implementation (files)

- `game_packs/skyrimse/{include/game_skyrimse,src}/package_brain.*`: `setCarryTravellers`, `travellerRefs`, `mElsewhereRef`,
  `mForcedRefs`, `mSnapNext`, `packagesRevision` handling, `Stats::mTravelSnaps`, `PackageSource::targetIsElsewhere` and
  `packagesRevision` (virtuals with defaults, so older fakes build).
- `actor_table.*`: `setCarryTravellers`, `brainPlace`, `followers()` is the union of the (not waiting) followers and the travellers.
- `world_view.*`, `dialogue.*`, `dialogue_runtime.hpp`: `targetIsElsewhere` (the target's cell is not the player's cell, one side an
  interior), `packagesRevision`, `setLiveActorPlace` / `setLivePlaces`, `livePlaces` in `distanceKnown` / `distanceTo`.
- `skyrimse_game_pack.cpp`: with `companions` on: `setCarryTravellers(true)` and the live actor place hook.
- `sdk/include/openeden/game/actor_rules.hpp` (`placeActor`, additive, `kActorRulesVersion` stays 0.6), `neutralactorbridge.*` and
  `physicssystem.*` (`placeLiving`: position, heading, fall state, request cleared).
- `files/data/scripts/openeden/skyrimse/forcegreet_global.lua`: the neighbour search. **Shared with task 0271 (Thieves Guild);
  the change is additive** (the old search of the player's cell runs first and every update; the wide search is every 30th).
- Tests: `apps/components_tests/skyrimse/testskyrimsecompanions2.cpp` (15 tests: `SkyrimTravellers` 12, `SkyrimLiveDistance` 3) and
  `apps/openmw_tests/mwphysics/testneutralactorplace.cpp` (4). No existing test was edited.
- `tools/skycompanions_check/yard_input.txt` and the scenario text in `check.sh`'s header.

## 3. In game (`tools/skycompanions_check`, Jorrvaskr basement, real clicks, quiet window, sound 0, frame time fixed)

The walk, from the log (every step is the game's own data and scripts):
Kodlak "I would like to join" -> scene 0xA3EB7 -> Kodlak's forced greeting -> "I have much to learn" -> dismiss scene 0xBD697
(stage 20, Vilkas' package 0xF7957) -> the harness uses the basement door and the Jorrvaskr back door (`furn` steps; **Vilkas
is carried through both**: "Neutral follower 108181 carried to WhiterunJorrvaskr", then to the exterior) -> scene phase 3,
`FORCEGREET 0001a695 asked to say 000a3e4c`, Vilkas says "The old man said to have a look at you..." (INFO 0xA3E99, voice file
`..._000a3e99_1.fuz`) -> **`quest 000A3EBC started` by its own fragment, no test aid** -> the clicks fight (`COMBAT ai engage
vilkas ... provoked`, three player hits) -> **stage 150** -> `C00VilkasForceGreetAfterCombat passes`, `FORCEGREET ... wants to
say topic 671307` -> Vilkas greets again ("Not bad. Next time won't be so easy..." and, through the unnamed link, "You might just
make it ... take it up to Eorlund") -> Goodbye -> the fragments set **stage 200**, `PassTheSword` (the sword goes to the player)
and **C00 stage 30** (objective "Give Vilkas's sword to Eorlund"), the training quest stops, the scene ends.
Frames: `I:\Projects\OpenEden-work\followers\companions2\keep` (`fight_vilkas_bar.png`, `yard_after_fight.png`,
`vilkas_greets_after_fight.png`, `yard_vilkas_placed.png`; viewed).

- **Three runs** (`d1`..`d3`, 14,500 frames): the 64 lines of FORCEGREET, SCENE, C00 and training quest stages, `COMBAT ai`,
  `COMBAT damage`, voice files and the carry lines are byte-identical (md5 prefix `1b9187e422`, trace kept as `keep/trace_d1.txt`).
- **Morrowind unchanged** against `I:\Projects\OpenEden-work\integ2\build` (a build of `main`, not of the exact parent):
  `tools/worldobjects_check` 111 lines OK (110 objects), the fixed Balmora frame (`--start Balmora --random-seed 7`,
  `OPENEDEN_FIXED_FRAME_TIME`, frame 600) has SHA-256 prefix `1c5d47ad0b5dc32d` for both.
- Tests: `components-tests` filter `*Skyrim*:*Dialogue*:*Papyrus*:*Scene*:*Package*:*Combat*:*Extension*:*Neutral*:*Frozen*:*Journal*:
  *Follower*` 4,303 pass; `openmw-tests` filter `*Neutral*:*Extension*:*Skyrim*:*Combat*` 1,105 pass.

## 4. Red evidence (honest)

The code was written before the tests. The new tests do not compile against the parent (`setCarryTravellers`, `travellerRefs`,
`setLivePlaces`, `placeActor`, `packagesRevision` do not exist there): **compile-red only**, no run against stubs, no mutation
run (not enough time on a shared machine; each rebuild of the pack takes minutes).

## 5. What is still missing (ranked)

1. **C00 stage 30 onwards (not run).** Eorlund's talk is `C00EorlundBlockEntryTopic` ("What brings you here?", `GetStage(C00) == 30`)
   with the player's topic "Vilkas sent me with his sword" as a link: another forced greeting, from an alias package, and Eorlund is
   in the Skyforge by day and in `WhiterunHouseGrayMane` at night; the harness could not enter the Skyforge (a talk in the house
   offers only the generic topics). Needs: the schedule-driven walk to the Skyforge (doors for NPCs), the sword item transfer
   (`AddItem` of an item REFERENCE exists since 0271) and the Aela and Farkas scenes (stages 40 and 50, `LastFarkasTourScene`).
2. **The neutral mover and the navmesh on the Jorrvaskr porch.** Vilkas' walk from the door to the training marker is blocked at the
   porch edge (z -2588 to -2734) and the snap hides it. Cause not found (a straight "path", no step down). A real fix is the
   mover or the exterior navmesh near stairs.
3. **C01 "Proving Honor"** is not reached: it starts from the radiant system after C00 (`CompanionsHousekeepingScript` cycles
   radiant quests); Farkas as `Observer` and follower (`SwapFollowers`), the Dustman's Cairn ambush triggers and the Wuuthrad fragment
   are not run. Survey only, as in task 0264 section 1 item 8.
4. NPCs walk to the yard by themselves while the player does not follow (the carry needs the player to use the doors).
5. The training marker's placement: Vilkas is put exactly at the marker (an NPC may stand there), the real game paths to it.

## 6. Decisions for toddhow

- The carry only applies to a scene's package (`actor.mForced == package`): NPCs with Travel packages of their own are NOT carried
  (the first version carried four of them through the doors; found in the log and narrowed).
- `placeActor` is a new virtual at the end of `ActorRequestWriter` (version unchanged, like `placeCorpse`).
- The snap is a workaround for a mover/navmesh fault; it only runs with `companions` on and for a scene package.
- `forcegreet_global.lua` is shared with the Thieves Guild work; the change is additive.
- Two lines of an existing file that tests read are untouched; no existing test was edited.

## 7. Not run

The Linux gate, clang-format 14 (formatted by hand, 120 columns checked by script), clang-tidy, the Lua integration tests, a fresh
review, `eden-studio-tests`, saves during the walk (a carried actor's package is not saved), C00 after stage 30.
