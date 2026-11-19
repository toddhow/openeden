# Task 0338: Skyrim Daedric quest "The Black Star" (Azura, DA01), first slice

Follow-up of tasks 0310 (Barbas, `daedric quests`) and 0332 (Waking Nightmare: escort and follower packages). Same hand wrote tests and
code; tests first where the API allowed (see section 5 for the red counts and the two compile-red files); no fresh reviewer. Windows only.
Branch `blackstar-0338` from `748641d7d3`, rebased onto `d1b31269b0` (one conflict, the player item source of task 0335's `golden claw`: both settings now install it, one mechanism), local commits, not pushed. Behind the existing
`[extension.skyrimse] daedric quests` (task 0310) and, for the one engine change, `[Game] neutral actors`. **No new setting.** The
harness of this task also sets `fast travel` (see section 4).

## 1. Survey (houseCARL records; `.pex` of `Skyrim - Misc.bsa` decompiled)

- **The quest.** DA01 "The Black Star" is QUST 0x28AD6 (priority 60, 16 aliases, 21 stages, 21 fragments in `QF_DA01_00028AD6`), with
  DA01Intro 0x92E25 (the "Visit the Shrine of Azura" objective), `DA01dunIlinaltasDeepQST` 0x89CD1 and DA01FIN 0x1DD49. Stage to
  fragment: 0 -> `DA01Intro.SetStage(10)`, 10 -> intro done, objective 10 ("Find the elven mage"), 20 -> objective 20 (the quest's own
  `OnUpdate` sets it when the player carries Nelacar's book), 30 -> Ilinalta's Deep on the map, 50 -> the Star is in the inventory,
  60 -> `DA01AraneaGiveStarScene` and the Star is taken, 61 -> the altar's trigger and objective "Speak to Azura", 62 -> "Tell Azura
  you're ready", 70 -> the player is moved into the Star (`MoveTo(StarPlayerMarker)`), 80 -> Malyn Varen turns hostile and
  `StartCombat(player)`, 90/95 the two endings' return scenes, 100/110 Aranea's or Nelacar's reward.
- **Who sets what.** Aranea's intro branch (`DA01AraneaIntro*`) sets stage 10 through `TIF__0008CF81`; Nelacar's "Get to the point" line
  sets stage 30 directly; `DA01BrokenStarScript` (an alias script on the Broken Star) sets stage 50 when the Star enters the player's
  inventory with stage 10 done; Aranea's "Here. Take it." (`TIF__000917B6`) sets 60; her scene's end sets 61; Azura's talk sets 62
  (`TIF__000D76C5`) and 70 (`TIF__00093775`); Malyn's choice (`TIF__0004C437`) sets 80.
- **Azura speaks through a TALKING ACTIVATOR.** The altar `DA01ShrineofAzuraTalkingREF` (REFR 0x92493, base TACT `DA01VoiceofAzura`
  0xAC9AA with VNAM voice type 0x2E, "FemaleUniqueAzura") is no actor: `DA01AzuraBlockingBranch*` are its Blocking-branch lines
  (INFO speaker = the NPC_ Azura 0x28AE9, which has the same voice type).
- **Malyn Varen** (NPC_ 0x28AD3, "DA01MalynVaren") stands in `AzurasStarInterior01`; his Blocking branch `DA01MalynVarenBlockingTopic`
  speaks when the player arrives; its choices set stage 80.

## 2. What runs in game (frames and logs `I:\Projects\OpenEden-work\combat3\n338\runs`, scripts `r5.sh`, `in_e5.txt`)

New game at Azura's shrine (`@Tamriel,112000,77500,2250`), level 10, `daedric quests` with `quest alias packages`, `actor cell moves`,
`services`, `fast travel`, real mouse clicks on the conversation box (`OPENEDEN_RMLUI_INPUT_SCRIPT`), quiet window, master volume 0,
fixed frame time, `player speech 60`, `player gold 500`:

1. Aranea's greeting and intro branch (voiced), "Very well. What does Azura need?" runs `TIF__0008CF81`: **DA01 stage 10**,
   DA01Intro stage 100, the journal's objective "Find the elven mage from Aranea's vision" (frame).
2. (stand-in) the player is put in Winterhold's Frozen Hearth; Nelacar's talk: the bribe line ("Look, I'm willing to pay. (120 gold)"),
   his answer is **four Invisible Continue lines in a row**, "Get to the point." sets **stage 30** and shows Ilinalta's Deep on the map.
3. (stand-in) the player is put in `IlinaltasDeep02` and activates the Broken Azura's Star: the pickup path puts it in the inventory,
   `DA01BrokenStarScript` sets **stage 50**.
4. (stand-in) back at the shrine: Aranea's greeting now offers "Azura's Star! ... Hand it over" (her condition reads the player's
   inventory), "Here. Take it." sets **stage 60**, her scene plays (the Star appears on the altar), its end sets **stage 61**.
5. The altar is activated: **Azura speaks** with her own voice ("Greetings, mortal. You have followed my guidance ..."), "Then why
   don't you remove it?" sets **stage 62**, "I'm ready to enter the Star." and Goodbye set **stage 70**: the game's `MoveTo` puts the
   player in `AzurasStarInterior01` (718 objects).
6. **Malyn Varen's Blocking branch speaks** there (frame), "This experiment of yours is over!", his reply, Goodbye: the walk-away topic's
   fragment sets **stage 80** (`Fragment_57`: aggression 2, the enemy faction, `StartCombat`) and the combat AI sends him walking
   toward the player (a path of 34 waypoints).

**Three runs** of this whole scenario (`r5.sh`, `in_e5.txt`, run in parallel after the rebase) give 88 trace lines (stages, talks,
scenes, pickups, steps, the topic and INFO chosen); with the poll counters of the click timing normalised the three are
byte-identical (md5 `ca3193692f7d`); raw, runs 1 and 3 are identical and run 2 differs in poll numbers only. Morrowind unchanged against
`integ2`'s build (a build of `main`, not the exact parent): `worldobjects_check` 110 objects OK, both Balmora map frames
byte-identical (`1eb68236352dc472`, `c819db116f4af8a6`). `components-tests` filter `*Skyrim*:*Dialogue*:*Papyrus*:*Quest*:*Scene*:*Package*:
*Daedric*:*Companions*:*College*` 4,230 ran (4,099 pass, the rest skipped for data), `openmw-tests` `*Neutral*:*Skyrim*:*Extension*` 1,192 ran
(1,190 pass). Frames viewed (`I:\Projects\OpenEden-work\combat3
338\keep`): the journal objective, Nelacar's chain, Aranea's Star
offer, Azura's talk (two), Malyn's greeting and reply.

## 3. Faults found by running it and fixes (one mechanism each)

| # | Fault | Cause | Fix |
| --- | --- | --- | --- |
| 1 | Nelacar's answer ended with a button "(Invisible Continue)" | the response flagged Invisible Continue (INFO flag 0x40) goes on through its links even when the topic linked has a NAME (the editor's own text "(Invisible Continue)"); only nameless topics were followed, and at most 4 | `DialogueRuntime::followNamelessLinks(out, from)` follows a named topic when the response just said has the flag; the depth is 8; the forced mode's `offerLinks` too |
| 2 | Aranea never offered the Star lines | `GetItemCount(star)` on the player read 0: the player's item source was installed only with `marriage` (and `golden claw`) | also installed with `daedric quests` (the same hook, combined with 0335's condition) |
| 3 | The altar's script never heard the activation | a quest ALIAS that holds a reference gets no `OnActivate` of it (`DA01ShrineofAzuraScript`) | `PapyrusHost::onActivate` also tells the alias scripts that hold the reference; `hasHearers`; the activation hook is on with `daedric quests` |
| 4 | Azura's altar could not be talked to | a TACT is an Activator, the engine opened the ore-vein event; the pack knew no voice type for it; the INFO's speaker is the NPC_ Azura | `ModelRecord::mVoiceType` (TACT VNAM); `SkyrimConditionWorld::setActivatorVoiceLookup`/`speakerSoundsLike` (a line meant for an NPC_ is spoken by a talking activator that has its voice type); new optional SDK interface `TalkingActivatorSource` (no version change); `Neutral::activate` opens a conversation for such an Activator |
| 5 | Azura's and Malyn's lines never started a talk ("no greeting passes") | their greeting is a Blocking branch | `setBlockingBranches(true)` under `daedric quests` (as the Civil War, task 0298); this also hides Nelacar's "Hello." choice, as in the game |
| 6 | Malyn's fight never started | his Goodbye runs the walk-away topic | `setWalkAwayTopics(true)` under `daedric quests` (task 0315's mechanism) |

Also: a developer trace `NQUESTGREET` (greeting topic chosen, topic and INFO of every button, the INFOs followed) under
`daedric quests`; `SetStage` logs a refusal ("no such quest", "the stage is done"); the debug line of a skipped INFO names the INFO's
speaker and the speaker.

## 4. Stand-ins (hand-written, named)

- **The trip Winterhold to Ilinalta's Deep to the shrine is teleports** (`cell` and `back` steps of `tools/skydaedric_check`): the real
  walk is 12 km and the doors of the Deep are not built.
- `activateBy` of the Lua test script stands for the key press at Aranea, Nelacar, the Star and the altar (the object is found in the
  player's cell or in a neighbouring loaded cell).
- `player gold 500` and `player speech 60`: Nelacar's persuade line failed at the default speech, the bribe line needs 120 gold.
- **`fast travel = true` is needed** for the game's own `Game.GetPlayer().MoveTo` into the Star (task 0325's move sink lives in the
  travel service); `daedric quests` does not switch it on.
- Stage 20 is skipped: the game sets it from the quest's `OnUpdate` when the player carries Nelacar's book (nothing registers that
  update here); the dialogue's stage 30 comes first.
- Malyn is not killed by the player: `summons test kill = DA01MalynVaren@30` (a developer aid) killed him in a run; nothing in the
  quest follows (see section 6).

## 5. Tests (written first, run red)

- `testskyrimseblackstarinvisible.cpp` (`SkyrimBlackStarInvisible`, 4): red 2 of 4 against the code before (the two that passed pin
  the unflagged topic and the switch off); `testskyrimseblackstaractivate.cpp` (`SkyrimBlackStarActivate`, 3): red 2 of 3 against a
  stub `hasHearers` that answered `hasInstances` and the old `onActivate` (the third pins another reference); both run in the red
  build of this task.
- `testskyrimseblackstartalking.cpp` (`SkyrimBlackStarTalking`, 4): **compile-red only** (the fields and functions did not exist; the
  file was written first and built once, after the code).
- No existing test edited. The glue (`skyrimse_game_pack.cpp`, `Neutral::activate`, `SkyrimCellSource::speaksAsActivator`) has no unit
  test; the game run is its evidence.

## 6. Not reached

The Deep itself (the real doors, the dungeon, Malyn's pedestal), stage 20, Nelacar's route (stages 45, 57, 65: his forced greeting scenes,
the soul trap), **the fight with Malyn and the endings (stages 90 to 110, the final scenes, Aranea's or Nelacar's reward)**; Malyn arrives
walking but no blow was exchanged in the runs (the player is a stand-in with no weapon); Azura's speaker name shows the editor id
`DA01VoiceofAzura` (the TACT has no FULL: the game shows "Azura").

## 7. Decisions for toddhow

1. **Blocking branches and walk-away topics are now on with `daedric quests`** for every actor (as for the Civil War and the Companions):
   a Blocking branch speaks in place of Hello when its conditions pass; "Hello." is no longer offered after one.
2. The player's item source is installed with `daedric quests` (also `GetEquipped` on the player).
3. A talking activator speaks the lines meant for any NPC_ with its voice type (not only the NPC_ Azura); `TalkingActivatorSource` is a new
   optional SDK interface (no version change).
4. The Invisible Continue rule is on the SOURCE response; the follow depth is now 8 for every setting that follows links.
5. Alias scripts hear `OnActivate` of the reference they hold (for every quest, with the hook on).

## 8. Not run

The Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review, saves in the middle of the quest, the fight, Nelacar's route.

## Merge note (2026-11-19)

Merged after task 0340, which added `mLastContinues` so a Goodbye line flagged InvisibleContinue goes on with its link. The follow loop now has one check, the flag of the line just said (`from`, else `mLastContinues`): it lets a named link be followed (this task) and the loop go on past a Goodbye (0340).
