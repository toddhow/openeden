# Task: Skyrim marriage, first slice (task 0270)

Status: ordered by the coordinator on 2026-11-16 (approved by toddhow). Same hand wrote tests and code; the pure and world tests were
written first and run red against stubs (see the stage log); no fresh reviewer. Windows only. Local commits on branch `marriage` from
`7c299491d9`, not pushed.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S7); decisions 0013, 0016, 0017; tasks 0137, 0187, 0258, 0261 (dialogue
and Papyrus scripts-first), 0194 (scenes), 0211 and 0217 (story manager, aliases), 0196 and 0239 (followers), 0185 (journal), 0184 (inventory).
Goal: the courting chain runs through the game's own quests, dialogue and Papyrus: Maramal's lines in Riften, buying and wearing the Amulet of
Mara, and a potential spouse who offers marriage through the real dialogue line, advancing RelationshipMarriage and the journal.
Constraints: behind the new `[extension.skyrimse] marriage` (off by default, needs `papyrus` and `inventory`); Morrowind unchanged.
Out of scope: see section 4.

## 1. Survey (Skyrim.esm and the string tables; scripts decompiled with houseCARL from `Skyrim - Misc.bsa`)

Quests: `RelationshipMarriage` (QUST 0x74793, **start-game-enabled**, the courting quest, script `RelationshipMarriageQuestScript`: property
`WeddingSet`, `GameDaysPassed`, `GameHour`), `RelationshipMarriageWedding` (0x7404E, started by the story manager), `RelationshipMarriageFIN`
(0x21382, the married state), `RelationshipMarriageBreakUp` (0x7431B). Factions: `PotentialMarriageFaction` (0x19809, 62 NPCs in the five masters,
among them Lydia 0xA2C8E, Ysolda 0x13BAB, Aela, Farkas, Mjoll ...), `PlayerMarriedFaction` (0xC6472), `MarriageCourtingFaction` (0x7431A),
`MarriageAskedFaction` (0xFF7F3), `MarriageExcludedFaction` (0x47A01). The amulet is ARMO 0xC891B (`ReligiousMaraLove`), the ring 0xC5809.

**The chain, as the data runs it (each INFO's conditions read from the CTDA, the fragments from the `.pex`):**

1. Maramal (NPC_ 0x1335B, `RiftenTempleofMara`, alias TemplePriest of the courting quest) offers the topic 0x74DE0 "I wanted to know more about the
   Temple of Mara." while stage 5 is not done and he is the alias; its fragment `TIF__00074DE2` sets **stage 5**.
2. With stage 5 done and stage 10 not, topic 0x74DDF "I could have a wedding at the temple?" answers and links (TCLT) two choices, "No, not really."
   (0x74DDE) and "I might. Why don't you remind me?" (0x74DDC); their INFOs have no text of their own (DNAM: the shared INFO 0x74DE7 explains
   marriage) and both fragments (`TIF__00074DE4`/`DE5`) set **stage 10**.
3. With stage 10 done, stage 15 not, no amulet carried (GetItemCount(amulet) == 0 on the player) and 200 gold, topic 0x26B2F "I'll buy an amulet of
   Mara. (200 gold)" (INFO 0x26B5C; `TIF__00026B5C`: `RemoveItem(Gold001, 200)`, `AddItem(ReligiousMaraLove)`, **stage 15**). Further INFOs 0x26B75 (the
   amulet was lost: free) and 0x26B7F (not enough gold).
4. Any speaker in `PotentialMarriageFaction` (and not in `MarriageCourtingFaction`) whose voice type is one of seven lists, with the player not
   in `PlayerMarriedFaction`, **wearing the amulet (GetEquipped(amulet) on the player)**, stage 10 done, relationship rank >= 1 (Friend) with the
   player and `RelationshipMarriageWedding` not running, offers the topic 0x74DDB "Interested in me, are you?" (INFOs 0x74DE1, 0xCD869, 0xCDA16 ...
   by voice type, TCLT: YES 0x74DDA with seven prompts by voice type, "No, I'm not." 0x74E25). The YES INFOs (`TIF__00074DE3` and the six
   `TIF__000CDA0x`) run `AddToFaction(MarriageAskedFaction)`, `LoveInterest.ForceRefTo(speaker)`, **stage 20**.
5. Stage 20 (`QF_RelationshipMarriageCourti_00074793.Fragment_32`): objective 10 completed, objective 20 ("Wedding") displayed,
   `WeddingQuestKeyword.SendStoryEvent(None, LoveInterest, ...)` (starts `RelationshipMarriageWedding` through the story manager: its node needs
   `GetEventData` on that keyword, fn 576, and a LoveInterest alias filled from the event, flag ALFE) and `WeddingSet = 1`.
6. The wedding quest (`QF_RelationshipMarriageWeddin_0007404E`): the player asks Maramal "I'd like to have a wedding at the temple." (topic 0x26AE7,
   `TIF__00026BAA` = **stage 30**); `Fragment_5` records tomorrow as `MarriageDay` and registers `RegisterForUpdateGameTime(1)`;
   `RelationshipMarriageWeddingScript.OnUpdateGameTime` sets stage 10 on the wedding day, and stage 10 (`Fragment_0`) dismisses a follower, **moves the
   priest, the lover and six witnesses into the temple** (`MoveTo` the markers), evaluates packages and **starts the scene**
   `RelationshipMarriageWeddingSceneView` (SCEN 0x74791, its phases end in `setstage(20/30/150)` through `SF_RelationshipMarriageWeddin_00074791`);
   the player is AI-driven (`SetPlayerAIDriven`, `DisablePlayerControls`, `ForceThirdPerson`). The vows are dialogue (0x75C9C to 0x75C9F), stage 100
   (`Fragment_3`): achievement, `RelationshipMarriage.SetStage(100)`, `SetRelationshipRank(player, 4)`, `AddToFaction(PlayerMarriedFaction)` on both,
   `AddItem(MarriageRingBondsofMatrimony)` to both, `RelationshipMarriageFIN.SendStoryEvent`, stage 500 then 1000.
7. `RelationshipMarriageFIN` (the married state): `SpouseShop` (job factions for the shop), `RegisterForUpdateGameTime(24)` on the spouse alias
   (the daily income: global `MarriageGoldEarned`, `MarriageStoreTopic` 0xD0651, the gold line is a dialogue line), `Fragment_4` links the spouse's
   bed, the house choice topics 0xC57DF to 0xC57EE (`RelationshipMarriageSpouseHouseScript.MoveSpouse` clears the eight hold house aliases and
   `ForceRefTo`s the chosen one: Breezehome's alias is `WhiterunHouse`; the spouse's packages `MarriageSpouseWhiterunSandboxPlayerHouse` 0xC5800 and
   `...SleepPlayerHouse24x8` 0xF13DA run there), `MarriageFINPlayerScript.OnLocationChange` (entering the house completes the objective).
8. Break-up and rival quests: `RelationshipMarriageBreakUp`, `MarriageRivalScript`; not read further.

## 2. Ranked engine gaps (found by reading; each is checked against the pack's code or a run)

1. **The player's worn items and carried counts were invisible to conditions** (the stand-in "wears nothing" and carries gold only): fixed here
   (section 3). Without it step 4's `GetEquipped(Amulet of Mara)` and step 3's `GetItemCount == 0` could not be answered.
2. **Relationship rank >= 1 with a potential spouse has no source** (the Friend rank comes from favour and follower quests this build does not run;
   RELA records hold the base rank, not enough): test aid `marriage test friends`, as `houses test permission`.
3. **Starting `RelationshipMarriageWedding` from the story event** (step 5): needs the story manager node and `GetEventData` for a keyword and the
   LoveInterest alias from the event; the story manager exists (task 0187, 0211, 0217), whether this node runs is checked in the stage log.
4. **Game time** (`RegisterForUpdateGameTime`, `GameDaysPassed`, `Utility.WaitGameTime`): the wedding is "tomorrow"; the rest and wait windows advance the clock
   (task 0210) but the Papyrus host's game-time updates were not checked here.
5. **`MoveTo` of actors** (priest, lover, witnesses into the temple): task 0202 moves objects, actors are refused by design ("neutral actors are not
   moved"); the wedding's stage 10 needs actor moves and the neutral actors' packages (task 0135) to walk to the altar.
6. **Scenes with `SetPlayerAIDriven`** and the vows' player dialogue: scenes (task 0194) run dialogue and package actions; an AI-driven player has no meaning
   in the neutral world (the player is the camera), the controls calls are counted no-ops.
7. **The spouse moves into the player's house**: `ReferenceAlias.ForceRefTo` and packages that name the alias's location; the house ownership of task 0261
   (`Ledger`) covers the cell, the house choice topics (0xC57DF ...) are dialogue the pack serves; the package of an NPC whose alias is a house is gap 5 again.
8. **The daily income** (`RegisterForUpdateGameTime(24)` on the spouse alias and the store's gold line): game-time updates on an alias, then
   `MarriageGoldEarned` and the dialogue gold line `AddItem(Gold001, MarriageGoldEarned)`.
9. Rings, `Game.AddAchievement`, the break-up quest and the rival quest: not read in this slice.

## 3. What was built

- `SkyrimConditionWorld::setPlayerWornSource` / `setPlayerItemSource` (`dialogue.{hpp,cpp}`, forwarded by `DialogueRuntime` and `SkyrimWorldView`):
  `isEquipped(player, item)` and `itemCount(player, item)` read the inventory model's equipment and stacks at the moment of the check (the same pattern
  as the gold source of task 0184). Only with `[extension.skyrimse] marriage`; off, the stand-in answers as before.
- **Four faults found by running the chain, fixed (each with a test first):**
  1. a linked topic with **no name of its own** (the marriage branch's YES and NO have no FULL; the choice's words are the INFO's RNAM prompt)
     was never offered after "Interested in me, are you?" (`DialogueRuntime::offerTopics`: the prompt now counts for a LINKED topic; a nameless
     topic is still never in the normal list);
  2. `ReferenceAlias.ForceRefTo` kept the fill only in the host, so a condition **run on that alias** (run-on 5; Maramal's wedding line asks
     `GetRelationshipRank(player) > 0` and `GetDead` on the `LoveInterest` alias that the YES fragment forces) read nothing: ForceRefTo and Clear now
     also set the quest state's alias fill (`PapyrusHost`, general, not behind `marriage`);
  3. a **keyword** form had no script class, so `WeddingQuestKeyword.SendStoryEvent` (stage 30's fragment) was never dispatched: KYWD is read as a
     model record (EDID only) and `SkyrimWorldView::scriptClassOf` answers "Keyword";
  4. the player's worn items and counts (section 3, first bullet).
- `marriage.{hpp,cpp}`: the names of the setting and forms and `parseBaseIdList` (the test aid's list).
- Settings (declared in `extension.json`, off): `marriage`, `marriage test friends` (hex NPC_ base ids: every placement is the player's friend, rank 2,
  through the follower state, once when the Papyrus host runs; log line `MARRIAGE test friends`).
- `tools/skymarriage_check/check.sh` (a copy of the skyhouses check with the Amulet of Mara in its trace; `luatest/scripts/hse/global.lua` gained an
  optional target id in the `talk` step) and, in the work folder, `run.sh`, `gen.py` and `click.py` (the input script generator).


## 4. Out of scope (not built, not run)

The wedding itself: the wedding day (`OnUpdateGameTime` on the quest's `MarriageDay`: gap 4), moving the priest, the lover and six witnesses into
the temple and running the scene with an AI-driven player (gaps 5 and 6), the vows, `PlayerMarriedFaction`, the rings, the spouse moving into
Breezehome and the house choice dialogue, the daily income, the break-up quest and the rival. Their scripts are listed in section 1 so the next
slice can start from the gaps.

## 5. Stage log

- **Tests (new, 14).** `apps/components_tests/skyrimse/testskyrimsemarriage.cpp`: `SkyrimMarriageList` 3, `MarriageWorldTest` 3, `SkyrimMarriageDialogue` 3,
  `SkyrimMarriageRecords` 1 and one `SkyrimMarriageAlias` test appended to `testskyrimsefollowercommands.cpp` (a NEW test in an existing file: no
  existing test was edited). Written before the code and run against the stubs: **world, list and records 6 of 7 red** (the one that passed pins
  that the stand-in wears nothing), **dialogue 2 of 3 red** (the third pins that a nameless topic is not in the normal list), **alias 1 of 1 red**; the
  keyword test 1 of 1 red; all green after. The area filter (`*Skyrim*:*Dialogue*:*Papyrus*:*Journal*:*Crime*:*Rest*:*Follower*:*Quest*:*Services*:
  *Inventory*:*Barter*:*Extension*:*Neutral*:*Frozen*:*Marriage*`) runs 3,667 tests: 3,564 pass, 103 data-gated skip, 0 fail.
- **In game** (`tools/skymarriage_check/check.sh`, five masters, real clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`, 500 gold,
  `marriage test friends = 13bab`; frames in `I:\Projects\OpenEden-work\combat5\marriage\keep`, viewed):
  Maramal (Riften Temple of Mara) offers "I wanted to know more about the Temple of Mara." (stage 5 after Goodbye: `TIF__00074DE2`), then
  "I could have a wedding at the temple?" and "No, not really." (stage 10, `TIF__00074DE4`), then "I'll buy an amulet of Mara. (200 gold)"
  (`TIF__00026B5C`: gold 500 to 300, the amulet in the inventory, stage 15); the Equip button of the inventory window wears it (`INV equip 000c891b`);
  **a test aid moves the player to Ysolda's house** (a Lua `teleport`; the walk from Riften to Whiterun is not part of this slice) where Ysolda offers
  "Interested in me, are you?" (only with the amulet worn, stage 10 done, Friend rank, `PotentialMarriageFaction`); her answer links to "Yes. Yes I am."
  and "No, I'm not."; Yes runs `TIF__00074DE3`, stage 20 runs `QF_..Fragment_28` and the journal lists **The Bonds of Matrimony: [ ] Speak to Maramal about
  arranging your wedding**; back in the temple (a second teleport) Maramal now offers "I'd like to have a wedding at the temple." and answers
  "Your wedding will be held tomorrow ..."; stage 30 runs `Fragment_32` and **`story event SCPT started quest 0007404E`** (RelationshipMarriageWedding, from
  the game's own `SendStoryEvent`); the journal then reads `[x] Speak to Maramal ...` and `[ ] Attend your wedding ceremony`.
- Fragments run after Goodbye (the conversation box pauses the world), as in tasks 0239 and 0261.
- **Hand-written, not the game's:** the Friend rank (`marriage test friends`), the two teleports, the Equip click (the game's own inventory window), nothing else.
- **Determinism.** Three runs of the whole scenario (frames 12,500) give the same 23 trace lines (md5 `b5b326262feb`: the stages, the fragments, the story event, the equip): `PAPYRUS quest 00074793`/`0007404E`, `dialogue line`, `INV equip`, `MARRIAGE`.
- **Morrowind unchanged.** `tools/worldobjects_check` 111 result lines for the reference (`combat4`'s build, not the exact parent) and the new build, exit 0, no dump;
  the fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600) is `1c5d47ad0b5dc32d` for the reference, this build and this build with the neutral settings on.
  The pack is not loaded in a Morrowind session; `ForceRefTo`'s quest-state write and the nameless-choice rule are Skyrim pack code only.
- **Edits outside the new files (small).** `dialogue.{hpp,cpp}`, `dialogue_runtime.{hpp,cpp}`, `world_view.{hpp,cpp}`, `records.cpp`, `papyrus_host.cpp`,
  `skyrimse_game_pack.cpp`, `extension.json`, both CMake lists, `apps/components_tests/CMakeLists.txt`, `tools/testprofile` (comment and README line).
- **Not run.** The Linux gate and GCC `-Werror` (the traps were checked by hand), clang-format 14, clang-tidy, the Lua tests, `openmw-tests` (no engine file
  changed), a fresh review, the whole components-tests binary (the area filter above), Lydia (rank -1: not marriable before Breezehome is bought), a refusal
  line ("No, I'm not."), the other potential spouses.

## 6. Decisions for toddhow

1. **`ForceRefTo` and `Clear` now also write the quest state's alias fill, not only the host's table** (general, not behind `marriage`): any condition
   run on a forced alias (DialogueFollower's, HousePurchase's) now sees the actor. The follower and house tests pass; the follower scripts' behaviour in
   game was not re-run.
2. **KYWD records are read as model records** (EDID only, 2,000 or so extra entries in the base table): needed so a keyword property is a `Keyword`.
3. **A nameless topic is a choice when its INFO has a prompt**, general (before: never offered).
4. The Friend rank is a test aid: should a Friend rank follow from something this build already runs (for example hiring a follower)?
5. The wedding needs game-time updates on a quest (`RegisterForUpdateGameTime`) and actor `MoveTo`; the second is refused by design (task 0202). Which first?
