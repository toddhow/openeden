# Task 0342: Skyrim side quest "Laid to Rest" (MS14, Morthal), first slice

Follow-up of tasks 0333 (a live GameHour, scenes with no actions), 0337 (scenes, kill moves, the pack's `enableDaedric` world)
and 0304 (quest greetings). Same hand wrote tests and code, tests first and run red (see section 6), no fresh reviewer. Windows
only. Branch `laidtorest-0342` from `d1b31269b0`, rebased onto `31579ff7ea`, local commits, not pushed. Behind the new pack
setting `[extension.skyrimse] laid to rest` (off; declared in `extension.json`; needs `papyrus`, `actor packages`, `quest alias
packages`, `inventory` and `[Game] neutral inventory` for the coffin's activation; the check also sets `summons` and `combat
spell delivery` for its kill aid).

## 1. The survey (houseCARL records, decompiled `.pex` of `Skyrim - Misc.bsa`)

- `MS14` "Laid to Rest" (QUST 0x25F3E, side quest, RunOnce, priority 60, **StartGameEnabled with a start-up stage 1**): 23 stages
  (1, 5, 6, 7, 9, 10, 20, 25, 30, 35, 40, 50, 60, 70, 80, 90, 95, 97, 100, 110, 120, 130, 200), 10 objectives, 20 aliases
  (Alva, Hroggar, Thonnir, Lami, Benor, Jorgen, Lurbuk the mob, Helgi's ghost, Laelette, Helgi's coffin, Alva's coffin, the burned
  house door, Alva's journal, the master vampire Movarth, the Innkeeper, JarlImperial 0x1AA66 = Idgrod, JarlRebel, a Location
  `GhorunnHall`). Scripts: `QF_MS14_00025F3E` (stage fragments), `ms14` (the quest script: `OnUpdate` for the mob), and alias
  scripts `MS14BurnedHouseScript` (OnUpdate), `MS14AlvasCoffinScript` (OnUpdate), `MS14HelgiCoffinScript` (OnActivate),
  `MS14AlvasJournalScript` (OnActivate), `MS14LaeletteScript` (OnDeath: stage 25), `MS14BossVampireScript` (OnDeath: stage 120),
  `MS14AlvaScript` (OnDeath, OnLocationChange), `MS14GhoruunEntrance` (a trigger).
- **The start is in the quest.** Stage 1's `Fragment_46` runs `RegisterForUpdate(5)` on the quest and on Alva; the burned house
  alias script's `OnUpdate` sets stage 10 when `Game.GetPlayer().IsInLocation(MorthalLocation)` and the player is within 1,000
  units of the burned house door `0x2749E`. Stage 10 makes the town's rumours and Jonna's (the Innkeeper alias) topic
  "Is there a story behind that burned down house?" available; its chain (fire, wife and child, Alva) ends in stage 20 (objective
  "Talk to the Jarl"). **Idgrod** (JobJarlFaction at rank -1) then offers "I hear you want someone to look into that house
  fire." and the chain "What does Hroggar say happened?", "Why would he do that to his own family?", "So why haven't you
  arrested him?" sets stage 30 when the conversation ends (`Fragment_4`: Helgi's ghost is enabled; "Investigate the burned
  house").
- **Helgi's ghost** (NPC 0x274A5, placed at (-40891, 63682, -13778) in Tamriel) speaks at night: "If I do, will you tell me who
  set the fire?" (hide and seek) and "The other one? What do you mean?"; ending that conversation sets **stage 40**
  (`Fragment_7`: Helgi and the coffin's dirt are disabled, Laelette is enabled as an invisible ghost, objective "Find Helgi after
  dark").
- **Laelette** (NPC 0x274A0, placed far away) is a vampire: her death (`MS14LaeletteScript.OnDeath`) sets **stage 25**
  (`Fragment_40`: starts `MS14ThonnirFindLaelette`, SCEN 0x76F4E, 3 phases, 3 actions). Helgi's coffin `0x70E7E` (an Activator,
  base 0xA734E "Saw Mill") carries the alias script `MS14HelgiCoffinScript.OnActivate`: with the stage between 40 and 60, stage
  25 done and stage 50 not, it starts the coffin scene `MS14HelgiCoffinScene` (SCEN 0x29545) and sets **stage 50** (`Fragment_11`:
  objective 60); with stage 50 not done and `GameHour` at 20 or later (or 6 or earlier) it starts `MS14HelgiLaeletteNotDead`.
- **Jonna** (Innkeeper) then offers "What can you tell me about Laelette?" (`MS14LaeletteRumorTopic`, INFO 0x29540, whose TIF
  sets **stage 60**, `Fragment_13`), and Thonnir (the husband) answers "What can you tell me about your wife?", "Did you notice
  anything strange before she left?", "I think they may have met after all." (stage 70: Alva's house, Alva's journal enabled).
- Not decoded or run here: Alva, Hroggar, Lami and the mob (Benor, Jorgen, Lami, Lurbuk walk to Ghorunn Hall), Movarth.

## 2. Faults found by running it, and fixes (all behind `laid to rest` unless said)

| # | Fault | Cause | Fix |
| --- | --- | --- | --- |
| 1 | Jonna never offered the house topic: Innkeeper alias (id 8) was never filled | the alias's first condition is GetIsEditorLocation (565) run on an ACTOR; the function only answered a candidate LOCATION (a location alias) and was 0 for an actor | `getIsEditorLocation` uses the subject's editor location when no candidate location is set and the switch is on (`LocationSubjectSource::actorEditorLocationOn`, `SkyrimConditionWorld::setActorEditorLocation`); the old answer is kept with the switch off |
| 2 | Idgrod's Jarl topic was never offered | JobJarlFaction (FACT 0x50920) is held by every jarl at rank -1 (VERIFIED: Idgrod's NPC_ SNAM) and the lines test GetInFaction == 1; rank -1 counted only for innkeepers, stewards (with `houses`) and hostlers | `laidtorest::kJobJarlFaction` joins the rank -1 members. **Side effect: Idgrod now takes the Jarl alias of BYOHHouseHjaalmarch** (the Hearthfire quest's data says so too) |
| 3 | Stage 10 was never set (or only by luck at start-up) | the burned house alias script handles `OnUpdate` and nothing in the data registers it: the quest's own `RegisterForUpdate(5)` reached the quest's non-alias scripts only (task 0325: "an alias has its own registration") | with the switch a quest script's `RegisterForUpdate`, `RegisterForSingleUpdate` and `UnregisterForUpdate` also reach the scripts of the quest's aliases (`PapyrusHost::setQuestUpdatesReachAliases`). RECALLED-VERIFY: the engine's own rule, inferred from the data (two alias scripts with OnUpdate and no registration anywhere) |
| 4 | Even then `IsInLocation(Morthal)` of the player was false in the open | `PlayerSite::mLocation` held an interior's location only; for an exterior square the player's Papyrus location was 0 | `SkyrimWorldView::setExteriorPlayerLocation` (the location of the square the core reports); `playerLocation()` falls back to it |
| 5 | A save made after a script was attached to a created reference could not be loaded ("the game no longer has what its attach key names") | `PapyrusHost::attachKeyExists` asked the game data about 0xFF8xxxxx forms, which exist only after the load has rebuilt the created table | a form in the created range is accepted when the switch is on |
| 6 | Helgi's coffin never heard `OnActivate` | `PapyrusHost::onActivate` reached scripts on the reference only | task 0338's alias `OnActivate` delivery (merged, one mechanism; my duplicate was removed in the rebase); `laid to rest` only also carries the activation of a scripted activator to the host (`dungeon mechanics`' path) |
| 7 | The coffin script's night test never passed | the global `GameHour` was the record's value (12) | `laid to rest` reuses task 0333's live GameHour and actionless-scene start (one mechanism, `mBloodIceOn`) |

## 3. What runs now

`tools/skmorthal_check` (a fork of `skforsworn_check`; settings listed in its `check.sh`, steps in `luatest/scripts/cmp/global.lua`:
`world`, `cell`, `talk <base>`, `hour`, `save`, `near`), real mouse clicks on the conversation box, frames viewed. A new game
on the road into Morthal: **MS14 stage 10 is set by the game's own alias script** (the player within 1,000 units of the burned
house); **Jonna's chain (real clicks, voiced) sets stage 20** and the journal lists "Laid to Rest: Talk to the Jarl" (frame);
**Idgrod's four-reply chain sets stage 30**; at night Helgi's ghost plays hide and seek (frames) and the end of that talk sets
**stage 40**; killing Laelette (aid) sets **stage 25** and begins Thonnir's scene (phase 0, the package of 0x3C773); the real
activation of Helgi's coffin at night sets **stage 50** by the game's own script; Jonna's Laelette line sets **stage 60**; Thonnir
answers three clicked replies.

Final runs (after the rebase onto `31579ff7ea`, rebuilt): a prelude run from a new game to stage 20 saves; three loads of that save
(`PHASE=load`, 9,800 frames each) give byte-identical 44-line story traces (md5 `e4adbed2a78fdc663f78cb6e38363b8c`), 0 errors, 0
dumps. Frames viewed: Helgi's ghost at night with the hide and seek reply (`I:\Projects\OpenEden-work\papyrus342\keep342\screenshot000.png`) and
Thonnir's "Ye gods! You think Alva is a vampire?" (`screenshot001.png`). Morrowind unchanged: `worldobjects_check` 110 objects OK and
the fixed Balmora frame hashes equal the build before.

## 4. Stand-ins and aids (hand-written, named)

- **Walks are Lua teleports** (`world`/`cell` steps): into Morthal's exterior (Tamriel), the inn, Highmoon Hall, Helgi's place,
  the coffin and Thonnir. The game hour is set to 12 and then 22 by the harness (`hour`).
- **Laelette is killed by the aid `summons test kill = MS14LaeletteVampire@40`** (needs `summons` and `combat spell delivery`):
  the real fight (she is in a lair, 2,300 units away) is not built.
- The activation of the coffin is `activateBy` from Lua (`talk <base>`), not the key.
- The checks load a save made after stage 20 (`save` step): the first 7,000 frames only wait for the host and the dialogue
  index; the clicks are keyed by frame, and the dialogue index needs real seconds, so every conversation waits.

## 5. Not reached (ranked)

1. **The coffin scene `MS14HelgiCoffinScene` is refused** (`none of its actors is loaded (alias references 0x70E7E)`): its actor,
   Helgi's ghost, is disabled by stage 40, and a scene with actions needs a loaded actor. Stage 50 does not depend on it.
2. Thonnir's `FindLaelette` scene runs phase 0 and 1 only; Thonnir never walks to the lair (no package target, no Ghorunn Hall).
3. Stage 70 and later (Alva's house, her journal, the mob and Movarth's lair, stages 80 to 200): Alva, Hroggar and Lami were not
   talked to in a run that finished (a Thonnir conversation left open ate the first Hroggar try), the vampire's lair and
   Laelette's real kill are not built.
4. `GetDistance` between two references that stand in different cells is the plain coordinate distance (Alva's coffin script
   fired a false stage 80 when the player stood in the Moorside Inn at coordinates near the coffin's in Alva's house: the harness
   keeps clear of them).
5. The quest's journal after stage 20 was not photographed (the frame shows stage 20).

## 6. Tests

`apps/components_tests/skyrimse/testskyrimselaidtorest.cpp` (5 tests: GetIsEditorLocation on an actor with the switch off and
on, no location, the candidate wins, the constants) and `testskyrimselaidtorestquest.cpp` (3 tests: the quest's registration
reaches its alias scripts with the switch on and not off, unregistering stops them). Written first; **red against the code with the two behaviours switched off in the `.cpp` (the header API existed):
3 of the 8 new tests failed (the two switched-off behaviours and the registration reach), the other 5 pin refusals and constants**. No existing test edited.

## 7. Decisions for toddhow

- A quest script's registration for updates reaches its alias scripts (only with the setting). The data needs it (MS14); the
  engine's real rule is RECALLED-VERIFY and a general change would alter every quest.
- JobJarlFaction at rank -1 counts as a membership (only with the setting): Idgrod (and, through the same data, every jarl) takes
  the Jarl alias of the Hearthfire quests.
- The alias `OnActivate` delivery is task 0338's; this task adds nothing to it. Stage 70 was not seen (Thonnir's last reply was not clicked).
- Not run: the Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, a fresh review.
