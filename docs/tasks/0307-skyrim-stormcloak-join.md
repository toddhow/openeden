# Task 0307: Skyrim Civil War, joining the Stormcloaks (first slice)

Branch `stormcloak-0307` from local `main` at `a703ddd3bc` (task 0298's Legion path). Windows only, not pushed. Same hand wrote the
test and the harness; **no pack or engine code changed** (see section 2), so there were no red runs. No fresh reviewer. Everything is
behind task 0298's setting `[extension.skyrimse] civil war` (and `quest alias packages`, `actor packages`, `papyrus`, `[GUI] rmlui`).

## 1. The game's chain (houseCARL read of `Skyrim.esm`, the decompiled `QF_CW00B`, `QF_CW01B` and `TIF__*` scripts)

1. **CW00B "Stormcloak Introductions"** (priority 71, start-game enabled, RunOnce; aliases Galmar 0 and Ulfric 2). Galmar has a Blocking
   branch (`CW00BGalmarBlockingTopic`): his first line, "I'm not sure you belong here, friend." (`TIF__000E1B06` = `SetStage(7)`).
2. Stage 7 (`QF_CW00B.Fragment_9`): the map-table scene is stopped and Ulfric's alias package `CW00BUlfricForcegreetPackage`
   (ForceGreet, condition `GetStage(CW00B) == 7`) makes him force-greet: "Only the foolish or the courageous approach a Jarl without
   summons... Do I know you?", choices "I was at Helgen." and the linked topics through "Destined for the chopping block ...", "I was set
   free ..." to "Speak with Galmar." (`TIF__000E1B25` = `SetStage(10)` when the conversation ends; `QF_CW00B.Fragment_4`: objective 2
   "Join the Stormcloak rebellion" displayed).
3. Galmar again (stage 10): "Hmm. Helgen, eh? ...", "But first, tell me. Why's a foreigner want to fight for Skyrim?", "Can't fault
   anyone for that", "All right. But before I can put you to use ... a little test", "What kind of test?" / "I can handle anything", "I'm
   sending you to Serpentstone Island", "What's at Serpentstone Island?", the four-choice menu, and "I'm off to kill that Ice Wraith. I'll
   be back soon." (`TIF__000E1AE8` stage 15, `TIF__000E1AE7` bravado flag, `TIF__000E2D28` and `TIF__000E1AF1`/`1B04` stage 20).
4. Stage 20 (`QF_CW00B.Fragment_6`): objective 2 completed, **`CW01B.SetStage(1)`** (`QF_CW01B.Fragment_0`: Ice Wraith reset, objective
   1 "Kill the Ice Wraith" displayed), the player-level help items, the map-table scene.
5. CW01B stage 100 (the wraith's death, `CW01BIceWraithScript`; `Fragment_2`): objective 100 "Return to Galmar". Galmar's blocking branch
   `CW01BGalmarGreetTopic`: "You're alive. I owe Ulfric a drink ...", "Oath?", "I'm ready to take the Oath.", the oath lines
   (`MQ102BStormcloakOath1` to `4`, `CW01BStormcloakOath3b`), `TIF__00099DA7` = stage 160 then 200 (`Fragment_5`, `Fragment_4`: the uniform
   is added, `CW02B.SetStage(10)`, `CWs.PlayerTookOath(2)`, the map-table scene, CW01B stops).

## 2. What the run showed: nothing was missing

All of it ran with the pieces of task 0298 (Blocking branches, alias packages by quest priority, forced greetings by package, the
quest runtime) and the earlier slices; no gap needed code. Observed in game (`tools/skycivilwar_check/check.sh`, section 4):
Galmar's blocking line and stage 7; Ulfric's forced greeting (5 turns) and stage 10 when it ends; Galmar's 8 turns and stages 15 and 20;
CW01B started with 4 aliases filled and stage 1; the journal lists "Joining the Stormcloaks" with the text "To prove myself worthy ..." and
`[ ] Kill the Ice Wraith`, and "Stormcloak Introductions" with `[x] Join the Stormcloak rebellion`; after the hand-set stage 100 Galmar's
"You're alive ..." greeting, "Oath?", "I'm ready to take the Oath.", the four oath lines (the clicks land on the choice rows), then
stages 160 and 200 run their fragments, CW01B completes (journal: Completed (1)) and **"The Jagged Crown" (CW02B) appears running,
"Talk to Galmar"**.

## 3. Test

`apps/components_tests/skyrimse/testskyrimsestormcloakreal.cpp`, one gated real-data test (`OPENEDEN_SKYRIMSE_DATA`, skipped without it):
CW00B (priority 71, start-game enabled, stages 7, 10, 15, 20, Ulfric's alias with his forced-greeting package, Galmar's alias), CW01B (not
start-game enabled, stages 1, 100, 150, 160, 200, objectives 1, 100, 160). **It passed at once: it pins data facts the run depends on,
it was not run red** (there was no code to be red against). 13 tests with the 0298 ones pass.

## 4. In game

`tools/skycivilwar_check/check.sh` with `TARGET=skyrimse:b82216` (Galmar), `CELLID=WindhelmPalaceoftheKings`,
`INPUT=stormcloak_input.txt`, 20800 frames, `civil war = true`, `quest alias packages = true`, `story test quest = E2D29:100`,
`story test delay = 62` and `PLAN="{2500,'talk'},{6300,'talk'},{12500,'talk'}"` (the header of `check.sh`). Real mouse clicks through
`OPENEDEN_RMLUI_INPUT_SCRIPT`; the dialogue needs about 500 frames a turn. **Three runs give identical traces** (68 lines of `NTALK`,
the quest stage lines, the dialogue fragments and `FORCEGREET`, poll counts included; md5 `31d058c5e56e` of the normalised lines).
Frames viewed: Galmar's blocking line, Ulfric's forced greeting and his last line, Galmar's test, the five-choice menu, the "You're
alive" greeting, the first oath line, the "Now you're one of us" turn and the journal before and after.

## 5. Decisions and limits for toddhow

1. **The kill of the Ice Wraith is hand-set** (`story test quest = E2D29:100` runs the real stage fragment at script time 62): the
   wraith is an ice wraith creature the pack has no profile for and Serpentstone Isle was not visited. The rest after stage 100 is the
   game's own.
2. Not applied: `faction.setally/setenemy` and the Stormcloak faction rank (`CWs.PlayerTookOath` only shows as tolerant natives), the
   Stormcloak uniform (`AddItem` of a form list was not checked in the inventory), the map-table scene after the oath, walking to the
   island, and CW02B and everything after it.
3. The harness keys on the layout of the dialogue box (a choice row's y is `368 + 8 * lines` for one choice plus Goodbye); other text
   would move the clicks.

## 6. Not run

The Linux gate and GCC `-Werror`, clang-format 14 (one small file formatted by hand), clang-tidy, the Lua tests, a fresh review, the
walk to Serpentstone Isle and a real kill, a save and load in the middle of the join, the Legion path against this build (only the
0298 unit tests). No existing test edited.
