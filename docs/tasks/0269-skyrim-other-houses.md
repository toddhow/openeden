# Task: Skyrim buying houses, the other four: Honeyside, Proudspire Manor, Vlindrel Hall, Hjerim (task 0269)

Status: ordered by the coordinator (approved by toddhow). Same hand wrote tests and code, no fresh reviewer. Windows only.
Local commits on branch `houses2` from `6a917ca54a`, not pushed.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S7); decisions 0013, 0016, 0017; task 0261 (Breezehome,
the path every house here goes through: read it first).
Goal: each of the four houses is bought through the steward's own dialogue and the shipped Papyrus fragments
(`QF_HousePurchase_000A7B33.Fragment_n` -> `HousePurchaseScript.PurchaseHouse`), with the key, the ownership of the cell, the
save; plus the "I can't afford that right now." reply 0261 did not run.
Constraints: behind the existing `[extension.skyrimse] houses` (off by default, needs `papyrus`); Morrowind unchanged.
Out of scope: furnishing of these houses (the `Decorate*` branches), housecarls, the Hearthfires plots, the favour quests.

## 1. Survey (Skyrim.esm read by an independent walker, scripts decompiled with houseCARL from `Skyrim - Misc.bsa`)

All five houses share the quest HousePurchase (0x0A7B33): one stage (10) with five log entries, each carrying
`GetInCurrentLoc(<hold>)`, and five fragments tied to them (Fragment_0 Whiterun, _5 Riften, _3 Solitude, _6 Markarth,
_7 Windhelm); each fragment calls `PurchaseHouse(<cell>, <key>, <guide>, <price global>)` and sets its own `<Hold>HouseVar`.
The offer is a topic with TCLT choices per hold (start topic of its own branch); the replies are the shared INFOs of
`HousePurchaseSharedInfo` (0xC6E04) through DNAM. 0261 built everything generic.

| House | Hold | Steward (base, cell) | Offer topic | Price (global) | Cell | Key | Conditions beyond the steward and `<Hold>HouseVar == 0` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Breezehome (0261) | Whiterun 0x18A56 | Proventus Avenicci 0x13BBA, WhiterunDragonsreach | 0xA7B19 | 5000 (HPWhiterun 0xF728B) | 0x165A8 | 0x93B08 | rank >= 2 with a jarl: alias 10 or 14 |
| Honeyside | Riften 0x18A58 | Anuriel 0x13349, RiftenMistveilKeep | HouseRiftenViewBranchTopic 0xA7B14 | 8000 (HPRiften 0xF728D) | RiftenHoneyside 0x16BDD | RiftenHoneysideKey 0xA7B36 | rank >= 2 with a jarl: alias 12 (Laila Law-Giver, 0x13366, external alias of DialogueRiften alias 0) or 16 (Maven 0x1336A) |
| Proudspire Manor | Solitude 0x18A5A | Falk Firebeard 0x13274, SolitudeBluePalace | HouseSolitudeBranch2Topic 0xA8519 | 25000 (HPSolitude 0xF728C) | SolitudeProudspireManor 0x16A06 | SolitudeHouseKey 0xA7B37 | rank >= 2 with ONE jarl: alias 9 (Elisif 0x1326A, external alias of DialogueSolitude alias 0x14) |
| Vlindrel Hall | Markarth 0x18A59 | Raerek 0x133B2, MarkarthUnderStoneKeep | HouseMarkarthViewBranchTopic 0xA7B13 | 8000 (HPMarkarth 0xF728E) | MarkarthVlindrelHall 0x16DFA | MarkarthVlindrelHallKey 0x94391 | alias 13 (Igmund 0x1339F) or 17 (Thongvor 0x133BB) |
| Hjerim | Windhelm 0x18A57 | Jorleif 0x14135, WindhelmPalaceoftheKings | HouseWindhelmViewBranch1Topic 0xA7B12 | 12000 (HPWindhelm 0xF728A) | WindhelmHjerim 0x16778 | (property of the quest) | alias 11 (Ulfric 0x1414D) or 15 (Brunwulf 0x14149), AND `MS11` (0x1F7A3) stage 1 done; with it NOT done the first INFO (0xA7B2F, no shared reply) speaks the house is not for sale |

The purchases' own INFOs ("I'll take it.") are `TIF__000A7B1E` (Whiterun, also adds Lydia's marriage faction),
`TIF__000A7B28` (Riften), `TIF__000A851F` (Solitude), `TIF__000A7B23` (Markarth) and `TIF__000A7B2A` (Windhelm): each one
`GetOwningQuest().SetStage(10)` and an achievement. Their conditions: gold >= the price global (`op 0x64`, fn 47), in the
hold, steward. The "can't afford" choice of each hold is the same shape with gold < the global (`op 0x84`).
Doors: Honeyside's interior door 0x16C4E (key only, level 255, key 0xA7B36), Proudspire's two front doors 0x16B0A and 0x5D8B9
(key 0xA7B37) and a third 0x5D8BB with another key (0xC13E7), Vlindrel's 0x77D5B (key 0x94391); Hjerim's interior door
0x168DC has no lock (the exterior door is the one a key is for).

## 2. What was missing (found by running, then fixed)

- **Falk Firebeard (Solitude) was never offered the house.** His NPC_ record has no JobStewardFaction (Skyrim.esm and the four
  other masters: no override either); the data gives it through the ALFC list of the steward alias (id 22) of the quest
  DialogueSolitude (0x2E1AD) while the quest runs; the other four stewards carry it themselves (at rank -1, task 0261 decision 2).
  Found with a temporary per-condition trace of the offer INFO (0xA851C): every condition answered and held except
  `GetInFaction(JobStewardFaction)`. `houses::aliasFactionGrants` (pure: the data-filled specific-reference aliases of the quests
  whose ALFC names the faction) and `houses::holdDialogueQuests` give the five quests; `applyHouses` adds the factions to the
  follower state once the host runs, with `houses` on ("the stewards ... are in JobStewardFaction: 4"). The trace code was removed.
- **Test aid.** `houses test permission` befriends every jarl the offers' aliases name (`houses::testPermissionJarlBases`: Balgruuf and
  Vignar, Ulfric and Brunwulf, Laila Law-Giver and Maven Black-Briar, Igmund and Thongvor, Elisif; 9 bases) and marks MS11 stage 1
  done (`PapyrusHost::markStageDoneForTest`: state only, no fragment, no event). The real conditions are Friend rank with a jarl
  (the favour quests `Favor250` to `Favor254`; Solitude needs the one jarl, alias 9) and, for Hjerim, MS11. **Nothing in this
  build gives either**, so a purchase here always runs with the aid.
- External quest aliases (Riften 12, Windhelm 11, Markarth 13, Solitude 9: ALEQ of the DialogueX quests) needed nothing: the alias
  filler copies the other quest's data-filled fill and the condition runs on the placed reference; Solitude's offer proves the
  external path (it is the only hold with just one jarl alias).

## 3. Test aids

No new setting. `tools/skyhouses_check` got `KEY` (hex id of the key its trace counts; the trace column is `key=`, was
`breezehomekey=`). The `rest` setting turns the bed into the sleep window, which pauses the world: a Lua `activateBy` after a
bed step is dropped (`activationhandlers.lua` returns while the world is paused), so the plan does door first, bed last.

## 4. Stage log

- **Tests.** `apps/components_tests/skyrimse/testskyrimsehouses2.cpp`, 7 tests: `SkyrimHouses2Data` (jarl bases, MS11), `SkyrimHouses2Host`
  (marking a stage), `SkyrimHouses2Steward` (grants from a generated quest with ALFC aliases, the five hold quests). Written
  before the code against stubs (`testPermissionJarlBases` empty, `markStageDoneForTest` false, `aliasFactionGrants` empty):
  **first batch 3 of 4 failed, steward batch 1 of 3 failed** (the other tests pin a constant, an unknown quest and absence).
  After: 7 of 7 pass. No existing test edited.
- **Results.** The area filter (`*Skyrim*:*Dialogue*:*Papyrus*:*Journal*:*Crime*:*Rest*:*Follower*:*Quest*:*Services*:*Inventory*:*Barter*:
  *Extension*:*Neutral*:*Frozen*`) 3,651 ran, 3,548 passed, 0 failed, the rest are the data-gated skips.
- **In game** (`I:\Projects\OpenEden-work\papyrus\houses2`, `run.sh`, five masters, quiet window, clicks through
  `OPENEDEN_RMLUI_INPUT_SCRIPT`, frames in `keep/`): each house from the steward's own cell with enough gold.
  - Honeyside (Anuriel in RiftenMistveilKeep, 10,000 gold): `TIF__000A7B28` then `QF_HousePurchase_000A7B33.Fragment_5` only;
    `item 0F x8000` out, `item 0xA7B36` (RiftenHoneysideKey) and `0xF11B7` (guide) in; `HOUSES cell 00016BDD is now owned by 00000DB1`;
    gold 2,000; the door (base 317165 at -512,-320) is `NLOCK ... -> 2 (unlocked by key)` and `NDOOR` takes the player to the
    Riften street (172426,-94072); without the key (control) `NLOCK ... -> 3 (refused)` and the player stays; the bed
    opens the sleep window and logs no `CRIME trespass`.
  - Proudspire Manor (Falk Firebeard in SolitudeBluePalace, 30,000): `TIF__000A851F`, `Fragment_3`; 25,000 out, key `0xA7B37`, guide
    `0xF1447`, cell `0x16A06` owned; gold 5,000; door 0x16B0A opens to the Solitude exterior (-55671,106071).
  - Vlindrel Hall (Raerek in MarkarthUnderStoneKeep, 10,000): `TIF__000A7B23`, `Fragment_6`; 8,000 out, key `0x94391`, guide
    `0xF1445`, cell `0x16DFA`; gold 2,000; door 0x77D5B opens to Markarth (-174940,7467).
  - Hjerim (Jorleif in WindhelmPalaceoftheKings, 15,000, MS11 marked): `TIF__000A7B2A`, `Fragment_7`; 12,000 out, key `0x21679`
    (MS11HjerimKey: the quest property), guide `0xF1446`, cell `0x16778`; gold 3,000; the interior door has no lock, it opens to
    Windhelm (130640,41639).
  - "I can't afford that right now." (Riften, 1,000 gold): the "I'll take it." choice is gone, the choices are "On second thought..." and
    "I can't afford that right now."; the reply is "Very well, just let me know when you have the necessary funds."; no stage, no
    gold, no key.
  - Save and load: saved in Honeyside after the purchase, loaded in a new process: `HOUSES restored 1 owned cells`, gold 2,000, key 1,
    `NLOCK ... unlocked by key`, the door opens (`the quest of the house offers was refused`: it is running already from the save).
  - Three runs of the Riften purchase, door, bed scenario: the 27 trace lines (md5 `5c2a42700f282b01be16868f29fdcb9d`) are byte-identical.
- **Morrowind unchanged.** `tools/worldobjects_check` OK, 111 lines, against `integ2`'s build (not a build of the exact parent); the
  fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600) is `1c5d47ad0b5dc32d`, the number earlier tasks recorded.
  No file outside the Skyrim pack, its tests, the check tool and the test profile README changed.
- **Edits outside the new files.** `houses.{hpp,cpp}`, `papyrus_host.{hpp,cpp}` (one method), `skyrimse_game_pack.cpp` (`applyHouses`
  and one member), `apps/components_tests/CMakeLists.txt`, `tools/skyhouses_check`, `tools/testprofile/README.md`, `CLAUDE.md`.
- **Not run.** The Linux gate and GCC `-Werror` (the traps were checked by hand), clang-format 14, clang-tidy, the Lua tests, a fresh
  review, Hjerim's "not for sale" line (the first INFO, 0xA7B2F, has its own response and shows while MS11 stage 1 is not done: the aid
  always marks it), the real jarl favours and the MS11 murders, the second and third door of Proudspire, the beds of the other three
  houses, furnishing (`Decorate*`) of the new houses (the same fragments, not run), the housecarls, two bought houses in one game.

## 5. Decisions for toddhow

1. **The stewards' faction from a quest alias** is applied once when the host runs, with `houses` on, only for the five hold dialogue
   quests' data-filled aliases. The general rule (any quest alias with ALFC adds its fill to the factions while the quest runs) would
   change other conditions; this is the smallest answer.
2. **MS11 is marked done by the test aid** (not run): the real Hjerim offer needs the player to have finished "Blood on the Ice";
   the offer text for it not done ("not for sale") is therefore not reachable with the aid on.
3. **The sleep window pauses the world**, so a scripted door press after a bed press is dropped (a property of the harness, not of
   the houses).
