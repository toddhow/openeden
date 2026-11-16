# Task 0221: Skyrim harvesting

Milestone: M-S7 (the Skyrim plan). Branch `harvest` from `main` at `b4bb085d90`. Same hand wrote tests and code; no fresh
reviewer; Windows only; local commits, not pushed. Behind `[extension.skyrimse] harvest` (default false, declared in
`extension.json`, noted in the test profile) and, in the engine, `[Game] neutral inventory`.

## 1. Goal

1. Activating a plant, mushroom or other harvestable object (FLOR, or TREE with an ingredient) with the real activate key
   gives its ingredient (PFIG, an INGR or a leveled list) to the player's inventory (task 0182, the pickup path of task
   0208), the plant looks harvested, grows back after a number of game days and is saved. The prompt says "Harvest <name>".
2. Mining and woodcutting: survey; build only if cheap; otherwise document what it takes.

## 2. Survey (the five masters, through the pack's own parser: `SkyrimHarvestRealData`, lines `SKYRIMSE-HARVEST`)

- FLOR 108, TREE 267 records; 178 name an ingredient (PFIG), 197 have none (a FLOR or TREE with only PFPC or neither).
- The ingredient link points at an item (INGR in the item table) for 171 and a leveled list (LVLI) for 7; 0 point at
  anything else. PFIG, PFPC and RNAM have the sizes the header says (an odd subrecord count of 0 is asserted).
- PFPC is 4 bytes in every record that has it: spring, summer, fall, winter percent (the season order is the CK's,
  RECALLED-VERIFY). 6 records with an ingredient have all four at 0 (for example the data's `ChickenNest`, whose eggs are
  not seasonal): treated as "no seasonal data", 100 percent (ours). No record has a zero in only some seasons that the
  masters' 178 hit ("some zero" 0), so out-of-season is rare in the data.
- RNAM (the activate text) is present on 87 FLOR records and is a localised string id in every one (resolved through the
  plugin's `.STRINGS` at index time; "Harvest", "Pick", "Catch" ... ). TREE records have no RNAM: the prompt says "Harvest".
- Placements of those bases: 31,597 (flora 13,627, trees 17,970). 186 in the Tamriel cells 4..8 by -4..0. The nearest
  to Whiterun's gate in the data are farms and plains plants about 5,400 units south (lavender, wheat, mountain flowers);
  the in-game check uses the `TreeFloraLavender01` placement `01000f66` at (24890, -13383, -4587) in Tamriel.
- The models: `FloraSnowberry02.nif` and `FloraLavender01.nif` have NO NiSwitchNode (one `BSLeafAnimNode` and one shape);
  `ChickenNest01.nif`, `ReachTreeStump01_Mush.nif` and `TestMountainFlower01Blue.nif` have one or two `NiSwitchNode`s
  with two children (child 0 the full plant, child 1 the harvested look: read with the `housecarl` NIF inspector on the
  Skyrim - Meshes1 copies; the mod list's loose copies were not used). `components/nifscene/flatten` has no switch
  handling, so today BOTH children are drawn for a model with a switch (the harvested state is a subset of the full one,
  so a plant looks unharvested).

## 3. Design

- `harvest.*` (pure): season of a month, the chance of a season, the seeded roll, the activate prompt, the `HarvestBook`
  (placement to the game second it grows back, attempts) and the save chunk `harvest` v1 (u32 count, per plant u32
  placement ascending and f64 due time, u32 attempts; strict: damaged, over the cap, not ascending, not finite or trailing
  bytes change nothing).
- `harvest_service.*`: `HarvestService` over hooks (plant data, leveled pick, add to the player, item name, hide, show)
  and the namespace `skyrimse.harvest` 1.0 (Read `state`, `message`, `revision`, `prompt(base)`; Mutate `harvest(placement,
  base)` and `set_clock(seconds, month)`).
- Records: `ModelRecord::mHarvest` (`HarvestData`: PFIG resolved, PFPC, RNAM as a `LString`) for FLOR and TREE.
- World view: `plantOf(base, ...)` and, with the setting on, `displayNameOf` of a harvestable plant answers its prompt
  ("Harvest Lavender"), so the engine's tooltip shows it without an engine change.
- Pack glue (`startHarvest`): the ingredient is picked through `inventory::resolveContents` seeded from the placement and
  the attempt number (a leveled list picks once), added to the player's inventory, and the placement is disabled through
  `WorldSourceService::setObjectEnabled(false)`; a cell built later (a load, a return) comes out with it disabled
  (`SkyrimCellSource::setHarvestedLookup`, like task 0208's taken items). The engine's own neutral save record already
  keeps the disabled state; the `harvest` chunk keeps WHEN it grows back.
- The pack never reads a clock: `harvest_global.lua` passes the game time and the calendar month (`gamemonth` of the
  engine's globals, -1 when unknown) in twice a second (the rest script's way); plants whose time has come are shown again
  (`setObjectEnabled(true)`). Regrowth is applied at the next clock tick, not lazily at cell build.
- Engine (small, `neutral.cpp`, `neutralinventory.*`): a Flora or Tree the player activates sends the event
  `openeden_skyrimse_harvest_activated` (kind "flora", with `[Game] neutral inventory`) and now shows a tooltip
  (`neutralKindHasToolTip`). Nothing happens when no script listens. The existing hasToolTip rule moved into that pure
  function unchanged for the other kinds.
- Ours, named and RECALLED-VERIFY: the calendar mapping (months 2 to 4 spring, 5 to 7 summer, 8 to 10 fall, the rest
  winter), the regrowth time (10 game days = 240 hours, Skyrim's cell reset GMST `iHoursToRespawnCell`), all-zero chances
  meaning 100, the roll (a failed roll empties the plant until it grows back; a season with chance 0 refuses and leaves it
  as it was), leveled ingredient level 1.
- The look of a harvested plant is a HIDDEN OBJECT (the whole placement). The proper look is the model's harvested
  `NiSwitchNode` child. That needs (1) a flatten option that picks a switch child (the `Limits` of
  `components/nifscene/flatten.hpp`), (2) a per-placement model variant: the model cache and the physics shape cache are
  keyed by VFS path, so a suffixed path fails the VFS lookup in `PhysicsShapeManager::readSceneShape` and the nif reader,
  and (3) a way to re-insert one placement with another model at run time (`setObjectEnabled` only toggles; a disable and
  an enable in the same frame collapse to the last change). A new optional SDK interface found by `dynamic_cast` (the
  `DisplayNameSource` pattern) is the right seam; it was not built. Plants with no switch node (snowberry, lavender,
  nightshade, the TREE plants) have no harvested look in the file, so hiding is the only option for them.

## 4. Mining and woodcutting (surveyed, not built)

- An ore vein is an ACTIVATOR (`MineOreIron01_LReachGrass` ...: ActivateTextOverride "Mine", a destructible, no
  keyword), driven by the Papyrus script `MineOreScript`: 11 properties (read from the decoded VMAD): `Ore`,
  `mineOreToolsList` (a form list of pickaxes), `lItemGems10` (a gem leveled list), `FailureMessage`, `DepletedMessage`
  and quest and location properties of the Cidhna mine (`CidhnaMineLocation`, `MS02`, `CidhnaMinePlayerBedREF`,
  `DialogueCidhnaMine`, `AchievementsQuest`, `DrScOreOpen`). The activation, the swing animation and the ore count are all
  script logic. The woodcutting block is a FURNITURE (`WoodChoppingBlock` 07022E) with `resourcefurniturescript`
  (`IdleWoodchopExit`, `Resource` = Firewood, ...) plus a second script.
- Neither fits the bed or bench path cheaply: a vein is an Activator, which the engine's neutral classes route to the
  default activation (nothing); the furniture event of tasks 0199 and 0210 is a Furniture and would need the pack to
  recognise the block (its script's `Resource` property), check for an axe in the inventory (a WEAP with the woodaxe
  animation type), pay a count and deplete. What it takes: (a) the engine sends an event for an Activator (a new decide
  function like `decideNeutralHarvest`), (b) the pack reads the base's VMAD for `MineOreScript` or
  `resourcefurniturescript` (`ModelRecord::mScripts` already holds them) and the tool list (a FLST, decoded by the
  dialogue worker), (c) the rules (a count of strikes, a depletion time, the pickaxe check, the ore plus a gem roll for
  the veins that name `lItemGems10`) and a depleted look (the vein model has a depleted state: hide or a second model),
  (d) the same regrow book. About the size of this task again. Not built.

## 5. Stage log

**Tests first.** `testskyrimseharvest.cpp` (pure rules, book, chunk, 24 tests with the later restored-sink test),
`testskyrimseharvestservice.cpp` (hooks and bindings, 16), `testskyrimseharvestrecords.cpp` (FLOR and TREE decode, 7),
`testskyrimseharvestreal.cpp` (gated, 1), `openmw_tests/mwworld/testneutralharvest.cpp` (7). Run RED against stubs of
`harvest.cpp`, `harvest_service.cpp`, `decideNeutralHarvest` and `neutralKindHasToolTip` (every answer neutral and
wrong): `components-tests` 34 of 46 failed (the 12 that passed test absence, refusals and the skipped real test),
`openmw-tests` 3 of 7 failed (4 test absence). The records test did not compile before `mHarvest` existed. After the code:
47 of 47 (plus the skipped gated test) and 7 of 7 pass; the restored-sink test was written with the code and not run red.

**Existing suites (Windows, build tree `combatai\build`):** `components-tests --gtest_filter=*Skyrim*:*Neutral*:*Frozen*:
*Extension*` 2,107 pass; `openmw-tests` `Neutral*:*Inventory*:*Pickup*:*Lockpick*` 630 pass. The whole binaries were not
run. No existing test edited.

**In game** (`tools/neutral_session_check` `n9pick` through `hv1.sh` in the work folder: `CELL=@Tamriel,24890,-13382`,
`[Game] neutral inventory`, `neutral saves`, `[GUI] rmlui`, `[extension.skyrimse] inventory` and `harvest`; REAL activate key
through `OPENEDEN_RMLUI_INPUT_SCRIPT`; fixed frame time; sound 0; quiet window):
- the tooltip over the lavender reads "Harvest Lavender" (frame viewed); the key press logs `NINV flora 01000f66 ...`,
  `HARVEST 01000f66 took 00045c28 x1 at 100%` and the HUD line "Harvested Lavender."; the Lua object reads `enabled false`;
  the next frame has no plant (viewed), the inventory window lists "Lavender" (viewed).
- three runs give a byte-identical trace (6 lines: focus, event, harvest, message, the second press finding nothing).
- save and load in a new process: `HARVEST restored 1 harvested plants`, the plant stays disabled, the inventory count 1.
- regrowth: a load plus `world.advanceTime(250)` logs `HARVEST 01000f66 grew back`, the object reads `enabled true` and the
  plant is drawn again (viewed, the daylight comes from the 250 hours).
- setting off (`harvest = false`, inventory on): the tooltip reads "Lavender", the event goes out, nothing listens, the plant
  stays and nothing is given.
- Morrowind unchanged: `tools/worldobjects_check` dump 111 lines identical (`cmp`) and the fixed Balmora frame
  `1c5d47ad0b5dc32d`, both against the same numbers from the baseline build of `b4bb085d90` made first.

**Harness notes.** Object record ids in the Lua API are DECIMAL (`skyrimse:b113469` for base 0x1bb3d); the focus ray misses
a low plant by a few units in some runs (aim with `N9_PITCH=1.0`, dist 80, and press the key twice); `n9pick` gained an
`N9_ADV_T` / `N9_ADV_H` pair (pass game hours once) and a listing when the target is not found; the harness's own check
"the save attempt was not refused" fails the run script with `neutral saves` on (the results are written before it).

**Not run:** the Linux gate and GCC `-Werror`, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, a fresh review,
a FLOR (the in-game plant was a TREE; FLOR differs only by its RNAM verb and is covered by unit tests), a leveled ingredient in
game (unit tests only: 7 of 178), seasons in game (the month comes from `gamemonth`; the data has no seasonal zero to show),
the real Skyrim calendar month, a plant in an interior, mining and woodcutting.

## 6. Decisions for toddhow

1. The flora tooltip: Flora, Tree and Item objects show a tooltip with `[Game] neutral inventory` alone, whether or not the pack
   harvests; the name shows (the prompt only with the pack setting). Strict "unchanged when off" would need an engine setting.
2. The harvested look is a hidden placement. The model's harvested switch child needs the per-placement variant seam above;
   say if it should be built next (I would add the optional SDK interface).
3. Regrowth is 10 game days, a failed roll empties the plant, a zero season refuses; all ours.
4. A TREE plant disappears completely (no switch child to show): acceptable for the 178 plants of the data?
5. Mining and woodcutting need an Activator route in the engine and Papyrus-property rules in the pack; build next?
