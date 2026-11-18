# Task 0298: Skyrim Civil War, joining the Legion (first slice)

Branch: this worktree's branch, rebased onto local `main` at `315eb033ae`. Same hand wrote tests and code (the unit tests first and
run red against stubs, the rest by the game), no fresh reviewer. Windows only, not pushed. Everything is behind the new pack
setting `[extension.skyrimse] civil war` (off by default, declared in `extension.json`; needs `papyrus`, `quest alias packages`,
`actor packages`, `[GUI] rmlui`).

## 1. What the data says (houseCARL read of `Skyrim.esm`, then the decompiled scripts of `Skyrim - Misc.bsa`)

The game's own chain for joining the Legion, with nothing hand-written:

1. **CW00A "Imperial Introductions"** (priority 71, start-game enabled). Its alias `Tullius` gives General Tullius the package
   `CW00ATulliusForcegreet` (ForceGreet template, condition `GetStage(CW00A) == 7`).
2. Talking to **Legate Rikke** before stage 10: she has a *Blocking* dialogue branch (DLBR `CW00RikkeBlocking`, flag 2) whose start topic
   `CW00RikkeBlockingTopic` has no name: its first INFO that passes is what she says instead of Hello ("Are you lost?", INFO
   `0D514D`, fragment `TIF__000D514D.Fragment_2` = `SetStage(7)`).
3. Stage 7: `QF_CW00.Fragment_7` stops the map-table scene and evaluates Tullius' packages; his forced greeting (topic
   `CW00TulliusForcegreetTopic`, branch `CW00TulliusForcegreet`, also Blocking) says "Are my men now giving free reign ...", choices
   "I believe we've already met." and "I was at Helgen.", linked topics lead through "Right... Helgen..", "One of the prisoners ...",
   "I suppose that's true. Fine. Why don't you have a chat with Legate Rikke?" to `TIF__000D5136` = `SetStage(10)` (stage 10 =
   `QF_CW00.Fragment_4`: objective 1 displayed, `CW.PlayerGotIntro = 1`).
4. Rikke again: INFO `0D514E` ("You survived Helgen? ...", choices "I can handle anything you throw at me." and "What kind of
   test?"), then the next conversation `0D514F` ("Are you ready to test yourself at Fort Hraagstad?", prompt "About that test..."),
   "What's at Fort Hraggstad?" (INFO `0D5138`, four links), and "Consider that fort already yours." (INFO `0D5153`,
   `TIF__000D5153` = `SetStage(20)`).
5. Stage 20 (`QF_CW00.Fragment_6`): objective 1 completed, `CW01A.SetStage(1)`, the map-table scene starts. CW01A stage 1
   (`QF_CW01.Fragment_0`): objective 1 "Clear out Fort Hraggstad" displayed.

## 2. What was missing, found by running it (three gaps)

| Gap | Cause | Fix (behind `civil war`) |
| --- | --- | --- |
| Rikke said her generic Hello ("I'm waiting") | `DialogueRuntime::begin` took the greeting from the `HELO` topics only; a Blocking branch (DLBR DNAM bit 2) starts the conversation instead | `DialogueIndex::blockingStarts()` (start topics of Blocking branches, sorted by id); `begin` tries them first (`setBlockingBranches`) |
| Tullius never force-greeted after stage 7 | an actor's alias packages were tried in quest-id order, so CW's `CWMapTableSandboxSolitude` (no real condition) always won; CW00A has priority 71, CW 70 | alias packages tried by quest priority (high first), ties by quest id (`questPriorityBefore`, `quest_priority.hpp`) |
| Rikke offered "I already cleared it out." instead of "Consider that fort already yours." | `GetRefTypeAliveCount(Fort Hraggstad, CWFortMonster)` (fn 592) was always 0: the location's LCSR special references were never counted | `LocationIndex::countSpecialRefs`, `SkyrimConditionWorld::refTypeAliveCount` counts the listed references not placed dead |

## 3. Tests (all new, none of the existing ones edited)

`apps/components_tests/skyrimse/testskyrimsecivilwar.cpp`, written first against stubs and run red:

- `SkyrimBlockingBranch` 6 tests: **3 of 6 failed** against the stub (the 3 that passed pin the off state and absence).
- `SkyrimQuestPriority` 3 tests: **3 of 3 failed**.
- `SkyrimFortCount` 3 tests: **2 of 3 failed** (the third, an unknown location, passes by nature).

After the code all 12 pass. The scenario (section 4) is the test of the glue; the engine side was not touched.

## 4. In game (`tools/skycivilwar_check/check.sh`, Skyrim neutral session in Castle Dour, real mouse clicks through
`OPENEDEN_RMLUI_INPUT_SCRIPT`)

See the changelog row for the runs and numbers. The scenario's input files and the PLAN are in the work folder
(`I:/Projects/OpenEden-work/combat3/cw/in9.txt` and `run.sh`), copied to `tools/skycivilwar_check/` (`join_input.txt`).

## 5. Decisions and limits for toddhow

1. A Blocking branch now says its first INFO that passes instead of Hello (with `civil war` on), for every actor and every Blocking
   branch in the five masters, not only Rikke (the CK's Blocking flag: RECALLED-VERIFY; the data fits: the CW00 branches are all
   Blocking). The lowest start-topic id wins when several pass (the game's order is not in the data).
2. Alias packages are ordered by quest priority only with `civil war` on; the other slices keep their order. RECALLED-VERIFY: the
   priority 71 against 70 is what makes CW00A's forced greeting win.
3. `GetRefTypeAliveCount` counts a special reference as alive unless the placed record is dead at the start; kills in the
   session are not counted (the 25 bandits of Fort Hraggstad cannot be cleared by this build's fight yet: not tried).
4. The hand-set items: none in the final scenario except the clicks. The quest CW00A was already running (start-game enabled).
5. Not built: the rest of CW01A (the fort, the 25 bandit aliases, the report to Rikke, the oath scene `MQ102ALegionOath1..4`), Galmar
   and the Stormcloak path (CW00B, CW01B), the Legion armour and the faction rank.

## 6. Not run

The Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua tests, a fresh review, the Stormcloak path, any
outcome after the player leaves Castle Dour.
