# Task 0345: Skyrim Daedric quest "A Night to Remember" (Sanguine, DA14), first slice

Follow-up of tasks 0310 (Barbas, `daedric quests`), 0332 (Waking Nightmare), 0338 (The Black Star) and 0342 (Laid to Rest). Same hand
wrote tests and code, tests first and run red where the API existed (section 5); no fresh reviewer. Windows only. Branch
`nighttoremember-0345` from `3b9a041ddf`, rebased onto `69f549168d`, local commits, not pushed. Behind the existing
`[extension.skyrimse] daedric quests` (and the settings the harness already uses: `quest alias packages`, `actor packages`,
`actor cell moves`, `fast travel`, `services`, `inventory`). **No new setting.** Two small general fixes (section 3) are not behind a
setting; each only changes a case that was wrong.

## 1. Survey (houseCARL records; `.pex` of `Skyrim - Misc.bsa` decompiled)

- **Two quests.** `DA14Start` (QUST 0x3FA17, priority 60, event CLOC, 7 stages) is the drinking contest; `DA14` (0x1BB9B, "A Night To
  Remember", priority 75, 26 aliases, 21 stages, 21 fragments in `QF_DA14_0001BB9B`) is the quest proper.
- **How DA14Start starts.** The story manager node `DA14QuestStartNode` (CLOC; the quest is not done, `GetLevel >= 14`, DA14's stage
  below 5, and the new location has `LocTypeHabitation`: a town, not an inn). The quest fills `InnLocation` (a location with
  `LocTypeInn` that has the same editor location as `TownCenterMarker` for the keyword `LocTypeHabitation`: the condition is
  `HasSameEditorLocAsRef` (180) with the alias flag, in the data a form that looks like fn 181's) and `InnMarker`; stage 10's fragment
  moves Sam Guevenne (alias `DA14StartSamwell`, NPC_ 0x1BB9C) to the inn marker.
- **The contest.** Sam's greeting topics (`DA14StartDrinkingContestHello`, Hello's own INFOs; "A drinking contest? You don't stand a
  chance!" is the choice), whose end fragment sets stage 30 (`SF_DA14StartSamsFirstDrink` scene: "I'll start round one. Down the
  hatch!"), stage 40 forces a greeting (`DA14StartPostDrink1`: "Your turn!" with "Here we go." / "This was a bad idea."), stage 50 is the
  second drink scene, stage 60 forces `DA14StartPostDrink2` ("One more. No Problemsh." then "Thash grape!" and Sam's invitation to "a great
  little place where the wine flows like water"). The last line's end sets **stage 70**, whose fragment `DA14.SetStage(5)`.
- **The temple.** DA14 stage 5 (`Fragment_3`): `Game.GetPlayer().MoveTo(DA14PlayerAwakenMarker)` (the inner sanctum of the Temple of
  Dibella in Markarth, `MarkarthTempleofDibella`), Senna moved to a second marker, Sam disabled, the hagraven and the giant enabled, the four
  mess items enabled (the Giant's Toe, two Alto Wines, Sam's note). Senna's alias package `DA14AwakenPriestessForcegreet`
  (`GetStage == 5`) opens her intro (`DA14PriestessSetup0`: "Wake up! That's right, it's time to wake up, you drunken blasphemer!"); its
  TIFs set **stage 10** (objectives "Find Sam Guevenne", "Find the Staff") and **stage 20** ("Help clean up the Temple of Dibella").
- **Cleaning.** `DA14ItemAdd` (an alias script on the four items) counts `OnContainerChanged` and sets **stage 30** at four; Senna's
  apology topic ("I really do apologize. I think I've picked up everything.") and "So do you remember anything I said when I got here?"
  (`TIF__000EA477`) set **stage 40** ("Ask about Sam and the staff in Rorikstead", Rorikstead on the map).
- **Rorikstead.** Ennis (alias `DA14Ennis`, in `RoriksteadCowflopFarm`) has the alias package `DA14EnnisForcegreet`; his chain ("You!
  You've got a lot of nerve showing yourself in this town again", "I'm sorry?", "That sounds pretty bad...") sets **stage 50** ("Find
  Gleda the Goat") when it ends. The rest (Gleda and the giant, Sam's `OnActivate` stage 75, Ysolda stages 84 to 130, Moira in Witchmist
  Grove 100 to 125, Morvunskar 135 to 200) is surveyed in `QF_DA14` only.
- **The temple also holds T01** ("The Heart of Dibella", StartGameEnabled): its Orla and Hamal packages (`T01OrlaHoldStillForProtocol`
  ...) force-greet a player who stands there ("What are you doing in here? ... The mother will deal with you", then Hamal's "penance"
  chain). See stand-in 3.

## 2. What runs in game (frames and logs `I:\Projects\OpenEden-work\papyrus\r345`, scripts `runfull.sh`, `in345_full.txt`, `run345.sh`)

A new game on the plains south of Whiterun, level 15, `daedric quests` with `quest alias packages`, `actor cell moves`, `services`, `fast
travel`, real mouse clicks on the conversation box (`OPENEDEN_RMLUI_INPUT_SCRIPT`), quiet window, master volume 0, fixed frame time:

1. Entering Whiterun's street raises the story event and **the story manager starts DA14Start**: `EventLocation`, `TownCenterMarker`,
   `InnLocation` The Bannered Mare, `InnMarker`, `DA14StartSamwell` Sam Guevenne are filled; stage 10 moves Sam into the inn.
2. Sam's voiced talk (real clicks): "A drinking contest? ..." then Goodbye sets **stage 30**; the first drink scene plays; **stage 40**
   forces his greeting; "Here we go." sets **stage 50** (second drink scene), **stage 60** forces the next, "One more", "Thash grape!" and
   Goodbye set **stage 70**, whose fragment sets **DA14 stage 5**.
3. **Stage 5 moves the player into the Temple of Dibella** (the game's own `MoveTo`), frames viewed. Senna force-greets ("Wake up! ..."),
   three real clicks set **stage 10 and 20**.
4. The four mess items are taken (`activateBy`): the game's own `DA14ItemAdd` sets **stage 30** at the fourth (not the third).
5. Senna's apology and "So do you remember ... when I got here?" end with **stage 40** (the Rorikstead map marker).
6. In Ennis's farm his forced greeting ("You! You've got a lot of nerve showing yourself in this town again ...") and two more clicks set
   **stage 50** ("Find Gleda the Goat"), frames viewed.

Frames viewed: Sam's choices and replies, "Your turn!", "Thash grape!", the temple with Orla (the T01 conflict), Senna's wake-up and her
three-reply intro, the apology, Ennis's three turns.

## 3. Faults found by running it and fixes

| # | Fault | Cause | Fix |
| --- | --- | --- | --- |
| 1 | DA14Start never started | the story node passes, but the alias `InnLocation` was never filled: its condition is `HasSameEditorLocAsRef` (function 180, with the alias flag) and the pack answered only function 181 | `aliasLocationExtraFunctions()` also holds 180 (the same function), and the filler waits for the alias a 180 condition names (`quest_alias.cpp`) |
| 2 | Stage 30 of DA14 came after three pickups | two aliases hold two placements of one base (the temple's two Alto Wines, aliases `DA14HolyWater` and `DA14Wine`), and a pickup told **every** alias that held a reference of that base (task 0271's `OnContainerChanged` rule), so `DA14ItemAdd` counted a wine twice | `PapyrusHost::setPickupSource(reference)` (set by `PickupService` around the add: a new optional hook `mSource`): with a source set, only the alias that holds that reference hears |

Also seen and **not fixed** (section 6): after a LOAD the alias packages of running quests are not rebuilt (Senna's DA14 greeting
becomes her T01 greeting); the T01 conflict below.

## 4. Stand-ins (hand-written, named)

1. **Walks are Lua teleports**: the plains to Whiterun's street (`world` step), the street to the inn (`cell` step; no door), the farm
   (`cell` step into `RoriksteadCowflopFarm`; the trip Markarth to Rorikstead is 800 km of map).
2. `activateBy` of the Lua test script stands for the key press at Sam (the talk), the four items and Senna (the apology talk).
3. **T01 is parked: `story test quest = 023B6C:60`** (The Heart of Dibella at stage 60) so that Orla's and Hamal's busted greetings (their
   conditions are `GetStage(T01) < 60` and a quest variable) do not take the conversation before Senna's. In the real game the same
   conditions would hold in the same temple; **what keeps those priestesses from greeting a player at the DA14 awakening marker is not
   known here** (the packages carry only those two conditions, the INFO four: stage, a script variable and two `GetIsAliasRef`); decision 3.
4. The fight with the giant and Gleda, the quest's later talks and Morvunskar are not reached.

## 5. Tests (written first, run red)

`apps/components_tests/skyrimse/testskyrimsenighttoremember.cpp` (`SkyrimNightToRemember`, 2; `SkyrimNightToRememberItems`, 3):
- fault 1: the table test failed against the table without 180 (**1 of 2**; the second pins the editor location alias and no neighbour);
- fault 2: the host test failed against a stub `setPickupSource` (**1 of 3**; the other two pin "no source known: every alias hears as
  before" and "a cleared source is forgotten").
- No existing test edited. The glue (`PickupService::Hooks::mSource`, the pack's `mPapyrus->host().setPickupSource`) has no unit test; the
  game run (stage 30 after four pickups, not three) is its evidence. `*Skyrim*` of `components-tests` 3,378 pass.

## 6. Not reached (ranked)

1. Gleda, Sam at the giant's camp (`DA14SammyScript.OnActivate`, stage 75 and the giant's attack), Ysolda (stages 84 to 130), Moira in
   Witchmist Grove (100 to 125) and Morvunskar (135 to 200), where Sanguine's realm, the hagraven's fight and the staff are.
2. The temple's T01 conflict (decision 3).
3. After a load the alias packages of running quests are gone (`mAliasPackagesOfReference` is not rebuilt: a save made in the temple
   greets with T01's topic).
4. Image space and fades of the drinking (tolerant no-ops), the wake animation, Sam being carried to Rorikstead.

## 7. Decisions for toddhow

1. `HasSameEditorLocAsRef` (180) is treated as the alias form (the Creation Kit's meaning is RECALLED-VERIFY); only a quest alias's
   location condition uses it.
2. A pickup of a placement tells only the alias that holds that placement (other aliases holding other placements of the base no longer
   hear). Pickups with no known placement keep the old rule.
3. T01 and the temple: is the busted greeting right for a DA14 awakening? The harness parks T01 at stage 60.

## 8. Not run

The Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, a fresh review, `openmw-tests`' whole binary, saves in the
middle of the quest, the fight and everything after Ennis.
