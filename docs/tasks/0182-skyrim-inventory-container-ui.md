# Task: Skyrim inventory and container UI (milestone M-S6)

Status: BUILT for containers and the player's inventory (Windows only, not pushed); dead-NPC loot is designed and NOT built
(see stage 6). toddhow chose "Inventory and container UI" for M-S6 on 2026-11-15.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.17 (gameplay rules: inventory, items), 4.24 (UI), M-S6
row; decision 0017 (neutral actors and items), decision 0015 (UI system), task 0097 (script bindings), task 0177 (saves).
Goal: in a Skyrim neutral session the player has an inventory window; activating a container opens a container window
beside it; items move between the two. The pack owns the state, the core only draws it.
Inherited code touched: `apps/openmw/mwclass/neutral.cpp` (`Neutral::activate`, the container branch that said "nothing
more to do before the neutral inventory (stage N5)").
Constraints: behind `[Game] neutral inventory` (new, off) plus `[extension.skyrimse] inventory` (new, off) and
`[GUI] rmlui`; Morrowind's windows are unchanged.

## 1. Survey (read at `b1023828c4`)

- **Activation.** `MWClass::Neutral::activate` (`mwclass/neutral.cpp`): an Actor goes to `TalkAction`; Door and Container
  run the lock decision (`decideNeutralLock`, task 0128); a Door then teleports; a Container fell through to
  `MWWorld::Class::activate` (nothing). `Neutral::hasToolTip` is true for Actor, Door and Container (task 0150), so the
  real activate key reaches it. The placement id is `MWWorld::neutralPlacementOf(ptr.getCellRef().getRefNum())`, the
  base is `NeutralBase::mBase`.
- **Data in the pack.** `ItemRecord` (`records.hpp`) has value and weight only for ARMO, WEAP and MISC; ALCH, AMMO, BOOK,
  INGR, KEYM, SLGM, SCRL and APPA are `ModelRecord`s with a name and no numbers. CONT contents exist twice:
  `ContainerRecord` in `placed_refs.hpp` (built on the dialogue worker, not always started in a session without
  actors) and nowhere else. NPC inventories: `ActorNpc::mInventory` (CNTO) and `ActorIndex::resolve(...).mInventory`
  plus the default outfit (`OutfitRecord::mItems`). Leveled lists: `ParsedPlugin::mLeveledLists`,
  `ActorIndex::pickLeveled` (entries by level, no chance none).
- **Names.** `SkyrimWorldView::displayNameOf(base)` (task 0150) for any base form including items.
- **Bindings.** `BindingsService::declareNamespace` (task 0097): Read functions run on the script thread; Mutate functions
  are global-script only, queued (4096), run on the main thread in `LuaManager::applyDelayedActions`, return nothing.
  Packages are `core.<game>.<area>`; `REQUIRES=core.<game>.<area>>=M.m` in an `.omwscripts` line keeps a script from
  starting without the namespace. No pack declares a namespace yet (`grep declareNamespace`: only tests and
  `pack_host.cpp`).
- **UI.** `openeden.ui` (tasks 0162 to 0166): `Document`, `Column`, `Row`, `Text`, `Button`, `List` (virtualised, keyed;
  a row is built once, so a row whose data changes needs a new key; a `List` needs an explicit `width`). Player scripts get
  `openeden.uibackend` only with `[GUI] rmlui`. The input router only acts in GUI mode (`uiinputfilter.cpp`).
  `I.UI.setMode('Interface', {windows = {}})` gives GUI mode, the pause and the cursor with no MyGUI window.
- **Engine to Lua.** `MWBase::LuaManager::sendExtensionEvent(name, fields)` delivers a flat event (double, string, bool
  fields) to global scripts; a global script can `player:sendEvent`. Events are delivered while the world is paused.
- **Saves.** `SaveChunkService::registerHandler` (task 0177's `LoadOrderStamp` is the template); chunks are written on
  the main thread before the save worker starts. Neutral actors are not saved (rebuilt at load).
- **Locks and keys.** `neutralLockState().hasKey` (task 0128) is separate from any inventory.

## 2. Design

**The pack owns the state** (`game_packs/skyrimse/{include/game_skyrimse,src}/inventory.*`, `inventory_service.*`):
`ItemTable` (value, weight, category of every carried item type, CONT contents and LVLI lists, read once on a worker the
first time it is asked for, never waited for), `InventoryModel` (who holds what: the player and any number of owners;
`transfer` moves a whole count or changes nothing; a revision number changes on every change), `InventoryChunk` (the save
chunk `inventory`: the player's stacks and every TOUCHED owner's stacks; an untouched container is rebuilt from the data,
the seeded pick gives the same contents). `InventoryService` owns the three and declares the namespace
`skyrimse.inventory` 1.0.

**Identity.** Ids cross the binding as hex text (an item or base is 8 lower-case hex digits, an owner is `player` or the
placement id in hex): a load-order id of a plugin from the eighth on passes INT32_MAX, so `Int32` is wrong, and a `Handle`
cannot go through a script event.

**Contents.** A container's CNTO entries are resolved ONCE, at its first open, into stacks. A leveled list is resolved by a
seeded pick: `splitmix64` of the owner's placement id (no clock, no `rand`), chance none first, then the level rule of
`ActorIndex::pickLeveled`, `UseAll` gives every entry; the stand-in player's level is 1. Counts multiply down nested
lists (cap depth 8). Two runs open the same chest with the same contents.

**The flow.** The engine does NOT open a MyGUI window. `Neutral::activate` (container, player, setting on) returns an
action whose `executeImp` sends the extension event `openeden_skyrimse_container_opened` {placement, base, kind, name};
the global script `inventory_global.lua` calls the Mutate `open(owner, base, kind)` (the pack registers the owner) and
sends `SkyrimInventoryOpen` to the player script; `inventory_player.lua` sets the mode `Interface` with no windows and
opens the two RmlUi documents. Clicks send `SkyrimInventoryTransfer` / `TakeAll` to the global script, which calls the
Mutate functions. The player script polls two Read functions a frame (`revision`, and `known` while it waits) and re-reads
the lists only when the revision moved, so a frame where nothing changed makes no UI call.

**The windows** (`files/data/openeden/skyrimse/inventory_windows.lua`, a plain module that takes `ui`, the package and
three hooks, so it runs over the headless backend in tests): left, the inventory (weight against capacity, gold, a
`List` sorted by category, name, id); right, the container (title, `Take all`, `Close`). A row has the name button
(click: move ONE), a detail text (weight and value of the stack) and an `All` / `Put` / `Take` button (the whole stack).
`I` opens the inventory alone. Escape or Close leaves the mode and the documents come down.

**Rules.** The base carry weight is 300 (`kBaseCarryWeight`, RECALLED-VERIFY: GMST fCarryWeightBase; no stamina bonus,
no burden effect). The player starts with the stand-in player's gold (`[extension.skyrimse] player gold`, default 100)
as Gold001. Gold is an ordinary item with weight 0.

**Saving.** The chunk is written whenever the host collects chunks (the pack registers it only when
`[extension.skyrimse] inventory` is on). It does not depend on `[Game] neutral saves` for being written, but a save needs
that setting to be allowed at all. A looted corpse comes back alive with its items at load (neutral actors are rebuilt,
decision 0017), so corpse loot is not durable; a chest's changes are.

**Not decided here, listed for toddhow:** whether a KEYM the player takes should feed `neutralLockState().hasKey`
(today the key ring and the inventory are separate: taking a key from a chest does not open the door it fits); whether the
stand-in player's gold for speech and bribe checks (`PlayerStandIn::mGold`) should follow Gold001 in this inventory
(today it does not); equipping and using items; barter; stealing (owner flags are decoded but not enforced).

## 3. Stages

1. Task spec (this file).
2. Pack: item stats, CONT contents, the seeded leveled pick, the model, the chunk (`components-tests`, generated plugins;
   gated real-data run on the five masters).
3. Pack: the service and the bindings namespace over a stub bindings service; wiring in `skyrimse_game_pack.cpp`
   (`[extension.skyrimse] inventory`, off).
4. Engine: `[Game] neutral inventory` (off), the activation event (`neutralinventory.*`, `Neutral::activate`).
5. Lua: the window module, the two scripts, `builtin.omwscripts` lines with `REQUIRES`.
6. Dead NPCs: designed, not built (see the log).
7. In game: Bannered Mare, real clicks, three runs equal; Morrowind unchanged.

## Acceptance criteria
- [x] Item stats decode with the layouts of `inventory.hpp`; a record of another size is counted, never guessed at.
- [x] A container's contents are the same on every run and for every open; leveled lists resolve by the seeded pick.
- [x] A transfer moves a whole count or nothing; the revision changes only on success; the chunk round-trips and a damaged
      chunk is refused and changes nothing.
- [x] The bindings declare `skyrimse.inventory` 1.0 with the listed Read and Mutate functions.
- [x] The windows follow the pack's revision and make no UI call in a frame where nothing changed.
- [x] In game: activate a chest, take an item, see it in the inventory window, put it back; three runs give the same trace.
- [x] With every new setting off, Morrowind and Skyrim sessions behave as before (`worldobjects_check`, the fixed Balmora
      frame, off and on).

## Out of scope
Barter, equipping, using items, stealing, pickpocketing, lock picking, dead-NPC loot (designed), 3D item preview, item
cards, favourites, categories tabs, sorting options, the `core` `openeden.ui` components and the shipped theme files
(another agent works on them), combat, Papyrus and perk files.

## Stage log (2026-11-15; Windows only; local commits on branch `invui` from `b1023828c4`, not pushed; same hand wrote tests and code, no fresh reviewer)

**Built.** Pack: `inventory.*`, `inventory_service.*`, two small accessors on `SkyrimWorldView` (`inventoryInputs`,
`npcInventory`), a block in `skyrimse_game_pack.cpp` (`startInventory`, the poll in the game phase callback, the chunk and
the namespace). Engine: `[Game] neutral inventory` (off), `mwworld/neutralinventory.*`, `OpenInventoryAction` in
`Neutral::activate`, a developer trace `NCONT` (with `OPENEDEN_NEUTRAL_DOOR_TRACE`). Lua: `openeden/skyrimse/inventory_windows.lua`,
`scripts/openeden/skyrimse/inventory_{global,player}.lua`, two `builtin.omwscripts` lines with `REQUIRES=`. Settings and
profile: `[Game] neutral inventory`, `[extension.skyrimse] inventory`, `tools/testprofile` (profile, README; `coverage.sh` OK).
Check tool: `tools/inventory_check`.

**Tests first, red against stubs.** Pack (generated plugins): 25 of 25 failed against a stub `inventory.cpp` and
`inventory_service.cpp` (16 model, record, resolve and chunk tests, 9 bindings tests), then 28 of 28 pass plus the gated
real-data test. Engine (`NeutralInventory*`): 3 of 3 red against a stub, then pass. Lua windows over the headless
backend (`SkyrimInventoryWindows`): 8 of 8 red with the module absent, then pass (including a test that 20 frames with
nothing changed make 0 protocol calls). The bindings tests needed one fix of the TEST after the first green build (the
temporary plugin was not named `test.esp`, so its own ids did not resolve); the code did not change. `openmw-tests`
2,677 pass, `components-tests` (without the Slang, complement and property groups) see below. Existing tests edited: none.

**Real data** (`SkyrimInventoryRealData`, the five masters, printed aggregates): 10,108 items, 591 CONT, 0 odd subrecords,
0 failed, 0 unresolved ids: every DATA/ENIT/CNTO size is as `inventory.hpp` says (AMMO DATA is 20 bytes: the reader
accepts 16 or more). Gold001 is value 1 weight 0, the Iron Sword value 25 weight 9. The 436 CONT of Skyrim.esm resolve at
level 1 to 9,130 stacks (103 empty, 9 unknown forms, twice equal). The table is built on the worker in 1.6 to 2.0 s in
game (5 s in the unit run with every leveled list parse); the main thread never waits.

**In game** (`tools/inventory_check/check.sh`, Bannered Mare, `NobleWardrobe01` placement 0x000c8fcd, base 0x0006b303,
frames at 1280 x 720, fixed frame time, real activate key and real mouse clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`):
activating opens the windows (frame viewed: inventory left with weight 0.0 / 300 and Gold 100, "Wardrobe" right with Fine
Boots x4 and an item that has no name); a click on Fine Boots logs `INV transfer 000c8fcd -> 00000000 item 00086993 x1: ok`
and the next frame shows Fine Boots x3 in the wardrobe and Fine Boots in the inventory, weight 1.0 / 300; "Put" moves it
back; "Take all" moves 5 items and empties the wardrobe (weight 4.0 / 300); "Close" leaves the mode and the world is
back with the crosshair. **Three runs give an identical trace** (md5 of the `INV open/transfer/take_all`, `NINV` and `N6INV`
lines equal) and the windows' regions of the screenshots differ by at most 6 grey levels, 0 pixels over 8 (the scene
behind the semi-transparent windows flickers; whole frames differ for the same reason). **Save and load:** with
`neutral saves`, two boots taken, saved by the menu script, a second process loads it: `boots player=2 container=2 gold=100
known=true` (the chunk is 208 bytes: the player and the one touched owner; `INV chunk restored: 2 owners, 1 touched`).
Cost: the overlay's mean frame over the 4,631 frames of the save run (windows open for part of it) was 17.6 microseconds update and 14.1 render on the GUI thread (0.8 / 8.2 / 8.2 in a run with no windows); not compared against a frame time.

**Morrowind unchanged.** `tools/worldobjects_check` dump: 111 lines, equal to the parent build's (`b1023828c4`, same tree,
before any change of this task); the fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600, fixed frame time)
has SHA-256 prefix `1c5d47ad0b5dc32d` for the parent and for this build with the settings off and with `neutral inventory`,
`neutral world`, `neutral actors`, `neutral combat`, `[GUI] rmlui` and the pack setting on. No log line about the
`REQUIRES=` scripts appears in a Morrowind session.

**Not built / not run.** Dead-NPC loot (`SkyrimWorldView::npcInventory` and the `actor` kind of `open` exist and are
unexercised in game; the engine does not open them: `Neutral::activate` sends Actors to `TalkAction`, and the engine
holds a corpse only as a combat-bridge handle, so a path from a placement to "is dead" needs its own task); equipping and
using items; barter; item cards and categories; a gamepad; the Linux gate and GCC `-Werror`; clang-format 14 and clang-tidy
(formatted by hand to 120 columns); the Lua integration tests; sanitizers; a fresh review; a container in a Skyrim
exterior; locked containers (they refuse as before: the lock decision runs first); the `I` key in game (only the Lua
logic is tested); very long lists (the pack caps an owner's list at 256 rows in `items`, `kMaxBindingArrayElements`).

## Decisions for toddhow
1. **Keys.** A KEYM taken from a chest does not open the door it fits: `neutralLockState().hasKey` and the inventory are
   separate. Wire them (the model knows the category `key`)?
2. **Gold.** Gold001 in this inventory is separate from the stand-in player's gold in the speech and bribe checks
   (`PlayerStandIn::mGold`, setting `player gold`); the inventory STARTS with that number. Make the checks read the inventory?
3. **Carry weight.** 300 flat (`kBaseCarryWeight`, RECALLED-VERIFY), no stamina bonus, no burden. Taking past 300 is allowed.
4. **Click rule.** A name click moves ONE item, the small button the whole stack; Skyrim asks for a count in a slider.
   A count slider needs the `Slider` component (task 0166 has it) and a shared dialog: later?
5. **Corpse loot is not durable** (neutral actors are rebuilt at every load), a chest's changes are.
6. **A 256-row cap** per list (the binding's array limit): a container with more shows its first 256 by category and name.
7. **Item with no FULL name** shows `<id>` (one in the wardrobe, `00104b40`); an editor-id fallback needs the record's EDID.
8. **Log noise:** the global script prints one line per open (`[skyrimse inventory] container opened`).
9. The builtin `.omwscripts` lines carry `REQUIRES=core.skyrimse.inventory>=1.0`; they never start without the pack
   setting, and say nothing in a Morrowind session.
10. **UI components wanted later** (not added: another agent owns them): a Dialog/Modal, a count Slider dialog, item
    tooltip, categories Tabs with icons.
