# Task 0314: Thieves Guild TG01 "Taking Care of Business", from the Ratway to the first debt

Status: partly done, 2026-11-18, Windows only. Branch `tg01-0314`, local commits, not pushed. Follow-up of tasks 0271, 0275
and 0287 (TG00 and the start of TG01), built on 0286 (actor cell moves) and 0312 (the Solstheim ship).

Same hand wrote the tests and the code (a first agent started the work and was stopped; its uncommitted edits were kept after
review). No fresh reviewer. Behind pack settings that are off by default: `[extension.skyrimse] thieves guild` (new) and, for
the Brynjolf move, the existing `actor cell moves`, `quest alias packages`, `papyrus`.

## 1. What the game's own data does (read from the decompiled scripts)

* TG01 stage 10 (set by TG00's end, 0287): `TG01PlayerAliasScript.OnLocationChange` (a script on a quest ALIAS that holds the
  player) waits for the player to enter the Ratway location, then does `pTG01QS.pTG01BrynMoved = 1`, **moves Brynjolf to the
  Ragged Flagon marker**, gives him his armour outfit and thieves guild crime faction, and disables his stall marker.
* In the Flagon, `TG01SceneTrigScript` (a box trigger, REFR 0x361CF, half extents 64 x 1012 x 613, turned 2.91 rad) starts the
  intro scene 0x361C6 when the player enters it at stage 10; the scene ends by setting stage 12 and Brynjolf's forced
  greeting follows ("Well, well... color me impressed, lad").
* The intro dialogue is a chain of response links; the linked topics test `GetVMQuestVariable(pTG01Dia01) == 1`, which the
  PREVIOUS line's fragment sets (`pTG01QS.pTG01Dia01 = 1`, `pTG01QS` being a script-typed property that holds the quest).
  The last line's fragment sets stage 20: Keerava, Bersi Honey-Hand and Haelga become objectives.
* Keerava's topics ("I have a message for you from Brynjolf", "It was never a request", "Don't say I didn't warn you
  (Brawl)") are gated by the same quest variables.

## 2. What was missing, and what was built

| # | Fault found by running it | Fix |
| --- | --- | --- |
| 1 | The host never sent `OnLocationChange` to scripts on the player (this branch first added its own `onLocationChange(from, to)`). | Main's task 0318 added `PapyrusHost::onLocationChange(actor, old, new)` (scripts on the actor and on every alias that holds it) behind `enablePlayerEvents`; after the rebase mine was dropped and `thieves guild` switches 0318's player events on. |
| 2 | `pTG01QS.pTG01BrynMoved = 1` on a property typed as a quest script, with the quest as value, found "no property" (the property belongs to the script ATTACHED to the quest). | `Runtime::instanceDefining(form, name, property)` and the property get/set path use the script attached to the form (`papyrusinterp.cpp`). |
| 3 | Brynjolf's `MoveTo` into the Flagon was refused (a move between two interiors). | The existing `actor cell moves` (task 0286) does it; the run needs that setting. Seen: `PAPYRUS MoveTo of 00020545 into cell 00016BCF by the world`, `A game pack moved the placement 132421 from Wilderness (42, -24) into RiftenRaggedFlagon`. |
| 4 | With `actor cell moves` on in Riften, TG00 never started: the start-up quests' real moves let `BYOHRelationshipAdoptableScript` start the Orphanage quest, whose `Orphan01..07` aliases have NO fill data and NO conditions (only a script forces them). The alias filler gave them "any reference" (Brynjolf's), and task 0305's dynamic alias packages then gave Brynjolf the orphans' Honorhall packages in front of his own (`PKTRACE ref 132421 has forced alias package BYOHHonorhallOrphansMeal`), so his forced greeting never came. | `SkyrimWorldView::setForcedAliasLookup` and `PapyrusHost::forcedAliasReference`: the packages of an alias with no fill data and no conditions go only to what a SCRIPT forced into it (`mScriptOnly`). Aliases with conditions keep the old behaviour. |
| 5 | The Flagon's box trigger never fired (boxes fire only for three other questlines). | New pack setting `thieves guild` (default false, `extension.json`, README line): box volumes fire for the player. |
| 6 | The chained links ("Deadbeats? What'd they do?") were not offered: they test the variable the previous line's fragment sets, and the fragment ran after the links were judged. | With `thieves guild` the dialogue's fragment flush is set (task 0304's mechanism). |
| 7 | Harness: several talks in one run. | `tools/skythieves_check/luatest/scripts/mq/global.lua` gets a step kind `{ update, 'talk', base }`. |

Developer aid: `OPENEDEN_PLACE_TRACE` (engine, `World::notifyNeutralPlayerPlace`) logs the place the engine tells the pack
(kept from the first agent's work; read once, no cost when unset).

## 3. In game (`tools/skythieves_check`, real controller events through the input script, quiet window)

Scenario files (outside the repository): `I:\Projects\OpenEden-work\tg14\env_t.sh`, `in_t.txt`, `run.sh`. Settings:
`quest alias packages`, `scene park seconds = 5`, `actor cell moves`, `thieves guild`, `[GUI] controller menus`.

1. TG00 as in task 0287 up to TG01 stage 10 (nine aliases filled).
2. Step `cell RiftenRatway01` (the Ratway door stand-in: see section 5): `PAPYRUS player place interior cell 0003B698 location
   0003B871` and `Papyrus event OnLocationChange to tg01playeraliasscript`; Brynjolf is moved into `RiftenRaggedFlagon`.
3. Step into the Flagon at (-760, 1953, -115): `OnTriggerEnter to tg01scenetrigscript of 000361CF`, then the scene 0x361C6 plays
   with the real lines and voices ("Give it up, Brynjolf... those days are over.", "I'm telling you, this one
   is different...", "Dying breed, eh? Well what do you call that then!") and ends; Galathil's own Dawnguard forced greeting
   (`DLC1Surgeon`) comes first on entering the cell, as it does in the game, and is closed with a real Goodbye click.
4. `TG01 stage 12` (`QF_TG01.Fragment_36`), Brynjolf's forced greeting begins; six real button presses through the intro chain
   (frame `q1/user/screenshots/screenshot016.png`: "Keerava, Bersi Honey-Hand and Haelga. Do this right, and I can promise you
   a permanent place in our organization." with "How did you want me to handle it?"), then `TG01 stage 20` runs
   `QF_TG01.Fragment_4` (objectives 20, 30, 40 shown, the three debtors made non-essential).
5. Keerava (`RiftenBeeandBarb`): her greeting menu, "I have a message for you from Brynjolf." (real topic), "It was never a
   request.", "Look around you, I'm barely keeping this rathole together" (frames `r1/user/screenshots/screenshot021.png` and
   `screenshot023.png`), and "Don't say I didn't warn you. (Brawl)": the INFO's fragment `TIF__000C483E.Fragment_0` runs
   (`pFDS.Brawl(akSpeaker, None)`).

## 4. Not done, ranked

1. **The debts are not collected.** The Brawl fragment runs but no brawl starts: `FavorDialogueScript.Brawl` is called and
   nothing of it shows in the traces (no combat, no `StartCombat`; the script's natives were not listed as missing, so the
   call through the property typed as that script may not reach the quest's script instance for a method; not investigated).
   The persuade, intimidate and bribe choices (`GetActorValue Speechcraft`, `GetIntimidateSuccess`, `GetBribeSuccess`, task 0155)
   and the choice conditioned on function 403 were NOT tried: the first Keerava run took the first button each time. Bersi
   (`RiftenPawnedPrawn`, the urn he wants smashed, `pTG01SmashUrn`) and Haelga (`RiftenHaelgasBunkhouse`, the statue) were
   not visited.
2. TG01's stage 50 ("Return to Brynjolf"), the reward dialogue and `Fragment_22` (TG02 stage 10, Mercer) were not reached.
3. The Ratway doors: the run teleports into `RiftenRatway01` (cell step) and does not use the real door pairs. The data
   (houseCARL, aggregates): exterior door `0AD0B4` at (197266, -96774, 9563) leads to the Ratway cell (-2951, -867, -448);
   the Flagon's `DoorRaggedFlagonToRW02` (908, 1855, -130) and the TG door `TGRatwayDoorBeforeTG02` (274, -3288, -125) lead
   back to it. The engine's door path (task 0115) is known to work for teleport doors, but this run did not press them.
4. Several other quests start in the Flagon (Galathil's, `CRHoldExpansion`, `WIGreeting`); not looked at.

## 5. Tests

* `apps/components_tests/skyrimse/testskyrimsepapyruslocation.cpp` (4 tests, new): the player's script hears the old and new
  location, another reference does not, a property of the script attached to a reference is set and read through a plain
  reference handle, every change is sent once. All pass. **Red evidence: not recorded by me.** The first agent's note says
  they were written against a stub `onLocationChange` and failed; I did not rebuild a stub to repeat it.
* No unit test for the script-only alias packages (`setForcedAliasLookup`): no fixture builds a `SkyrimWorldView` with package
  records; the evidence is the in-game pair (broken: `PKTRACE ... has forced alias package BYOHHonorhallOrphans*`, TG00 never
  starts; fixed: it does not and TG00 reaches TG01 stage 10).
* Not run: the Linux gate, clang-format 14 (formatted by hand, 120 columns), clang-tidy, the Lua tests, a fresh review.

## 6. Decisions for toddhow

* The alias rule (an alias with no fill data and no conditions is filled only by a script's ForceRefTo for packages) may
  be wrong for a quest the pack has not yet met; a find-matching alias with no conditions is still FILLED by the filler (four
  existing tests pin that); only its packages changed.
* `thieves guild` makes box triggers fire for the player; the half-extent assumption of task 0274 is unchanged and worked
  for this box (the player at the box centre fired).
* The `actor cell moves` setting has a side effect in Riften (item 4 above) that the alias rule hides; other starts in
  towns with BYOH orphans may show similar ones.

## 7. Checks run

* `components-tests` filter `*Skyrim*:*Papyrus*:*Quest*:*Alias*:*Trigger*`: 3,566 pass; `SkyrimSeAsyncIndex.aCancelledBuildBuildsNoTerrainIndex`
  failed once while a game ran beside the tests and passed 5 of 5 repeats alone (timing, not this change).
* Determinism (scenario `env_t.sh` to Keerava's Brawl line, trace lines of the quest stages, scene, forced greeting,
  dialogue lines, the cell move and the location events, with the dialogue's poll counters cut): runs `u1` and `u3` are
  identical; `u2` has one more line, a second `OnTriggerEnter to tg01scenetrigscript` (the player left the box and came back;
  not investigated). The runs `u1` to `u3` used the build before the rebase onto `9852336cc7`; run `v1` on the rebased build
  equals `u1`.
* Morrowind unchanged against `I:/Projects/OpenEden-work/integ2/build`: `tools/worldobjects_check` 110 objects `OK` (111
  result lines both sides), the fixed Balmora frame SHA-256 prefix `1c5d47ad0b5dc32d` for both.
* Not run: the Linux gate, clang-format 14, clang-tidy, the Lua tests, `openmw-tests`, a fresh review.
