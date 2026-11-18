# Task 0312: Skyrim Dragonborn, the ship from Windhelm to Raven Rock (Solstheim)

Follow-up of task 0306 (section 5, item 1). Branch `solstheim-0312` from local `main` (`b34600a4fd`, rebased onto `4ac6fb8c89`).
Windows only, not pushed. Same hand wrote tests and code (tests first, run red against stubs where they could be), no fresh
reviewer. Behind the existing pack setting `[extension.skyrimse] dragonborn` (needs `fast travel`, `map`, `papyrus`, `actor cell
moves`, `quest alias packages`) and one developer aid, `dragonborn test stage` (a string, off), declared in `extension.json`.

## 1. What the data says (houseCARL reads of `Dragonborn.esm`; scripts decompiled from the scripts archive)

- **Gjalund Salt-Sage** has two NPC_ records: `DLC2RRGjalundSaltSageWindhelm` (0x0182AE, placed 0x0182AF at (141310, 34084,
  -13996) in Tamriel, Windhelm's docks) and `DLC2RRGjalundSaltSageSolstheim` (0x02582B, placed 0x02582C at Raven Rock). Both
  talk through the quest `DLC2DialogueRavenRock` (0x018B0F), script `DLC2DialogueRRQuestScript` (75 properties).
- **The passage branch** (Windhelm, DLC2MQ01 below stage 10): "I need passage to Solstheim." (`...InitBranchTopic`) -> choices
  "How much will it cost me?" and "Solstheim?" -> "Why sail there at all then?" (needs `GetStage(DLC2MQ01) < 10`) -> **"Yes.
  (250 gold)"** (`...InitBranchTopic04`; gold >= `DLC2CostToSail` = 250, global `DLC2RRGjalundInit` == 0) and "Not now.". The
  chosen INFO 0x02ADCF carries the fragment `DLC2_TIF__0202ADCF.Fragment_2` (`DLC2RRGjalundInit.SetValue(1)`, then
  `DLC2DialogueRRQuestScript.SetSail(akSpeaker, 1)`). With DLC2MQ01 at stage 10 or more Gjalund instead has the "MQIntro"
  branch (the cultists took his ship: Persuade, Intimidate, "I'll pay you double (Give <Global=DLC2CostToSailx2>)", INFO
  0x039DD2 calls SetSail with flag 2): not driven in this slice.
- **`SetSail(speaker, costFlag)`**: disable player controls, `StopCombatAlarm`, wait while the speaker is in dialogue, fade out
  (`ImageSpaceModifier.Apply`, `Utility.Wait(2.1)`, `PopTo`), then **if the player's world is Solstheim**: take the fee and
  `Game.FastTravel(pDLC2RRWindhelmLandingMarker)` (0x018FE2 at (141333, 33718, -13991)); **else** take the fee and, the first
  time (`FirstTimeToSolstheim`), `FirstTimeTravel()` (below), later `Game.FastTravel(pDLC2RRSolstheimLandingMarker)` (0x018FE1 at
  (28933, 33531, 311)); then enable controls, fade in, `RequestAutoSave`.
- **`FirstTimeTravel`**: `ForceFirstPerson`, moves Gjalund (Solstheim) to `GjalundOutOfSightMarker`, disables the dock objects
  of `DisableList` (7), enables `BoatNavmeshCutter`, `Game.FastTravel(BoatRideTarget)` (0x03A9E6 at (29013, 33583, 312), 80
  units from the landing marker), `StowFollowers` (about 25 alias and package tests), `PlayIdle(BoatRideAnim)`, music, fade in,
  `Utility.Wait(7)`, `WaitForAnimationEvent("BoatRideFadeOut")` on the player, fade out, `FinishBoatRide` (re-enable what was
  disabled, restore followers). The boat's own animation is not part of the neutral world (no animated boat is drawn).
- **DLC2MQ01 stage 10** has three log entries: unconditional (completes objectives 5 and 7, Neloth), `GetInCurrentLoc(
  DLC2SolstheimLocation) == 1` (`Fragment_16`: `SetStage(20)`) and `== 0` (displays objective 10). Stage 20 (`Fragment_2`)
  completes objective 10 and displays objective 20 "Search for information about Miraak". Raven Rock's location
  (`DLC2RavenRockLocation` 0x0143B9) has `DLC2SolstheimLocation` (0x016E2A) as its parent. A second route to stage 20 exists:
  `DLC2MQ01Stage20TriggerScript` (a reference-alias script, `OnTriggerEnter` of the player while the stage is 10): not exercised.
- Raven Rock's arrival scene (`DLC2RRArrivalScene`, 0x02ADDA: Gjalund and Adril Arano talk when the player arrives) is started
  by `GjalundShouldTalk`; **no scene ran** in this slice and Adril Arano's greeting was not reached (section 5).

## 2. What was missing (found by running it)

| # | Gap | Fix |
| --- | --- | --- |
| 1 | `Game.FastTravel(reference)` (task 0223) refused a destination in a world that shares no land with the player's | `TravelService::setCrossWorldHours(n)`: with n above 0 `planToRef` also accepts a reference of an UNRELATED world; the travel then takes `n` game hours whatever the distance (6: `dragonborn::kShipHours`, OURS: the data has none), the plan names the destination's world (`Plan::mWorld`), the commit discovers there. Off (0) unless `dragonborn` |
| 2 | The global travel script had no way to enter another world | new Read binding `world_of_ref(ref)` (`skyrimse.travel` 1.2); `travel_global.lua` asks `world.getExteriorCell(x, y, "<gameId>:w<world>")` for that world |
| 3 | Lua's `getExteriorCell` knew a neutral world only after a door or a start had registered it | a text "<gameId>:w<world id>" (what a cell's `worldSpaceId` shows) registers that world (`WorldModel::registerNeutralWorldspaceText`, the pure `parseNeutralWorldspaceText`) |
| 4 | `ObjectReference.GetWorldSpace` was unregistered, so `SetSail` always took the "going to Solstheim" branch | registered; `PapyrusGameData::worldOf(reference)`: the player's world (the place events) or a placed reference's cell's world |
| 5 | `WaitForAnimationEvent` (the boat ride's `BoatRideFadeOut`) is latent and unregistered | registered: the script parks for 8 script seconds (`kAnimationEventWaitSeconds`, OURS: the ride's clip is not played) |
| 6 | `IsInDialogueWithPlayer`, `Actor.StopCombatAlarm`, `Actor.EnableAI`, `Game.RequestAutoSave` were tolerant (logged) | registered as no-ops (`IsInDialogueWithPlayer` answers false: the world is paused while the box is open, so a script that waits for the talk to end already runs after it) |
| 7 | Stage 10's `Fragment_16` (`SetStage(20)`) never ran on Solstheim: `GetInCurrentLoc` on the player in a quest's log-entry conditions was answered from the LAST SPEAKER's place | `evaluateQuestConditions` takes the player's location (`SkyrimWorldView::playerLocationNow`: an interior's, or the exterior grid square's) and puts it in for the player and the stand-in speaker; `GetInCurrentLoc` of the player answers from it. Only the stage-entry check passes it (installed with `houses`, `main quest start`, `dragonborn`) |
| 8 | A run that did not play the ambush could not show DLC2MQ01 moving on | aid `dragonborn test stage = <stage>[,<script seconds>]` starts DLC2MQ01 at that stage (the host's `startQuestForTest`) |

## 3. Tests

`apps/components_tests/skyrimse/testskyrimsesolstheim.cpp` (12 tests, all new) and `apps/openmw_tests/mwworld/
testneutralworldspacetext.cpp` (3). No existing test edited.

- `ShipTest` 7 (cross-world plan, hours, same-world plan, interiors stay refused, commit log, `worldOfRef`, the binding):
  written first against stubs, **5 of 7 failed** (the 2 that passed pin absence: no plan without the setting; an interior
  destination).
- `ShipNativesTest` 3 (GetWorldSpace, the 8 second park, the no-ops are not tolerant): **3 of 3 failed** against the unregistered
  natives.
- `ShipLocation` 2 (the player's location in `GetInCurrentLoc`; the last speaker standing where the player does): the first was
  **red** (1 of 1) against the old code; the second was written with the code and only compile-red (the setter did not exist).
- `NeutralWorldspaceText` 3 (`parseNeutralWorldspaceText`): **1 of 3 failed** (the 2 that passed are refusals of a stub).
- Runs after the rebase onto `4ac6fb8c89` (build `combat3`): `components-tests` `*Skyrim*:*Papyrus*:*Extension*:*Frozen*:*Follower*:*Package*:*Quest*:*Travel*:*Carriage*:*Dialog*:Ship*` **3,710 pass**; `openmw-tests` `*Neutral*:*Extension*:*Lua*:*Skyrim*:*Combat*` **1,209 pass**; `tools/testprofile/coverage.sh` ok.

## 4. In game (`tools/skysolstheim_check/check.sh`, five masters, Windows, real clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`)

A new game at `@Tamriel,141200,34000` (Windhelm's docks, night), 6,000 gold.

1. Gjalund's conversation: "I need passage to Solstheim." -> "How much will it cost me?" / "Solstheim?" -> "Why sail there at all
   then?" -> "Yes. (250 gold)" (the `<Global=DLC2CostToSail>` tag shows 250; frames viewed) -> "You've got yourself a ship. We'll
   cast off immediately." and Goodbye.
2. The box closes and the game's own `SetSail` runs: `DLC2_TIF__0202ADCF.Fragment_2`, fade (tolerant), `GetWorldSpace`, `RemoveItem`
   (`PAPYRUS item 0000000F x250`, gold 6000 to 5750), `FirstTimeTravel` (`MoveTo` of Gjalund and the boat's objects, `Enable`,
   `Game.FastTravel(BoatRideTarget)`), `[skyrimse travel] carriage to 0403a9e6 at (29013, 33583, 312) in 6 hours ... into
   skyrimse:w67110912`, `TRAVEL to 0403a9e6`. The player stands at (29013, 33583, 308) on Solstheim; the story manager's CLOC
   event names Raven Rock. Frame: Raven Rock's docks (the player in first person; no boat is drawn).
3. The aid starts DLC2MQ01 at stage 10 on Solstheim: **`Fragment_16` -> `SetStage(20)` -> `Fragment_2`** (objective 10 done,
   objective 20 displayed). Before gap 7 it ran only the "not on Solstheim" fragments.
4. Gjalund of Solstheim (`DLC2RRGjalundSaltSageSolstheim`, shown by his editor id: the record has no FULL name) offers "I'd like
   passage back to Skyrim." -> "Here you go. (250 gold)" -> Goodbye: `SetSail` takes the Solstheim branch (`GetWorldSpace` is
   Solstheim), `item 0000000F x250`, `[skyrimse travel] carriage to 04018fe2 at (141333, 33718, -13991) in 6 hours ... into
   skyrimse:w60`; the player stands at (141333, 33718, -13990) in Tamriel, gold 5500.

**Determinism:** three runs of the default scenario (`f1` to `f3`, after the rebase) give the same 29 trace lines (the Gjalund
turns, `item 0000000F x250`, both `[skyrimse travel]` lines, both `TRAVEL to` lines, the DLC2MQ01 fragment and stage lines, the
`CMP` position and gold lines) in all three; the only difference is the `CMP start` line at update 10, whose gold reads 6000 in
one run and -1 (the inventory not ready yet) in the other two: a harness timing, not the game. Frames viewed:
`I:\Projects\OpenEden-work\combat3\keep312\` (Gjalund's "Yes (250 gold)" line, Raven Rock's docks on arrival, Gjalund of
Solstheim's "Here you go (250 gold)" menu). A frame of Windhelm after the return was not captured (the last two screenshot requests
of the input script were not written).

**Morrowind unchanged:** `tools/worldobjects_check` OK (110 objects) and the fixed Balmora frame `1c5d47ad0b5dc32d` for the
reference build (`I:/Projects/OpenEden-work/integ2/build`, a build of `main`, not this commit's exact parent) and for this build.

## 5. Not built / not shown (ranked)

1. **The arrival scene and Adril Arano's greeting were not reached.** `DLC2RRArrivalScene` never started (the log says nothing
   for it): its start depends on `GjalundShouldTalk` and a quest start this slice did not follow. Nothing was built for it.
2. **The boat ride is not played**: no boat animation, no `BoatRideAnim` clip on the player, the ride is the 8 second park and the
   teleport to the target marker (80 units from the landing marker). The player ends in first person (`ForceFirstPerson` is a
   no-op, he was in first person already).
3. **The fades are tolerant no-ops** (`ImageSpaceModifier` forms have no class here: the log says "class 'Form' has no function
   'Apply'"), so the screen does not fade; `MusicType.Add` likewise.
4. The "MQIntro" Gjalund (DLC2MQ01 at stage 10 or more: Persuade, Intimidate and the double-fare bribe) is not driven; the frames
   show that with the quest at stage 10 BEFORE sailing his menu differs.
5. DLC2MQ01 reaches stage 10 here by the developer aid, not by the cultist ambush (task 0306 has that run; chaining them in one
   scenario was not done), and stage 20 came through the quest's own stage-10 fragment, not `DLC2MQ01Stage20TriggerScript`.
6. Followers are not carried through `StowFollowers` (none in the scenario).
7. The 6 hour passage is ours (the data has none); a save made on Solstheim was not tried (the neutral save records the player's
   cell; not run).

## 6. Decisions and test edits for toddhow

- No existing test edited. One new setting-like aid, `dragonborn test stage` (a string, default empty), declared in
  `extension.json`, noted in `tools/testprofile/profile.cfg` and its README (pack settings: not set in the profile, like the other
  questline settings).
- `dragonborn` now also allows cross-world `Game.FastTravel` (6 hours). Nothing changes with it off.
- `evaluateQuestConditions` gained an optional trailing parameter (`playerLocation`, default none): every quest stage-entry check
  now gives the player's real location where the pack knows it (before: the last speaker's). This changes the answer of
  `GetInCurrentLoc` in a log entry for any quest whose entries test it, with `houses`, `main quest start` or `dragonborn` on.
