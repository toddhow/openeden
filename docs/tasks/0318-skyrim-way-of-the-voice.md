# Task 0318: the main quest after Dragon Rising (Dragonsreach outro, the Greybeards' call, MQ105 "The Way of the Voice")

Branch `mq105-0318` from `5648e037bc` (task 0316), rebased onto local `main` `bc71fa64b4`. Windows only, not pushed. Behind the existing
`[extension.skyrimse] main quest whiterun` (off by default, no new setting) except two general fixes listed in section 5.
Same hand wrote the tests and the code; the tests were written first and run red against stubs (section 4); no fresh reviewer.

## 1. Survey (houseCARL on Skyrim.esm, scripts decompiled from `Skyrim - Misc.bsa`)

- **The "guards' Dragonborn lines" of task 0313 are `MQ104OutroScene`** (SCEN `0x05A6BE`, 23 phases, actors Balgruuf (alias 5), Hrongar (10) and
  Proventus (54), all in Dragonsreach). Task 0313's guess that its aliases need stages 10 to 60 was wrong: the scene needs only its three unique-actor
  aliases and **the player in Dragonsreach**. In 0313 the player stood at the dragon, so the scene had no loaded speaker. Phase 0 waits for MQ104 stage
  120; phases 15, 16 and 18 (Balgruuf's "Dragonborn ... Go to High Hrothgar" lines) start only when `GetDistance(Player alias) < 1000` holds on Balgruuf's
  alias; phase 19 waits for the reward greeting (stage 160); `SF_MQ104BOutroScene` sets stage 170.
- **MQ104 stages after 90:** 100 (a Fus cast), 105/108 (`DragonDeathScene`), **110 `Fragment_39`: story event `MQGreybeardCallKeyword` (the Greybeards'
  call scene `0x10EC63`, the summons the whole of Whiterun hears) and `MQ105.SetStage(5)`**, 120 (`SetStage(110)`), 130/150 (**150: `MQ105.SetStage(10)`**),
  **160 `Fragment_45`: Balgruuf's relationship 3, `GetOutofJailCard`, `AddItem(Alias_JarlWeapon.GetRef())`, Lydia (`Alias_PlayerHousecarl`, `HousecarlWhiterun`)
  enabled and moved to the messenger marker**, 200 done. The player alias script `MQ104PlayerScript` waits for **`OnLocationChange`**: it sets stage 110 six
  seconds after he enters the headquarters hold or `HighHrothgarLocation`, and `OnObjectEquipped` (a shout: stage 92).
- **Balgruuf's outro dialogue** is a Blocking branch (`MQ104BalgruufBlockingTopic`): A1 "The watchtower was destroyed...", A2 "Turns out I may be something
  called Dragonborn", A3 "I killed the dragon...", then B1/B2 and C1/C2 (`TIF__0005EE38` sets stage 150, which the host runs when the box closes). The
  **reward greeting** (phase 19: "...I name you Thane of Whiterun. I assign you Lydia as a personal Housecarl, and this weapon from my armory...") is
  `TIF__000E0EB8`; its Goodbye sets stage 160.
- **`JarlWeapon` (alias 59):** `ALCO` = leveled list `FavorRewardEnchAxeBest`, `ALCA` = 5 **with bit 31 set** (houseCARL: `Create = In`: the axe is created IN
  Balgruuf's inventory, not at his feet).
- **MQ105 (QUST `0x0242BA`, Update.esm wins):** stage 5 (the call, `Fragment_9`: Greybeards friends, High Hrothgar doors unlocked), 10 (objective, map
  markers), **15 (`MQ105PlayerScript.OnLocationChange` into `HighHrothgarLocation`)**, **30 (Arngeir's "I am answering your summons" `TIF__0002F2C5`: objective
  10 done, 20 shown; three fragments with log-entry conditions: 17, 75 (MQ104 130 and 150, only if the player came here before returning) and 20 (stage 50 only
  if a Greybeard was already struck))**, 25 (`MQ105ShoutTargetScript.OnMagicEffectApply`: the player's push effect hits Einarth, Wulfgar or Borri), 50/55/56/58/59/60
  (the push trial scenes, `EinarthTeachScene`), 82/85 (`OnObjectEquipped`: the player equips Unrelenting Force), 105 (`UnlockWord(WordRo)`) and **`Fragment_25`:
  `Game.TeachWord(WordRo)`** (Einarth's word, taught when the player has "absorbed" it). Fus is `TeachWord` and `UnlockWord` at MQ104 stage 90.

## 2. What was built

1. **`PapyrusHost::onLocationChange(actor, old, new)` and `onObjectEquipped(actor, base, reference)`** (OnLocationChange and OnObjectEquipped to the scripts that
   hold the actor, i.e. the player's alias scripts). Fed from the player-place sink (the same place that raises the story manager's CLOC event) and from the shout
   menu's Equip (`shouts::Hooks::mEquipped`, optional). Only with `main quest whiterun` (`PapyrusDriver::enablePlayerEvents`).
2. **Alias distances between two references** (`SkyrimConditionWorld::aliasSubjectPlace/aliasPlace`): `GetDistance(alias)` on a quest alias used to be measured only
   from the player to an alias holding a placed reference; "the player alias" (ALFR = PlayerRef) was never known, so MQ104OutroScene's phases 15, 16 and 18 were
   skipped. Now the subject can be a placed actor (live place when the core tells, else where it is placed), the alias can hold the player, and two placed
   references must be in one cell. Not behind a setting (a general fix of the quest and scene conditions).
3. **A quest alias that creates IN an inventory** (ALCA bit 31): `createAliasObjects` masks the flag; the item is the leveled list's pick
   (`PapyrusWorldHooks::resolveLeveledItem`, `inventory::resolveContents` seeded by the list id at the player's level) and the alias holds the item, so the game's
   own `AddItem(Alias_JarlWeapon.GetRef())` finds something to give. Not behind a setting (three other quests create items this way: they now hold them).
4. **A stage's fragments honour their log entry's conditions with `main quest whiterun`** (as with `houses`, `main quest start`, `dragonborn`): without it
   Arngeir's talk (stage 30) ran all three of its fragments and jumped to stage 50, skipping the push trial.
5. Harness inputs: `tools/skywhiterunmq_check/inputs/` (the click scripts; `README.txt`).

## 3. In game (real mouse clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`, the five masters, player level 10, harness `tools/skywhiterunmq_check`)

Aids (the Helgen part and the dragon fight are parked by the earlier tasks): `story test quest = 02610C:90,02610C:120@25` (MQ104 stage 90 = the dragon is
dead, the soul and the word given; stage 120 = Balgruuf's scene waits for it). The player starts in Dragonsreach.

**Dragonsreach (frames `I:/Projects/OpenEden-work/mq318/runs/*/user/screenshots`):** MQ104 90 runs `Fragment_35` (Fus taught and unlocked, `OutroScene.Start`); 120
runs `Fragment_41` and 110 (`Fragment_39`): MQ105 stage 5 and **the Greybeards' call scene (`0x10EC63`)**. The outro scene plays with the real lines and
voices: Proventus ("Good. You're finally here"), Balgruuf ("You heard the summons"; "The Greybeards..."), Hrongar. **Balgruuf force-greets** (the blocking topic:
"So what happened at the watchtower?"); the clicks A2 "Dragonborn" (his answer: "Dragonborn? What do you know about the Dragonborn?"), B1, C1 "The Greybeards?" and
C2 "What do these Greybeards want with me?" (answer "Masters of the Way of the Voice...", "The Dragonborn is said to be uniquely gifted in the Voice...") run
`TIF__0005EE38`; Goodbye ends the conversation and **MQ104 stage 150 and MQ105 stage 10 are set** (journal objective). The scene goes on (phases 6 to 14,
Hrongar and Proventus), then **phases 15, 16 and 18 play** (Balgruuf: "Whatever happened when you killed that dragon, it revealed something in you, and the
Greybeards heard it...", "I envy you, you know. To climb the 7,000 Steps again...", "No matter. Go to High Hrothgar. Learn what the Greybeards can teach you.")
and phase 19 **force-greets with the reward: "You've done a great service for me and my city, Dragonborn. By my right as Jarl, I name you Thane of Whiterun. I
assign you Lydia as a personal Housecarl, and this weapon from my armory to serve as your badge of office..."**; its Goodbye sets **stage 160**: `Fragment_45` runs
(Lydia `000A2C94` enabled and moved into the hall's cell), stage 200. The alias now holds the axe (`PAPYRUS quest 0002610C alias JarlWeapon holds the item 000896B0
made from 000F8A49`; before: "is created at alias 2147483653, which holds nothing"). Lydia then talks ("Yes?") with only a Goodbye.

**High Hrothgar (the walk up the 7,000 Steps is replaced by a Lua teleport of the player into the cell `HighHrothgar`, `STEPS="{ 3000,'cell','HighHrothgar',...}"`):**
**`MQ104PlayerScript.OnLocationChange` fires by itself: `PAPYRUS story event CLOC to location 00018E34`, six seconds later MQ104 stage 110** (the call); with MQ105 at
stage 5 (aid) **`MQ105PlayerScript` sets stage 15**. Arngeir (activated with `object:activateBy`, Lua) opens his real greeting ("So... a Dragonborn appears, at this
moment in the turning of the age."; topics "I'm answering your summons.", "You call me Dragonborn. What does that mean?"); the click runs `TIF__0002F2C5`
("We will see if you truly have the gift. Show us, Dragonborn. Let us taste of your Voice."), Goodbye sets **MQ105 stage 30** (objective 20 shown); with the
log-entry conditions on, `Fragment_20` (stage 50) is NOT run and `Fragment_75` (MQ104 130 and 150: the player came here first) is. The real **H** key opens the shout
menu ("Fus (Force)" unlocked), a click on Equip **equips Unrelenting Force** (`OnObjectEquipped` is sent), the real `=` key shouts and **Arngeir is hit** (health bar,
cooldown 12.1 s on the HUD). Frames: `runs/b1/.../screenshot005.png` (the Thane speech), `runs/c5/.../screenshot000.png` (Arngeir), `runs/c9/.../screenshot000.png`, `001.png`
(the shout menu equipped; the shout).

## 4. Tests and checks

- Tests written first: `SkyrimPapyrusMq318Events` 4 (red against empty host functions: **2 of 4 failed**, the 2 that passed test absence), `SkyrimAliasDistance` 6
  (**2 of 6 failed** against the player-only measure, the other 4 test absence and pins), `SkyrimMq318Created` 4 (**2 of 4 failed** against the old ALCA decode, the other 2
  test absence). Green: the 14 pass; the wide filter `*Skyrim*:*Papyrus*:*Scene*:*Dragon*:*Shout*:*Quest*:*Dialog*:*Condition*` runs 4,015 tests, 0 failed (after the rebase onto `bc71fa64b4`).
  **The glue has no unit test** (the driver's calls, the shout menu hook, the stage-entry-check condition): the game runs are its evidence.
- **Repeatability** (final build, three runs in parallel each, trace lines of the quest stages, scenes, dialogue turns (poll numbers masked), shouts and the harness steps): the
  High Hrothgar scenario (`inputs/mq318_arngeir.txt` with the shout) gives **52 identical lines (md5 `88f0f22b4ebe`)**; the Dragonsreach chain to the Thane speech and stage 160
  (`inputs/mq318_thane.txt`) gives **79 identical lines (md5 `c170d1bcf7e2`)**.
- **Morrowind unchanged** (against `I:/Projects/OpenEden-work/integ2/build`, not a build of the exact parent): `tools/worldobjects_check` 110 objects OK; the fixed Balmora frame
  (`--start Balmora --random-seed 7`, frame 600) is `1c5d47ad0b5dc32d` for both.
- Existing tests edited: **none**. New test files: `testskyrimsepapyrusmq318.cpp`, `testskyrimsemq318alias.cpp`, `testskyrimsemq318created.cpp`.

## 5. Gaps, ranked

1. **MQ105 stops after the push at Arngeir.** The push trial needs the shout to hit Einarth, Wulfgar or Borri (`OnMagicEffectApply` on their aliases: stage 25); in the
   run the shout hit Arngeir, who stood in front. The trial scenes (50 to 85), Einarth's word (`TeachWord(Ro)`, `Fragment_25`) and "Fus Ro Dah" (the Horn, stage 105+) were not
   run, so **Ro and Fus Ro are NOT taught**. The way up (the 7,000 steps, the pilgrim, the door) is a teleport.
2. **Arngeir is activated by Lua** (`activateBy`), not the real key (the Greybeards stand in a cave with the crosshair on the wall in the harness view).
3. **Lydia is not a follower yet:** after stage 160 she stands in the hall and force-greets "Yes?" with no topics (the housecarl's "follow" topics test
   `PlayerHousecarlFaction`, which nothing sets; not investigated). The axe is held by the alias; whether `AddItem` put it in the inventory was not seen (the
   inventory window was not opened).
4. The "Thane" status itself: the game's `GetOutofJailCard` and the dialogue are the only effects; no thane flag exists in this host.
5. Fus is given by the aid's stage 90 (`TeachWord` and `UnlockWord`); the dragon fight is task 0313's. The Helgen opening and the first MQ104 stages are parked.
6. The real way into Dragonsreach for the outro (the player returns to Whiterun) is the harness start; stage 120 is the aid (nothing in the data run set it).

## 6. Decisions and edits for toddhow

1. **Two general changes are not behind `main quest whiterun`:** the alias distance (section 2.2) and the ALCA "In" decode (2.3). Both fix quest conditions that were silently
   false or empty; the second makes three other quests' item aliases hold an item (`000266F2 Gloves`, `00028A24 Book`, `Note`).
2. **A stage's fragments honour their log entry's conditions with `main quest whiterun`** (as `houses` does), which also changes the earlier MQ102 to MQ104 chains:
   stage 160 still runs `Fragment_45` and `Fragment_77` (stage 200), checked in game.
3. **The alias holds the ITEM id for an `In` creation** (the pack has no item references); `GetRef()` of such an alias is a base form, not an object reference.
4. Existing tests edited: **none**.

## 7. Stage log

- Edits outside new files: `papyrus_host.{hpp,cpp}`, `papyrus_world.{hpp,cpp}`, `shouts_service.{hpp,cpp}`, `dialogue.{hpp,cpp}`, `skyrimse_game_pack.cpp`,
  `apps/components_tests/CMakeLists.txt`.
- Harness: `I:/Projects/OpenEden-work/mq318/run.sh <name> <input file> <frames>` (a copy of 0313's), env `STEPS`, `ACTIVATE`, `SETTINGS_EXTRA`.
- Decompiled scripts (read only, not committed): `I:/Projects/OpenEden-work/mq318/psc`.
- **Not run:** the Linux gate, GCC (the pitfalls were checked by hand), clang-format 14 (formatted by hand), clang-tidy, the Lua tests, a fresh review, saves in the
  middle of the chain, the whole `openmw-tests`, Einarth's word and "Fus Ro Dah".
