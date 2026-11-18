# Task 0311: Skyrim Hearthfire, buying a plot (stage 1 of the home building)

Design doc: `docs/design/skyrim-se-implementation-plan.md` (M-S8, Hearthfires). Same hand wrote tests and code; Windows only;
local commits on `hearthfire-0311` from `b34600a4fd`, rebased onto `4ac6fb8c89`; not pushed. Pack only plus one small
addition to the Papyrus runtime (`components/script_papyrus`); no new setting that changes play.

Goal: buy a Hearthfire plot from a jarl's steward, get the deed, see the plot's markers enable, then build with the drafting
table and the carpenter's workbench through the game's own records and scripts, and see the house appear piece by piece.

**Reached: the purchase, the deed and the site (frames viewed). Not reached: the drafting table, the workbench and any
building (ranked gaps below).**

## 1. Survey (HearthFires.esm, the user's install; housecarl records and decompiled `.pex` from `Skyrim - Misc.bsa`)

- 22 QUST: the three plot quests `BYOHHouseFalkreath` (0x305E), `BYOHHouseHjaalmarch`, `BYOHHousePale`, the master
  `BYOHHouseBuilding` (0x305D, script `BYOHHouseBuildingScript`, start-game-enabled), the steward offer, adoption quests and
  the house attacks. 506 COBJ, 559 MISC, 25 FURN (`BYOHDraftingTable` 0x305B keyword `BYOHBuildingDrafting` 0x305C;
  `BYOHHouseCarpentersWorkbench` 0x3065 keyword `BYOHBuildingCarpenter`; thirteen interior workbenches with 17 to 58 room
  keywords each), 95 globals, 523 `byoh*` scripts.
- **The buy path.** Falkreath: the steward Nenya (NPC_ 0x13659, REFR 0x19823, in `FalkreathJarlsLonghouse`, alias `Steward`
  of `BYOHHouseFalkreath`: find-matching, `GetInFaction GovRuling` and `GetIsID` her) and the jarl Siddgeir (0x13653, alias
  `Jarl`). The topic `BYOHHouseFalkreathPurchaseTopic` ("I'd like to purchase a house") has INFOs whose conditions are
  `GetVMQuestVariable bOwnProperty == 0`, `GetInCurrentLoc FalkreathLocation`, `GetIsAliasRef Steward`,
  **`GetRelationshipRank >= 2` run on the quest alias `Jarl`** (RunOnType QuestAlias 0), `GetStageDone 30` and `GetStageDone 50
  == 0`. "I'll take it. (5000 gold)" runs `BYOH_TIF__01003000` whose end fragment does `GetOwningQuest().SetStage(100)`;
  stage 100 runs `Fragment_1`: `byohhousescript.BuyProperty()` (`bOwnProperty = true`, `RemoveItem(Gold001, BYOHHPCost)`,
  `BYOHHouseBuilding.BuyProperty(iHouseIndex)` which does `houseOwned[i] = true`, `SiteEnableMarker.Enable`,
  `HouseMapMarker.Enable` and `AddToMap`, `AddItem(BYOHMaterialLog, 20)`), then `PlaceAtMe(Charter)` and `AddItem` of it.
- **The site.** The marker `BYOHHouse1SiteMarker` (REFR 0x307B, InitiallyDisabled) is the enable parent (XESP) of the drafting
  table (REFR 0x3063, (-8740, -70681, 2335) in Tamriel, grid -3,-18) and the carpenter's workbench (REFR 0x3066) and of the
  site's other furnishings.
- **The building.** Every crafted part is a MISC with a script `extends Form` (`BYOHBuildingObjectScript`,
  `...ExteriorScript`, `...InteriorScript`): `OnContainerChanged(player, None)` calls `BYOHHouseBuilding.BuildHousePart(...)`
  / `BuildHouseExteriorPart` / `BuildHouseInteriorPart`, which find the part's place in formlists (`GetFormListIndex`:
  `FormList.Find/GetAt/GetSize`, casts of a Form to the script class) and `byohhousescript.BuildHousePart` enables
  `EnableList[i]`, `EnableList2..4[i]` and room lists (arrays of references). The workbenches' `OnActivate`
  (`BYOHWorkbenchScript`) calls `SetActiveHouseLocation` and `GetLinkedRef().StartCrafting()`; `StartCrafting` moves the layout
  tokens from a holding container to the player with `RemoveItem(FormList, 1, true, player)`. The 51 drafting-table recipes
  carry no materials; their six conditions are `GetItemCount` of layout tokens (`==1` for the part, `==0` for the others).

## 2. What was built

1. **Array properties for scripts (the engine gap that stopped the purchase).** The Papyrus host skipped every array property
   ("skipped 149" in every summary), so `BYOHHouseBuildingScript.BuyProperty` ended with `[none receiver] the array is None`
   (`houseOwned[i] = true`) and killed the rest of the fragment: gold was taken and nothing else happened. New
   `ScriptPapyrus::PapyrusRuntime::setArrayProperty(instance, name, elements)` (a virtual with a refusing default on the
   concrete runtime class, NOT on the SDK contract: no version change), implemented by the VM (kind check of every element, the
   array cap, nothing changes on a refusal), and the host builds an array from a property's stored elements
   (`PapyrusHost::convertValue`). Summary line after: "properties set 7667 (rejected 31, skipped 0)", script errors 0.
   (The 31 rejected were not examined; ranked below.)
2. **The Steward alias at a conversation, and the quest's other aliases.** `QuestAliasFiller::fillForSpeaker` also fills an
   alias that names its actor by id (GetIsID), and then fills the quest's other empty aliases from the loaded area
   (`fillQuest(..., matching = true)`), so the alias the offer asks about (`Jarl`, through `GetRelationshipRank` run on the
   alias) is filled when she is the speaker.
3. **Test permission.** `houses test permission` also befriends the Falkreath jarl and steward
   (`houses::testPermissionHearthfireBases()`; the old nine-jarl list and its pinned test are unchanged).
4. **A developer aid for conditions.** `[extension.skyrimse] conditions debug quest = <hex quest id>` (default empty) logs every
   condition of the lists that name that quest (`CONDDEBUG`, with a `LIST of n` line): it found the two faults above in minutes.
5. `tools/skyhearth_check` (a copy of `skyhouses_check`; plan steps `ext` (move the player into an exterior square), `hf`
   (count and list the enabled Hearthfire objects of the player's cell) and `npcs`).

## 3. Tests

- `PapyrusArrayProperty` (5): an Int array set and read by a generated script, Bool and Form arrays, an empty array, refusals
  (wrong element kind, unknown name, a non-array, an unknown instance) that change nothing, the 128 element cap.
  **Written after the code** (the API did not exist; before it the file does not compile).
- `SkyrimHearthfireReal` (gated by `OPENEDEN_SKYRIMSE_DATA`, 2): the plot quest's aliases fill with the player in the longhouse
  (steward 0x19823, jarl 0x19822, the drafting table and workbench references, the site's location aliases), and the steward
  alias is filled from the speaker at a conversation and brings the jarl with it. The second was written after the code; its
  failing state was observed in game (the offer was not listed: `GetIsAliasRef` and `GetRelationshipRank` were false in
  `CONDDEBUG`).
- `SkyrimHearthfireData` (1) and `SkyrimConditionDebug` (1). No existing test edited.

## 4. In game (`tools/skyhearth_check`, five masters, real clicks on the conversation box, fixed frame time, volumes 0)

- Nenya's conversation: before the fixes 2 topics (cemetery, steward); after, "I got your letter about the land for sale. Is it
  still available?" (frame viewed), "Ah, excellent. The Jarl was hoping you would be interested..." with "I'll take it.
  (5000 gold)", "You won't regret it. Here's the title to your steading. Follow the road east from Falkreath..." (frame viewed).
  Stage 50 then 100 set after Goodbye; gold 6000 to 1000; `item 0F x5000 from player`, `0300300E x20` (the logs) and
  the charter to the player; `enable of 0300307B` (the site marker) and its map marker.
- The site (player put at the plot by the harness; `story test quest = 0300305E:100` as the aid for the runs that start in the
  exterior): before the purchase the cell holds 10 enabled Hearthfire objects, after 19; the frames
  (`I:\Projects\OpenEden-work\hf311\run_e_ctl` and `run_e_aid`, viewed) show an empty clearing before and after it the
  drafting table with its plans, the carpenter's workbench, a chest and an anvil.

## 5. Gaps, ranked (what stops the building)

1. **FormList natives and data.** `FormList.GetSize/GetAt/Find/HasForm/AddForm/RemoveAddedForm` and `RemoveItem(FormList, ...)`
   are not implemented; `GetFormListIndex` needs them for every part. FLST contents need a source in the host.
2. **Scripts on forms that are never placed.** A crafted part is a MISC with a script `extends Form`; the host attaches
   scripts to placed references and quests only, so `(form as byohbuildingobjectscript).ID` and the `OnContainerChanged` of the
   crafted item have no instance. (`PapyrusHost::onItemMoved` already sends `OnContainerChanged` to instances of the item form.)
3. **A bench kind for the house furniture.** `CraftingTable` knows nine bench kinds by keyword editor id; the drafting table
   (keyword `BYOHBuildingDrafting`) and the workbenches (up to 58 room keywords) offer none, so activating them opens nothing.
   Sketch: `BenchKind::House`, the furniture's `BYOH*` keywords as the session's keyword set, recipes by `BNAM` in that set.
4. **The workbench's `OnActivate` script chain**: `GetLinkedRef` (XLKR of the table to its crafting trigger), the trigger's
   holding container (a CONT whose contents are the layout tokens), `HouseQuest.SetStage(110)`.
5. The 31 rejected properties (not examined); `Actor.GetRelationshipRank` is an unanswered native (the quest start's friend
   letter never fires: `InitializeDLC` reads the jarl's rank at game start).
6. Hjaalmarch and The Pale (the same scripts, another steward and jarl): not run.
7. The master quest `BYOHHouseBuilding` and the other house quests start at game start in every session (start-game-enabled);
   harmless but their start-up fragments run on every Skyrim session with `papyrus` on.

## 6. Decisions for toddhow

- `setArrayProperty` is on the concrete `PapyrusRuntime`, not the SDK contract: the contract stays at 1.2. If you want other
  hosts to set arrays, it needs a contract bump and an edit of the version pins.
- Filling the other aliases of a quest the speaker joined (item 2 above) widens `fillForSpeaker` from "fn_567 aliases" to
  "fn_567 and GetIsID aliases plus the rest of that quest": a conversation with a named actor now fills more aliases.
