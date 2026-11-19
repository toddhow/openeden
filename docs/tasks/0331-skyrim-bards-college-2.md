# Task: Skyrim Bards College, second slice (task 0331, follow-up of 0296)

Status: ordered by the coordinator on 2026-11-18. Windows only. Local commits on branch `bards2-0331` from `e400a9f4ec`, not pushed.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S7); tasks 0296 (Bards College start), 0304 (nameless links),
0315 and 0325 (walk-away topics), 0310 (Daedric: the same follow of a nameless link).
Goal: carry MS05 "Tending the Flames" past stage 75 through the game's own dialogue, scenes and Papyrus.
Setting: `[extension.skyrimse] bards college` (off by default, declared in `extension.json`, README line in `tools/testprofile`; needs
`papyrus`, `quest alias packages`, `journal`).

## 1. Why MS05Start's stage 10 was never offered (item 1 of 0296's ranked gaps): not a fault

The topic `MS05GenericJoinTopic` ("What do you do around here?") has INFOs 0x534F2, 0x534F3 and 0xBB964. Their conditions (read with
houseCARL, `GetIsID`): 0x534F2 requires NOT Viarmo and NOT Giraud (and a list of other named bards) and membership of JobBardFaction;
0x534F3 requires NOT Viarmo and one of Ataf, Jorn, Inge Six Fingers, Pantea Ateia, Illdi. **The topic is for the other bards of the
college; the data excludes Viarmo and Giraud on purpose.** Pantea (`skyrimse:b78495`), Jorn, Inge, Ataf and Illdi all stand in
`SolitudeBardsCollege`. In game, with no new code: Pantea offers "What do you do around here?", the line runs `TIF__000534F3`, and after
Goodbye `quest 00042868 set to stage 10` runs `QF_MS05Start.Fragment_0` (objective "Investigate the Bards College").

## 2. The chain that now runs through the game's own dialogue, quests and scripts

Real mouse clicks on the conversation box (frames in `I:\Projects\OpenEden-work\bow6\b331\r\u1\user\screenshots`):

1. Pantea: MS05Start stage 10.
2. Viarmo: apply, "What do you need me to do?", "And that's where I come in?" (INFO 0x6035D): MS05 stage 75, `Fragment_4`
   (MS05Rumor stage 20, the Dead Men's Respite marker added to the map).
3. **Stand-in: the player is TELEPORTED into `DeadMensRespite01` (-2136, -1843, -2300) and the real activation (`activateBy`) takes
   the book `MS05KingOlafBook01` (REFR 0xAE325).** The existing pickup path gives it to the inventory and the game's own scripts set
   **MS05 stage 100** (`Fragment_5`: "Find King Olaf's Verse" done, "Return to Viarmo" displayed). The dungeon itself (ghost bards,
   traps, King Olaf's fight, the doors) is NOT played.
4. **Stand-in: teleport back to `SolitudeBardsCollege`.** Viarmo offers "I found King Olaf's Verse." (INFO 0x534F0 removes the book
   with `RemoveItem`, "I have to admit I didn't think it would actually be there...", "What does that mean?" (0x534E9), "Can't we just make
   up missing parts of the verse?" (0x534DD), 0x534F8 whose fragment sets **stage 125 when the box closes**).
5. Viarmo again: "We can do this. What's the first verse?" (stage 125 only: its INFO condition), the recitation, the verse 2 prompt and
   its three choices, `TIF__000534FA` sets `Verse2Choice` on `ms05questscript`, "Okay, what's next?", verse 3 and the verse 4
   choices, `TIF__00053505` (`Verse4Choice`), the closing lines, `TIF__000534FC`: **stage 135** ("Meet Viarmo at the Blue Palace"
   in the journal).
6. **Stand-in (hand-set, `story test quest = 053511:150@70`): stage 150**, which runs `QF_MS05.Fragment_0`: `MS05CourtScene.Start()`
   and `MS05DungeonQuest.SetStage(255)`. With the player teleported into `SolitudeBluePalace` the court scene `0005351E` begins (14
   phases, 26 actions) and Elisif speaks phase 1 ("Ah, Viarmo. I assume you are here to petition for the reinstatement of the
   Burning of King Olaf Festival."); phase 2 waits for Viarmo, who is not there.

## 3. The fault found and fixed (one mechanism)

After the first verse the dialogue went back to the top menu: the verse 2 prompt (INFO 0x54F88, a response with no name) was
followed by `followNamelessLinks` (task 0304) only with `college`, `daedric quests`, `dawnguard` or the main quest on, and when it
WAS followed (run with `daedric quests` on) the loop went on to the nameless topic `MS05PoemExit` and said its line ("You're
leaving? Okay, we'll just work on the verse in a bit.") as a continuation, losing the three choices. **That topic is the response's
walk-away topic (TWAT 0x62169's topic), said when the player LEAVES.** `offerTopics` and `offerLinks` already skip it
(`hidesWalkAway`, tasks 0325); `DialogueRuntime::followNamelessLinks` did not. Now it skips the walk-away topic of the response just
said, as the menus do. The pack setting `bards college` turns on the three switches Viarmo's poem needs, ONE mechanism shared with
the Daedric and College blocks: quest greetings (the follow of a nameless link), walk-away topics and the fragment flush (a
response's fragments, here the `Verse2Choice` writes, run before the next choice is judged).

## 4. Tests

`apps/components_tests/skyrimse/testskyrimsebardspoem.cpp` (new, 2 tests, generated plugin): the prompt is followed and its two
named choices are offered without the walk-away line; a choice answers as its own topic. **Red against the unfixed code: 1 of 2
failed** (the unfixed code spoke "You're leaving?" and gave 3 topics, `red1.txt` in the work folder); the second pins the choice
and passes by nature. Green after the fix; `SkyrimDaedricContinue*` and `SkyrimQuestGreeting*` (15 tests with them) still pass. No
existing test edited.

## 5. Not reached (ranked)

1. **Viarmo's walk to the Blue Palace** (alias package `HeadToCourt` with the end fragment `PF_MS05ViarmoHeadToCourtPacka` that sets stage
   145): needs an alias package that travels across cells (task 0277's carry exists for scene packages only). Stage 145 has no fragment
   of its own; stage 150 (the court trigger's work) is hand-set.
2. The court scene's later phases (they wait for Viarmo), stage 175 ("The court has accepted the verse"), the induction, Jorn, the
   coat theft from Tullius (`MS05TulliusCoatFavorTopic*`), the festival (`MS05KingOlafsFestival`), the effigy, stage 300.
3. The poem's PERSUADE choices (`pFDS.Persuade`: INFOs 0x534F7, 0x53502; not clicked), the Patronage totals (`TIF__000534EE`, `0x534F6`,
   `0x54F8B` add to `Patronage`; the final reward by it was not reached).
4. Dead Men's Respite itself (ghost bard scenes `SF_MS05_BardGhost*`, the quest `MS05_dunDeadMensRespiteQST` 0x16C8D, the rotating
   doors, Olaf's fight): the book is taken by a teleported player.
5. The 0296 gap 2 (the apply topic still offered after stage 75) was not looked at.

## 6. Decisions for toddhow

- A new setting `bards college` rather than reusing `college` (which also turns on cell moves, carried travellers and spell events for the
  Winterhold quests); it only enables the dialogue switches. The follow now skips a walk-away topic in EVERY block that turns
  walk-away topics on (companions, college? no: `setWalkAwayTopics` is set by `companions` and `bards college` only; `college`
  and `daedric quests` leave it off, so their behaviour is unchanged).
- Verse choices were clicked on fixed screen coordinates (the box layout depends on the text length).
