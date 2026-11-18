# Task 0315: Skyrim Companions, third slice (the end of C00; C01 surveyed)

Design: `docs/tasks/0264-skyrim-companions.md`, `0277-skyrim-companions-2.md`. Same hand wrote tests and code (the first agent was
stopped partway; its uncommitted work was kept and finished). Windows only, local branch `companions3-0315`, not pushed. Behind
`[extension.skyrimse] companions` (no new setting).

## 1. What was built

- **Walk-away topics (TWAT).** An INFO's `TWAT` names the topic whose response the game plays when the player ends the conversation
  (Eorlund's greeting and intro lines name `C00EorlundWalkaway`, whose fragment starts the follow-up scene
  `C00EorlundFollowupScene`, which makes Eorlund forced-greet the player a second time). `DialogInfo::mWalkAwayTopic` reads it,
  `DialogueRuntime::end()` runs the first passing INFO of the last shown response's walk-away topic once (its fragments only, no line
  and no voice), `setWalkAwayTopics` / `SkyrimWorldView::setWalkAwayTopics` switch it on with `companions`.
- With `companions` the Blocking-branch rule of task 0298 (`setBlockingBranches`) and the fragment flush of task 0304 are on too:
  Eorlund's "What brings you here?" is the first line of a Blocking branch (C00 stage 30). **Combined, not duplicated:** the flush
  lambda is the same one `college` and `daedric quests` install (one mechanism, set idempotently).
- Harness: `tools/skycompanions_check/luatest/scripts/cmp/global.lua` has the steps `talkref <placement id>` (activate the neutral
  reference of a placement in the player's cell or its eight neighbours) and `actors` (list the actors of the cell).

## 2. In game (real clicks on the conversation box, quiet window, sound 0)

Start `@WhiterunWorld,29200,-4057,-2700` (the Jorrvaskr yard). **Stood in by the test aid** `story test quest = 4B2D9:20@65,A3EBC:200@72`
(C00 stage 20, and the training quest's stage 200, whose own fragment sets C00 stage 30: the Kodlak/Vilkas walk and the fight are
the task 0277 scenario). Everything after that is the game's data and scripts:

1. Eorlund (moved into the yard by stage 20's `MoveTo`; the `actor cell moves` setting) is talked to with `talkref` (Lua `activateBy`,
   not the activate key): `C00EorlundBlockEntryTopic` -> `C00EorlundIntroTopic` (`TIF__000A3E94` gives him the sword) -> "I'm a scrub"
   topic -> Goodbye.
2. On the end of that conversation `TIF__000BD687` starts `C00EorlundFollowupScene`, whose fragment lets Eorlund forced-greet
   (`C00GetAMoveOn`) -> his favour reply -> "happy to help" (`TIF__000A3E8E`: **the shield goes to the player, `SetStage(40)`**). The
   stage is set when that conversation ends (fragments of the last line run at the end; a Goodbye click is needed).
   Stage 40 (`QF_C00.Fragment_12`) moves Aela and Skjor to their spots in the basement.
3. The player is put into `WhiterunJorrvaskrBasement` (`cell` step, a harness teleport, not the doors), talks to Aela: "I have your
   shield." is offered (5 buttons, 4 without the shield), `TIF__000F11BF` gives her the shield and starts `AelaSkjorScene`
   (`SF_C00AelaSkjorScene`: three voiced lines), then her forced greeting `C00AelaCanYaTakeHimTopic` ("Do you think you could handle
   Vilkas in a real fight?"), the choice "I don't care for boasting." (`TIF__000A3E5C`), "Ah, a man of action. Here, let's have Farkas
   show you where you'll be resting your head." -> Goodbye.
4. `SF_C00AelaSkjorFarkasScene` (six phases, nine actions: Skjor "Farkas!", Farkas "Did you call me?", Aela "Of course we did,
   icebrain...", Farkas "New blood? Oh, hello. I'm Farkas. Come, follow me.") ends and its fragment sets **C00 stage 50**
   (`Fragment_13`: objective 40 done, `PlayerMetAela`).
5. Frames: `I:\Projects\OpenEden-work\followers\companions3\keep` (Eorlund's box with Goodbye, Aela's five choices, Aela's reply).

## 3. C01 "Proving Honor" (survey and one bridge run, NOT a play-through)

- C01 (`0x6E803`, `QF_C01_0006E803`, `c01questscript`) is started by the game's `CompanionsHousekeepingScript.CycleRadiantQuests`
  (`C01.GetStage() < 1` and `RadiantQuestsDoneInCurrentSegment >= RadiantQuestsUntilC01`, then `StartStoryQuest(C01)` = `SetStage(1)`).
  Stage 1 runs `Fragment_12` -> `c01questscript.Init()`. The Farkas tour's end is C00 stage 100 (`Fragment_14`: `PlayerMetFarkas`,
  `PlayerJoin`: the player joins `CompanionsFaction`, `RadiantMiscObjQuest.Start()`).
- Bridge run (`story test quest = 4B2D9:20@65,A3EBC:200@72,4B2D9:100@78,6E803:1@85,6E803:10@92`): C00 stage 100 runs `Fragment_14` (PlayerJoin) and C01 stage 1 runs `Fragment_12` (`Init`, 5 aliases filled) and stage 10 `Fragment_0` (CycleRadiantQuests) with no script error.
- **Not built:** Farkas' tour walk and its ending (stage 100 is set by the aid), the radiant quests that lead to C01 (the aid starts
  C01), the walk to Dustman's Cairn, the fragment of Wuuthrad and the Silver Hand ambush (`SF_C01AmbushScene`).

## 4. Red evidence (honest)

- `testskyrimsewalkaway.cpp` (5 tests) was written before the code by the first agent (no red run was recorded then). Red run now
  by mutation: with the `end()` block disabled and the `TWAT` case renamed, **2 of the 5 fail** (`anInfoKeepsItsWalkAwayTopic`,
  `theEndOfAConversationRunsTheWalkAwayInfosFragmentWhenTheSettingIsOn`); the other 3 test absence and pass by nature. Restored: 5 of 5.
- Unused helpers copied from the quest-greeting test were removed from the test file (they would fail `-Wunused-function` on GCC).

## 5. Decisions for toddhow

- `companions` now also turns on Blocking branches (`mBlockingBranches`, `setCountRefTypes`) and the fragment flush.
- The conversation's closing runs the walk-away topic's fragment (OURS: the game plays that response; here only its script work).
- No existing test edited.

## 6. Not run

The Linux gate, clang-format 14 (by hand), clang-tidy, the Lua integration tests, a fresh review, stage 30 by real play (aid), the
doors to the basement (teleport), Farkas' tour and C00 stage 100 by play, C01 beyond its first stage, saves during the chain.
One run (`h1`) of the bridge scenario ended with exit code 1 and no dump after about 3,000 frames (the crash catcher was disabled);
the identical rerun (`h2`) ran to the end; not investigated.

## 7. Runs
- Three runs of the whole C00 end chain (`g5`, `g6`, `g7`; g6 and g7 in parallel): the NTALK, SCENE, FORCEGREET, TIF and C00 stage lines
  (timestamps and poll/frame numbers cut) have the same md5 `d9a8be79479c8a32662fc6842509f818`. Trace kept: `I:\Projects\OpenEden-work\followers\companions3\keep\trace_g5.txt`.
- Morrowind unchanged against `I:/Projects/OpenEden-work/integ2/build`: `tools/worldobjects_check` OK (110 objects), the Balmora frame
  (`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600) `1c5d47ad0b5dc32d` for both.
- Scenario: `CELLID='@WhiterunWorld,29200,-4057,-2700'`, `story test quest = 4B2D9:20@65,A3EBC:200@72`, `actor cell moves = true`,
  `companions`, `quest alias packages`, PLAN `{6400,'go',29300,-2351,-2090,0},{7000,'talkref',108163},{12600,'cell',
  'WhiterunJorrvaskrBasement',1330,60,0,0},{13000,'talkref',108183}`, input file with clicks at 7400..12006 (Eorlund) and 13700, 15400,
  15900 (Aela), 20,000 frames. Click positions: Goodbye of a single-button box (640,389 short text, 640,397 longer).
