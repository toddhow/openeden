# Task 0346: Skyrim Solitude, first slice: the execution of Roggvir and the start of "The Wolf Queen Awakened"

Status: BUILT for the execution (the whole quest chain `SolitudeOpening`, stage 10 to its end at 200) and for the start of MS06 (the court
scene, Elisif's and Falk's real talk to stage 50, Wolfskull Cave's ritual scenes to stage 83); Windows only, local branch `solitude-0346`
rebased onto `69f549168d` (local `main`), not pushed. Behind the new pack setting `[extension.skyrimse] solitude` (off; declared in
`extension.json`; needs `papyrus`, `actor packages`, `quest alias packages`, `rest`; README line and a profile comment in `tools/testprofile`).
Follow-up of tasks 0333 (a city entry, scenes with a quest start), 0337 (box triggers, city scenes, kill aid), 0341 (alias package order),
0269 (Falk Firebeard as the steward of Solitude) and 0276 (game-time updates). Same hand wrote tests and code; the new tests were written first
and run red (counts below); no fresh reviewer.

**A correction of the task text:** "The Wolf Queen Awakened" is **MS06** (QUST 0x26C4D), started by the quest **MS06Start** ("The Man Who Cried
Wolf", 0x93807); MS04 is "Lost to the Ages" (Avanchnzel). The survey below is of MS06Start, MS06 and SolitudeOpening.

## 1. The survey (houseCARL records, decompiled `.pex` of `Skyrim - Misc.bsa`)

- **`SolitudeOpening`** (QUST 0xB2FD9 "Solitude Dialogue", StartGameEnabled, priority 30, 33 aliases, stages 0, 10, 20, 29, 30, 31, 35, 40, 45, 190,
  200). Its start is a box trigger: `OpenningSceneStartTrigger` is REFR 0x85373 (Tamriel/SolitudeWorld grid (-16, 25), centre (-65237, 104652,
  -8180), half extents (276, 362, 200)), script `SolitudeOpeningSetStageScript` (stage 10, TriggerType 3 = OnTriggerEnter). The ground there is at
  z -8416, so **the box hangs 36 units above the road**: only the player's body reaches it.
- Stage 10 (`Fragment_0`) starts `ExecutionSceneLeadIn` (SCEN 0xA9623, 8 phases, 13 actions: Svari, Addvar, Vivienne, Aldis and the crowd; its end
  fragment sets stage 20) and sends `EvaluatePackage` to four citizens. Stage 20 starts **`ExectionScene`** (SCEN 0xA3BE5, 15 phases, 62 actions,
  13 actors: Aldis reads the charge, the crowd shouts, Roggvir answers, "On this day... I go to Sovngarde."). Its **phase 13 completes only when
  GetDead holds for the quest alias with index 58 (the Prisoner)** and phase 14 when scene action 36 (the executioner's package) is complete;
  the scene's end fragment sets stage 30. The headsman's kill is an animation we do not have.
- Stage 30 (`Fragment_11`): stops the scene, shuts the interrupt trigger, makes the guards fight a prisoner who is still alive, `EvaluatePackage`
  on Evette and Jala and `RegisterForUpdateGameTime(0.1)`; `SolitudeOpeningScript.OnUpdateGameTime` then steps 30, 35, 40, 45, 190; stage 190 sets
  200 (`Fragment_9`: `MS07.SetStage(3)`, the prisoner `Kill()`, the gates enabled and disabled, the headsman's axe, `Stop()`).
- **`MS06Start`** (QUST 0x93807, StartGameEnabled, RunOnce, priority 60, 38 aliases, 18 stages). Stage 5 is a box trigger in the Blue Palace's throne
  room (REFR 0x9E013, `defaultsetstagealiasscript`, an INTERIOR box in `SolitudeBluePalace`) and starts `MS06StartAppealForHelpScene` (SCEN 0x96555,
  12 phases, 31 actions: Varnius Junius's appeal, Elisif, Falk, Sybille Stentor); its end sets stage 10 (`MS06Rumor` stage 20). **Stage 15** (the
  objective "Ask Falk Firebeard about Wolfskull Cave") is set by the end fragment `TIF__000D8E97` of Elisif's top-level topic "Do you need someone
  to check out Wolfskull Cave?" ("I think Falk is taking care of that"). **Stage 50** is set by Falk's own topic: the branch `MS06FalkInvestigate`
  is TOP LEVEL and its starting topic 0x26C48 has **no name**; the player reads the prompt of its INFO ("I hear you're looking for some help with
  Wolfskull Cave."). Stage 50 (`Fragment_3`): the objective, the ritual enabler and the cave's state marker enabled, **the cave's map marker added
  to the map**.
- The ritual in `WolfSkullCave02` (an interior): box triggers 0x97E23 (stage 60, `MS06StartRitualScene1` 0x96560, its end sets 65), 0x99D25 (stage
  70, `RitualScene2` 0x9655F), 0x99D27 (stage 80, `RitualScene3` 0x9DA28) and 0x108A62 (the tower top: stage 83, the tower scene, and the cultists
  `StartCombat(player)`); 85 and 90 follow the head necromancer's fight (not run).

## 2. Faults found by running it, and fixes (all behind `solitude`)

| # | Fault | Cause | Fix |
| --- | --- | --- | --- |
| 1 | The gate's box never fired (a teleport onto the road, then 3 000 frames: no edge) | `TriggerTracker` tested the player's FEET point; the box's lower face is 36 units above the road | `volumeContainsBody`, `TriggerTracker::setPlayerBodyHeight` (default 0: the feet), `PapyrusHost::setPlayerBodyTriggers`: the body is the vertical segment feet to 128 above (OURS), tested every 16 units; other subjects keep the point test |
| 2 | The execution scene stopped in phase 13 for ever although Roggvir was dead | the scene's condition world (`SkyrimWorldView`'s scene `mEvaluate`) had no dead lookup: `GetDead` of the Prisoner alias read only the record's flag | the lookup (the pack's kill ledger of tasks 0326, 0337, 0341, ONE mechanism) is given to the scene world too |
| 3 | After the scene the quest never went on from stage 30 | `RegisterForUpdateGameTime(0.1)` needs the game-time updates and the game clock, which only `marriage` switched on | `solitude` switches them on: task 0276's `enableGameTimers` and `mGameClockOn` (the same clock, fed from the rest clock), the vampirism quest's own update still ignored |
| 4 | Falk offered no request: his MS06FalkInvestigate0 was never listed | task 0270's rule "a nameless topic is only ever a choice" | **one flag with task 0343's** `setPromptStartsAreMenus`: a quest start whose INFOs all carry prompts is a menu topic and not a greeting (0343), and such a start that is the top of a branch that is NOT Blocking is also LISTED (new `DialogueIndex::isBlockingStart`); a start of a Blocking branch stays out. (Merged with 0343 at the rebase: my first version had its own `setPromptStarts`, now removed.) |
| 5 | (diagnostics, no behaviour) | a phase whose start conditions fail was silent, a box trigger that fired was not named | `OPENEDEN_SCENE_WAIT=1` now also says "SCENEWAIT ... cannot start" and ticks; the host logs "PAPYRUS trigger <id> entered/left by the player (n scripts)" |

Not a fault, found on the way: a fresh game with `player level = 10` (the old harness setting) lets the Dawnguard messenger arrive in Solitude and
force-greet, which opens a conversation that PAUSES the world (no scene ever ran); the harness uses level 5. The open dialogue box also holds the
queued INFO fragments: a topic's fragment (Elisif's stage 15) runs only after the box closes, so the harness clicks Goodbye.

## 3. What runs now

`tools/solitude_check` (a fork of `tools/skymytime_check`; `scenarios.sh s1..s4`), real clicks on the game's own boxes, frames viewed:

- **s1, the execution.** The player is put into the gate's box in SolitudeWorld (a Lua teleport; the box fires: `PAPYRUS trigger 00085373 entered by
  the player`), `SolitudeOpening` goes to stage 10, `ExecutionSceneLeadIn` plays (Svari: "They can't hurt uncle Roggvir...", Aldis: "Positions.",
  Addvar, Vivienne, ... with the real voice files and subtitles), stage 20, `ExectionScene` plays all 15 phases (Aldis, Beirand: "Traitor!", Roggvir:
  "There was no murder!...", Taarie: "Liar!", the crowd's "Booooo", Roggvir: "On this day... I go to Sovngarde."), phase 13 completes at his death
  (the kill aid), phase 14 ends after the package's 60 s, **stage 30**. In a longer run the game-time updates step the quest 35, 40, 45, 190 and
  **200 ends it** (the headsman's axe, MS07 stage 3, the gates).
- **s2, the Wolf Queen's start by real talk.** The player is put into the throne room's box: stage 5 and the whole appeal scene (Varnius: "I swear to you,
  unnatural magics are coming from that cave!", Sybille, Falk, Elisif), **stage 10**; a real click on Elisif's "Do you need someone to check out
  Wolfskull Cave?" (voiced: "...I think Falk is taking care of that.") and Goodbye give **stage 15**; Falk's "Yeah?" lists "I hear you're looking
  for some help with Wolfskull Cave." (frame viewed), the click gives his own voiced lines and **stage 50** with the objective, the cave's map marker
  and the enabled ritual.
- **s3, Wolfskull Cave.** Stages 5 to 50 by the story aid, the player teleported into `WolfSkullCave02` at the game's own trigger boxes: stage 60,
  the ritual scene 1 (the necromancers' "We summon Potema!", the Ritual Master: "Long have you slept the dreamless sleep of death, Potema...")
  to its end and **stage 65**, stage 70 (scene 2), stage 80 (scene 3, which ends scene 2), stage 75 and **stage 83** (the tower top scene, the
  cultists' attack).

## 4. Stand-ins and aids (hand-written, named)

- **The crowd and the actors are brought by hand** (`bring` steps: a Lua teleport of the placements of Aldis, Ahtar, Jala, the Addvar household, the
  Winking Skeever's Lisette, Noster and Sorex, Silana, Evette, Taarie, Beirand and Vivienne out of their homes onto the platform): the game's own
  stage 10 sends them there by `EvaluatePackage`, but they stand in interiors that are not loaded and the pack has no off-screen schedule.
  Roggvir and the guard stand on the platform already.
- **Teleports for walks:** into the gate's box (a free drop through it, so that the body segment meets the box), into the throne room's box, into the
  cave (`WolfSkullCave02` at each trigger); the way from the Blue Palace to the cave and the cave's entrance doors are not walked (no door of the cave
  was found in the neighbouring cells; the map marker's cell has none).
- **Roggvir's death is `summons test kill = Roggvir@60`** (a developer aid; it also needs `summons` and `combat spell delivery`): the headsman's kill
  animation does not exist here.
- Stages 5 to 50 in s3 are `story test quest = 093807:5@3,...`; the real chain is s2.
- The harness's game hour is the engine's default; `rest` supplies the clock for the game-time updates.

## 5. Not reached (ranked)

1. Stage 85 and later: the head necromancer's fight (`MS06HeadNecromancer` `StartCombat`), Potema's escape scene (stage 90), the return to Falk
   (`MS06StartFalkReturnForcegreet`, stages 100 to 115) and MS06 itself (stage 5 to 250, the Dragon Bridge catacombs).
2. The walk from Solitude to Wolfskull Cave, the cave's exterior door and `WolfSkullCave01`.
3. The executioner's axe and the death animation; Roggvir's body and the crowd leaving (their packages after stage 30 are not seen).
4. The prisoner alive branch (stage 29: the guards attack him and the player).
5. A save in the middle of the scene (scene saves exist, task 0211; not exercised here).

## 6. Tests

- `apps/components_tests/skyrimse/testskyrimsesolitude.cpp`, 9 tests (`SkyrimSolitudeBody`), written first and run red against a stub of
  `volumeContainsBody` that is the point test (a private MSVC link of the test and `triggers.cpp` with gtest): **3 of 9 failed** (a body below the
  box, the tracker with the body height, a sphere); the other six pin refusals and absence. All 9 pass with the code.
- `apps/components_tests/skyrimse/testskyrimsesolitudeprompt.cpp`, 3 tests (`SkyrimSolitudePrompt`, a generated plugin, no game data): the flag off
  keeps task 0270's rule, the flag on lists a nameless start with a prompt and answers it as its own topic, a Blocking branch's start is never a
  button. Run red against a one-line mutation of `offerTopics` (the flag ignored): **1 of 3 failed**, the other two pin the default and the
  Blocking exclusion. A first version of the Blocking test had the wrong DNAM flag (2, not 4) and was fixed before the green run.
- The scene's dead lookup, the game-time wiring and the pack block have no unit test of their own; the game runs are their evidence.
- No existing test edited. `components-tests` filter (`*Skyrim*:*Papyrus*:*Dialogue*:*Quest*:*Package*:*Scene*:*Travel*:*Extension*:*Trigger*`) 4,454
  tests, 4,323 pass, 131 data-gated skips, 0 failed; `openmw-tests` (`*Neutral*:*Skyrim*:*Extension*`) 1,192 tests, 1,190 pass, 2 skips.

## 7. Game runs (final build, after the rebase onto `69f549168d`; three runs each, started together)

- s1 (to stage 30, 9,500 frames): runs a and b byte-identical (86 trace lines, md5 `62f8ab0e25b2`); run c has the same lines and differs only in the
  order of two subtitles spoken in the same frame (Jala and Sorex's "Booooo").
- s2 (real clicks, 5,400 frames): 3 of 3 identical (60 lines, md5 `976f5643dd7b`).
- s3 (the cave, stages 5 to 50 by the story aid): 137 trace lines each in 3 runs, equal as sorted sets (md5 bdadf5920498); the order of same-frame subtitles differs. Stages 60, 65, 70, 80, 75 and 83 by the game's own boxes.
- Morrowind: `worldobjects_check` 110 objects OK, Balmora frame `1c5d47ad0b5dc32d` for the integ2 build and this build.
- Morrowind unchanged: see the changelog row.

## 8. Decisions for toddhow

1. **The player enters a box trigger with his body** (a 128 unit segment above his feet, OURS), only with `solitude`: the engine tests the player's
   collision, our stand-in has none. The same rule would be more faithful for every trigger (the Daedric, College and cart boxes were found with feet).
2. **One flag for 0343 and 0346** (`setPromptStartsAreMenus`): a nameless start with prompts is never a greeting and, if its branch is top level, is a
   menu button. 0343 does not run this change's menu half unless it also lists; its tests are unchanged and pass.
3. The scene's condition world now knows deaths of the session (only with `solitude`): the same lookup the quests of tasks 0326, 0337 and 0341 use.
4. The game-time updates and the rest clock now also run with `solitude` (marriage's mechanism, not a second one).
