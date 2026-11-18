# Task 0317: Skyrim Hearthfire, building the house (stage 2 of the home building; follow-up of task 0311)

Design doc: `docs/design/skyrim-se-implementation-plan.md` (M-S8, Hearthfires). Same hand wrote tests and code, the tests AFTER
the code (red evidence by mutation and by the failures seen in game and in the gated real-data test, below); Windows only;
local commits on `hearthfire2-0317` from `5648e037bc`, rebased onto `bbd3bb965a`; not pushed. Everything new is behind the new
pack setting `[extension.skyrimse] house building` (off by default, declared in `extension.json`; needs `papyrus`,
`inventory`, `crafting`, `houses`, `[GUI] rmlui`). Two pieces are not behind it and change nothing that could be seen
before: the FormList natives and one correction in the recipe condition decoder (section 3).

Goal: at Lakeview Manor (the Falkreath plot), choose the small house at the drafting table, craft its parts at the carpenter's
workbench and see them appear piece by piece through the game's own `BYOHHouseBuildingScript` and `byohhousescript`, with no
hand-written construction rule.

**Reached, in game with real clicks: the layout, the foundation, wall framing, walls, roof framing, roof, floor and door are
crafted and the house stands on the plot, one part after another (frames below).** The 0311 "not built" list items 1 to 4
are done; the layout tokens that `StartCrafting` moves to the player are not moved (section 6).

## 1. What the scripts need (decompiled `.pex`, `I:\Projects\OpenEden-work\hf311\psc`)

`OnContainerChanged` of a crafted part (`BYOHBuildingObjectScript extends Form`) calls `BYOHHouseBuilding.BuildHousePart(...)`,
which finds the part's number in `BuildingMasterList` (`FormList.Find/GetSize/GetAt`, casts of a list entry to the part's script
class and reads of its `ID`), and `byohhousescript.BuildHousePart` does `EnableList[i].EnableNoWait` (arrays of 224
references). The workbench's `OnActivate` calls `SetActiveHouseLocation` (`Location.IsSameLocation`, which the shipped
`Location.psc` builds on the native `HasCommonParent`) and the crafting trigger's `StartCrafting` (`FormList.RemoveItem`).

## 2. What was built

1. **FormList natives** (`PapyrusHost::registerInventoryNatives`): `GetSize`, `GetAt`, `Find`, `HasForm` over the entries of an
   FLST in the record's own order (LNAM order is the index every `EnableList[i]` hangs on; the dialogue index reads them with
   a cap of 65,536, `BuildingMasterList` has more than 50), and `ObjectReference.RemoveItem` given a form list moves or removes
   up to `count` of each form the owner holds. `PapyrusGameData::formListEntries` (new trailing virtual, default false) is
   implemented by the pack over `SkyrimWorldView::formListEntries`; a list the data cannot give yet is empty and said once.
   FLST joined the record kinds the world index keeps (`records.cpp`, as KYWD did) so a form list has the class `FormList`
   when a script property is filled (the class is baked into the value then).
2. **Scripts on forms no cell places** (option `PapyrusHostOptions::mFormScripts`, set by the pack setting):
   `PapyrusGameData::baseScripts` (new trailing virtual; over `SkyrimWorldView::scriptsOfBase`, the VMAD of the MISC record)
   and `Impl::attachFormScripts`, called when a list hands a form out (`GetAt`) and when an item is first added to an
   inventory (`onItemMoved`), at most 2,048 forms; a script cannot be loaded from inside a native, so a list entry gets its
   script there only when its class is loaded already (the building script names the part's class, so it is). `attachKeyExists`
   accepts these keys (without it every later save is refused whole, as in task 0239).
3. **A House bench kind** (`crafting.*`): `BenchKind::House`, every keyword whose editor id starts with `BYOH` is recorded,
   `CraftingTable::benchOfKeyword` calls it House, `benchesOf(furniture, includeHouse)` lists it only when asked, and the
   service shows a House recipe only when the furniture carries the recipe's BNAM keyword (`furnitureOffers`): the data decides,
   there is no `BYOH*` name table. A recipe with no materials is craftable, the part goes to the inventory through
   `InventoryModel::add` (which reports the move with no old container; `grant` does not) and no skill is trained.
4. **The workbench's `OnActivate`**: `CraftingHooks::mBenchOpened` tells the pack when a bench session is ready and the pack
   calls `PapyrusHost::onActivate(placement, player)`.
5. **Three engine faults found by running it and fixed:**
   - A property read or write on a FORM value (a script typed `byohhousebuildingscript` holding the quest's form, filled before
     the quest's script was attached) looked at the class script (`Quest`) and failed with "class 'Quest' has no property
     'bCraftingTriggerBusy'". `Runtime::doProperty` now resolves the instance of the same form that defines the property
     (`instanceDefining(..., property = true)`, as a call already did): `components/script_papyrus/papyrusinterp.cpp`.
   - The runtime's array limit of 128 refused all the `EnableList` properties (224 references): "rejected 31" of task 0311 was
     mostly these. `PapyrusRuntime::setMaxArrayLength` (concrete runtime only, no SDK contract change); the pack raises it to
     1,024 only with the setting. Rejected properties went from 31 to 13.
   - `GetItemCount` (function 47) condition parameters of a recipe were not resolved to the load order: a DLC recipe named its own
     part with its own file's index, so every "layout held" test of a Hearthfire recipe was false (and "none held" true by luck,
     which is why the drafting table worked first). `crafting.cpp`'s CTDA decoder resolves it with the others.
6. `Location.HasCommonParent` (same location, or one inside the other: the keyword rule of the game is not known here).
7. `tools/skyhearth_check`: new steps `inv` (the player's counts), `en` (enabled objects of the cell and a hash of those near a
   point), `hour` (the game hour).

## 3. Tests (`apps/components_tests/skyrimse/testskyrimsehousebuilding.cpp`, 18 tests, 2 gated)

Written after the code. Red evidence: four one-line mutations each failed named tests (a wrong `Find` index: 1; the craft using
`grant` instead of `add`: 1; no lazy attach: 3; the list `RemoveItem` count rule: not valid, the build was broken at that moment);
the gated real-data test `theCarpentersWorkbenchOffersTheFoundationRecipeWhenTheLayoutIsHeld` FAILED before the condition
parameter fix (the foundation recipe's conditions did not hold with the layout held: parameters printed 0x02003014 and 0x02003060
for ids 0x03003014 and 0x03003060); the property resolution and the array limit were seen failing in game (log lines above) and
have tests that were written afterwards (`SkyrimHouseFormProperty`, `SkyrimHouseArrays`).

- `SkyrimHouseFormList` (4): size, at, find, has in order; outside the list and past the end; a list not read yet is empty and
  said once; `RemoveItem` with a list.
- `SkyrimHouseFormScripts` (4): an item hears `OnContainerChanged` with the option and not without; a list hands out a form with
  its script (and one without a script none); a save after the attach restores.
- `SkyrimHouseFormProperty` (1), `SkyrimHouseArrays` (1: 224 elements refused by default, kept with the option).
- `SkyrimHouseBench` (6): the keyword rule, the setting off, the recipes of each furniture, a part stays in the inventory and is
  reported with no old container, the workbench recipes follow the tokens held, the open hook.
- `SkyrimHouseBuildingReal` (2, gated by `OPENEDEN_SKYRIMSE_DATA`): the master list holds the parts in order (the foundation
  after the layout, `ID` 2 read from the MISC's VMAD) and the carpenter recipes of the real data.
- No existing test edited.

## 4. In game (`tools/skyhearth_check`, five masters, real clicks on the crafting window, fixed frame time, volumes 0)

`I:\Projects\OpenEden-work\hf317\go5.sh` (plan and input generated by `mk5.py`): the plot is bought with the 0311 test aid
(`story test quest = 0300305E:100`), materials come from `inventory test items`, the player is put at the table and the bench by
the harness (a stand-in for walking), the activations are `object:activateBy` (the engine's own furniture activation, the same
event the real key sends) and the recipe and Close buttons are REAL clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`. The game
runs while the window is closed only (the window pauses the world: the scripts run after Close).

Frames (`I:\Projects\OpenEden-work\hf317\keep`, a sheet `house_sheet.png` and the two windows, viewed): the same view of the
plot (`-9100, -68900`, daylight set by the harness) before the layout, after the layout (pegs and rope), the foundation, wall
framing (posts), walls, roof framing, roof, floor and door: the house stands on the plot part by part. The drafting table's window
offers only "Small House Layout"; the workbench's window lists "House, Wall Framing" and "House, Foundation" with their needs.
Log (three runs, `trace_f1..3.txt` there): `CRAFT craft Small House Layout`, `House, Foundation`, `Wall Framing`, `Walls`,
`Roof Framing`, `Roof`, `Floor`, `Door` in that order; `PAPYRUS enable of 0300321B`, `0300321C`, `0300321D` and `0300322E`, `0300320D`,
`0300321F` ... sent to the world (the part references of the plot) after each craft; the player's counts of the parts rise 0 to 1
one at a time. After the rebase onto `main` at `bc71fa64b4` the three runs were repeated: the 65 trace lines (CRAFT, enable, inventory,
OnContainerChanged, `en` counts and hashes) are identical in the three runs except the player's own position printed on 3 of them
(`player=-9108,-68896` against `-9111,-68899` and `-9107,-68901`: where the player settled after the harness's teleport, 1 to 20
units). Before the rebase two of three runs were byte-identical and the third differed in the `en` counts by 20 objects (which
square was current while the harness moved the player), not in the game-side lines.
Script errors 0 in the summary; rejected properties 13 (was 31); `house building` off: no `unplaced form` line and the drafting
table opens nothing (unit test).
Morrowind unchanged against `I:\Projects\OpenEden-work\integ2uild`: `tools/worldobjects_check` 111 lines, 110 objects OK; the
fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600) `1c5d47ad0b5dc32d` for the reference and this build.
Wide test runs (`components-tests` `*Skyrim*:Papyrus*:*Papyrus*:*Crafting*:*Extension*:*Frozen*`, `openmw-tests`
`*Neutral*:*Skyrim*:*Lua*:*Extension*`, with `OPENEDEN_SKYRIMSE_DATA`): no FAILED line; the pass counts were cut off by a `tail`
(not re-run: 25 minutes).

## 5. Decisions for toddhow

- `house building` is a new pack setting (off). The FormList natives and the `GetItemCount` parameter resolution are not behind it.
- `FLST` records now sit in the world index with the model-like kinds (`records.cpp`); the dialogue index keeps reading the lists.
- The array limit is raised to 1,024 for a session with the setting (the runtime default 128 stays).
- `Location.HasCommonParent` ignores the keyword argument.
- Unplaced item scripts are attached lazily, not at start: the first part is the first to hear `OnContainerChanged`.

## 6. Not built, not run (ranked)

1. The layout tokens: `StartCrafting` moves `ItemList` from `HoldingContainer` to the player; the holding container is a placed
   container whose owner the inventory model does not know, so the move is a no-op (nothing is stored there either: the
   `onTriggerLeave` that would store the tokens needs the crafting trigger volume to fire and was not checked). The recipes of
   the other houses (two layouts at one site) depend on it.
2. Hjaalmarch and The Pale (the same scripts) and the room additions, the interior workbenches (up to 58 room keywords each),
   steward and furniture: not run.
3. Saves: a save in the middle of the building was NOT made or loaded in game (only the unit test of the attach key).
4. The Linux gate, clang-format 14, clang-tidy, the Lua integration tests, a fresh review.
5. The drafting table's visible recipes (only the small house layout was seen offered at the start: the other layouts' conditions
   fail closed; a recipe whose conditions are not item counts is hidden).
