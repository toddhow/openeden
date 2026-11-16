# Task: Skyrim barter window (milestone M-S6, follow-up of tasks 0182 and 0184)

Status: BUILT (Windows only, local commit on branch `barter` from `1e32992a21`, not pushed); see the stage log at the end. toddhow chose "Barter window" for
M-S6 on 2026-11-15.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.17 (barter: vendor lists, Speech haggle), M-S6 row;
task 0155 (the dialogue service `barter`), task 0182 (inventory model and windows), task 0184 (equip, gold), decision 0015
(UI), decision 0017.
Goal: in a Skyrim neutral session the player picks a merchant's barter line, a two-column barter window opens, and buying
and selling move items and gold both ways in the pack's inventory model; stock and gold changes are saved with the
inventory chunk.
Inherited code touched: `apps/openmw/mwworld/neutraldialogue.cpp` (`NeutralDialogueSession::update`, the `Topic` case,
where task 0155 logs "service not implemented"); nothing else under `apps/openmw` but the pure helpers in
`mwworld/neutralinventory.*`.
Constraints: behind the existing `[Game] neutral inventory` + `[extension.skyrimse] inventory` + `[GUI] rmlui`; no new
setting. With them off a `barter` service line is shown and logged as before. No existing test is edited. The shipped
`openeden.ui` components and themes, the combat, perk, HUD, Papyrus, quest and VMAD files are not touched.

## 1. Survey (read at `1e32992a21`; data = the five Skyrim SE masters)

- **Service detection (task 0155).** `DialogueRuntime::fillResponse` (`dialogue_runtime.cpp` line 119 to 132): a response
  whose INFO fragment script (VMAD, `DialogInfo::mFragmentScript`) calls `ShowBarterMenu` gets `DialogueTurn::service ==
  "barter"` and `endsConversation = true`. The generic merchant line is the `OfferServicesTopic` DIAL (0x7f6bb): its
  INFOs carry `TIF__...` fragments and conditions `GetInFaction(<services faction>)`; the 44 scripts that call
  `ShowBarterMenu` are counted by the gated test of task 0155. The core logs the note in
  `NeutralDialogueSession::update` (`neutraldialogue.cpp`, `neutralServiceNote`) and shows the response with one Goodbye.
- **Vendor data is in FACT** (found with a Python walker over `Skyrim.esm`, aggregates only). 149 of the FACT records
  carry, in this order, `VEND` (4 bytes: an FLST of keywords, the vendor's buy list; 14 distinct lists, e.g.
  `VendorItemsInnkeeper` = food and raw food, `VendorItemsBlacksmith` = weapons, armour, ingots, tools, hides, arrows),
  `VENC` (4 bytes: a REFR, the vendor chest; 145 present, 5 FACT with a list and no chest), `VENV` (12 bytes: start hour
  u16, end hour u16, radius u32, then byte 8 `buys stolen` and byte 9 `buy/sell everything NOT in the list`; 996 present)
  and `PLVD` (12 bytes, a location; not read). Byte 9 is set for the miscellaneous vendors: of the 37 factions whose list is
  `VendorItemsMisc` 35 have it (9 of them with byte 8, the fences, too), and both factions of `VendorItemsMiscLucan`; no
  faction with another list has it. Their list (`VendorNoSale`, keys) is an EXCLUSION list. VENV values seen: hours 0 to 24
  in most (also 7 or 8 to 19 or 20 and 0 to 23 or 25), radius 0 up to 700.
  Hulda (`NPC_` 0x13ba3) is in `ServicesWhiterunBanneredMare` (0x9cafc: list 0x914f0, chest REFR 0x9cafb, hours 0 to 24).
- **The vendor chest** is a persistent REFR of a CONT: REFR 0x9cafb names base 0x9cafa (`MerchantWhiterunBannerdMareChest`)
  whose six CNTO entries are items and leveled lists (`LItemFoodInnCommon` x10, `LItemInnRuralDrink` x7,
  `VendorGoldInn` x1 = the merchant's gold as a leveled list of Gold001, `PerkMasterTraderGold` x1). So the merchant's
  gold is the count of Gold001 in its chest, and the existing seeded `resolveContents` (task 0182) gives the same stock
  and gold on every run.
- **Keywords.** The vendor list holds keyword forms; items carry them in `KWDA` (ARMO 2,621, WEAP 2,466, BOOK 816, ALCH
  336, KEYM 330, MISC 205, INGR 78, SCRL 44, AMMO 29, SLGM 16 records, every KWDA after a KSIZ). `ItemStat` has no
  keywords yet.
- **GMSTs** (Skyrim.esm, VERIFIED values): `fBarterMin` 2.0, `fBarterMax` 3.3, `fMinBuyMult` 1.05. No other barter GMST
  exists in the data (`fBarterBuyMin`, `fBarterSellMax` are not there).
- **Pack side.** `FactionRecord` (`actors.hpp`) reads relations only. `ActorIndex::resolve` gives an NPC's effective
  factions (templates followed) and `ActorIndex::faction(id)`. `SkyrimWorldView::placedRefs()` (task 0160) finds the
  vendor chest REFR's base, and `DialogueIndex::formList(id)` (task 0137) the keyword list; both exist once the first
  conversation began, which is the only way to reach barter. `InventoryService::open(owner, base, "container")` (task
  0182) registers a chest owner from its CONT. `InventoryModel::transfer` moves a whole count or nothing; every change
  moves the revision; a touched owner is saved by the `inventory` chunk keyed by the owner id (a REFR id here).
- **Engine side.** `Neutral::activate` builds `OpenInventoryAction` which sends the extension event
  `openeden_skyrimse_container_opened` (flat string fields) to the global Lua scripts (`neutralinventory.*`,
  `neutral.cpp`); the global script calls the Mutate binding and tells the player script, which opens the RmlUi windows
  with `I.UI.setMode('Interface', { windows = {} })`. The conversation box is a MyGUI interactive message box that
  returns a pressed button through `readPressedButton`; no box is on screen between the press and the next `show()`.
- **Stand-in player.** `[extension.skyrimse] player speech` (default 15), gold = Gold001 of the inventory (task 0184).

## 2. Design

**The pack owns everything** (`game_packs/skyrimse/{include/game_skyrimse,src}/barter.*`, `barter_service.*`):

- `VendorData` (FACT: list, chest, hours, buys stolen, list-is-exclusion) in `actors.hpp`; `ItemStat::mKeywords`
  (KWDA) in `inventory.hpp`.
- `barter::VendorInfo` {faction, chest ref, chest base, keywords of the list, exclusion, hours}; `SkyrimWorldView::vendorOf
  (npcBase, VendorInfo&)` returns Ready, Pending (data not read yet) or NotVendor: the first faction of the NPC (templates
  followed, SNAM order) with a list and a chest.
- Rules (pure): `vendorBuys(info, item)`: the item has a keyword of the list, inverted when the list is an exclusion; Gold001
  never; a worthless item (value 0) is not traded. Prices (RECALLED-VERIFY, constants in `BarterSettings`, GMST values
  VERIFIED): `haggle = clamp(speech, 0, 100) / 100`, `ratio = fBarterMax - (fBarterMax - fBarterMin) * haggle`, the player
  PAYS `max(fMinBuyMult, ratio / 2)` x value and SELLS at value / ratio, each unit rounded to nearest and at least 1.
  At Speech 15: buy x1.5525, sell x0.322; at 100: buy x1.05, sell x0.5. No perk, no disposition, no merchant-gold
  multiplier.
- `trade(...)` is one function that validates everything and then makes two transfers (item, then gold) or changes nothing.
  Refusals: bad count, not an item, vendor will not buy it, worthless, the player's copy is equipped (the equipped copy is
  not for sale), not enough items, not enough player gold, not enough vendor gold. The vendor's items and gold are the
  chest owner's stacks in the model: selling puts the item into the chest, so it can be bought back, and the chest is a
  touched owner, so the new stock and gold are in the saved chunk.
- `BarterService` declares the namespace `skyrimse.barter` 1.0. Read: `state()` ("none", "pending", "ready", "refused"),
  `reason()`, `owner()` (the chest id as hex), `vendor_gold()`, `buy_price(item)`, `sell_price(item)` (0 = the vendor will not
  buy it), `sellable(item)` (the player's count minus an equipped copy), `last_result()` and `trades()` (a count that
  moves with every attempt, for the status line). Mutate (global script only): `open(placement, npc_base, name)`,
  `buy(item, count)`, `sell(item, count)`, `close()`. A session is `pending` until the vendor data and the item table are
  read (polled every frame in the game phase, never waited for; 600 polls then refused), `refused` with a reason for a
  non-vendor.

**Engine** (`mwworld/neutralinventory.*`, `neutraldialogue.cpp`): a pure `decideNeutralBarter(service, settingOn)` and
`neutralBarterEventFields(placement, base, name)`. In the `Topic` case, after the pack's turn: when the turn's service is
`barter` and `[Game] neutral inventory` is on, the session sends the event `openeden_skyrimse_barter_opened` (placement and
base of the SPEAKER, its name) instead of showing the turn, and closes the conversation (the barter menu replaces the
dialogue, as in the game). Otherwise as before. One new function, called from one place.

**Lua** (`files/data/openeden/skyrimse/barter_windows.lua`, two scripts, two `builtin.omwscripts` lines with
`REQUIRES=core.skyrimse.barter>=1.0` and `core.skyrimse.inventory>=1.1`): left "Your items" (rows with the unit sell
price, `Sell`, `All`), right the merchant's name and stock (unit buy price, `Buy`, `All`), both with gold, a status line
(the last result), `Close`. A click on a name moves one, the small button the whole stack. The window re-reads when the
inventory's revision moves; a frame where nothing changed makes no UI call (as the inventory windows).

**Saving.** Nothing new: the inventory chunk already writes every touched owner and the player; a trade touches the chest.
The session itself is not saved. The merchant's chest base list is rebuilt from the data by the seeded pick, so an
untouched merchant is the same on every run; a touched one is exactly what the chunk says.

## 3. Stages

1. This spec.
2. Pack data: `VendorData` (FACT), `ItemStat::mKeywords`, tests on generated plugins; the gated real-data test.
3. Pack rules: prices, `vendorBuys`, `trade` (pure, tests).
4. Pack service and bindings over a stub bindings service; wiring and the world view's `vendorOf`.
5. Engine: the pure decision and the one hook in the dialogue session (openmw-tests).
6. Lua windows (headless backend tests) and the two scripts.
7. In game: Bannered Mare, Hulda, real clicks, three runs equal; save and load in a new process; Morrowind unchanged.

## Acceptance criteria
- [x] A FACT's `VEND`, `VENC` and `VENV` decode, a record of another size is counted and the vendor stays absent; items
      carry their keywords.
- [x] The vendor rule (list, exclusion, gold, worthless), the prices at Speech 0, 15 and 100 and the trade refusals are
      pinned by tests; a refused trade changes neither stock nor gold; an accepted one moves item and gold both ways.
- [x] The namespace `skyrimse.barter` 1.0 declares the listed functions; `open` finds Hulda's vendor chest; a non-vendor
      is refused with a reason; the state is `pending` until the data is read.
- [x] A `barter` service turn opens the barter event instead of the box when the setting is on; off, the turn is shown as
      before.
- [x] In game: the barter line opens the window, buying an item lowers the player's gold by the price and raises the
      merchant's, selling does the opposite; three runs give the same trace; the new stock and gold survive a save and a
      load in a new process.
- [x] With every setting off, Morrowind and Skyrim sessions behave as before.

## Out of scope
Perks and disposition in prices, buying and selling stolen goods and the fence rule, the vendor's hours and the
`radius`, the merchant's gold refresh and stock respawn (`iDaysToRespawnVendor`), item cards, a count slider, selling
the equipped copy, barter by gamepad, the `openeden.ui` components and themes, combat, perks, HUD, Papyrus, quests, VMAD.

## Stage log (2026-11-15; Windows only; same hand wrote tests and code, no fresh reviewer)

**Built.** Pack: `VendorData` in `FactionRecord` (`actors.*`), `ItemStat::mKeywords` (`inventory.*`), `barter.*` (prices,
`vendorBuys`, `buy`, `sell`, `sellableCount`), `barter_service.*` (session, namespace `skyrimse.barter` 1.0),
`SkyrimWorldView::vendorOf` and `setVendorServicesLookup`, `SkyrimConditionWorld::setVendorServicesLookup`, `startBarter` in
`skyrimse_game_pack.cpp`. Engine: `decideNeutralBarter`, `neutralBarterEventFields` (`mwworld/neutralinventory.*`) and
`sendBarterEvent` in `neutraldialogue.cpp`. Lua: `barter_windows.lua`, `barter_global.lua`, `barter_player.lua`, two
`builtin.omwscripts` lines. No new setting (the profile and `exclude.txt` need nothing).

**Tests first, red against stubs.** Components (generated plugins, a stub bindings service, the headless UI backend): 48
of 50 new tests failed against stub bodies (and the Lua window module absent); the 2 that passed test absence (a
faction without chest or list is no vendor; gold and worthless items are never bought, which a stub `false` satisfies).
Engine: 2 of 3 failed (the event-name pin passes against a stub). Written after the first green build, not red first:
`SkyrimBarterLine.aMerchantOffersItsServicesWhateverItsPackage` (the vendor-services lookup), the all-button test of the
windows (a test fault fixed twice: `item-all` matches several elements). One fixture name collision (`ServiceFixture`
exists in another test file) was found by the red run and the fixtures renamed. After: 52 new components tests and 3 engine
tests pass; `openmw-tests` and `components-tests` whole runs: see the changelog row. **Existing tests edited: none.**

**Real data** (`SkyrimBarterRealData`, five masters): 189 vendor factions (145 in Skyrim.esm), 47 exclusion lists, 9 that buy
stolen goods, every one with a VENV, 0 odd subrecords; `ServicesWhiterunBanneredMare` (0x9CAFC) has list 0x914F0, chest 0x9CAFB,
hours 0 to 24; the Iron Sword carries VendorItemWeapon and the Cabbage VendorItemFood.

**In game** (Bannered Mare, Hulda, `tools/inventory_check/check.sh` with `BASE_ID=skyrimse:b80803`, `NEAR=336,-416,64`,
`FROM=436,-416`, `inventory test items = 00064b3f:3`; real Space and mouse clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`;
a helper `barter/brun.sh` in the work folder; frames viewed): the topic list shows "What have you got for sale?" (5 topics
against 4); a click opens the window (`NINV barter`, `BARTER open Hulda ... chest 0009cafb, 5 stacks, gold 100, speech 15,
list of 2 keywords`): Ale x7 at 8g, Honningbrew Mead x4 at 31g, Rabbit Haunch x10 at 5g, Salt Pile at 3g; the player's
Cabbage x3 sells for 1g. A click on Ale: `BARTER buy ... unit 8, gold 8, player gold 92, vendor gold 108`; a click on the
Cabbage: `BARTER sell ... unit 1, player gold 93, vendor gold 107`; Close ends the session (`BARTER close Hulda, 2 trades`) and
the world is back. **Three runs give the same trace** (md5 of the `BARTER` and `INV` lines equal). **Save and load:** a save
after the two trades and a load in a new process print player gold 93 and merchant gold 107, and player Ale 1 and merchant
Ale 6 (`INV chunk restored: 2 owners, 1 touched`).

**Morrowind unchanged.** `tools/worldobjects_check` dump (110 lines) equal to combat5's build (another agent's build of main,
no parent build of my own), and the fixed Balmora frame `1c5d47ad0b5dc32d` for that build, this build and this build with
`neutral world`, `neutral actors`, `neutral inventory`, `[GUI] rmlui` (debug panel off) and the pack's `inventory` on. (With the RmlUi
debug panel on the frame differs: the panel is drawn.)

**Not run.** The Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua integration tests, sanitizers, a fresh review,
mutation checks beyond the red runs, the vendor's hours and radius, a merchant that is not Hulda, a vendor with an
exclusion list in game (unit tested), selling an equipped weapon in game (unit tested), a gamepad.

## Decisions for toddhow
1. **A merchant always offers its services.** The barter line's INFO is gated by `GetOffersServicesNow`, which the pack answers
   from the current package's PKDT flag bit 0 (85 packages). Hulda's package (`WhiterunInnkeeperPackage`, flags 0x2000) has
   no such bit (innkeeper packages in the data: 29 have 0x0, 21 have 0x1, 5 have 0x2000), so the line never showed.
   I added a hook: a speaker that is a vendor (a faction with a buy list and a chest) offers services, package or not
   (`SkyrimConditionWorld::setVendorServicesLookup`, in `dialogue.*`, `dialogue_runtime.hpp`, `world_view.*`). This also
   makes every vendor's other services lines pass their GetOffersServicesNow condition. The real rule of the game is not known.
2. **Prices** are RECALLED-VERIFY: ratio 3.3 to 2.0 by Speech, the player pays max(1.05, ratio / 2) x value (1.55 at Speech
   15, 1.05 at 100) and is paid value / ratio (0.32 at 15, 0.5 at 100). No perks, no disposition, no merchant multiplier.
   Tunable in `BarterSettings` (not a setting yet).
3. **The merchant's gold** is the Gold001 in its chest (a leveled list in the data), picked by the seed of the chest's id; it
   is NOT refilled (`iDaysToRespawnVendor` 2 is ignored) and not capped.
4. **Hours and radius** of the vendor (VENV) are carried and not enforced: a merchant trades at any time of day, anywhere.
5. **Stolen goods** are not tracked, so `buys stolen` is carried only.
6. **The equipped copy** of an item is not for sale (refused as "that is equipped"); Skyrim lets you sell it by taking it off.
7. **A click moves one item, the small button the whole stack** (as the inventory windows; no count slider).
8. **A vendor list names keywords; an item with none is bought only by an exclusion list** (VendorNoSale).
9. **The window layout** is plain (the price column overlaps the right edge of the name button; the components are another
   agent's). The conversation closes when the barter opens (the box is not shown), as the game does.
10. **No new setting:** barter follows `[extension.skyrimse] inventory`.
