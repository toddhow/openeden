# Research note 0055: what real Skyrim SE content uses (conditions, Papyrus natives and events, NIF block types)

Status: measured, aggregates only. Task 0116 (`docs/tasks/0116-skyrim-data-survey.md`), item 2 of section 8 of `docs/design/skyrim-se-implementation-plan.md`. Everything below was measured on this machine's installed Skyrim Special Edition with `tools/skyrimse_survey_check/check.sh`; nothing from the game is committed. **VERIFIED** means checked against the data in this run; **RECALLED-VERIFY** means from memory and not checked; **unknown** means the data cannot say.

## 0. Summary

- **Conditions.** The five masters (Skyrim, Update, Dawnguard, HearthFires, Dragonborn) hold **117,272 conditions** (CTDA) in 1,178,642 records, using **255 distinct function indices**. **5 functions give 50 percent of all uses, 20 give 80, 43 give 90, 66 give 95, 141 give 99.** Dialogue responses (INFO) hold 65 percent of the conditions, quests (QUST) 12 percent, AI packages (PACK) 6 percent.
- **The layout is verified.** Every one of the 117,272 CTDA subrecords is exactly 32 bytes; the compare operator (top 3 bits of byte 0) is only ever 0 to 5; run-on is only ever 0 to 7; the parameters of most functions resolve to form ids of one or two record types (section 2).
- **The data holds no function names; upstream OpenMW has a partial table.** Records carry no names, so the survey ranks `fn_<index>`. Upstream's `components/esm4/script.hpp` (Oblivion/FO3/FNV numbering, cc9cii, zlib-style notice) names 90 of the 255 surveyed indices, all at most 330, which cover 61 percent of uses, and the data's parameter shapes agree with those names (section 2.4). It is wrong from about 360 on and lacks Skyrim's own additions (the 560 to 700 range: fn_566, fn_629, fn_560 among the top nine), so those still need a clean source (question 1 in section 7, for toddhow).
- **Papyrus.** 14,049 game scripts (all parse), **126,088 call sites, 63,327 of them to natives (528 distinct natives)**. **12 natives give 50 percent of native call sites, 55 give 80, 109 give 90, 172 give 95, 308 give 99.** 117 distinct event handler names; 6 events (OnActivate, OnTriggerEnter, OnLoad, OnDeath, OnUpdate, OnEffectStart) give 50 percent of handlers, 18 give 80 and 37 give 95. Only 3,048 of the 14,049 scripts handle any event: most scripts are fragments.
- **NIF.** The 22,091 meshes in the master archives use **143 block types; the engine's record reader parses all of them (0 unsupported block types, 0 header failures)**. That says nothing about the renderer, collision or animation use of those blocks.

## 1. What was run and what it can and cannot say

- Code: `game_packs/skyrimse/{include/game_skyrimse,src}/survey*.{hpp,cpp}` (pure, no file access, no function table), tests `apps/components_tests/skyrimse/testskyrimsesurvey.cpp` (30 tests on generated records, written first and run red against stubs: 25 of the first 26 failed; built and run in the real CMake tree: `components-tests --gtest_filter=*Skyrim*` 210 pass, the gated `SkyrimSeSurveyReal.*` 3 pass), the gated run `testskyrimsesurveyreal.cpp` and `tools/skyrimse_survey_check/check.sh`.
- Conditions: the five masters only (the installed Data folder; the Creation Club archives and the user's mod list were **not** surveyed). Records are counted **as stored in each file**: a record that Update.esm or a DLC overrides is counted in both files, so the numbers describe file contents, not the winning load-order state. Compressed records are inflated (0 failures).
- Papyrus: the 14,049 distinct script names of the master archives (`Skyrim - *.bsa`; Update, Dawnguard, HearthFires and Dragonborn archives when present) plus the loose `Scripts` folder, each name counted once. Creation Club and mod scripts were not counted (note 0046 counted 15,231 with them). Natives are those `link()` of the runtime (task 0093) resolves; calls whose receiver class is not known statically (136 sites) and one unresolved call are left out.
- NIF: every `.nif` of the master archives, not only meshes a record references (one pass is cheaper than resolving every model path; it over-counts unused meshes slightly).

## 2. The condition record (CTDA)

Layout, 32 bytes:

| Offset | Field | Check against the data |
| --- | --- | --- |
| 0 | type byte: compare operator in the top 3 bits, flags in the low 5 | **VERIFIED** values: compare 0 to 5 only (0: 98,179, 3: 7,161, 4: 6,181, 5: 2,620, 1: 1,996, 2: 1,135). Which operator is which is **RECALLED-VERIFY** (assumed ==, !=, >, >=, <, <=; 84 percent at 0 fits "==") |
| 1..3 | unused | **not zero**: non-zero in 22,489 conditions. Never read these bytes (same finding as XLOC) |
| 4 | comparison value (float) or a global's form id | no non-finite float in any condition without the use-global flag |
| 8 | function index (u16) | 255 distinct values |
| 10..11 | unused | non-zero in 18,092 conditions. Never read |
| 12, 16 | parameter 1, 2 | form id or integer, section 4 |
| 20 | run-on | **VERIFIED** 0 to 7 only (0: 105,931, 1: 2,154, 2: 5,618, 3: 682, 4: 79, 5: 1,943, 6: 232, 7: 633); meanings RECALLED-VERIFY (subject, target, reference, combat target, linked reference, quest alias, package data, event data) |
| 24 | reference | a small integer in most uses (for example the player's reference number, which is not a record of these files, so it is kept as an integer); a form id of a placed reference for some functions |
| 28 | parameter 3 | a large value (the generated tests use 0xFFFFFFFF as the recalled "unused" marker; the survey counts 114,091 large values, 397 zero, 1,392 small and 1,392 form ids, and does not histogram the exact value) |

Flags (low 5 bits of byte 0): 0 (97,369), 1 (16,703), 4 (1,854), 2 (843), 8 (164), 5 (146), 3 (83), 6 (46), 16 (23), 7 (21), 9 (17), 17 (3). Bit meanings are RECALLED-VERIFY (1: OR with the next, 2: use aliases/parameters, 4: comparison value is a global, 8: use package data, 16: swap subject and target). The data shows the shape only: bit 1 is the only common one (16,703, 14 percent). String parameters (CIS1/CIS2) follow 7,254 conditions; no CTDT (the older form) exists; no CTDA of another size exists.

### 2.1 Conditions by record type

| Record | Conditions | Records with conditions | Distinct functions | Functions for 90 percent |
| --- | --- | --- | --- | --- |
| INFO (dialogue responses) | 76,556 | 32,148 of 41,188 | 123 | 19 |
| QUST | 14,425 | 1,714 of 2,391 | 94 | 25 |
| PACK | 6,512 | 3,387 of 7,611 | 89 | 19 |
| IDLE | 5,050 | 2,416 of 4,370 | 111 | 44 |
| SCEN | 3,338 | 622 of 2,141 | 56 | 12 |
| PERK | 3,296 | 468 of 523 | 58 | 13 |
| COBJ (crafting) | 2,448 | 1,285 of 1,387 | 10 | 3 |
| MGEF | 1,436 | 534 of 1,633 | 39 | 14 |
| SPEL | 1,083 | 204 of 1,440 | 46 | 15 |
| SMQN | 948 | 448 of 535 | 36 | 13 |
| SNDR | 706 | 336 of 3,260 | 11 | 3 |
| LSCR | 362 | 123 of 354 | 10 | 3 |
| CPTH | 323 | 170 of 211 | 16 | 8 |
| MUST | 220 | 101 of 352 | 3 | 2 |
| SMBN | 218 | 100 of 127 | 13 | 7 |
| FACT | 154 | 154 of 1,414 | 5 | 1 |
| ENCH | 144 | 32 of 752 | 17 | 9 |
| SCRL | 27 | 6 of 102 | 5 | 4 |
| MESG | 25 | 7 of 944 | 3 | 3 |
| ALCH | 1 | 1 of 443 | 1 | 1 |

Quest aliases and other nested structures are counted inside their record (QUST). The data shows record types beyond the plan's list: SNDR, MUST, CPTH, FACT, SMQN, SMBN, MESG, ALCH, ENCH, SCRL. Perk entry-point condition blocks are a different subrecord and were not surveyed (section 7).

### 2.2 Coverage curve

| Fraction of all 117,272 uses | Functions needed |
| --- | --- |
| 50 percent | 5 |
| 80 percent | 20 |
| 90 percent | 43 |
| 95 percent | 66 |
| 99 percent | 141 |
| 100 percent | 255 |

### 2.3 The top 45 functions (names unknown: only the index)

Columns: rank, index, uses, cumulative percent, the three record types using it most, the commonest run-on, and what parameter 1, parameter 2 and the reference field hold. "form X/Y" means the value is a form id that resolves to those record types in the five masters (the types are the data's; a function that takes "an object" resolves to whatever base type the condition names); "int" is a small number (at most 65,535, not a known form); "none" always zero; "some none" means some uses leave it zero.

| # | Function | Uses | Cum. % | Record types | Run-on | Parameter 1 | Parameter 2 | Reference field |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `fn_72` | 23816 | 20.3 | INFO, QUST, IDLE | 0 | form NPC_/STAT/TACT (some none) | none | none |
| 2 | `fn_58` | 10632 | 29.4 | INFO, PACK, SCEN | 0 | form QUST (some none) | none | int (some none) |
| 3 | `fn_71` | 9010 | 37.1 | INFO, QUST, FACT | 0 | form FACT | none | form ACHR, int (some none) |
| 4 | `fn_566` | 8852 | 44.6 | INFO, PACK, QUST | 0 | form STAT/DOOR/CONT, int (some none) | none | form ACHR, int (some none) |
| 5 | `fn_426` | 8237 | 51.6 | INFO, QUST | 0 | form VTYP/FLST | none | none |
| 6 | `fn_629` | 6900 | 57.5 | INFO, PACK, QUST | 0 | form QUST (some none) | form REFR/INFO/NAVM, other (some none) | int (some none) |
| 7 | `fn_59` | 5476 | 62.2 | INFO, PACK, SCEN | 0 | form QUST (some none) | form STAT/MISC/ECZN, int (some none) | none |
| 8 | `fn_74` | 3557 | 65.2 | INFO, PACK, SPEL | 0 | form GLOB | none | int (some none) |
| 9 | `fn_560` | 3486 | 68.2 | PERK, MGEF, QUST | 0 | form KYWD | none | int (some none) |
| 10 | `fn_47` | 1978 | 69.9 | COBJ, INFO, QUST | 2 | form MISC/BOOK/ALCH (some none) | none | form REFR, int (some none) |
| 11 | `fn_359` | 1590 | 71.2 | INFO, LSCR, QUST | 0 | form LCTN | none | form ACHR, int (some none) |
| 12 | `fn_77` | 1569 | 72.6 | INFO, IDLE, LSCR | 0 | none | none | none |
| 13 | `fn_448` | 1518 | 73.9 | COBJ, PERK, MGEF | 0 | form PERK | none | int (some none) |
| 14 | `fn_67` | 1286 | 75.0 | INFO, QUST, PACK | 0 | form CELL (some none) | none | form ACHR, int (some none) |
| 15 | `fn_46` | 1252 | 76.0 | INFO, QUST, PACK | 2 | none | none | form ACHR/REFR (some none) |
| 16 | `fn_14` | 1040 | 76.9 | INFO, PACK, QUST | 0 | form STAT/GLOB/MISC, int (some none) | none | form ACHR, int (some none) |
| 17 | `fn_1` | 974 | 77.7 | SCEN, INFO, QUST | 0 | form REFR/STAT/ACHR, int (some none) | none | form ACHR/REFR, int (some none) |
| 18 | `fn_550` | 924 | 78.5 | SCEN, PACK, QUST | 0 | form SCEN | form STAT/MISC/DOOR, int (some none) | none |
| 19 | `fn_561` | 865 | 79.3 | QUST, INFO | 0 | form LCRT | none | none |
| 20 | `fn_18` | 858 | 80.0 | SNDR, MUST, SCEN | 0 | none | none | none |
| 21 | `fn_606` | 783 | 80.7 | INFO, SMQN, PACK | 0 | form LCTN | form KYWD | none |
| 22 | `fn_562` | 752 | 81.3 | QUST, INFO, PACK | 0 | form KYWD | none | int (some none) |
| 23 | `fn_597` | 744 | 81.9 | IDLE, SPEL, ENCH | 0 | form STAT (some none) | none | int (some none) |
| 24 | `fn_131` | 683 | 82.5 | INFO | 0 | form STAT (some none) | none | none |
| 25 | `fn_69` | 625 | 83.1 | INFO, IDLE, PERK | 0 | form RACE (some none) | none | int (some none) |
| 26 | `fn_543` | 604 | 83.6 | INFO, PACK, QUST | 0 | form QUST | none | form ACHR (some none) |
| 27 | `fn_70` | 596 | 84.1 | INFO, PERK, QUST | 0 | form STAT (some none) | none | int (some none) |
| 28 | `fn_249` | 583 | 84.6 | INFO, SCEN, PACK | 0 | none | none | none |
| 29 | `fn_35` | 539 | 85.0 | INFO, PACK, QUST | 2 | none | none | form REFR/ACHR (some none) |
| 30 | `fn_579` | 538 | 85.5 | INFO, IDLE, SCEN | 0 | form SHOU | none | none |
| 31 | `fn_659` | 524 | 85.9 | COBJ | 0 | none | none | none |
| 32 | `fn_310` | 512 | 86.4 | INFO, SPEL, QUST | 0 | form WRLD/FLST | none | int (some none) |
| 33 | `fn_56` | 472 | 86.8 | INFO, PACK, QUST | 0 | form QUST (some none) | none | none |
| 34 | `fn_73` | 472 | 87.2 | INFO, PACK, QUST | 0 | form FACT | none | form ACHR, int (some none) |
| 35 | `fn_567` | 443 | 87.6 | QUST, PACK | 0 | form DOOR/STAT/MISC, int (some none) | none | none |
| 36 | `fn_403` | 441 | 87.9 | INFO, QUST, SCEN | 0 | form STAT/DOOR/ACHR, int (some none) | none | form ACHR, int (some none) |
| 37 | `fn_576` | 429 | 88.3 | SMQN, QUST, SMBN | 0 | other | form KYWD/LCTN/FLST (some none) | int (some none) |
| 38 | `fn_300` | 422 | 88.7 | QUST, SPEL, INFO | 0 | none | none | form ACHR, int (some none) |
| 39 | `fn_360` | 411 | 89.0 | QUST, INFO, PACK | 0 | form DOOR/STAT/WATR, int (some none) | none | int (some none) |
| 40 | `fn_136` | 390 | 89.4 | QUST, PACK, INFO | 7 | form REFR/STAT/FACT, int (some none) | none | none |
| 41 | `fn_159` | 377 | 89.7 | IDLE, INFO, SCEN | 0 | none | none | int (some none) |
| 42 | `fn_277` | 373 | 90.0 | PERK, INFO, QUST | 0 | form STAT/NPC_/MISC, int (some none) | none | int (some none) |
| 43 | `fn_286` | 361 | 90.3 | INFO, IDLE, SCEN | 2 | none | none | int (some none) |
| 44 | `fn_362` | 357 | 90.6 | PACK | 0 | form KYWD (some none) | none | none |
| 45 | `fn_620` | 346 | 90.9 | IDLE | 0 | int | none | none |

The full 255-row table (per-record counts, run-on, compare, flags and parameter kinds) is printed by `check.sh` and not committed.

### 2.4 Names from upstream OpenMW (added after the first version of this note)

**Source (clean, GPL-compatible).** Upstream OpenMW has a table: `components/esm4/script.hpp`, `enum ESM4::FunctionIndices` (about 250 `FUN_<Name> = <index>` entries, indices 1 to 619; file last changed in upstream commit `9889b225b155951b1c6effc896e6857f95886c33`, read at upstream master `97722d48f98f14b323c3880a698e8ce81babf7b3` with `git show` only). It is Copyright (C) 2020-2021 cc9cii under a zlib-style notice (free to use, alter and redistribute; origin not misrepresented; altered versions plainly marked; the notice stays). Its header says the facts come from the UESP wikis (Tes4Mod, Tes5Mod, FO3/FNV pages) and TES5Edit documentation, so it is an Oblivion/FO3/FNV table, not a Skyrim one. We took only index and name pairs (`game_packs/skyrimse/src/survey_names.cpp`, with the notice, the citation and an "altered" statement in its header); nothing else of the file, and nothing from xEdit, the Creation Kit wiki or CommonLib.

**Does the data agree with it?** Mostly for low indices, not above 330. Checks of the parameter shapes in section 2.3 against the upstream names (every name stays RECALLED-VERIFY):

| Index | Upstream name | Data shape | Agrees |
| --- | --- | --- | --- |
| 72 | GetIsID | NPC_ form (23,338 of 23,816) | yes |
| 58 | GetStage | QUST form, run on subject | yes |
| 71 | GetInFaction | FACT form | yes |
| 59 | GetStageDone | QUST form, second parameter an integer | yes |
| 74 | GetGlobalValue | GLOB form | yes |
| 47 | GetItemCount | item forms (MISC, BOOK, ALCH), most in COBJ | yes |
| 77 | GetRandomPercent | no parameters | yes |
| 67 | GetInCell | CELL form | yes |
| 46 | GetDead | no parameters, reference field set | yes |
| 14 | GetActorValue | small integer | yes |
| 1 | GetDistance | reference form | yes |
| 18 | GetCurrentTime | no parameters (SNDR, MUST) | yes |
| 69 | GetIsRace | RACE form | yes |
| 73 | GetFactionRank | FACT form | yes |
| 56 | GetQuestRunning | QUST form | yes |
| 310 | GetInWorldspace | WRLD form | yes |
| 136 | GetIsReference | reference form | yes |
| 300, 159, 286, 35 | IsInInterior, GetSitting, IsSneaking, GetDisabled | no parameters | yes |
| 70, 131 | GetIsSex, GetPCIsSex | 0 or 1 (counted as "form STAT" where the number happens to be a record id) | plausible |
| 362 | GetPlayerHasLastRiddenHorse | PACK only, keyword form | **no** |
| 403 | HasFriendDisposition | quest/INFO, object forms | **no** |
| 426 and 448 | not in the table (the table has GetIsVoiceType at 427 and HasPerk at 449) | VTYP/FLST form at 426, PERK form at 448 | Skyrim sits one below the table here |

So the table is usable up to about 330 and wrong from 360 on; the indices Skyrim added (the 560 to 700 range: fn_566, fn_629, fn_560, fn_561, fn_562, fn_576, ...) are not in it at all. **90 of the 255 surveyed indices get a name (those at most 330 that the table has): they cover 71,472 of the 117,272 uses (61 percent).** The nine top functions of build step 2 split as: fn_72 GetIsID, fn_58 GetStage, fn_71 GetInFaction, fn_59 GetStageDone and fn_74 GetGlobalValue have names; fn_566, fn_426, fn_629 and fn_560 do not. The survey output prints `fn_72 (GetIsID?)` for named indices (the question mark means RECALLED-VERIFY).

### 2.5 Names and functions from xEdit, and the measured coverage (task 0122 follow-up, 2026-11-11)

Source (toddhow's permission of 2026-11-11, decision 0014 amendment): xEdit `Core/wbDefinitionsTES5.pas`, table `wbConditionFunctions`, branch dev-4.1.6 (MPL-2.0; see `game_packs/skyrimse/src/survey_names_xedit.cpp` and `docs/dependencies.md`). All 255 surveyed indices now have a name (`survey::xeditFunctionName`); 17 more functions are evaluated (14, 46, 47, 67, 70, 73, 131, 249, 359, 426, 543, 560, 562, 566, 579, 629, 77).

Measured on the five official masters (`SkyrimSeConditionsReal.every_info_condition_list_of_the_masters`, aggregates only, stub world):

| | before (5 functions) | after (22 functions) |
| --- | --- | --- |
| INFO lists fully evaluated | 12,570 of 32,148 (39.1 percent) | 27,421 of 32,148 (85.3 percent) |
| lists failed closed on an unknown function | 19,578 | 4,727 |
| conditions unknown | 37,207 of 76,556 (48.6 percent) | 6,415 of 76,556 (8.4 percent) |
| distinct unknown functions | 118 | 101 |

Top remaining unknowns (conditions): fn_606 595 (GetKeywordDataForLocation, meaning unclear), fn_403 358, fn_69 GetIsRace 354, fn_35 GetDisabled 334, fn_286 IsSneaking 323, fn_56 GetQuestRunning 296, fn_130 GetPCIsRace 291, fn_310 GetInWorldspace 287, fn_182 GetEquipped 262, fn_1 GetDistance 241, fn_161 GetIsCurrentPackage 227, fn_84 GetDeadCount 213. Caveat: "fully evaluated" means every function is known, not that a real world would answer well; the stub world answers 0 or false.

## 3. Papyrus natives and events

### 3.1 Natives, ranked by call sites

126,088 call sites in 14,049 scripts; 62,624 go to script functions, **63,327 to natives**, 1 to a missing function, 136 have a receiver whose class is not known statically. 528 distinct natives are called (note 0046 counted 686 vanilla natives declared). Coverage: **12 / 55 / 109 / 172 / 308 natives for 50 / 80 / 90 / 95 / 99 percent** of native call sites.

| # | Native | Sites | Cum. % |
| --- | --- | --- | --- |
| 1 | `topicinfo.getowningquest` | 6,413 | 10.1 |
| 2 | `game.getplayer` | 5,746 | 19.2 |
| 3 | `referencealias.getreference` | 4,522 | 26.3 |
| 4 | `objectreference.disable` | 2,139 | 29.7 |
| 5 | `quest.setobjectivedisplayed` | 2,089 | 33.0 |
| 6 | `utility.wait` | 2,008 | 36.2 |
| 7 | `objectreference.enable` | 1,979 | 39.3 |
| 8 | `quest.setobjectivecompleted` | 1,900 | 42.3 |
| 9 | `objectreference.getlinkedref` | 1,509 | 44.7 |
| 10 | `objectreference.additem` | 1,304 | 46.8 |
| 11 | `objectreference.moveto` | 1,262 | 48.7 |
| 12 | `actor.evaluatepackage` | 1,241 | 50.7 |
| 13 | `alias.getowningquest` | 1,130 | 52.5 |
| 14 | `quest.stop` | 1,056 | 54.2 |
| 15 | `scene.getowningquest` | 1,006 | 55.7 |
| 16 | `scene.start` | 941 | 57.2 |
| 17 | `objectreference.activate` | 857 | 58.6 |
| 18 | `objectreference.removeitem` | 806 | 59.9 |
| 19 | `globalvariable.setvalue` | 802 | 61.1 |
| 20 | `referencealias.forcerefto` | 737 | 62.3 |
| 21 | `objectreference.playanimation` | 645 | 63.3 |
| 22 | `objectreference.placeatme` | 569 | 64.2 |
| 23 | `actor.removefromfaction` | 535 | 65.1 |
| 24 | `scene.stop` | 486 | 65.8 |
| 25 | `actor.isdead` | 412 | 66.5 |
| 26 | `debug.trace` | 399 | 67.1 |
| 27 | `objectreference.getitemcount` | 389 | 67.7 |
| 28 | `message.show` | 387 | 68.3 |
| 29 | `sound.play` | 378 | 68.9 |
| 30 | `globalvariable.getvalue` | 364 | 69.5 |

Ranks 31 to 200 are in the tool's output. By class (call sites): objectreference 16,274; actor 8,087; game 6,891; quest 6,416; topicinfo 6,413; referencealias 5,579; utility 2,538; scene 2,510; alias 1,326; globalvariable 1,166; form 965; debug 516; sound 445; effectshader 425; locationalias 415; message 411; visualeffect 346; faction 342; imagespacemodifier 303; formlist 301; spell 286; actorbase 281; location 254; package 170; math 109. Quest, alias, scene and topic-info natives together are one quest runtime's surface, not 60 separate features.

### 3.2 Scripts: what they are

Script name prefixes (of the 14,049): `tif_` 5,964 (topic info fragments, RECALLED-VERIFY), no prefix 3,711, `dlc2_` 1,089, `dlc1_` 957, `qf_` 880 (quest fragments), `sf_` 587 (scene fragments), `byoh_` 469 (Hearthfires), `pf_` 350 (package fragments), `prkf_` 9 (perk fragments). Only 3,048 of 14,049 script objects handle any event; **the rest are mostly fragment scripts whose functions the engine calls by name** (VMAD fragments). The fragment mechanism (plan 4.20, 4.21) is therefore ahead of events in value.

### 3.3 Events (handler names, by number of script objects that declare a handler)

The native class files declare **no events** (no native function named On...), so the set of events the engine can send is not in the scripts. The 117 handler names the scripts define, most common first: OnActivate 728 scripts, OnTriggerEnter 597, OnLoad 426, OnDeath 362, OnUpdate 265, OnEffectStart 221, OnHit 218, OnCellAttach 155, OnInit 147, OnContainerChanged 146, OnBeginState 143, OnTriggerLeave 124, OnUnload 112, OnEffectFinish 111, OnCellLoad 96, OnDying 96, OnCombatStateChanged 86, OnAnimationEvent 78, OnLocationChange 77, OnCellDetach 76, OnReset 73, OnEquipped 62, OnUpdateGameTime 55, OnRead 51, OnMagicEffectApply 46, OnItemAdded 45, OnTrigger 41, OnEndState 38, OnEnterBleedout 32, OnItemRemoved 30; the tool prints 120. OnInit, OnBeginState and OnEndState are VM events (object creation, state change) of the runtime of task 0093; OnReset, OnLoad, OnUpdate and OnUpdateGameTime depend on the game clock and on loading. Handler coverage: 6 events for 50 percent of handlers, 18 for 80, 26 for 90, 37 for 95, 72 for 99.

## 4. Parameter kinds from the data

For each function the survey classifies every parameter value: zero, a form id of a record that exists in the five masters (with the record type), a small integer (at most 65,535 and not a known form), or other (a float bit pattern or a large number). The top 45 are in the table above. Observations that matter for the engine:

- Most of the top functions take **one form** and no second parameter: a quest (fn_58, fn_629, fn_59, fn_56, fn_543), a faction (fn_71, fn_73), a keyword (fn_560, fn_562), a location (fn_359, fn_606), a global (fn_74), a perk (fn_448), a race (fn_69), a cell (fn_67), a world space (fn_310), a shout (fn_579). The condition engine can specialise on "function + form kind" before a general object model exists.
- fn_72 (20.3 percent of all uses alone) takes an object form that resolves to NPC_ in 23,338 of 23,816 uses (STAT 155, TACT 133); fn_566 and fn_426 are the next two for dialogue.
- Some functions take a form or an integer in the same slot (fn_566, fn_59 p2, fn_14, fn_1, fn_550 p2, fn_136): an enum or actor value index in some uses, a form in others; the data cannot tell which without the function's meaning. A form id that coincides with a small number or a record that exists by chance counts as "form": types such as STAT in the data are partly such coincidences.
- fn_629 has a second parameter that is a form for 377 uses and a large "other" value for 6,520.

## 5. NIF block types

22,091 files in the master archives (22,090 at stream version 100, one at 83), header read for every file (0 failures), 759,588 blocks, **143 distinct block types, all parsed by `Nif::Reader` with all versions allowed (0 unsupported, 22,091 of 22,091 parsed)**. The ten commonest by blocks (files holding one in brackets): NiNode 98,457 (10,233), BSLightingShaderProperty 73,237 (20,316), BSShaderTextureSet 62,642 (20,316), BSTriShape 60,277 (18,015), NiFloatInterpolator 37,201 (2,159), NiAlphaProperty 35,600 (11,941), NiFloatData 34,306 (1,996), NiSkinData 26,924 (6,905), NiSkinPartition 26,924 (6,905), BSDynamicTriShape 21,062 (3,647). Collision: bhkCollisionObject 13,261 blocks in 10,985 files, bhkRigidBody 11,029 (8,269), bhkMoppBvTreeShape 8,323, bhkCompressedMeshShape and its data 8,211 each in 7,904 files, bhkConvexVerticesShape 3,527. Animation and effect controllers (NiControllerSequence 4,772 in 1,281 files, BSEffectShaderPropertyFloatController 8,684, NiPSys* particles) are a minority of files. "Supported" here means the record reader constructs the block; whether `nifscene` flattens it, the renderer draws it or physics uses it is a different question (plan 4.7, 4.9, 4.12).

## 6. Recommended build order

### 6.1 Conditions engine v1

The evaluator (parse CTDA into a plain struct, register functions by index, evaluate in order with OR chaining, strict mode errors and normal mode logs once and returns false) is function-independent. Build it first, then add functions in this order, each with a hand-derived test:

1. **Framework.** CTDA decode (this survey's `decodeCtda`; never read the unused bytes), compare operators, the use-global flag (RECALLED-VERIFY), OR chaining, run-on resolution. Subject is 90 percent of uses (105,931 of 117,272); the alias run-on (1,943 uses) is tied to the quest runtime.
2. **Dialogue and quest set, 9 functions = 68.2 percent**: fn_72, fn_58, fn_71, fn_566, fn_426, fn_629, fn_59, fn_74, fn_560. Their parameters are an object form, a quest (three), a faction, a base object, a voice type or form list, a global and a keyword.
3. **20 functions = 80 percent** (adds fn_47, fn_359, fn_77, fn_448, fn_67, fn_46, fn_14, fn_1, fn_550, fn_561, fn_18). fn_47, fn_448 and fn_659 cover 92 percent of all COBJ (crafting) conditions.
4. **43 functions = 90 percent; 66 = 95 percent**: the milestone for "a quest slice runs". Do it when the quest runtime exists; most of this tail is specific to a record type (IDLE needs 44 functions for 90 percent of its own conditions, MGEF 14, PERK 13).
5. The remaining functions on demand, with strict mode in tests.

**The blocker is the meaning of the indices the table lacks** (question 1, section 7). Names exist for 90 indices up to 330 (section 2.4, RECALLED-VERIFY, shapes agree); fn_566, fn_426, fn_629 and fn_560 of the top nine and everything Skyrim added above 330 have none. This survey gives the call shapes against which a proposed meaning can be checked: a meaning for fn_58 must take a quest form, return a number compared with a constant, and run on the subject (10,619 of 10,632 uses). The game's own quests and scenes are the oracle (plan risk 3).

### 6.2 Papyrus world natives v1

1. **The quest runtime surface first (about 40 percent of native sites).** topicinfo.getowningquest, game.getplayer, referencealias.getreference, quest.setobjectivedisplayed/setobjectivecompleted/stop/start/isrunning/completeallobjectives, alias.getowningquest, scene.getowningquest/start/stop, referencealias.forcerefto/clear, locationalias.getlocation, globalvariable.setvalue/getvalue. These need the quest/alias/scene runtime (plan 4.20), not just bindings.
2. **Object surface.** objectreference.disable/enable/disablenowait/enablenowait/getlinkedref/additem/removeitem/moveto/activate/playanimation/placeatme/getitemcount/blockactivation/getdistance/lock/delete; actor.evaluatepackage/removefromfaction/isdead/startcombat/playidle/setactorvalue/kill/setghost/addspell/removespell/isinfaction/setrelationshiprank. These need the neutral world's reference model (stages N4 and N5).
3. **Utility, debug, message, sound.** utility.wait (latent; the scheduler exists), randomint and randomfloat (done), debug.trace, message.show, sound.play, effectshader and visualeffect play.
4. The first 55 natives are 80 percent and 109 are 90 percent. Fragment script wiring (VMAD, `tif_`, `qf_`, `sf_`, `pf_`) and the top six events (OnActivate, OnTriggerEnter, OnLoad, OnDeath, OnUpdate, OnEffectStart) come with step 1; OnHit and the cell attach/load events next.

## 7. What remains unknown, and decisions for toddhow

1. **Function meanings above 330.** Upstream OpenMW's table gives RECALLED-VERIFY names for 90 surveyed indices up to 330 (section 2.4). For the rest (fn_426 and fn_448, which sit one below the table's GetIsVoiceType and HasPerk; fn_560 to fn_700 and others Skyrim added) options are: (a) toddhow supplies meanings from his own knowledge, function by function, checked against the shapes in this note; (b) derive by experiment as the ObScript table was (note 0046 section 10): not possible here, because no condition text exists to pair with the bytes; (c) a licensed source. Recommended: (a) for the few high-use ones (fn_566, fn_426, fn_629, fn_560, fn_448, fn_359), each RECALLED-VERIFY until a quest slice passes; the first nine build-order functions then need only four decisions.
2. The **compare-operator, run-on and flag bit meanings** are recalled, not verified (section 2).
3. **Winners.** Counts are per file; overridden records count twice. A load-order-winner count needs the record merge (the world view does it for cells only).
4. **Mods.** The Creation Club content and the user's mod list were not surveyed; the tool takes the Data folder only. Extending it to a Mod Organizer 2 profile is small.
5. **The events the engine sends**, as opposed to those scripts handle, are not in the scripts. The 117 handled names are a lower bound.
6. **Other condition carriers.** Perk entry-point condition blocks and any condition sub-structure that is not a plain CTDA subrecord were not separately surveyed.
7. **NIF.** Whether `nifscene`, the renderer, collision and animation support each of the 143 block types; only record parsing was run.
8. The unused CTDA bytes hold non-zero data in 22 percent of conditions; whether it is garbage or meaning (for example an extended function index) is unknown.

## 8. Reproduction

`tools/skyrimse_survey_check/check.sh "<Data folder>" [conditions,papyrus,nif] [build folder]`. The conditions part takes about 10 s, Papyrus 3 s, NIF about 45 s on this machine. It reads the Data folder only and writes nothing.
