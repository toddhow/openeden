# Task: Skyrim map and compass (milestone M-S7)

Status: BUILT (compass, world map, discovery, save chunk, local map cell size); Windows only, local commits on branch `skymap`, not pushed. Not run: the Linux gate, clang-format 14, clang-tidy, the Lua integration tests, a fresh review (see "Not run").
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.24 ("compass with quest markers"); decision 0015 (UI system), task 0097 (script bindings for game packs), tasks 0180 (HUD), 0185 (journal) and 0187 (aliases) as the patterns.
Goal: a Skyrim session has a compass strip with the cardinal points, the map markers near the player and the quest targets, a world map window with the player and the discovered markers, discovery by coming near a marker, and a local map whose markers are placed by the worldspace's own cell size.
Inherited code touched: `apps/openmw/mwgui/mapwindow.{hpp,cpp}` (`LocalMapBase::getMarkerPosition`, `setPlayerPos`, `markerPosition...`, the file-static `cellSize`), `apps/openmw/mwgui/windowmanagerimp.cpp` (`changeCell`), `game_packs/skyrimse/src/world_view.cpp` (`buildDialogueData`: one more walk, one new `mapAccess`), `terrain_source.*` (two new const queries), `files/data/openeden/hud/skyrimcombat.lua` (ONE line: the enemy bar moves from top 28 to 56 so the compass keeps its place above it).
Inputs / outputs: new pack files `map_markers.*` (parse, index, discovery, query; in `game_skyrimse_world` too) and `map.*` (targets, chunk, service, bindings); the script package `core.skyrimse.map` 1.0; Lua `openeden/skyrimse/{compass,worldmap}.lua`, player and global scripts `scripts/openeden/skyrimse/map_{player,global}.lua`; the pack setting `[extension.skyrimse] map` (default false, declared in `extension.json`).
Constraints: off by default; needs `[GUI] rmlui` for any window; Morrowind unchanged (nothing of the new code runs without the pack's `core.skyrimse.map` namespace; the local map change is the identity at 8192).
Out of scope: fast travel, map markers the player places, interior quest markers through doors, fog of war, zoom and pan, names of marker types, the engine's MyGUI global map for Skyrim.

## 1. Survey (citations at commit `3885a4091b`)

- **The HUD pattern:** `game_packs/skyrimse/src/hud.cpp` `CombatHudBindings`, Lua `files/data/openeden/hud/skyrimcombat.lua` (`M.create(ui, combat)`), the builtin line `PLAYER REQUIRES=core.skyrimse.combat>=1.0` in `files/data/builtin.omwscripts`. The journal (`journal.*`, `journal_windows.lua`, `journal_player.lua`) is the pattern for a service, its Lua window logic and the headless test (`apps/components_tests/ui/testskyrimsejournalwindows.cpp`).
- **Bindings:** `sdk/include/openeden/game/bindings.hpp` (2.0): arrays hold at most `kMaxBindingArrayElements` = 256; a Mutate function has no result and only global scripts may call it. So marker answers are parallel arrays by a page of 256, and discovery is a Mutate call from a global script.
- **Quest targets:** `quest.hpp` `QuestObjective::mTargets` (QSTA: alias id), `QuestState::aliasFill(quest, alias)` (task 0187's filled aliases: `AliasFill::mReference` or `mActor`), `PlacedRefIndex::find/byBase/initiallyDisabled` (position and cell of a placement), `SkyrimWorldView::fillSpeakerCell` and `mCells[cell].mInfo.world/interior` (world of a cell). Objective flags (displayed, completed) are `PapyrusHost::objectiveState`, as the journal reads them.
- **Local map:** `apps/openmw/mwgui/mapwindow.cpp` used the file-static `cellSize = Constants::CellSizeInUnits` (8192) in `getMarkerPosition`, `setPlayerPos` and the custom marker placement; `MWRender::Map` already follows the worldspace (`mapWorldSizeFor`, task "skyext", changelog 2026-11-12); `ESM::getCellSize(worldspace)` (`components/esmbridge/util.hpp`) is 4096 for `skyrimse:w...` worldspaces.
- **Terrain for the world map:** `SkyrimTerrainIndex::buildTerrain` (heights 33 x 33, vertex colours), `cellWater`, `mLands` keyed by (world, x, y).
- **Images:** RmlUi reads images only through `VfsFileInterface` (`components/ui_rml/fileinterface.*`): there is no in-memory image path for a pack, and the baked LOD tiles are not reachable from Lua. The world map is therefore drawn as rows of coloured blocks (see 3.4); a picture file would need a new generated-file hook in `ui_rml`, which the task forbids touching (themes and core components).

## 2. Layouts settled by the data (the five masters, Python walker, aggregates)

- A map marker is a REFR (base 0x10) whose subrecords include the empty `XMRK`, then `FNAM` (1 byte), `FULL` (4 bytes: a string id, every plugin here is localised), `TNAM` (2 bytes: type, then 0 in every marker) and `DATA` (24 bytes: position, rotation). Counts: Skyrim.esm 397, Dawnguard 17, Dragonborn 54, HearthFires 3, 471 in all; 35 of them (Skyrim.esm) sit in interior groups and are dropped, so **436 exterior markers** remain, which is exactly what the pack logs (`MAP ready: 436 exterior map markers`).
- `FNAM` values: 0 (377 of Skyrim.esm's 397), 1 (9), 3 (11), 4 (3, Dawnguard). Names are RECALLED-VERIFY (xEdit): bit 0 visible, bit 1 can travel to, bit 2 show all hidden. The pack reads only bit 0: such a marker is shown from the start, the others once discovered.
- `TNAM` byte 0: 0 to 58 in the data; kept as stored, not named (names of types are not decoded).
- Placement: 362 of Skyrim.esm's 397 sit in a worldspace's persistent cell (the world group, type 1, then cell children, type 6, then persistent, type 8); 35 sit in interior cell groups (they have no exterior place and are dropped). The world of a marker is the label of the type 1 group.
- **Coordinates are shared by a world and the worlds it borrows land from:** the markers near Whiterun's street are in both `WhiterunWorld` (107119) and `Tamriel`, so a query for a world covers the world and its parents (up to four).
- The world map's land: Tamriel has land in a 100 x 100 cell range; `WhiterunWorld` has 7 land cells of its own and borrows Tamriel's, so the map uses the topmost ancestor that has land.

## 3. Design

1. **Markers and discovery (pure).** `parseMarkers` walks a plugin with `Content::Visitor` and keeps each REFR that has an XMRK (world from the type 1 group; hostile input: odd subrecord sizes counted and never guessed, a cut-short record fails that record and the walk goes on, caps). `MarkerIndex` merges a load order (later plugin wins, a deleted record removes, a marker with no world is dropped). `Discovery` keeps the ascending ids of the discovered markers; `visit(world(s), x, y, radius)` marks every marker within a horizontal radius. `query` answers the shown ones (Visible flag or discovered) within a range and the undiscovered ones within a hint range, nearest first, paged by 256. **Ours (the data has no rule), named constants in `map_markers.hpp`:** discovery radius 4096 units, hint range 8192, compass range 16384.
2. **Quest targets (pure).** `buildTargets`: for each running quest, each objective that is displayed and not completed, each QSTA target whose alias is filled (a placed reference, or a unique actor's first placement that starts enabled): position, cell, world (none for an interior), objective text. At most 64.
3. **Service and bindings.** `MapService` polls for its data (never waits: `SkyrimWorldView::mapAccess` + `papyrusAccess`), keeps targets keyed by a revision (discovery + quest state + objective flags) and declares `skyrimse.map` 1.0: `ready`, `revision`, `marker_x/_y/_type/_discovered/_name(world, x, y, range, hint, page)`, `target_x/_y/_world/_text`, `raster_info(world)`, `raster_row(world, row)`, and the Mutate function `visit(world, x, y)`. The name of an undiscovered marker is not given. The save chunk `map` v1 holds the ascending discovered ids (strict reader: count must fit, ids ascending, version known; a refused chunk changes nothing).
4. **The world map's land.** `SkyrimTerrainIndex::landExtent` and `mapRow`: one pixel per exterior cell from the mean vertex colour lit from the north west by the cell's slope, the water colour where the cell's water surface is above its mean height; a row is built when the window asks (one per frame, so no frame builds more than one row of about 100 cells). The window draws a row as runs of equal pixels, so a page of about 100 rows holds a few thousand blocks, and overlays the markers, the quest targets and the player (all by slots made once).
5. **The compass.** `compass.lua`: relative angle = bearing minus heading (the heading is clockwise, 0 faces +Y), the strip shows the 180 degrees ahead, cardinals in the first row, markers (`O` discovered, `?` hinted) and quest targets (`!`) in the second; the name of the discovered marker nearest the middle (within 7 degrees), or the objective of a quest target there, is written under the strip. The pack is asked again only when the player moved 256 units or the revision changed. In an interior the strip shows the cardinal points only (no way through doors is worked out).
6. **Local map.** `MWGui::localMapCellOf(x, y, cellSize)` (pure, `localmapcell.hpp`) replaces the Morrowind cell split in `LocalMapBase`; `WindowManager::changeCell` gives both maps the worldspace's cell size.
7. **Scripts.** `map_global.lua` calls `visit` twice a second of simulation time (the update's `dt` is not used: it is 0 in this build); `map_player.lua` builds the compass and opens the world map with M (the window takes the `Interface` mode with no MyGUI window, as the journal does; Escape or Close leaves).

## 4. Stages

| Stage | What | State |
| --- | --- | --- |
| A | marker parse, index, discovery, query | built, tested |
| B | quest targets, chunk, service, bindings | built, tested |
| C | compass window | built, tested, seen in game |
| D | world map window and the land raster | built, tested, seen in game |
| E | local map cell size | built, tested (pure part), seen in game (interior and exterior minimaps) |

## 5. Tests

`apps/components_tests/skyrimse/testskyrimsemap.cpp` (27: parse 5, index 3, discovery 5, query 4, targets 4, chunk 3, service 9 over a fake bindings service, written with the code and checked by four one-line mutations each caught by named tests: radius `<` for `<=`, the heading sign, the chunk's ascending check, the completed-objective filter), `apps/components_tests/ui/testskyrimsemapwindows.cpp` (24 over the headless backend and a fake package), `apps/openmw_tests/mwgui/testlocalmapcell.cpp` (5, **written first and run red against a stub: 5 of 5 failed**, then green). **The pack and window tests were written together with the code, not run red against stubs first: that step of the full loop was not done for them** (mutation checks were used instead; the first window tests did fail for real faults that were then fixed: elements made outside a document's build function).

## 6. In game (Whiterun, five Skyrim masters, `tools/skymap_check/check.sh`, quiet window, master volume 0)

See the changelog row of 2026-11-16 for numbers and frames.

## Decisions for toddhow

1. Discovery radius 4096, hint range 8192, compass range 16384 are ours; the data has no rule.
2. A map marker is "shown from the start" by FNAM bit 0 only; bit 2 (show all hidden) is not used.
3. Markers on the compass are the letters `O` and `?` and quest targets `!` because the Morrowind bitmap font has no star or diamond; a picture glyph would need a font or an icon image (the VFS has the Skyrim interface textures, nothing loads them yet).
4. The world map is a coarse picture (one block per cell, no zoom) because RmlUi reads images only from the VFS; a better map needs a generated-image hook in `components/ui_rml` (a core change) or a baked file.
5. Interior quest targets show nothing on the compass (no door routing).
6. The one edit of the combat HUD file: the enemy bar's `top` 28 to 56.
