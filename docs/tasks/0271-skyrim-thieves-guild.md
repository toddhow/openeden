# Task 0271: Skyrim Thieves Guild questline, first slice (TG00 "A Chance Arrangement", TG01 "Taking Care of Business")

Design doc section: `docs/design/` Skyrim plan, milestone M-S6/M-S8 (quests). Approved by toddhow.
Branch `thievesguild` from `fafd08bfd9`. Same hand wrote tests and code (tests AFTER the code for the new parts; mutation checks
and the in-game run are the evidence, see "Red counts"). No fresh reviewer. Windows only.

Goal: Brynjolf's approach in Riften's market starts TG00 through the game's OWN forced greeting, dialogue, quest fragments and
Papyrus; the journal follows. As far as the engine allows, the job itself (the ring) follows.

Inherited code touched: `game_packs/skyrimse` (`package.*`, `package_brain.*`, `quest.*`, `world_view.*`, `dialogue.*`,
`dialogue_runtime.*`, `conditions.cpp`, `skyrimse_game_pack.cpp`, `actor_table.hpp`, `extension.json`, CMake lists, new
`force_greet.*`, since unified into task 0264's `forcegreet.*`: see the merge note at the end), `files/data/builtin.omwscripts` and `files/data/CMakeLists.txt` (one script each), new Lua
`forcegreet_global.lua`, tests `testskyrimsethievesguild.cpp`, tool `tools/skythieves_check`. No engine, SDK or contract file.

## 1. The data (Skyrim.esm; records by houseCARL and an independent Python walker, scripts decompiled from `Skyrim - Misc.bsa`)

**TG00 "A Chance Arrangement"** (0x021556, start-game-enabled, type Thieves Guild, priority 70). Stages 0, 5, 8, 10, 20, 30, 35,
40, 200; objectives 8 "Meet Brynjolf during daytime", 10 "Steal Madesi's Ring", 20 "Plant Madesi's Ring", 30 "Speak to
Brynjolf". Scripts `QF_TG00_00021556` (fragments below) and `TG00QuestScript` (variables `pTG00FGDone01`, `pTG00Dialogue01`,
`pTG00Killer`, `pTG00ArrestStopper`, a 0.5 game day timer that sets stage 40). Eight aliases: `BrynjolfAlias` (unique actor
0x01B07D, **seven ALPC packages**: `TG00BrynjolfForceGreetPlaza`, `...ForceGreetBee`, `...ForceGreetPlayerAfter`,
`...WaitPlazaDay`, `...WaitBeeNight`, `...DayStart`, `...NightStart`), `BrandSheiAlias` (0x01334F, essential, package
`RiftenBrandSheiInPrison`), `MadesiAlias` (0x01B072), `TG00MadesiChest` (REFR 0x076F14, a strongbox owned by Madesi, XLOC level 0
with key 0x0B8F42), `TG00RingAlias` (REFR 0x0CDF65, base 0x076F12 the ring, parked in a holding safe), `TG00HoldingSafe`,
`TG00City` (location Riften) and `TG00PlayerAlias` (the player, script `TG00PlayerScript`).

| What | How the data runs it |
|---|---|
| The approach | Brynjolf's **ForceGreet** package `TG00BrynjolfForceGreetPlaza` (template 0x03C1C4, target the player, topic input PDTO = DIAL 0x0352D5 `TG00BrynjolfIntroBranchTopic`). Conditions: `pTG00FGDone01 == 0` (script variable), the player in RiftenWorld, `IsPlayerMovingIntoNewSpace == 0`, not in an interior, not in combat; schedule 8:00 to 20:00. The Bee and Barb variant `...ForceGreetBee` needs `GetInCell(player, 0x016BDF)` instead of the world |
| The greeting | INFO 0x0352E0 / 0x04FCDA (voiced, fragment `TIF__000352E0`: `pTG00FGDone01 = 1`) links to the choices; the tree runs `IntroBranch01..09`, `MQ203A1..D1`, `WalkAway`. "What do I have to do?" (06) then "Why plant the ring on Brand-Shei?" (09, fragment `TIF__00038AB3`: **SetStage(8)**) |
| Stage 8 | `Fragment_14`: objective 8 displayed, `TG00MiscHandler.SetStage(200)` (its fragment `CompleteAllObjectives()`, `Stop()`) |
| "I'm ready. Let's get this started." | topic 0x020A9C, INFO 0x0C0BC2: condition stage 8 and `GetInCell(0x016BDF) == 0`; fragment `TIF__000C0BC2`: **SetStage(10)** |
| Stage 10 | `Fragment_4`: `AddPerk(TG00PickpocketHelper)` (entry point 0x38, value 30.0), `SetNotShowOnStealthMeter`, timer, objective 8 done / 10 shown, `TG00SP.Start()` (the distraction scene quest 0x020BE7: Brand-Shei and Madesi argue, Madesi leaves his stall), `MadesiChest.AddItem(RingAlias.GetRef())`, enables an Imperial or a Stormcloak guard by `GetKeywordData(CWOwner)` of the city |
| Stealing | the ring is an ARMO placement moved into the chest; `TG00RingScript.OnContainerChanged` on the alias: stage 20 when it reaches the player |
| Stage 20 | `Fragment_6`: objective 10 done, 20 shown |
| Planting | `OnContainerChanged` again: new container = Brand-Shei -> stage 30 |
| Stage 30 | `Fragment_8`: `BrandShei.RemoveItem(ring)`, objective 20 done, 30 shown, `TG00SP.SetStage(20)` (the arrest scene `TG00SPArrestScene` 0x04EF26: a guard arrests Brand-Shei, `TG00SPArrest` moves him to jail) |
| Failure | stage 35 (Brand-Shei or Madesi killed), 40 (left Riften, or the 0.5 day timer ran out): objectives failed, objective 30 shown |
| Reward and end | the outro topics (`TG00BrynjolfOutroBranchTopic01..04`, stage 30 to 40): `TIF__0008B5E1` gives 100 gold; stage 200 `Fragment_10`: `TG01.SetStage(10)`, `Stop()` |

**TG01 "Taking Care of Business"** (0x01F326): `QF_TG01_0001F326`, `TG01QuestScript`, `TG01PlayerAliasScript`,
`TG01SceneTrigScript`, `TG01HaelgaStatueScript`, `TG01BersiDwemerUrnScript`, the scene `TG01UrnSmashScene`; Brynjolf sends
the player to collect debts from Haelga (a statue), Bersi Honey-Hand (a Dwemer urn) and Drifa; the player may assault or
persuade; `TG01ActorKillMonitor`, `TG01AssaultMonitor` quests watch violence. **Not run in this slice** (it needs TG00's end:
stage 200 and the debt targets are in Riften's houses and the market, see section 4).

## 2. Ranked engine gaps (what stopped the questline, most blocking first; status after this slice)

1. **Packages that quest aliases give an actor (ALPC).** Brynjolf's whole approach lives in alias packages; the pack never read
   them. **Built** (3.1).
2. **A ForceGreet package opening a conversation.** The pack classified ForceGreet as Stand; no package started a talk (the only
   forced greeting was the guard's, task 0258, driven by the crime system). **Built** (3.2).
3. **Conditions the package world could not answer:** `IsPlayerMovingIntoNewSpace` (358) and conditions that run ON THE PLAYER
   about his place (`GetInWorldspace`, `GetInCell`, `IsInInterior`: the pack answered only for the speaker); and quest script
   variables read before any script ran (`pTG00FGDone01`). **Built** (3.3).
4. **A link that is no choice** (the speaker goes on with an unnamed topic: the payment line). **Built** (3.5).
5. **Items with identity: `AddItem(ref)` and `OnContainerChanged` on an alias that holds an ITEM reference.** The ring is a placed
   reference moved from container to container. **Built narrowly** (3.4): by base, no per-reference item table; saves do not
   keep it (nothing saved: the inventory chunk keeps counts).
6. **Reverse pickpocketing** ("give an item into a living NPC's inventory while sneaking"): task 0208 builds the take path only
   (the pickpocket window refuses a put). **Not built.** In the check a harness call moves the ring (see 5.3). Needs an
   inventory-into-NPC action with its own chance and crime, and the `TG00PickpocketHelper` perk entry point (0x38, value 30.0).
7. **The strongbox by hand:** lock picking (task 0203: level 0, key 0x0B8F42), the owned container's theft (task 0204) and the
   window clicks were NOT exercised: the harness moved the ring with the inventory binding. **Not run.**
8. **The distraction scene's actors.** `TG00SP` started with 4 aliases and played its first lines (Brynjolf's "Everyone! Gather
   'round!", Madesi's "Turn out your pockets"); that Madesi walks away from his stall and Brand-Shei is arrested was not
   looked at (the arrest quest `TG00SPArrest` starts at `TG00SP` stage 20, which the plant set). **Not checked.**
9. **Brynjolf walking up to the player.** The package's ForceGreet is a walk to the player then a greeting; the brain stands him
   at his post and a Lua script activates him when he is within 3,000 units (hand-written, 5.3). **Not built.**
10. **Timing:** the forced greeting waits for the Papyrus host (about 20 s of real time after the start), the dialogue worker and
    the 8:00 to 20:00 schedule; the check sets the game hour to 12 (a harness aid).

## 3. What this slice builds

### 3.1 Alias packages (`[extension.skyrimse] quest alias packages`, off by default; needs `actor packages`)

`QuestAlias::mPackages` (the alias's ALPC subrecords, in order). When an actor is resolved by the package brain, the packages of
every RUNNING quest whose alias is that unique actor come first in its list (`SkyrimWorldView::packagesFor`). Which quests run is
read at that moment: a quest that starts or stops later changes the list when the actor is resolved again (an alias that is
filled by a find-matching condition is not handled; only unique actors). The base lists are unchanged.

### 3.2 ForceGreet packages

`PackageRecord::mGreetTopic` (the first PDTO of type 0 with a topic), `PackageIndex::forceGreetTopic` (own or template
procedures name ForceGreet). The package brain tells a hook when an actor's chosen package is one (`PackageBrain::setForceGreetHook`:
at once, then every `kGreetAskEvaluations` evaluations while it stays chosen). The pack records the wish
(`SkyrimWorldView::requestForcedGreeting`, one at a time) and a global Lua script (`forcegreet_global.lua`, the script package
`core.skyrimse.forcegreet` 1.0: `pending_base`, `pending_name`, `gave_up`) opens the conversation the way the engine opens every
talk: it activates the actor by the player when the actor is in the player's cell within 3,000 units, at most every 8 real
seconds. `SkyrimWorldView::beginConversation` then begins `DialogueRuntime::beginForcedTopic` (the greeting is an INFO of the
package's own topic, its choices the TCLT links, as the guard's greeting in task 0258). The fragments of the INFOs run, so
`pTG00FGDone01` becomes 1 and the package stops passing by itself.
A trace aid: `OPENEDEN_PACKAGE_TRACE=<decimal base id>` logs that base's packages and why each passes or fails (`PKTRACE`).

### 3.3 Conditions and live variables (with `quest alias packages` only)

With the setting on, the package world answers `IsPlayerMovingIntoNewSpace` (always 0: the pack is not told when the player passes
a door), the conditions that run on the player (run-on reference 0x14) about his place (`GetInWorldspace`, `GetInCell`,
`IsInInterior`, from the place the core tells the pack: `SkyrimConditionWorld::setPlayerPlace`, fed by the driver's
`processPlace`) and reads quest script variables live (`PapyrusHost::anyNumberVariable`, as the houses and the main quest do). With
the setting off none of it is told to the package world: lists that contain these conditions fail closed, as before.

### 3.4 Items with identity (the ring), unconditional but narrow

`AddItem` given a placed item REFERENCE adds the item it is an instance of (`MadesiChest.AddItem(RingAlias.GetRef())` put the ring base
0x076F12 into the strongbox; before, the reference's own id went to the inventory and nothing arrived). `PapyrusHost::onItemMoved`
also sends `OnContainerChanged(new, old)` to the scripts of every alias whose filled reference is an instance of the moved item
(`TG00RingScript` on `TG00RingAlias`), besides the scripts on the item's base form it already told. This is NOT behind the setting
(papyrus on is enough): any other quest that adds a reference through `AddItem` gets the item now. The ring is tracked by its BASE:
two rings of that base would both fire the event.

### 3.5 A link that is no choice (dialogue, general)

A TCLT link to a topic with no name whose INFO has no prompt used to be dropped by `offerLinks`. In the game's own flow it is the
speaker going on: Brynjolf's after-plant greeting (`TG00BrynjolfOutroPreBranchTopic`, INFO 0x0A6D76) links to `...Branch01a`, a topic with no
name whose INFO (`TIF__0008B5E1`) pays 100 gold and sets `pTG00Dialogue01`. `DialogueRuntime::offerLinks` now follows such a link
when no named choice is offered: the INFO's text joins the turn, its voice files are appended, its fragments run, and the links of the
followed INFO are offered in turn (depth 4). Applies to every link-mode conversation (the guard's of task 0258 included); with a named
choice present nothing changes.

## 4. Not built, with reasons

Gaps 6 to 9 above. Reverse pickpocketing needs an inventory-into-NPC action (the pickpocket window's `transfer` ignores a put) and
the perk entry point; both touch task 0208's code, which other work is changing. The strongbox's real opening (lock picking,
window clicks) is task 0203's and 0182's code; the ring's identity by BASE is a stand-in for a per-reference item table.

## 5. Results (Windows, build tree `I:\Projects\OpenEden-work\ragdoll5\build`, five Skyrim masters)

### 5.1 In game (`tools/skythieves_check/check.sh`, a new game in Riften's market, real input through `OPENEDEN_RMLUI_INPUT_SCRIPT`)

With `[extension.skyrimse] quest alias packages = true` (and `papyrus`, `journal`, `actor packages`, `inventory`, `lockpicking and books`, `crime`,
`sneak`, `[GUI] rmlui`, `controller menus`), the voice archive, fixed frame time, volumes 0, quiet window. Every line below is the game's
own data and scripts running; `MQ ...` lines are the harness. Frames: `I:\Projects\OpenEden-work\ragdoll5\thieves\keep\`.

1. About 20 s in (the host starts; game hour 12 by the harness) Brynjolf's package `TG00BrynjolfForceGreetPlaza` passes, the brain asks
   (`FORCEGREET ... wants to say topic 217813`, DIAL 0x0352D5), the Lua script activates him and the game's own box opens with his
   voiced line "Running a little light in the pockets, lad?" (`greeting_box.png`); fragment `TIF__000352E0` runs.
2. Seven turns through the game's own choices (controller buttons on the box): "I'm sorry, what?", "How could you possibly know that?", ...
   "What do I have to do?", "Why plant the ring on Brand-Shei?" (`TIF__00038AB3`): **TG00 stage 8**, `Fragment_14`; the journal (J) lists
   "A Chance Arrangement - running ... [ ] Meet Brynjolf during daytime" (`journal_stage8.png`).
3. A second talk (the harness activates him; the real key found only his stall): the box offers "How am I supposed to do all of
   this?", "Why are we doing this to Brand-Shei?", "I'm ready. Let's get this started." (`second_talk_box.png`); the third gives
   **stage 10**: `Fragment_4` runs, `TG00SpectatorQuest` starts with 4 aliases, its scene plays Brynjolf's "Everyone! Everyone! Gather 'round!"
   and Madesi's "Turn out your pockets"; the journal lists "[x] Meet Brynjolf during daytime, [ ] Steal Madesi's Ring"
   (`journal_stage10.png`).
4. `MadesiChest.AddItem(RingAlias.GetRef())` puts the ring in the strongbox (`MQ move: 00076f14 holds 00076f12 x1`); a harness call of the
   inventory binding moves it chest -> player: `TG00RingScript.OnContainerChanged` sets **stage 20** (`Fragment_6`); a second call player ->
   Brand-Shei sets **stage 30** (`Fragment_8`) and `TG00SP` stage 20.
5. Stage 30 makes `TG00BrynjolfForceGreetPlayerAfter` pass: a second forced greeting; its INFO links to an UNNAMED topic and the speaker
   goes on: "Looks like I chose the right person for the job. And here you go... your payment, just as I promised..."
   (`TIF__0008B5E1`: 100 gold, `pTG00Dialogue01`; `after_plant_payment_f4.png`). The next greeting offers "What's been going on?" and
   "I can handle it." (`TIF__000355CB`): **TG00 stage 200**, `Fragment_10`: `TG01.SetStage(10)`, `Stop()`. The journal then lists
   "Taking Care of Business - running: Brynjolf has promised me more work ... [ ] Locate Brynjolf at the Ragged Flagon" and TG00 under
   Completed (`journal_tg01_started.png`, which is `final_8.png` of run f6).
6. **Three runs** (f6, f7, f8; 6,800 frames each): the 92 `NTALK`, `PAPYRUS quest`, `PAPYRUS dialogue`, `FORCEGREET`, `SCENE`, `MQ`, `INV` and
   `JOURNAL` lines are byte-identical after the timestamps and poll counters are cut (md5 `f417814c65fa`); exit 0, no dump, the two error
   lines are `CompanionsBladeFragmentTracking` Papyrus errors that every Skyrim run with Papyrus has. An earlier set of three runs (before the
   chained link) also matched (md5 `a1a3d6d837b1`).
7. Setting off (g0): no `FORCEGREET`, no `NTALK`, `forcegreet_global.lua` is not started (the namespace is not declared).

### 5.2 Tests

New `components-tests` (`testskyrimsethievesguild.cpp`, 21: package 4, alias 1, brain 5, dialogue 5, bindings 1, conditions 5), all pass.
**Written after the code** (no red run against stubs for them), except the chain test, which was run against the code with the chain
switched off (`false &&`): 1 of the 5 dialogue tests failed (the 4 that passed pin unchanged behaviour: the greeting, a named choice winning
over an unnamed link, no passing INFO, no such topic). Six one-line mutations of the finished code each made named tests fail (9 failures in
all): the ForceGreet procedure check ignored (`aPackageThatIsNotForceGreetNamesNoTopicWhateverItsInputs`), the ALPC push removed
(`anAliasKeepsThePackagesItGivesInTheirOrder...`), the ask interval 0 (`theRequestIsRepeatedEveryFewEvaluationsNotEveryOne`), `onlyTopic`
ignored (4 dialogue tests), the player-worldspace branch off (`anExteriorPlaceAnswersTheWorldspace...`), 358 always answered
(`theMovementIntoANewSpaceIsAnsweredOnly...`). Wide run (`*Skyrim*:*Scene*:*Papyrus*:*Extension*:*Neutral*:*Frozen*:*Package*:*Quest*:*Dialogue*:*Condition*`):
3,838 ran, 3,732 passed, 106 skipped (gated on game data), 0 failed. No existing test was edited. **Not tested:** the host glue (`AddItem` by
reference, `OnContainerChanged` to alias instances), the world view's alias-package list, the live variables in the package world and the Lua
script: they are proven by the in-game run only.

### 5.3 What was hand-written (not the game's own)

* `forcegreet_global.lua`: the activation of the actor (in the player's cell within 3,000 units, at most every 8 real seconds) and the pack's
  wish. Brynjolf does NOT walk up to the player (gap 9).
* The harness (`tools/skythieves_check/luatest`): the game hour 12; `ACTIVATE` (the second talk is a Lua `activateBy`, the real Space key found only
  the stall, see 5.4); `MOVES` (the ring is moved with the inventory binding in place of the window's Take and a pickpocket Put: the
  strongbox's lock, the sneak and the chance of the plant were NOT exercised); the input script's controller presses.
* The pack: the ALPC list, the ForceGreet wish, 358 = 0, the player's place for conditions, the ring's identity by base, the unnamed link as a
  continuation. Constants (ours): `kGreetAskEvaluations` 5, 3,000 units, 8 s.

### 5.4 Findings by running it

* The first run never greeted: the package conditions needed `IsPlayerMovingIntoNewSpace` (358, unanswered), and `GetInWorldspace` run on the
  PLAYER, which the pack answered only for the speaker; the daytime schedule (8:00 to 20:00) needed the game hour; `PKTRACE` (the new aid) showed each.
* The crosshair of the real activate key finds `TG00BrynjolfStall` (distance 140), a static in front of Brynjolf, or nothing (aimed from the
  side); the conversation through the key was not reached, the check activates him from Lua.
* The prison guard of `DialogueRiften` asks for a ForceGreet too (base 0x458A7, topic 0x65C40): the first asker keeps the single slot until its
  wish is served or dropped; the Lua script drops a wish whose actor is absent (`gave_up`).
* After stage 30 `...ForceGreetPlayerAfter` has no variable gate (only the stage), so Brynjolf greets again each time until the outro topics
  set stage 200 (the data's design, seen: "Hmm?" with "What's been going on?").
* An unnamed TCLT link is the speaker going on (the payment): without it the after-plant greeting ended with 0 choices.
* **Not shown:** that a greeting said once is not repeated at stage 0 through the live variable `pTG00FGDone01` (a run that said Goodbye at once was
  not conclusive: the box took the chosen path); in the runs the package stopped because stage 8 let `WaitPlazaDay` win.

### 5.5 Morrowind unchanged

See the changelog row (`tools/worldobjects_check`, the fixed Balmora frame).

## Decisions for toddhow

1. `quest alias packages` also switches on the ForceGreet conversations of EVERY actor whose chosen package is one (also from the base lists):
   the Riften prison guard asks too. Alternative: only alias packages greet.
2. `AddItem` with a reference argument and the alias `OnContainerChanged` are not behind the setting (papyrus on is enough).
3. An unnamed link is followed in every link-mode conversation (the guard's included).
4. The real activate key could not reach Brynjolf (his stall is in the way); the stall's collision/ray is a separate defect.
5. Reverse pickpocketing and the strongbox by hand: which of the two should come first (they need the pickpocket window's put and the perk entry point 0x38).

## Stage log

Not run: the Linux gate and GCC -Werror (hand-checked for the traps), clang-format 14 (not installed; some comment lines pass 120 columns), clang-tidy, the Lua tests, a fresh review, TG01 beyond its first stage, the strongbox by hand, the plant by a sneaking player.


## Merge onto Companions (task 0264)

The branch was rebased by a squash merge onto `main` after task 0264 had built its own forced greeting. There is one
mechanism now: `forcegreet.*` (`ForceGreet`, namespace `skyrimse.forcegreet` 1.0, unchanged), one Lua script, `SkyrimWorldView::setForcedGreeting`
and `DialogueRuntime::beginForcedTopic` (Companions' `selectInfo` version: the greeting is marked said). From this task
it keeps `ForceGreet::requestFromBrain` (the brain's re-asking: dropped when the conversation ends or after
`kBrainMaxAsks` asks, the plain conversation when the greeting cannot be said), the unnamed-link following in
`offerLinks`, the alias packages (ALPC), the player-place conditions and the live quest variables. Removed: `force_greet.*`,
the `pending_name` and `gave_up` functions, `SkyrimWorldView::requestForcedGreeting` and the second builtin script line;
`mGreetTopic` is `mForceGreetTopic` (the first PDTO of type 0 with a topic). Setting `companions` and setting
`quest alias packages` each switch the mechanism on.
