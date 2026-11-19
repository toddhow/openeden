# Task: Baked global illumination for Skyrim (neutral) cells
Status: BUILT (2026-11-19; branch `skygi-0358` from `a4238aabd6`, local commits, not pushed; Windows only).
Approved by toddhow 2026-11-19: "the smallest complete change". Follow-up of tasks 0028 (baked light probes for interiors)
and 0036 (exterior sky visibility); part of the lighting work of task 0355.

Goal: `openmw --bake-gi <interior>` and `openmw --bake-gi-exterior` work in a game pack's neutral world (Skyrim SE), and the
runtime uses the result, with the existing settings `[Rendering] gi` and `gi exterior` (no new setting).
Inherited code touched: `apps/openmw/probebaker.cpp` (`begin`), `apps/openmw/skyvisbaker.cpp` (whole baker),
`apps/openmw/mwrender/rendermanager.cpp` (`exteriorCellLoaded`, `exteriorCellUnloaded`, `updateSkyVisibility`,
`updateSkyDebug`), `apps/openmw/mwworld/scene.cpp` (the two calls), `components/render/probes.cpp` and
`skyvisibility.hpp` (file path), `apps/openmw/mwworld/neutrallight.*` (one new pure function).
Constraints: a Morrowind bake is byte-identical to the build before; no new setting; bakes live in the user data folder and
are never committed.

## What was wrong (measured)

**Interiors: no lights.** Baking `WhiterunBanneredMare` on the build before: `[GI] baking "WhiterunBanneredMare": 3213
probes, 0 light(s)`. The baker collects lights with `cell->forEachType<ESM::Light>`; a Skyrim light is not an `ESM::Light`
reference but a `NeutralBase` placement whose base carries a `LightComponent` (the engine makes the `ESM::Light` on the spot in
`Scene::insertModel`), so the bake saw nothing and the probes held only the cell's ambient. The runtime side needed no change:
the probe file is found by the cell's name id (`Render::Gi::CellId`; a neutral interior's name id is its editor id) and the
log shows `GI: 3213 probe(s) for WhiterunBanneredMare` and `8 reflection probe(s)` on loading it.

**Exteriors: three assumptions of 8192 unit Morrowind cells.**
1. The baker moved the player with `ESM::RefId::esm3ExteriorCell(x, y)`: Morrowind's world, not a game pack's.
2. The runtime told the sky field about loaded game cells `(x, y)` and read the file of the SAME `(x, y)`; the field is made
   of 8192 unit cells in absolute coordinates (the shader finds a column by `floor(position / spacing)`), but Skyrim's game cells
   are 4096 units, so game cell (7, 1) would have loaded the file of field cell (7, 1), 28,000 units away from where it stands.
3. File names had no world, and a world has its own coordinates (WhiterunWorld is not Tamriel).
Also: the ray from above the land started at 20,000 units (Skyrim's mountains are higher).

## What was built

- **Interior bake:** `MWWorld::neutralGiLight(LightComponent, fadeGain)` (`neutrallight.*`) returns what the bake needs of a
  neutral light, taken from the engine's own `makeNeutralEsmLight` record (so the pack's 0.6 radius mapping of task 0355 b and
  the fade gain of the same task are in): colour bytes, gain, radius, negative flag; nothing for a light that is off by default.
  `ProbeBaker::begin` visits `forEachNeutralRef` after the `ESM::Light` references (enabled, not deleted), the same sort by
  reference number. Bannered Mare: 18 lights baked = the 18 the scene creates (`NLIGHTOBJ`); Dragonsreach 28.
- **Exterior field cells of a world:** `Render::Gi::skyFieldCellOf(x, y, cellSize)` maps a game cell to its 8192 unit field
  cell (floor division; 8192 gives the cell back), `skyVisFilePath(x, y, world)` puts a world's files in
  `openeden/probes/exterior/<world>/<x>_<y>.skyvis.json` (the world's text, lower-cased, every other character `_`; an empty
  world is the old path). `RenderManager::exteriorCellLoaded/Unloaded` take the cell size and the world key (defaults are
  Morrowind's), count the game cells inside each field cell (up to four in Skyrim) and read the world's files; the sky maps are
  keyed `(world, x, y)` (the same order as before for Morrowind). `Scene` passes `ESM::getCellSize(worldspace)` and the
  worldspace text for neutral exteriors only.
- **Exterior baker in a neutral world:** when the player starts in a neutral exterior (`--start @WhiterunWorld,x,y`), the cells
  named on the command line are FIELD cells (8192 units: "3,-1" or "2,-2:4,0"), the baker moves the player to the land under
  the centre of each (`neutralLandHeight` + 20, into the game cell that holds it, a 64 unit nudge so rounding cannot leave him in
  the neighbour), waits for that game cell and bakes with rays that start 70,000 units up. Morrowind: unchanged code path
  (`changeToCell(esm3ExteriorCell)`, 20,000).
- Tools: `tools/skygi_check/check.sh` (bakes and views in a Skyrim session: `interior`, `exterior`, `view`, `spots`),
  `tools/skygi_check/morrowind_check.sh` (the Morrowind comparison), `tools/skygi_check/luatest`.

## Tests (new files only; no existing test edited)
`apps/components_tests/render/testskyvisworld.cpp` (8: field cell mapping incl. negatives and nonsense sizes, the world's path,
no way out of the folder) and `apps/openmw_tests/mwworld/testneutralgilight.cpp` (5). **Red first against stubs of the two pure
functions: 4 of 8 and 3 of 5 failed (the others test absence and the old path); green after: 8 of 8 and 5 of 5; the `SkyVis*`,
`Probe*` and `NeutralLight*` neighbours (90 tests) pass.** The engine wiring (the baker, the render manager counts, the scene
calls) has no unit test: it was run in the game (below).

## Evidence (RTX 4070, 1280 x 720, `hdr`, `gbuffer`, `pbr`, `atmosphere`, hour 12; files in `I:\Projects\OpenEden-work\bow6\gi58`, frames in `keep/`)
- **Bannered Mare:** 3,213 probes, 18 lights, bake 1.5 s plus 0.25 s for 8 reflection probes, files 1.26 MB and 0.31 MB. Before
  (bake with no lights) the mean probe DC term is 0.121, now 0.264 (max 3.98); 773 of 3,213 probes changed. Frame:
  `keep/mare_off_before_after.png` (gi off, gi with the old bake, gi with the new one). **The frames are dominated by the
  lights' random flicker: two runs of the same setting differ by 93 to 121 mean luminance, so a mean-brightness comparison is
  not usable and nothing is claimed from one.**
- **Dragonsreach:** 7,350 probes (the spacing is capped at 241), 28 lights, bake 1.5 s, 2.9 MB; `keep/dragonsreach_off_on.png`.
- **Whiterun exterior:** 9 field cells (2,-2 to 4,0) baked in 50 to 106 s (1.6 to 3.5 s a cell, 8,712 probes, 256 rays; the
  world is `skyrimse:w107119`, game cells 4096, field 8192), 3.9 MB; at run time `GI: sky-visibility field for 9 exterior
  cell(s)` and the frames (`keep/whiterun_off_on.png`, left off, right on) show the ambient shaded by the world (darker under
  the eaves and on the street, less flat); three spots, 33 to 48 percent of pixels differ by more than 8 levels (the spots are
  rough: ground close-ups, not a considered view).
- A first bake of field cell (3, 0) put the player exactly on a game cell border and he ended in the neighbour (`did not load`);
  fixed by the nudge, the message now says where the player was.

## Morrowind unchanged
Against `I:\Projects\OpenEden-work
6savesuild` (`tools/skygi_check/morrowind_check.sh`): the bake of `Seyda Neen,
Arrille's Tradehouse` (16 lights, 5,733 probes; `.json` and `.reflections.json`) and of the exterior cells -2,-9 to -1,-8 (4
files) is byte-identical (SHA-256 prefixes equal for all 6 files); the Balmora fixed frame (`--start Balmora --random-seed 7`,
`OPENEDEN_FIXED_FRAME_TIME`, frame 600, empty settings.cfg, `--no-sound`) is `1c5d47ad0b5dc32d` and with `hdr = true`
`b451a8a6ca771e79` for both builds; `tools/worldobjects_check` 110 objects OK.

## Not done / not checked
- The Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review.
- A 4096-cell shift of the field is not needed (the field cell is a whole number of game cells in Skyrim); a world with game
  cells that are not a divisor of 8192 would still map correctly but a field cell would then straddle game cells unevenly.
- The Skyrim bake loads the game cells round the player only (5 by 5 of 4096 = 20,480 units across, so a field cell is covered
  but its sky rays see only that much geometry; Morrowind's 5 by 5 of 8192 sees more). Distant mountains do not shade it.
- A Skyrim exterior has no unit test of the world-keyed render manager (`exteriorCellLoaded`): the numbers above come from the game.
- Interior lights without a base `LightComponent` of their own (model-borne light nodes) are not baked, as in Morrowind (the
  baker puts the light at the reference, not at the light node in the model).
- Flicker is not part of the bake (the light is baked at its full colour), as in Morrowind.
- The Whiterun frames are not a considered comparison; no frame from the real game exists to judge "right".

## Decisions for toddhow
1. Bakes are not shipped: a user must run `--bake-gi` per interior and `--bake-gi-exterior` per block (Whiterun interiors: about
   2 s each; an exterior field cell 2 to 4 s). A prepare script for Skyrim in `tools/testprofile` could bake a standard set.
2. The world key in the file name is the engine's worldspace text (`skyrimse:w107119`), not the editor id (`WhiterunWorld`).
