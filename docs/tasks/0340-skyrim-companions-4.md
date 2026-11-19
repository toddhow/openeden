# Task 0340: Skyrim Companions, fourth slice ("Proving Honor", C01)

Design: `docs/tasks/0264-skyrim-companions.md`, `0277-skyrim-companions-2.md`, `0315-skyrim-companions-3.md`. Same hand wrote tests and code,
no fresh reviewer. Windows only, local branch `companions4-0340`, not pushed. Behind `[extension.skyrimse] companions` (no new
setting).

## 1. What the data says (C01, QUST `0x6E803`, `QF_C01_0006E803`, `c01questscript`)

- C01 is started by `CompanionsHousekeepingScript.CycleRadiantQuests` (not built: the aid starts it, as in task 0315). Stage 1 runs
  `Init` (the `Observer` alias is forced to Farkas: "it's always Farkas now"), stage 10 "Speak to Skjor".
- Skjor's talk: the blocking-branch entry `C01SkjorEntry` ("Ah, there you are." / "You wanted to see me?"), `C01SkjorIntroBranch`
  ("I did. Your time, it seems, has come." / "What do you mean?"), `C01SkjorPushOutTheDoor` (the scholar; its TIF sets
  `SkjorGaveLowdown`), then the topics "What does this have to do with me?" and "I would be honored to retrieve it." (INFO `0x81DE2`,
  flagged InvisibleContinue, links to the nameless `C01SkjorObserverSplit`; INFO `0x6F6C3` "Farkas will be your Shield-Sibling..." is
  flagged **Goodbye AND InvisibleContinue** and links to the nameless `C01SkjorSendoff`; its end fragment `TIF__00071DE3.Fragment_0`
  sets **stage 20**).
- Farkas (the Observer): his first line sets **stage 30** (`TIF__00071DE8`; `QF_C01.Fragment_2`: objective 30 "Retrieve the
  fragment", `SetPlayerTeammate(true)`, `SwapFollowers`, the Dustman's Cairn map marker). Stages 30 to 50 give him the alias package
  `C01ObserverFollowPackage` (Follow PlayerRef); `C01ObserverWaitOutsidePackage` when he is dismissed.
- Dustman's Cairn: `DustmansCairn01` (the ambush: the five Silver Hand `C01SilverHandAmbusher1..5` at about (-300, 4100, -950) and the
  levers `dunDustmansCairnLever` on REFRs `0x236f5` at (-2385, 4457, -880) and `0x3C847` at (-928, 4725, -881); the second lever has
  `secondLever` set), `DustmansCairn02` (the fragment `0x6E807`, a `YsgramorsBladePiece07`, at (-2754, -1267, -1462)). The
  ambush is the scene `C01AmbushScene` (`SF_C01AmbushScene`: Farkas drops his weapon, `ObserverDoTransform` casts `WerewolfChangeFX`,
  `SetRace`, `FightStart`; `C01QuestScript.FakeFight` is true, so the game's own script kills the five), then `C01PostAmbush`.
- Skjor on return: `C01SkjorUponReturnBranchTopic` ("Ah, you're back." -> `C01GetReport` "I've brought the fragment."); stage 200
  (`Fragment_4`) returns the fragment; stage 40 (`Fragment_3`) starts C02 ("Take Care of Business" is the Silver Hand quest, C02).

## 2. Faults found by running it (all behind `companions`, found with real clicks on the conversation box)

| # | Fault | Cause (measured with `dialogue debug topic` and `conditions debug quest`) | Fix |
| --- | --- | --- | --- |
| 1 | Skjor greeted with the generic "Hmm?" | the entry line of a Blocking branch tests `IsInDialogueWithPlayer == 0` (the game picks it BEFORE the actor is in dialogue); the runtime marks the conversation open at the start | the blocking starts are tried again with the conversation not yet open when none passed (`DialogueRuntime::begin`, additive) |
| 2 | after "What do you mean?" the topics "What does this have to do with me?" and "I would be honored to retrieve it." never appeared | they test `GetVMQuestVariable(C01, ::SkjorGaveLowdown_var) == 1`, which the previous line's fragment sets; the plain topic list was judged before the queued fragments ran (only `offerLinks` and `followNamelessLinks` flushed) | `offerTopics` runs the fragment flush first (every setting that installs the flush) |
| 3 | after "Farkas will be your Shield-Sibling..." the sendoff (and stage 20) never came | that INFO is flagged Goodbye AND InvisibleContinue: the follow loop stopped at the Goodbye | `followNamelessLinks` goes on while the last INFO is flagged InvisibleContinue (`mLastContinues`) |
| 4 | the branch start `C01SkjorObserverSplit` is nameless | needs the quest-greeting rule of task 0304 | `companions` now also sets `setQuestGreetings(true)` |
| 5 | Farkas does not follow after stage 30 | `C01ObserverFollowPackage` is a Follow package naming PlayerRef; `companions` did not switch on `setFollowPlayerPackages` (task 0310) | switched on with `companions` (the package list of Farkas is right: the three C01 packages come first); **but the three packages are still not chosen in the brain's evaluation (section 5, open)** |
| 6 | the ambush scene's first phase never ended while Farkas is the player's follower | a follower never evaluates packages (task 0196), so the scene's forced Travel package (`C01ObserverSeesYourFate`) never ran | `PackageBrain::update`: an actor with a FORCED package (a scene's) does not follow while it is forced and follows again afterwards |

Also a developer aid: the `CONDDEBUG COND` line prints the string parameter of a condition (`s2 ::SkjorGaveLowdown_var`), and with
`OPENEDEN_PACKAGE_TRACE=<base id>` every package evaluation of that actor says what it chose (`PKCHOSEN ... evaluated among N`).

## 3. In game (`tools/skycompanions_check`, Jorrvaskr basement, real clicks on the conversation box, quiet window, sound 0)

Scenario (`CELLID=WhiterunJorrvaskrBasement`, `companions`, `quest alias packages`, `actor cell moves`, `dungeon mechanics`, the
`story test quest` aid `4B2D9:20@65,4B2D9:40@70,4B2D9:100@75,6E803:1@85,6E803:10@92,6E803:40@225`, `follower test recruit = Farkas`,
`summons test kill = C01SilverHandAmbusher1..5@190`, `combat trace`, 20,800 frames; the runner, plan and clicks are in section 7):

1. **Real:** Skjor's greeting ("Ah, there you are.", voiced `c01_c01skjorentry_000c0085_1.fuz`), "You wanted to see me?", "What do you
   mean?" (`TIF__0006F6BC` runs), the scholar's story with the two topics, "I would be honored to retrieve it.", the chained lines
   (`C01SkjorObserverSplit`, `C01SkjorSendoff`: `TIF__00071DE3` runs) and Goodbye: **C01 stage 20** (`Fragment_1`).
2. **Real:** Farkas's greeting and his first choice: `TIF__00071DE8` sets **stage 30** (`Fragment_2`: Farkas a teammate, the Dustman's
   Cairn marker).
3. **Stand-ins:** the player is teleported into `DustmansCairn01` at the lever (a harness step), and Farkas is the player's follower by
   the `follower test recruit` aid (his own Follow package is not chosen, section 5), so he is carried through the teleport
   ("Neutral follower 108179 carried to DustmansCairn01").
4. **Real:** the lever REF `0x236F5` (Lua `activateBy`; the game's own `dundustmanscairnlever` script runs, `GetTriggerObjectCount` is a
   tolerant 0): `AmbushLeverPulled`, Farkas moved to the compensation point, `Scene.Start 000A6F59` ("SCENE 000A6F59 begins (11 phases,
   34 actions)"), `AmbushPrep` enables the five Silver Hand, and the scene runs **phases 0 to 10 with the real voiced lines and
   subtitles** ("Now look what you've gotten yourself into...", "It's time to die, dog.", "We knew you'd be coming here.", "Your
   mistake, Companion.", ... "None of you will be alive to tell it."), then `Fragment_3` (`ObserverDoTransform`) and ten script seconds
   later `ObserverActuallyTransform` -> `FightStart`.
5. **Not reached:** the fight itself and what follows (section 5): the Silver Hand did not die, `AmbusherKilled` and the `C01PostAmbush`
   scene did not run, no werewolf body, no fragment pickup.
6. **Stand-in (aid):** stage 40 (`QF_C01.Fragment_3`: objective 30 done, "Return to Jorrvaskr", `C02Kicker.Start`) at script second 225;
   the harness teleports back to the basement (Farkas carried).
7. **Real:** Skjor's greeting on return ("Ah, you're back.", the "I've brought the fragment." reply: `TIF__000681EF` runs, "I'll speak with
   Farkas about how you handled yourself. Come back later.") and, when the box closes, **stage 200** (`QF_C01.Fragment_4`; 0 script errors).

Three runs started at once (`g1`, `g2`, `g3`) give the same 83 trace lines (NTALK turns without poll numbers, the quest stage and
fragment lines, the scene lines, the carry line, `DUNGEON activated`), md5 `03625b4a422ba4ac2871a78db49ff3c0`; each exits 0 with 0 crash
dumps, 0 error lines and 0 script errors. Frames viewed: `I:\Projects\OpenEden-work\combatai\keep0340` (Skjor's box, the sendoff text,
Farkas's choices, the ambush subtitles, Skjor's return) and the trace `trace_g1.txt`.
Morrowind unchanged against `I:\Projects\OpenEden-work\integ2\build` (a build of `main`, not of the exact parent): `tools/worldobjects_check`
111 lines OK (110 objects) and the fixed Balmora frame (`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600)
`1c5d47ad0b5dc32d` for both.
Tests: `components-tests` filter `*Skyrim*:*Dialogue*:*Papyrus*:*Scene*:*Package*:*Combat*:*Extension*:*Neutral*:*Frozen*:*Journal*:*Follower*`
4,904 pass; `openmw-tests` filter `*Neutral*:*Extension*:*Skyrim*:*Combat*` 1,224 pass.

## 4. Red evidence (honest)

- `SkyrimBlockingBranch` (`testskyrimsecivilwar.cpp`, tests appended): the entry line that needs the actor not yet in dialogue: **1 of 4 new
  tests failed** against the old code (the other 3 pin the old behaviour and the setting off); the fragment flush before the topic list:
  **1 of 2**; Goodbye with InvisibleContinue: **1 of 2**. `SkyrimFollowerBrain` (`testskyrimsefollowers.cpp`, tests appended): a forced
  package over following: **1 of 2**. Each run red against the unchanged code before its fix.
- Not red-first: the pack wiring (`companions` now calls `setQuestGreetings` and `setFollowPlayerPackages`) has no unit test, and the
  `CONDDEBUG` string and `PKCHOSEN evaluated` trace lines; the game run is the evidence.
- Existing tests edited: none. Two helper lines of `testskyrimsecivilwar.cpp`'s `InfoSpec` and `infoRecord` (a flags field, default 0)
  were added so the new tests can set INFO flags; no expectation changed.

## 5. What is not done (ranked)

1. **Farkas's own Follow package is not chosen.** The data and the list are right (`PKTRACE` shows `C01ObserverWaitOutsidePackage`,
   `C01ObserverCheckOutCrapInAmbushSpace` and `C01ObserverFollowPackage` first in his list; `conditions debug quest = 6E803` shows the six
   conditions of the wait package all true in a dialogue-world evaluation), but the brain's evaluation (`PKCHOSEN ... evaluated among 10`)
   keeps returning `C00FarkasHangAroundGiant`. Cause not found; suspect `SkyrimWorldView::conditionsPass` reading the alias `Observer`
   (filled by a script's `ForceRefTo`) through a lookup that does not include it. The harness stands in with the follower aid.
2. **The ambush fight and the werewolf:** `FightStart` ran, `Actor.ForceActorValue`/`SetRace`/`DropObject`/`SetInvulnerable` are
   unregistered (tolerant zeros), so no werewolf body exists and nobody died; the `summons test kill` aid at combat second 190 did not fire in
   the run (no `COMBAT test kill` line: the combat clock was behind). `AmbusherKilled`, `C01PostAmbush` (`ObserverTurnBack`) did not run.
3. **The fragment pickup** (`0x6E807` in `DustmansCairn02`, the key and chest, the end controller with its 17 draugr) is not walked; stage
   40 is set by the aid. The radiant quests that start C01 (`CycleRadiantQuests` after C00 stage 100) are not run (the aid starts C01, as in
   task 0315). Farkas's dismissal topic and the Wait package are not exercised.
4. The doors to the cairn are a harness teleport; `GetTriggerObjectCount` is a tolerant 0 and the lever's `FullPush` animation is
   instant (no behaviour clip in the neutral world); the ambush trigger boxes were not reached.
5. Skjor's return at stage 40 needs the fragment item in the player's inventory for the game's `RemoveItem` (a no-op without it).

## 6. Decisions for toddhow

- `companions` now also turns on quest greetings (nameless branch starts and chained nameless links), the follow-player packages and
  the fragment flush before the plain topic list (the flush is shared with every setting that installs it: `college`, `daedric quests`,
  `thieves guild`, `bards college`, `main quest whiterun`, `civil war`; it only runs queued fragments earlier).
- A Blocking branch entry is tried a second time with the conversation not yet open when no start passed (additive).
- An actor with a scene-forced package stops following until the scene lets go (every follower during any scene; additive).
- A line flagged Goodbye + InvisibleContinue continues into its link before the conversation ends (settings with quest greetings only).
- No existing test edited.

## 7. Scenario files

`I:\Projects\OpenEden-work\combatai\r.sh` (runner), `final.sh` (settings and plan), `c4/in14.txt` (clicks), `cmp.sh` (trace comparison).
The click positions depend on the box layout: Skjor's first topics (600, 383), "I would be honored" (640, 416), the sendoff's Goodbye
(640, 436), Farkas's first line (600, 383) and his Goodbye (643, 511), Skjor's return topic (600, 383) and Goodbye (640, 397).
Plan: `{7000,'talk',108176},{9300,'talk',108178},{10800,'cell','DustmansCairn01',-2385,4380,-880,0},{11300,'activate','@0x3fffffff000236f5'},
{17500,'cell','WhiterunJorrvaskrBasement',1330,60,0,0},{18000,'talk',108176}` (the global script gained the steps `near` and `activate`).

## 8. Not run

The Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua integration tests, a fresh review, the whole test binaries.
