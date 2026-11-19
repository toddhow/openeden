# Task 0343: Skyrim Dark Brotherhood, third slice (DB02 to its end, the Black Door, the Sanctuary, Nazir, DB03)

Status: IN PROGRESS, see section 4 for what runs. Follow-up of tasks 0294 (DB01) and 0325 (DB02 to stage 30). Same hand wrote the
tests and the code (the one pure rule test first and run red; the rest by the game, see section 5). No fresh reviewer. Windows only.
Branch `darkbrotherhood3-0343`, from the merge of tasks 0338 and 0340 (`979bbc98db`, local; the local `main` was still at
`31579ff7ea` when I started, so the talking-activator and Invisible Continue work of 0338 would have been missing), not pushed.
Behind the NEW pack setting `[extension.skyrimse] dark brotherhood` (off, declared in `extension.json`, README line in
`tools/testprofile`).

## 1. Survey (the records of `Skyrim.esm` read with a small Python parser, the `.pex` of `Skyrim - Misc.bsa` decompiled with houseCARL;
nothing of either is committed)

* **DB02 stages.** 10 (sleep greeting done), 30 (a captive died: `pDB02CaptiveNScript.OnDeath`), **40** (Astrid's key: `QF_DB02` Fragment_0
  completes objective 30, shows "Enter the Dark Brotherhood Sanctuary", registers an update), **50** (the player entered the
  Sanctuary), **200** (Astrid's welcome: `TIF__00020BDD` sets DB02 200 and DB02a 10), 220 (Astrid killed).
* **Who sets 40.** At stage 30 Astrid's second greeting is the Blocking branch `DBAstridSecondGreetBranch` (DLBR flags 0x02, start topic
  `DBAstridSecondGreetForcegreet` 0x2B831; no package forces it, the name is old). Its INFOs choose by `GetDeadCount(captive base)`
  (fn 84, one INFO per combination of dead captives). The player's reply `DBAstridSecondGreetResponse1..3` links to
  `DBAstridThirdGreetBranch` whose INFO `TIF__00022F34` (the "So... I'm free to go?" line) sets **stage 40**, moves the shack key
  from Astrid to the player, sets `DarkBrotherhood.BlackDoorPass = 1` and enables the Sanctuary's map marker.
* **Astrid leaves.** `DBAstridShackLeaveTriggerScript` is on the base ACTI 0xFE52A (the box REFR 0xFE52B in `AbandonedShackExterior`,
  at the shack's door): `OnTriggerEnter` by the player at DB02 stage 40 sets `AstridMove = 1` and `MoveTo`s Astrid to the Sanctuary.
* **The Black Door** (TACT `DBBlackDoorActivator` 0x25BFA, REFR 0x2B83B at (-42175, -83965, -4440) in Tamriel, voice type
  `MaleUniqueDBBlackDoor`): its Blocking branch `DBBlackDoorSanctuary` speaks "What is the music of life?" with four answers;
  answer 1 ("Silence, my brother.", shown while `BlackDoorPass == 1`) runs `TIF__0002B836`: the door's sound, `Game.GetPlayer().MoveTo`
  the Sanctuary marker, `BlackDoorPass = 2`, the doors swap. Its line "Welcome home." follows.
* **Stage 50.** No quest script sets it: it is the base ACTI `DB10SancTrigger` 0x4DD9E (script `DB10EntranceScript`, a box REFR
  0x4DD9F inside `DarkBrotherhoodSanctuary` at (3199, -411, -283)): `OnTriggerEnter` by the player at DB02 stage 40 sets stage 50.
  The package `DBAstridSanctuaryFirstGreet` (ForceGreet, topic `DBAstridSanctuaryFirstArrive` 0x2B83E, DB02 stage 40 to 50) walks Astrid to him.
* **Stage 200.** Astrid's "So what happens now?" opens `DBAstridSanctuaryGreet` and its three replies `DBAstridNextStep1..3`; the first
  (`TIF__00020BDD`, DB02 stage 50) sets DB02 stage 200 and DB02a stage 10. DB02a ("Sanctuary", `QF_DB02a` Fragment_2) gives the faction and
  the four armour pieces.
* **Nazir** (NPC_ 0x1C3AB): `DBNazirFirstGreetBranch` (Blocking) when DB02a >= 10; its replies lead to `DBNazirContractBranchTopic` and
  `DBNazirAnythingElseTopic`; `DBNazirContract1ReadyBranch` (`TIF__00020017`) sets DB02a 200 and the first three side contracts (stage 10).
* **DB03 "Mourning Never Comes"** (QUST 0x1EA52): Astrid's Blocking branch `DBAstrid03GiveBranch` (DB02 >= 200, DB03 < 10) and its two
  replies end in `TIF__0001FED8`: DB03 stage 10.

(Sections 2 to 8 follow when the runs are done.)
