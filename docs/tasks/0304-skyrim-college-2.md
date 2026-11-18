# Task 0304: Skyrim, the College of Winterhold questline, second slice (follow-up of task 0292)

Same hand wrote tests and code, tests first where stated below. Windows only. Not pushed. Behind `[extension.skyrimse] college`
(off by default, declared in `extension.json` since task 0292) and the settings 0292 already needs (`papyrus`,
`quest alias packages`, `actor packages`, `journal`, `[Game] neutral combat`, `neutral inventory`). No new setting.

## 1. How the game enters Faralda's branch (the blocker of 0292)

`MG01FaraldaInitialBranchTopic` (0xAF0DD) is a Custom topic with NO FULL, the starting topic (SNAM) of a Blocking DLBR
(`MG01FaraldaInitialBranch`, Category Player); its INFO (0xAF0E3, Say Once, conditions GetStage < 30, IsInDialogueWithPlayer,
FaraldaProgress == 0, GetIsAliasRef 11) has no prompt and TCLT-links to three player topics. houseCARL's reverse scan finds
NOTHING that references the topic but its branch: no package (PDTO), no scene, no INFO link. The only way left is the
conversation's own start: **a quest's nameless branch start that nobody links to is its greeting** (the NPC says it when the
player begins a talk, before the Hello). RECALLED-VERIFY as an engine rule: nothing in the data states it; the data is
consistent with it (the other nameless starts are either TCLT targets, `MG01FaraldaYoureInBranchTopic` and
`MG01NiryaGreetingBranchTopic`, or package topics, the bridge and shout forced greetings).

Rules built (`DialogueIndex::questGreetingTopics`, `DialogueRuntime::begin`, only with `college`):

* a topic of category Topic, subtype CUST, no name, the start of its DLBR branch, **no INFO links to it**, and **an INFO of it
  names its speaker (GetIsID 72 or GetIsAliasRef 566)** (the shared dialogue quests, 437 topics, name none and passed for
  everybody: the first attempt greeted Faralda with `WESharedDialoguePrisonerSetFree`);
* ordered by priority, ties by the HIGHER form id first (OURS: Faralda's Initial branch and the Nirya greeting catch-all both
  have priority 50; the data names no tie rule). The first whose INFO passes is the greeting, else the HELO pick as before.

A second rule the same data needs (`DialogueRuntime::followNamelessLinks`): **a response that TCLT-links to a nameless topic
whose passing INFO has no prompt is not a player choice: the speaker goes on with it** (text and voice appended, up to 4
steps; the chained INFO's fragments run). "Excellent." (`MG01FaraldaEntryTakeTest`'s reply, INFO 0xB811B) links to the nameless
`MG01NiryaGreetingBranchTopic`; which of its eight INFOs follows depends on `NiryaInit`, which 0xB811B's own fragment sets, so
the script work queued by the line just shown is run first (`setFragmentFlush`, `PapyrusHost::tick(0)` through the glue).
The flush also makes a response's fragment effects (FaraldaProgress = 1) visible to the next topic list at once.

### The greeting order (after the merge with task 0298)

`DialogueRuntime::begin` has ONE ordered greeting step; the first rule that gives an INFO that passes speaks, each behind its
own setting:

1. task 0298, `civil war` (`setBlockingBranches`, which also turns on `setCountRefTypes`): the starts of the Blocking branches
   (`DialogueIndex::blockingStarts()`, DLBR DNAM bit 2), the lowest start-topic id first;
2. task 0304, `college` (`setQuestGreetings`): the quest greeting topics above, then;
3. the Hello pick (HELO, by priority) as before.

Both rules use the same `SelectionOptions` (the placement as the random pick number). With neither setting on the step is the
Hello pick alone.

## 2. Results (Windows, test game off-screen, real clicks on the conversation box, frames viewed)

Start `@Tamriel,110000,105500,-8300`, a harness step stands the player on the bridge (as in 0292), `tools/skycollege_check` and
a run script (`LOADARG` must be given: the check script defaults to `--start Winterhold`, which does not exist).

* Bridge forced greeting (0292), Goodbye by click.
* A second talk: **"Welcome to the College of Winterhold. I am Faralda ..." (voiced, subtitled, INFO 0xAF0E3)** with
  "What is this place?" and "Why are you out here?" (the third choice fails its own conditions).
* "What is this place?" -> reply, and "May I enter the College?" appears (FaraldaProgress = 1 by TIF__000AF0E5) ->
  "what is it you expect to find within?" with four motives -> "I want to use the power of ice and fire" (TIF__000AF0EC) ->
  "May I enter the College?" -> "a small test, if you will" -> "I'll take your test, then." -> "Excellent." followed by the Nirya
  spell branch: "The Mage Light spell is useful ... Can you cast one on the seal on the ground?" (the spell is `TestRandomVar`,
  a seeded `RandomInt(1,5)` of the script), choices "Okay, I can do that.", "I don't know that spell." -> "a mere 30 gold" ->
  "Okay, this is for the spell. (Give 30 gold)" -> TIF__0001F814 runs: **MG01 stage 20** (objective 20 displayed,
  `QF_MG01_0001F251.Fragment_3`), 30 gold, Magelight added to the player.
* **Faralda's test:** `combat test player spell = MageLight` (a developer setting: the attack control casts it), the player
  pitched 30 degrees at the seal; the harness's `abs` step now also puts the pitch into the player's own rotation (the combat
  reads the actor's pitch, not the camera's; the engine's pitch has the opposite sign of the camera's, the step uses
  `rotateX(+pitch)`). `COMBAT spell launch Magelight player ... pitch -0.524`, the shot ends 146 units from `MG01GateStoneRef`,
  `OnHit` goes to its alias script (`PAPYRUS spell ... ended at 109884 105957 -8512, OnHit sent to 1 script(s)`), the game's own
  `MG01StoneScript` plays `PlayAnim01` (`objectreference.playanimation self 00051190`) and evaluates Nirya's and Faralda's
  packages. NiryaSpellSuccess = 1.
* Talking to Faralda again: the greeting is her congratulation, **"Well done, indeed. I think you'll be a superb addition to the
  College. Welcome, Apprentice. I'll lead you across the bridge. Once you're inside you'll want to speak with Mirabelle Ervine,
  our Master Wizard. Please, follow me."** (INFO 0x1F811 followed by `MG01FaraldaYoureInBranchTopic`'s 0xB8119, voiced).

* **Stage 30:** after Goodbye on that line the game's own `QF_MG01_0001F251.Fragment_13` runs (`PAPYRUS quest 0001F251 set to
  stage 30`; objectives 20 to 24 done, objective 30 "Report to Mirabelle Ervine" displayed, the player joins `CollegeFaction`)
  and Faralda's own package walks her across the bridge ahead of the player (frame `y1`).
* **Determinism:** three runs of the whole scenario (`y1`, `y2`, `y3`, 91 trace lines of NTALK, NQUESTGREET, PAPYRUS quest, spell
  and dialogue lines, times and poll counts removed) are equal as sets; one harness line (`MQ step abs`) sits at a different place
  in one of the three (thread interleaving of a Lua print).
* **Morrowind unchanged:** `tools/worldobjects_check` against `integ2`'s build 110 objects OK, the fixed Balmora frame
  `1c5d47ad0b5dc32d` for the reference and this build.

Frames: `I:\Projects\OpenEden-work\bow6\mgrun\{q1,s1,t1,u1,v1,x1,y1}\user\screenshots`. Traces: `mgrun\y{1,2,3}.trace`.

## 3. Not reached (ranked gaps)

1. **Mirabelle, the tour, Tolfdir, stages 40, 50 and 200** (objective 40, Mirabelle's `MG01MirabelleStage30BranchTopic`, the tour
   scene, the ward lesson). Faralda walks across the bridge but the player was not taken to Mirabelle (the harness has no
   way inside: the gate is `SetOpen`, a tolerant no-op, and the College interior was not entered).
2. The gate does not open in the world (`TempGateRef.SetOpen(true)` of stage 30 is a no-op); Nirya's package walks and the
   Mage Light casts of the demonstration were not seen (the data gives the one NPC `Faralda` the packages named for Nirya).
3. Only Magelight was cast, with the developer setting and a harness pitch; Firebolt, Fear and Healing as the test spell
   are not run, and no spell menu lets the player choose one.
4. Controller A still does not activate the conversation box; mouse clicks do.
5. `Lock`, `ModFavorPoints`, `GlobalVariable.GetValueInt` are still tolerant no-ops.

## 4. Decisions for toddhow

1. The greeting rule is RECALLED-VERIFY (section 1) and ours at ties (higher form id). It is only active with `college`.
2. The flush runs the Papyrus tick (dt 0) from inside the dialogue code, only with `college`, after each response; the usual
   timing (fragments after Goodbye) is unchanged without it.
3. `Papyrus: spell ... ended at x y z, OnHit sent to N script(s)` is a new log line (every spell that ends on the world).
4. `NQUESTGREET` lines (the trace of the greeting choice and `followed <info>`) are logged with `college`.

## 5. Tests

`SkyrimQuestGreeting` (10, `apps/components_tests/skyrimse/testskyrimsequestgreeting.cpp`): off by default, a nameless start
greets, a member of a branch does not, a named start does not, Say Once, ties, a linked start does not, a start naming no
speaker does not, a reply links on to a nameless topic, the same without the setting.
Red against stubs: the first five (2 of 5 failed), the link rule (1 of 7), the follow rule (1 of 10). NOT run red: the
speaker-naming rule and the tie order (the code changed in the same step as the test). Green: `*Skyrim*:*Dialogue*:*Papyrus*`
and `SkyrimQuestGreeting.*`. No existing test edited.

## 6. After the rebase onto main `63cd0c00e1` (tasks 0298 and 0305)

Rebuilt (`components-tests`, `skyrimse`, `openmw`), `*Skyrim*:*Dialogue*:*Papyrus*:*Quest*:*Extension*` 3,584 pass. The College scenario
runs as before (Faralda's Welcome, stage 20, the cast reaches the stone's script, stage 30). Task 0298's `tools/skycivilwar_check`
with its PLAN and `join_input.txt` (`civil war` and `quest alias packages` on, 14,800 frames) still runs the chain: quest
`CW` 0xD3C5F goes to stages 7, 10 and 20 and `CW01A` (0xD517A) starts. Conflicts were in the greeting step (see above), the
`world_view` setters and CLAUDE.md; `setCountRefTypes` is kept (inside `DialogueRuntime::setBlockingBranches`).

