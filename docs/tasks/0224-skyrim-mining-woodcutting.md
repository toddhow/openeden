# Task 0224: Skyrim mining ore veins and the woodcutting block

Milestone: M-S7 (the Skyrim plan). Branch `mining` from `main` at `6e2f210b81` (task 0221 merged). Same hand wrote tests and code;
no fresh reviewer; Windows only; local commit, not pushed. Behind `[extension.skyrimse] harvest` (the setting of task 0221, no new
setting) and, in the engine, `[Game] neutral inventory`.

## 1. Goal

Striking an ore vein with a pickaxe gives ore; the vein depletes after a few strikes, reads as depleted, fills again after a time
and is saved. Using the wood chopping block with a wood axe gives firewood. Without the tool the game's own refusal line shows.

## 2. Survey: what the scripts really do

Task 0221 section 4 named the scripts; this task decompiled them (`housecarl_decompile_script` on `scripts/mineorescript.pex` and
`scripts/resourcefurniturescript.pex` of `Skyrim - Misc.bsa`; extracted outside the repository, deleted afterwards).

`MineOreScript` (ACTI):
- Properties with the script's own defaults: `ResourceCountTotal` 3 (drops before depleted), `ResourceCount` 1 (items a drop),
  `StrikesBeforeCollection` 1, `AttackStrikesBeforeCollection` 3 (strikes by attack, OnHit; not used here), plus `Ore` (MISC),
  `mineOreToolsList` (FLST), `lItemGems10` (LVLI, optional), `FailureMessage`, `DepletedMessage` (MESG), and quest, location and
  sound properties of the Cidhna mine and the achievements counter (not used).
- OnActivate by the player: `ResourceCountCurrent == 0` shows DepletedMessage; no tool (`GetItemCount(mineOreToolsList) > 0`) shows
  FailureMessage; otherwise the script activates its **linked reference** (a MineOreFurniture that plays the mining animation) and
  each swing calls back as one strike. A strike counter reaches `StrikesBeforeCollection`, then `giveOre`: one drop
  (`ResourceCount` x `Ore`, plus one roll of `lItemGems10`), the counter of drops falls, at 0 the vein is destroyed
  (`setDestroyed`, a destruction stage) and DepletedMessage shows. `onReset` (a cell reset) refills it.
- `onCellAttach` depletes a vein that has NO linked reference (`depleteOreDueToFailure`). Not applied here (see decisions).

`ResourceFurnitureScript` (FURN, the block `WoodChoppingBlock` 07022E): `Resource` (Firewood01), `ResourceCount` (2 on the block),
`MaxResourcePerActivation` (6), `requiredItemList` (`woodChoppingAxes`: Woodcutter's Axe 02F2F4, Poacher's Axe 0AE086),
`FailureMessage`. The player stays at the block and each `AddToInventory` animation event gives `ResourceCount`; the session ends
when the running count reaches `MaxResourcePerActivation`. The block never depletes.

Data (the five masters, `SkyrimMiningRealData`, lines `SKYRIMSE-MINING`): 586 ore bases (ACTI with MineOreScript), 3 wood bases (FURN),
all 589 with a tool list and a failure message, 586 with a depleted message, 268 with a gem list, 586 with an activate text
override ("Mine"), 19 distinct ore items, only 18 veins with a total other than 3 and 2 with strikes other than 1. 1,738 ore
placements and 110 block placements (1,133 resource placements in Tamriel). MESG: 944 records, all with a DESC and
ALL of them localised (the text is in a string table: the pack reads it lazily). The tool list `MineOreToolsList` holds
`weapPickaxe` 0E3C16 ("Pickaxe"), `SSDRocksplinterPickaxe` 06A707 and `dunVolunruudPickaxe` 1019D4. The game's own lines: "You need a
pickaxe to use this.", "This ore vein is depleted.", "You need a woodcutter's axe to use this.".
Nearest to Whiterun's gate: iron veins at (23867, -11073, -3793) and (28493, -6114, -3102), the block `000fe2b9` at (30367, -12244).
An ore vein's model is a large rock group (its bounding box was 1,150 units wide); the ore can only be struck from where the vein's own
collision is.

**Animation.** The mining and chopping animations (`IdleMineOre*`, `IdleWoodChopExit` ...) belong to the linked furniture, played by
the actor's furniture use. None exists in the neutral world: a strike here is instant. Nothing was built for them.

## 3. Design

- `mining.*` (pure): `resourceFromScripts` (the properties above by name, ignoring case; defaults when absent; counts clamped to
  1..1000), `orePrompt` ("Mine Iron Ore Vein"), `depletedName` ("Iron Ore Vein (depleted)"), `gemSeed`, `VeinBook` (per placement:
  strikes since the last drop, drops, the game second it is full again; `strike`, `collectGrown`) and the save chunk `mining` v1
  (u32 count, then per vein u32 placement ascending, u16 strikes, u16 drops, f64 due; strict).
- `mining_service.*`: `MiningService` over hooks (resource of a base, the player's count of a form list's items, the leveled pick,
  add to the player, item name, message text, warm-up) and the namespace `skyrimse.mining` 1.0 (Read `state`, `message`,
  `revision`; Mutate `activate(placement, base)`, `set_clock(seconds)`).
- A new chunk, not a bump of `harvest`: the two books have different layouts and the harvest reader refuses any version but 1.
- Records: `ModelRecord::mDescription` (DESC of a MESG, the type is now read as a model record) and `mActivateText` (RNAM of an ACTI;
  not `mHarvest`, which an existing test pins as absent for an ACTI).
- World view: `resourceOf(base)` (ACTI with MineOreScript or FURN with ResourceFurnitureScript), `formListEntries` (the FLST from the
  dialogue worker's data, which it starts on first use), `messageTextOf` (a localised DESC: `.DLSTRINGS`, then `.STRINGS`,
  `.ILSTRINGS`, like the loading tips).
- Rules (every number is the script's own except the ones named OURS): the tool check, one activation = one strike, a drop every
  `StrikesBeforeCollection` strikes (ore x `ResourceCount`, plus one gem roll of `lItemGems10` seeded by placement and drop number),
  depleted after `ResourceCountTotal` drops, the last drop is still given. A refused try (no tool, depleted, the inventory cannot take
  it, the lists not read yet) changes no counter. Regrowth `kRegrowSeconds` = 10 game days: OURS (the game resets a cell).
  The block gives `ceil(MaxResourcePerActivation / ResourceCount) x ResourceCount` firewood per activation (6 for the data's
  block) and never depletes: one activation is the whole chopping session (OURS, no animation drives the chops).
- Engine (small): `decideNeutralResource` (a placed Activator the player activates sends `openeden_skyrimse_resource_activated`,
  kind "resource"; only with `[Game] neutral inventory`; nothing happens when no script listens). A Furniture already sends the bench
  event, which `mining_global.lua` also answers (the pack ignores a base that is not a resource). The tooltip of a placed
  object asks the pack for ITS name (`DisplayNameSource::placementNameOf(placement, base)`, SDK 0.2, additive, default empty):
  an Activator shows a tooltip only when the pack names it (levers, markers stay quiet), and a depleted vein reads
  "Iron Ore Vein (depleted)". `NeutralDisplayNames::intern` keeps those strings alive for the view `getName` returns.
- Lua: `mining_global.lua` (events, the clock, the HUD line), `mining_player.lua`.

## 4. Stage log

**Tests first.** `testskyrimsemining.cpp` (pure: scripts, text, book, chunk), `testskyrimseminingservice.cpp` (hooks and
bindings), run RED against stubs of `mining.cpp` and `mining_service.cpp` (every answer neutral and
wrong): **36 of 41 failed**, the 5 that passed test refusals and absence. Written AFTER the code and not run red: the two tests added
for the not-ready lists and the warm-up (service), `testskyrimseminingrecords.cpp` (4 tests: the ACTI RNAM and MESG DESC decode,
written before the fields were added but compiled only after), `openmw_tests/mwworld/testneutralresource.cpp` (4 tests), and the
gated `testskyrimseminingreal.cpp`. The first run of the pure tests found one fault of the TEST (a book with total 3 struck once).

After the code: `components-tests` `SkyrimMining*:MiningServiceFixture*` 48 pass (+1 gated skip), `*Skyrim*:*Neutral*:*Frozen*:
*Extension*` 2,168 pass, the whole `openmw-tests` 2,853 pass. `CI/check_tests_registered.sh`, `check_layer_rules.sh` (104 exceptions,
unchanged), `check_extension_sdk.sh`, `check_no_osg.sh`, `check_game_conventions.sh` and `tools/testprofile/coverage.sh` pass.
**No existing test edited.**

**In game** (`tools/neutral_session_check` `n9pick` through `mn1.sh` and `mnenv.sh` in the work folder; real activate key through
`OPENEDEN_RMLUI_INPUT_SCRIPT`; fixed frame time; sound 0; quiet window; frames viewed):
- iron vein `MineOreIron01_LTundraRocks` (placement `0010e5ad`, near Whiterun, `N9_AIM` on a point of its mesh, `N9_AUTOAIM`):
  the tooltip reads "Mine Iron Ore Vein"; with no pickaxe the key press shows "You need a pickaxe to use this." (the game's own
  `PickaxeFailureMessage`, read from `.DLSTRINGS`); with `inventory test items = 0e3c16:1` three presses give three Iron Ore, the
  third logs `strike 1/1 gave 00071cf3 x1, depleted` and the HUD shows "This ore vein is depleted."; the tooltip then reads
  "Iron Ore Vein (depleted)" and the fourth press shows the depleted line and gives nothing; the inventory window lists Pickaxe,
  Gold x100, Iron Ore x3.
- save and load in a new process (`N9_PHASE=save`, then `--load-savegame`): `MINING restored 1 veins`, the vein is still depleted
  (tooltip and press), Iron Ore x3 in the inventory.
- regrowth: a load plus `world.advanceTime(250)` logs `vein is full again`, the tooltip is the prompt again and the next press mines
  (count 4).
- the chopping block `000fe2b9` (`WoodChoppingBlock`, base 0007022e, with `inventory test items = 02f2f4:1`): the tooltip reads
  "Wood Chopping Block", the press logs `block gave 0006f993 x6` and shows "Chopped 6 Firewood."; the inventory lists
  Woodcutter's Axe, Firewood x6 and Gold. (Without an axe the same message path as the vein, line "You need a woodcutter's axe to use
  this."; unit tested, not run in game.)
- three runs of the vein scenario (presses after the lists are ready): the 12 `NINV` / `MINING` / `mining` lines are byte-identical
  (md5 `d562b7cc...`). **An early press (frame 4500, about 18 s after start) fell in the window where the dialogue worker has not read
  the form lists: it logs `waits for the form lists` and shows "The tools cannot be checked yet. Try again in a moment." (a refusal that
  costs nothing); that one run differed from the other two for that reason.**
- Morrowind unchanged: `tools/worldobjects_check` 110 lines OK and the fixed Balmora frame `1c5d47ad0b5dc32d`, both equal to the
  numbers recorded for the parent (`mw_h6.out` of task 0221, same parent code; no second build tree made).

**Harness edits** (`tools/neutral_session_check`, not tests): `n9pick` gained `N9_HOUR`, `N9_AIM` (stand at a point on the object),
`N9_AUTOAIM` (the player script casts rays and turns to a spot where four rays round it all hit the target: the player's place on a
rock is not repeatable), `N9_HIDE` (disables the statics of one record id in the cell: `RockCliff04` stood in the focus ray of the
vein), `N9_SCAN` (a ray scan that prints the hits), the player position in the `N9 t` line, the box of the target, and the load
phase also places the player. None changes an existing scenario's behaviour (the new keys default to nil).

**Not run:** the Linux gate and GCC `-Werror` (traps checked by hand), clang-format 14 (formatted by hand, 120 columns; some
long lines in `world_view.hpp` are older), clang-tidy, the Lua tests, a fresh review, a gem drop in game (unit tested; the
`lItemGems10` rolls gave nothing in three strikes), a vein with several strikes per drop (unit tested), a wood block refusal and the
chopping block with `crafting` on (that script also answers the bench event and may open a bench window for any furniture), a
vein in an interior, the Hearthfire `BYOHMineStone01` quarry, any mining or chopping animation (none exists in the neutral world).

## 5. Edits outside the new files

SDK `sdk/include/openeden/game/display_name_source.hpp` (0.2: `placementNameOf`, additive); `apps/openmw/mwclass/neutral.cpp`,
`mwworld/neutralinventory.{hpp,cpp}`, `mwworld/neutraldisplayname.{hpp,cpp}`; `game_packs/skyrimse`: `records.{hpp,cpp}` (two
`ModelRecord` fields, MESG routed like the model records), `world_view.{hpp,cpp}`, `neutral_world.hpp`, `skyrimse_game_pack.cpp`,
`CMakeLists.txt`, `CLAUDE.md`; `files/data/builtin.omwscripts` and `CMakeLists.txt`; `apps/components_tests/CMakeLists.txt`,
`apps/openmw_tests/CMakeLists.txt`; the harness above; `CLAUDE.md` (changelog). `papyrus_host*` and the combat contract are untouched.

## 6. Decisions for toddhow

1. A vein is struck once a press, instantly: the vanilla linked furniture and its animation are not built. Veins whose data has no
   linked reference are deplete-on-attach in vanilla (`depleteOreDueToFailure`); here every vein works.
2. The block gives its whole session (6 firewood for the data's block) per activation, and never depletes.
3. Regrowth of a depleted vein is 10 game days (ours). A depleted vein stays visible (the vanilla destruction stage is not shown).
4. The pack tooltip hook (`placementNameOf`) is asked every frame while a placed Activator or Furniture is looked at, on the main
   thread. An Activator has a tooltip ONLY when the pack names it, so other activators keep their silence.
5. Until the dialogue worker has read the form lists (about 20 s after start) a strike with a tool check is refused with a
   try-again line instead of "you need a pickaxe". The first clock tick starts that worker.
6. With `[extension.skyrimse] crafting` on, the block's bench event may also reach the crafting script (existing behaviour for any
   furniture).
