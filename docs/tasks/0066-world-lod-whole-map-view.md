# Task: World LOD, the whole map from altitude (no visible edge)
Design doc section: `docs/design/world-lod-and-streaming.md` sections 3 (the OpenEden world-LOD model), 5 (the streaming core), 7 (hiding the transitions), 8 (graphics options and fallbacks); research note `docs/research/0029-whole-map-view.md` (the numbers and the open facts below come from it).
Goal: from a high enough vantage the whole map is in frame, and from the ground the land ends in haze, never at a line: far land is drawn as basic tiles and clusters out to the map's edge, with a fog model that does not depend on the far plane.
Status: stages a to e built and committed on 2026-11-03 (`f336b57244` to `62d3525ddd`, with the later fix `7ebbc09e09`); stage f, the close-out, on 2026-11-04: the acceptance table and the checklist of toddhow's own flight are below the acceptance criteria. Waiting for toddhow's flight and his open decisions. 2026-11-04 (later): toddhow chose option (b) of decision 1 and kept `map view` off by default; built and measured in task 0060's stage log ("Far cluster eviction on the low tiers"); the weak-GPU run is done (criterion 9). Drafted 2026-11-02 from toddhow's request (the RDR2 goal). Full loop (a renderer change, the projection and the fog every material uses, new baked data levels, a settings default). Every fact is cited from the code at the current commit or marked **verify**.
Builds on: `docs/tasks/0055-lod-s1-tile-model.md` (terrain tiles, layout version 1, frozen), `docs/tasks/0059-lod-s4-object-clusters.md` (clusters, layout version 0, draft), `docs/tasks/0060-lod-s6-streaming-core.md` (budgets and eviction), `docs/tasks/0061-lod-s8-background-baker.md` (the baker that will make the extra levels), and commit `ed53100fd3` (`[WorldLod] view distance`).

Inherited code touched (at the commit that starts each stage; writing this spec changes nothing): `apps/openmw/mwrender/rendermanager.cpp` (`effectiveViewDistance`, `updateProjectionMatrix`, `processChangedSettings`, line numbers 144, 2682, 2700 at `ed53100fd3`), `apps/openmw/mwrender/fogmanager.{hpp,cpp}` (`FogManager::configure`, `getFogStart`, `getFogEnd`), `components/lod/tileselection.*` (`pixelError`, `distanceToTile`, the view-distance cut), `components/lod/tilekey.*` (levels above 3 need no change), `apps/openmw/lodterrainbaker.*` and `lodobjectbaker.*` (the level lists; the object baker's `levelErrors` {128, 320, 960} and `mManifest.mLevels = {1, 2}`), `components/lod/clusterbuild.*` (classification thresholds), `apps/openmw/mwrender/lodterrain.*`, `lodclusters.*`, `components/render/depth.hpp`, `attachmentformat.hpp`, the fog function in the shader library (**verify** its file under `files/shaders/lib/`), `components/render/atmospheremath.hpp` (the horizon colour), the sky and cloud dome radius (**verify**), `components/render/simplewater.*` (far sea edge), `files/settings-default.cfg` `[WorldLod]`.

Inputs / outputs: new `[WorldLod]` settings, all off or inert by default: `map view` (bool, false), `far levels` (the highest tile level drawn, 3), `view distance` accepts `auto`; a fog/aerial-perspective formula with a C++ twin; terrain tile levels 4 to 6 and cluster levels 3 to 5 in the baker (manifest `levels` extended; layout version of terrain stays 1, clusters stay the draft 0 until toddhow approves); a debug readout of the selected tiles per level; no change to saves.

## What the code says today (research note 0029, with the derivations)

- **The map:** Morrowind + Tribunal + Bloodmoon land cells span x -28 to 24 and y -20 to 30 (level 1 tile bounds from the baked pack's keys), 1540 land cells in a 52 x 50 cell box of 8,192 units per cell: **425,984 x 409,600 units, diagonal about 591,000**. The farthest point from any camera in the box is at most that. Today's `[WorldLod] view distance` is 40,000 (7% of it).
- **Where the edge comes from:** `Lod::pixelError` accepts a tile when its geometric error at its distance is at most the threshold in pixels, but `evaluateTile` (`components/lod/tileselection.cpp`) discards any tile with `distance > camera.mViewDistance`, `updateProjectionMatrix` sets the far plane to `mViewDistance`, and `FogManager::configure` makes the linear land fog end at `viewDistance` (start at `viewDistance * (1 - fogDepth)`). Three limits, all equal to one number. Raising the number raises all three, which is what commit `ed53100fd3` does; none of them is a design property of the LOD.
- **Levels baked:** terrain tile levels 1 to 3 (571 tiles, 46 MB), clusters levels 1 and 2 (458 clusters, 157 MB, level 3 skipped because it only starts at about 75,000 units). The tile key allows levels up to 15 (`Lod::maxLevel`); a level L tile is 2^L cells on a side (65,536 units at 3, 262,144 at 5, 524,288 at 6).
- **Depth:** `D32_SFLOAT` and a reversed-depth convention is marked in `depth.hpp` and the shadow samplers. **Verify** that the main camera's projection and depth compare are reversed: the projection is a plain `vsg::Perspective` (`rendermanager.cpp` line 632). With near = 1 (`[Camera] near clip` default) and far 590,000 a non-reversed float depth would resolve about 600 units at 100,000 units; a reversed one under 1 unit.
- **Cost today (task 0054/0059, 1280 x 720, RTX 4070):** the whole far field at a 30,000 view is 188 draws and 123k triangles with clusters; the near live objects are the expense (28.2k draws, 2.11M triangles, 3.53 ms CPU in Balmora). The far field is not what a bigger view costs.

## Design, in plain language

1. **Take the far plane, the fog end and the tile range apart.** Three numbers instead of one: the *tile range* (how far tiles are chosen: the map's far corner, or `view distance`), the *far plane* (set from the tile range plus the sea/sky needs, with reversed depth required), and the *fog* (an exponential model with its own scale, not tied to either). With `map view` off the three stay the old single number and the game is unchanged.
2. **More levels, same code.** Add terrain tile levels 4 to 6 and cluster levels 3 to 5 to the baker; the selection already picks the coarsest level whose error is under the threshold at that distance (the distance at which a level is accepted is about 77.9 x its error at 720p, 117 x at 1080p, so a level reaches the far corner when its error is at least about 5,000 to 7,600 units). Terrain levels 4 to 5 (and 6 only if the 4 px setting needs it) cover the map in about 10 + 4 + 1 tiles; cluster levels 3, 4, 5 use errors from the same 2.5 to 3 times growth (about 960, 2,900, 8,700, **starting values to be calibrated**), with the "too small" classifier excluding everything that no pixel at that distance could show.
3. **Aerial perspective instead of the linear fog end.** Transmittance `exp(-beta * integral(exp(-h/H)) ds)` along the ray (a closed form for a straight line), blended toward the atmosphere's horizon colour (`atmospheremath.hpp`), with the weather's fog density raising beta so a storm still shortens the view from the ground. Starting values: H 2,000 to 8,000 units, beta so that T = 0.1 at 590,000 units at ground level; **tune by eye in stage c**. One shared function (GLSL include for the material shaders, C++ twin with tests as `volumemath.hpp` has) used by the default, terrain, cluster and water materials, so the sea and the land end in the same haze. With `[Fog] use distant fog` the old behaviour stays selectable.
4. **Distance means basic.** Tiles and clusters above `near levels` skip every near-field effect: no shadow sampling past the last cascade, no GI/AO/wetness/cover/SSS paths (**verify** the LOD materials already omit them), plain ambient from the atmosphere, no parallax, no normal-map-heavy paths. The cluster material stays the default pipeline variant with `LOD_FADE`.
5. **`view distance = auto`.** The tile range becomes the distance from the camera to the farthest corner of the baked pack's bounds (read from the manifest), capped by a `max view distance` setting for low-end hardware. The default stays 40000 until toddhow approves `auto`.
6. **Memory and streaming.** The whole-map view needs only the coarse levels resident; the streaming core (task 0060) keeps the near levels bounded. Pin the top levels (design doc section 5). A weak GPU lowers `far levels` or raises `terrain tile threshold`; section "Low-end" below.

## Stages (each its own commit; every stage with its frames viewed before it is committed)

- **a. Facts and the budget (no behaviour change).** Verify the depth convention (read the main pass's pipeline depth compare and the projection) and the sky/cloud dome radius; read tile geometric errors for levels 2 and 3 from the manifest; bake terrain levels 4 to 6 and cluster levels 3 to 5 into a test pack; run a flight to 25,000 units with the tile range forced to 600,000 and `[Rendering] frame counters` on; record draws, triangles, CPU median and p99, RSS and device MiB (the table in note 0029 section 6 with real numbers). **Go/no-go:** the far field costs at most 1.5 ms CPU and 150 MiB resident more than the 30,000 view, or the numbers are used to set the level count.
- **b. The three limits.** `map view`, `view distance = auto`, the tile range separate from the far plane, reversed depth confirmed or fixed; with the setting off the frame is identical (0 pixels differ, as for earlier stages).
- **c. Aerial perspective.** The shared fog function, its C++ twin and tests (monotonic in distance, T in [0, 1], T = 1 at zero distance, ray-height integral against numerical integration within 1%, weather raises beta), applied to terrain, objects, clusters, water and sky; frames at the six poses of the verification plan.
- **d. The baker levels** (with task 0061's baker): terrain levels 4 to 6, cluster levels 3 to 5 with calibrated errors, the manifest's `levels`, the staleness digest includes the level list.
- **e. Calibration and fallbacks.** Threshold per hardware tier, `far levels`, `max view distance`, the weak-GPU run (llvmpipe, note 0025) for correctness only.
- **f. Close-out.** Docs, the changelog row, the checklist of toddhow's own flight.

## Constraints

- **Off means unchanged.** `map view = false` and `view distance` numeric: no new code runs, no new file is opened, the frame at the benchmark pose is identical to the commit before (0.000 percent of pixels differ, the comparison used in tasks 0055/0059). With `enabled = false` for the whole world LOD, the same.
- **Determinism.** The baker's output for levels 4 to 6 is byte-identical across runs and across thread counts (the existing tile/cluster pack tests, extended).
- **Threads and allocation.** Selection and the fog uniforms are per-frame on the main thread: no allocation (reuse the tile state vectors, as `LodTerrain` does now); tile loads stay on the worker (`LodTerrain`'s loader).
- **No magic numbers.** H, beta, the level errors, the thresholds are settings or named constants with their source (this note).
- **Shaders.** The fog function is a GLSL include edit (existing materials stay GLSL, decision 0009); any new full-screen pass is Slang.
- **Licences and assets.** Baked packs of Morrowind are never committed (guardrail); no new dependency.

## Acceptance criteria

- [x] The depth buffer is reversed for the main pass (read from the pipeline state, not assumed), or the far plane is documented as limited with the alternative chosen by toddhow.
- [ ] A frame at 25,000 units up over Vivec shows the whole island, Solstheim and the coast with **no visible edge, hole or fog wall** (toddhow's verdict on the frames).
- [ ] From the ground (Seyda Neen, Balmora) the horizon ends in haze at every weather; storms and ash still shorten the view.
- [ ] No crack or pop at a level boundary during a fast ascent from the ground to 25,000 units (a scripted flight), beyond the existing hysteresis behaviour.
- [ ] Cost: draws, triangles, CPU median/p99, RSS and device memory at the 25,000 pose recorded in the stage log; the far field adds at most 1.5 ms CPU and 150 MiB resident over the 30,000-unit view, or the stage-a numbers say what the budget becomes.
- [ ] The near field at the benchmark pose is unchanged with `map view` on (the difference image is below the tolerance the earlier LOD tasks used).
- [ ] With `map view = false` the frame is identical to the commit before, and the 32 Lua integration tests pass.
- [ ] Unit tests: the fog function and its twin, the tile selection at distances to 600,000 (levels chosen are monotonic with distance, hysteresis holds), tile keys of levels 4 to 6, the baker's determinism for the new levels.
- [ ] The weak-GPU run (software device, `far levels = 3`, `texture max size = 512`) starts and draws a frame at the 25,000 pose.

### Where each criterion stands (stage f, 2026-11-04)

| # | Criterion | Verdict | Evidence, and what would settle it |
| --- | --- | --- | --- |
| 1 | Depth reversed for the main pass | **Met** | Stage a (`f336b57244`): read from the pipeline state and VSG's sources (compare GREATER, depth clear 0, `vsg::perspective` from 1 to 0, the shadow and occluder samplers, the editor's projection); nothing needed converting. `Render::DepthMath`, 12 tests; the shaders' reconstruction is off by 0.0625 units at 590,000. |
| 2 | From 25,000 units over Vivec: the whole island, Solstheim and the coast, no edge, hole or fog wall | **Not verified: needs toddhow's verdict** | Frames: `I:\Projects\OpenEden-work\mapview3\frames\on_viv_25000.png`, `on_bal_25000.png`, `on_viv_60000.png`; the climb sheets `I:\Projects\OpenEden-work\mapview4\ascent_on_sheet.png` and `ascent_tmin_sheet.png`; after task 0060's planner `I:\Projects\OpenEden-work\streaming4\run_high\frames\screenshot011.png`. The agents saw no hole, crack or seam in them. The 25,000 frames look down at 35 degrees and show only part of the island; Solstheim is in the 60,000 frame and in the top frames of the climb. Settled by your flight (below). |
| 3 | From the ground the horizon ends in haze in every weather; storms and ash shorten the view | **Met with a relaxation** | Stage c (`14527ba896`), frames in `I:\Projects\OpenEden-work\hazefog`: Seyda Neen at noon in rain, thunderstorm, ash storm and blizzard (`G_rain`, `G_storm`, `G_ash`, `G_blizzard`); clear weather from 3,000 and 25,000 units over Balmora. Not seen: Balmora from the ground, cloudy, overcast, foggy, blight and snow, dawn and dusk, a weather change while it happens. Settled by flight steps 3, 6 and 7. |
| 4 | No crack or pop at a level boundary during a fast ascent | **Met with a relaxation** | Stage e (`62d3525ddd`): `tools/mapview_check/run_ascent.sh` climbs from 2,257 to 27,257 units in 40 s and back; high tier: no frame over 16.7 ms (worst 11.3 ms); the screenshots taken every 5 s show no crack, hole or seam. After the planner (task 0060, third review): two high-tier climbs and one with a 24 MiB budget, frames viewed, no hole. The relaxation: one frame every 5 s cannot show the pop of a single cluster or the dither of a hand-over, and nobody has watched a climb as it happens. Settled by flight step 5. |
| 5 | Cost recorded; the far field adds at most 1.5 ms CPU and 150 MiB resident over the 30,000 view | **CPU met; memory not met with object clusters** | Tables of stages d and e. Median frame time +0.0 to 0.1 ms (Balmora and Vivec at 25,000). Resident memory: terrain tiles alone +93 and +126 MiB (met); with object clusters +197 MiB (minimal tier) to +452 MiB (high tier, Vivec). Task 0060's planner did not change the high tier, where no budget is reached (climb peaks 1,442 to 1,457 MiB resident against 1,450 in stage e). Stage e's "map view off" baselines ran with the fault fixed later in `7ebbc09e09` (no terrain tile drawn without `map view`), so those differences are somewhat too large; they were not measured again. Open decision 1. **Update 2026-11-04 (task 0060, option b):** on the minimal and low tiers the far clusters are now bounded by the budget when the ground beneath them is drawn: with `far memory budget = 24` the clusters held at the top of the climb went from 149 to 183 MiB to 19 to 27 MiB (counted), the process from about 1,300 / 1,376 to about 1,130 / 1,138 MiB resident and the device by 65 to 100 MiB, at the price of +12 percent draws and about +0.5 to +1.0 ms median frame time (live objects draw the cells the clusters leave). At the tier default budgets (256 / 320 MiB counted) the rule never fires on this climb, so the default numbers above stand until the tier budgets are lowered (toddhow's call). Medium and high are unchanged. |
| 6 | The near field unchanged with `map view` on | **Not verified** | By construction the live range (live terrain, objects, object paging) keeps the old number with `map view` on (stage b, `Lod::computeViewLimits`); the haze replaces the linear fog in exteriors, so near pixels can differ slightly. No difference image was taken. Settled by a difference image at the benchmark pose with `map view` on and off, or by your look at Balmora's streets (flight step 3). |
| 7 | With `map view = false` the frame is identical to the commit before; the 32 Lua tests pass | **Met with a relaxation, after a fix** | Frames: the pairs of stages b and c are within the run-to-run noise of one executable (their tables); only `enabled = false` is byte-identical, because the game animates on real time. Lua: the pre-push Linux gate log for `1c1db899a5` (2026-11-04) shows the unit test suites (5,387, 868 and 1,861 tests), the 32 Lua integration tests and `eden-studio-tests` (589) passing; this stage did not run it again. **A regression got through:** stage e (`62d3525ddd`) made `[WorldLod] enabled` without `map view` draw no terrain tile at all ("0 tiles of levels 1 to 6 loaded"), and no "off" comparison caught it (stage e's off runs measured memory, not frames against the commit before). Task 0060's third review found it; `7ebbc09e09` fixes it (4 lines in `lodterrain.cpp`, no test; task 0055's tiles are drawn again). The off frames were not compared again after the fix. Open decision 4. |
| 8 | Unit tests: the fog function and its twin, selection out to 600,000, tile keys of levels 4 to 6, the baker's determinism | **Met with a relaxation** | `testaerialmath.cpp` (14), `testviewlimits.cpp` (29, and stage d's six-level selection test), `LodTileBakeLevels` (3), `ESMTerrainFarLod` (2), the tier tests of stage e (38). The determinism of the new levels is a manual check, not a unit test: two bakes have the same SHA-256 (stage d, and again in stage f below); not compared across thread counts. |
| 9 | The weak-GPU run (software device, `far levels = 3`, `texture max size = 512`) starts and draws a frame at 25,000 | **Met (starts and draws; speed not shown), 2026-11-04** | Task 0060's stage log, "The weak-GPU run": Linux build of `1eafaa6918` under Docker with Mesa llvmpipe (GCC `-Werror`, 576 `Lod*` tests pass); `tier = auto` picked **minimal** for the software device; the game starts and draws the 25,000 pose with 571 tiles and 489 clusters (frames `I:\Projects\OpenEden-work\lodtier\linux\shot_default_budget.png` and `linux\out\user\screenshots`), peak resident 1,495 MiB (default budget) and 1,308 MiB (`far memory budget = 24`, the far-eviction rule firing). **Finding: the terrain pack baked on Windows FAILS the staleness check on Linux** (no tile is used and the far land is missing in the frame); a pack baked on Linux (33 s in a 160 x 90 window; at 640 x 360 the bake did not finish in 25 minutes) has the same size and byte-identical content in all 594 entries (8,317 container bytes differ), so the baked values are portable and the staleness digest is not (cause not isolated; a bug candidate). The cluster pack has no staleness check and loaded. Not shown: speed, validation-layer cleanliness (none installed), a real weak card. |

## Your flight (toddhow's check)

Everything here uses a COPY of your config. Your own folder, `C:\Users\smith\OneDrive\Documents\My Games\OpenMW`, is only read, never written. Edit the copy's `settings.cfg` only while the game is closed: the game rewrites that file when it quits, so a change made while it runs is lost. The game reads these settings and chooses the hardware tier at start, so change them between runs, not in game.

**1. Make the copy and the bakes (about two minutes, the game closed).** In Git Bash, from the repository folder:

```bash
bash tools/mapview_check/prepare_flight.sh "C:/Users/smith/OneDrive/Documents/My Games/OpenMW" "I:/Projects/OpenEden-work/flight"
```

What `tools/mapview_check/prepare_flight.sh` does:
- It refuses a work folder outside `I:\Projects\OpenEden-work`, inside your config folder, or one that exists, is not empty and was not made by the script (it may be run again on its own folder).
- It copies `openmw.cfg` and `settings.cfg` into `<work>\config`. In the copy's `openmw.cfg` it drops `config=`, `user-data=` and `resources=` lines and adds what the build's own `openmw.cfg` would give: the build's `resources=`, `data="<build>/resources/vfs-mw"` ahead of your first `data=` line, and `extension=compat_morrowind`.
- It writes these keys once each into the copy's `settings.cfg`, in the first section of that name (a key written twice stops the next start with a parse error):

```ini
[Terrain]
distant terrain = true

[WorldLod]
enabled = true
map view = true
view distance = auto
object clusters = true
haze = true
tier = auto
```

- It bakes the terrain tiles and the object clusters into `<work>\user\openeden\lod` (`terrain.edenpak` and `objects.edenpak`, about 52 MB and 187 MB for Morrowind, Tribunal and Bloodmoon). Each bake starts the game in a window off screen (`OPENEDEN_QUIET_WINDOW=1`).
- It prints the start command and writes it to `<work>\start_game.txt`.

A third argument names another build folder (default `I:\Projects\OpenEden\build-jolt`). The executable there is whatever was built last, by any agent; it must be built at or after `7ebbc09e09`. Your saves are not in the work folder (`<work>\user` is a new user data folder); to fly with one of your characters, copy its save folder into `<work>\user\saves`.

Every command uses `--replace=config --config <copy> --user-data <work>\user`. Without `--replace=config` the build's `openmw.cfg` loads too and chains to your real config folder (`config="?userconfig?"`): the content list arrives twice ("Content file specified more than once") and your own folder is used again.

To bake again by hand (after changing mods or the content list; a stale pack is logged as `World LOD terrain tiles are not used: the pack was made from other land, land textures or settings (stale)`), with the game closed, in PowerShell:

```powershell
$env:OPENEDEN_QUIET_WINDOW = "1"
& "I:\Projects\OpenEden\build-jolt\openmw.exe" --replace=config --config "I:\Projects\OpenEden-work\flight\config" --user-data "I:\Projects\OpenEden-work\flight\user" --bake-lod-terrain
& "I:\Projects\OpenEden\build-jolt\openmw.exe" --replace=config --config "I:\Projects\OpenEden-work\flight\config" --user-data "I:\Projects\OpenEden-work\flight\user" --bake-lod-objects
```

The variable stays set in that PowerShell window: start the game from a new window, or run `Remove-Item Env:OPENEDEN_QUIET_WINDOW` first, or its window opens off screen.

**2. Start the game.** Use the printed command and add `--skip-menu --start Balmora` (a new game in Balmora; if a character window waits, accept it). For about a second after the first cell loads the far land may be missing: the pack is checked on a worker (log `World LOD terrain: the check read the land for levels up to 2`). Judge from then on. Open the console and type `tgm` (god mode) and `tcl` (no collision: you fly where you look, and you do not fall from a height).

**3. On the ground in Balmora, clear noon.** `ChangeWeather "West Gash Region" 0`, `set gamehour to 12`. Expected: the far hills and the sea fade into the sky's colour; no line where the land stops; Balmora's houses and streets look as they do without `map view`.

**4. Over Vivec.** `coe 3 -11`, then `player->setpos z 25000`; look toward the horizon in every direction, then down. Expected: the whole island to the coast, Solstheim to the north-west, the far sea paling into the haze; Vivec's cantons and the large landmarks as simplified shapes; no hole (a square of sky or black in the land), no edge, no wall of fog. Small objects far away are not drawn, on purpose (answer 4: levels 5 and 6 are terrain only).

**5. The climb.** From the ground, look up and fly up with `tcl`, or set heights in steps: `player->setpos z 3000`, `8000`, `15000`, `25000`, `60000`. Expected: no crack between pieces of land, no step where the land or the objects change detail in one jump, no hole while you climb or come down. A scripted climb, the same one the agents ran (150 s, off screen unless `OPENEDEN_QUIET_WINDOW=0` is set in front; it EMPTIES `<work>\user\screenshots` first, so move your F12 shots out before it):

```bash
bash tools/mapview_check/run_ascent.sh "I:/Projects/OpenEden-work/flight/climb" "I:/Projects/OpenEden-work/flight/config" "I:/Projects/OpenEden-work/flight/user"
```

It leaves 25 screenshots in `<work>\climb\frames`; `bash tools/mapview_check/analyze_ascent.sh "I:/Projects/OpenEden-work/flight/climb/stats.txt"` gives the frame times of the climb.

**6. Weather.** On the ground and at 3,000 units: `ChangeWeather "West Gash Region" 4` (rain), `5` (thunderstorm), `6` (ash storm), `9` (blizzard), then `0` (clear); the numbers are the order of `WeatherManager`'s `addWeather` calls in `apps/openmw/mwworld/weather.cpp`. The change takes a few seconds. Expected: rain is a mild haze, the storm hides the far hills, the ash storm hides all but what is near, the blizzard hides nearly everything.

**7. Dawn and dusk** (nobody has checked these): `set gamehour to 6` and `set gamehour to 19`, from the ground and from 25,000 units. Look at the colour of the haze against the far land.

**Not wrong:** the far land missing for the first second (step 2); far land without small objects. **Should be gone:** white and blue noise on far water (fixed in task 0068, `710df9f2bf`); white strips on far ridges are the Ghostgate fence (the same task). Report either if you see it.

**If something looks wrong, send:**
- the F12 screenshot (`<work>\user\screenshots`);
- where and when: the cell (`coe x y`) or `player->getpos z`, the weather number and the hour;
- the copy's `settings.cfg` (`<work>\config\settings.cfg`);
- from `<work>\config\openmw.log`: the lines that begin with `World LOD tier:`, `World LOD terrain tiles:`, `World LOD terrain: the check read the land`, `LOD clusters:` (the first one names the number of clusters, the later ones what is shown and held), any line with `not used`, `over the budget`, `cannot` or `could not`, and any `Warning` or `Error` near the time it happened.

**Your decisions after the flight** (recommendation first):
1. **The memory of the whole-map view with object clusters** (criterion 5, and task 0060's finding 11: in `map view` a budget cannot bound the clusters past the live range, because nothing else draws their cells). (a) Keep the rule: no holes, the overrun is reported; the measured cost is +200 to +450 MiB with clusters (recommended for now; superseded 2026-11-04: toddhow chose (b), built in task 0060). (b) On the low tiers let far clusters be freed: memory bounded, far objects vanish and pop in and out. (c) A smaller, cheaper far cluster level for the low tiers: a bake change. Neither (b) nor (c) is built.
2. **The default of `map view`** (today false, with `view distance = 40000`): keep it off until the flight and decision 1 (recommended); then on for everyone with the tier capping the reach, or a graphics option (Low, Medium, High, Whole map).
3. **The haze's starting values:** `haze visibility = 100000` (where 10 percent of the light gets through along the ground in clear weather; larger is clearer), `haze height = 4000` (the scale height; smaller keeps the haze nearer the ground), `haze weather gain = 10` (how much rain, storms and ash shorten the view). Change them in the copy with the game closed and say which you like.
4. **`7ebbc09e09`**: with `[WorldLod] enabled` and `map view` off, the terrain tiles of task 0055 are drawn again. Task 0060's third review asked for your word because it changes what `enabled` draws without `map view`; the fix is committed and pushed, without a test. Approve it (a test to follow), or say what `enabled` without `map view` should draw.

## Low-end hardware

`far levels` (3 means today's reach, 6 the whole map), `max view distance`, `terrain tile threshold` raised (16 or 32), `texture max size` capping the 256 px albedo, the aerial perspective stays on (a few ALU per fragment). A weak GPU lowers the level count first, then raises the threshold; no setting changes the near field. The software-device run proves it starts and the draws are valid; it says nothing about speed.

## Risks

- **Depth precision (section 4 of the note):** if the main pass is not reversed the far plane cannot exceed about 60,000 units without a visible near-clip change; the alternative is rendering the far field in a second depth range (two passes, a larger change). Stage a decides.
- **Sky and cloud dome clipped by the far plane or too small.** Stage a verifies; the fix is the dome radius from the far plane or infinite clip for the sky.
- **Far clusters at errors of 3,000 to 9,000 may be mostly empty** (the classifier removes everything below a few pixels), so the far field may be terrain only plus a few landmarks. That is acceptable ("basic"); an impostor or heightfield landmark layer is a later task (design doc S10).
- **Aerial perspective double-counting with the froxel fog** (`volumetrics`): the two must split the ray at the volume's far depth (**verify** in `volumegpu.hpp`).
- **Aspect of the map:** Solstheim shares the worldspace; the box includes empty sea. The tile range `auto` uses the pack's bounds, so it does not shrink for a smaller game; mods that extend the land (Tamriel Rebuilt) need the pack's bounds, not constants (they come from the baked manifest).
- **A very tall view and the active grid hand-off:** the near objects/live terrain are unchanged; a high camera sees the live chunk boundary as a visible seam unless the tile hand-off (task 0055/0059 fades) holds at distance (**verify** from altitude in stage a frames).

## Open questions for toddhow

1. Is the target the **whole Morrowind island from a height of about 25,000 units** (a Levitate/teleport flight), or also a camera in the sky much higher than the game allows? It sets the number of levels (4 to 5 vs 6).
2. **Default `view distance = auto` for everyone** once measured, or an explicit graphics option (Low, Medium, High, Whole map)?
3. **How much haze from the ground?** RDR2 hides the far field at about a few km on a clear day; Morrowind's own look is thick fog. Starting values give T = 0.1 at the far corner; an alternative is T = 0.3 at 100,000 units. Your taste decides H and beta.
4. **Are far objects needed at all** beyond a few landmark clusters (Vivec towers, Ghostgate, Red Mountain's rocks), or is "terrain with its albedo plus landmarks" the intended far look?
5. **Approve cluster layout version 0** as version 1 (the object clusters are still a draft; levels 3 to 5 are baked into a layout that might still change).
6. **Reversed depth:** if the main pass turns out not to be reversed, accept the larger change (it touches every pipeline's depth compare) or the split-range far field?

Out of scope: real-time terrain tessellation, ray-marched terrain, impostors (S10), grass at distance (S7), any change to the live cell grid or loading, Lua, saves, shadows past the existing cascades, night sky changes, sea rendering beyond using the shared fog, other games' worldspaces.

## Answers of toddhow, 2026-11-03

1. The reach is not a fixed distance. It follows the loaded game's world: the extent of the loaded worldspace (land cells and their bounds from the content files) decides how far the view can go, so a large future world (Project Tamriel, once it is released) fits without a settings change. The spec's fixed altitude target (open question 1) is dropped; the verification poses are taken relative to the loaded world's extent instead.
2. `view distance = auto` is accepted as the way to say this. Auto means: the farthest point of the loaded worldspace's land from the camera, capped by what the hardware budget allows (stage a measures the cost). A number still overrides it.
3. The haze follows Red Dead Redemption 2's aerial perspective: distance and altitude dependent, tinted toward the sky and sun colour, thick near the ground and thin at altitude (details tuned by eye in stage d; the starting values in the spec are guesses).
4. The goal is human vision at far distances: beyond the distance where an eye can tell detail apart, grass, single trees and small objects are not drawn as objects; they blend into the ground colour (baked into the far terrain's albedo and haze), so far land is basic terrain plus a small set of large landmarks. This answers open question 4 (far objects beyond landmarks are not needed) and shapes open question 7: the baker's coarse levels keep only objects whose projected size would still be visible, and everything else is represented by the terrain picture.

Still open: approval of cluster layout v0 as version 1 before baking more levels (5), whether the depth buffer is reversed (6, checked in stage a), and the exact budget cap for auto (the hardware tiers).

5. Approved by toddhow (2026-11-03): cluster layout version 0 becomes the frozen layout. Freezing it needs a small task like tile layout S1 (header version 0 to 1, version 0 packs refused, a re-bake byte-identical, the pinned layout tests updated in one reviewed change) before the coarser levels are baked.
6. Asked what "reversed depth" means; explained in chat. Stage a checks the code and decides.

6 (decision). toddhow chose the reversed depth setup (near = 1, far = 0, float depth), 2026-11-03. Stage a first checks what the main camera, the shadow cascades, the G-buffer, SSAO/GI/fog/cloud passes and the editor viewport use today; if any is not reversed it is converted in one reviewed change (every pipeline's depth compare and clear value, every shader that reads depth, projection matrices, the linear-depth reconstruction), with the full loop and an off-means-unchanged image comparison (rendered frames identical within noise before the far plane grows).

## Stage log

### Stage a, 2026-11-03: facts and the budget (no behaviour change; the commit that adds `components/render/depthmath.hpp`)

**Depth is already reversed everywhere; nothing was converted.** Read from the code (VSG sources are those fetched into `build-jolt/extern/fetched/vsg`, v1.1.16):

| Site | What it does | Reversed? |
| --- | --- | --- |
| Main camera projection, `RenderManager::updateProjectionMatrix` (`rendermanager.cpp`, `mProjectionMatrix` is a `vsg::Perspective`) | `vsg::perspective` in `vsg/maths/transform.h`: "Reverse depth perspective matrix, convention 1 to 0": depth `n (f - d) / ((f - n) d)`, 1 at near, 0 at far | yes |
| Pipelines' depth compare | `Pipeline::Graphics` and `Pipeline::terrain` build a `vsg::DepthStencilState` and set only `depthTestEnable`/`depthWriteEnable` (`components/pipeline/graphics.cpp:452`, `terrain.cpp:142`); VSG's `depthCompareOp` default is `VK_COMPARE_OP_GREATER` (`vsg/state/DepthStencilState.h:30`). No code in `components` or `apps` sets another compare op | yes (GREATER) |
| Main and HDR render graphs' depth clear | `setClearValues(color)` keeps VSG's default depth clear `{0.0f, 0}` (`vsg/app/RenderGraph.h:66`; `rendermanager.cpp:813, 1984`; the HDR graph copies it, `hdrtarget.cpp`) | yes (0 = far) |
| Shadow cascades | `components/view/shadow.cpp:144-169`: sampler `compareOp = GREATER`, border colour opaque black ("reversed"), graph clear `Render::farDepth` (0); `components/render/cascade.hpp:52-54` frustum corners use `nearDepth = 1`, `farDepth = 0` (`components/render/depth.hpp`); GLSL `lib/view/shadow.glsl:62` `zBlocker > zReceiver` | yes |
| Precipitation occluder | `precipitationoccluder.cpp:37, 49`: clear `farDepth`, black border (reversed); `occlusionbox.hpp:24` says "reversed depth" | yes |
| Dummy depth images | `components/view/descriptors.cpp:56, 73`: filled with `farDepth` | yes |
| Sun disc | `sky/sun/main.vert:15` `gl_Position.z = 0.001` (farDepth + epsilon), same in the Slang ports | yes |
| Water volume | `water/main.vert:24-31` uses `nearDepth`/`farDepth` for the frustum corners | yes |
| G-buffer, AO, velocity, SSS, tonemap | store and read the LINEAR depth (`-viewPos.z`, `main.frag:554`, `terrain/main.frag:373`), a colour output, not the depth buffer: no convention at all | n/a |
| Reconstruction from the depth buffer | `lib/view/screencoord.glsl` (`screenCoordToViewPos`, used by the water's reflection/refraction and soft particles in `default/main.frag:542`) and `lib/view/depthbuffer.glsl` (`depthBufferValueToViewZ`, the screen-space ray tracer) with the float inverse projection: the numbers below | yes, accurate (table below) |
| Fullscreen passes (post-process, AO, bloom, tonemap, velocity, MyGUI) | `depthTestEnable = false` | n/a |
| `fx` post-processing, `components/fx/vulkanpass.cpp:19` | `OMW_REVERSE_Z 1` | yes |
| Editor viewport | `apps/opencs/view/render/scenehost.cpp:253, 445` use `vsg::Perspective`; `picking.hpp:60-64` checks `projection(2, 2) > 0` for reversed | yes |
| Volume fog and clouds (Slang) | build rays from `invProjection` and the froxel slices from distance, not from depth-buffer values | n/a |

So toddhow's choice (reversed, float D32) is what the renderer has had since the vsgopenmw import; stage a made no change to any render file and the frames before and after are the same program (0.000 percent difference by construction). Not checked by reading: the depth of the MyGUI and the map/preview views (they do not test depth).

**Precision** (`Render::DepthMath`, `components/render/depthmath.hpp`, 12 tests in `apps/components_tests/render/testdepthmath.cpp` (11 when this log was first written, plus one that only printed the table: the review of 2026-11-03 found that printing pinned nothing, so that test now asserts the table's values and the reconstruction tolerance is 0.1 unit; the first version allowed 300 units at 300,000); near 1, far 600,000; the smallest change of distance that changes the stored float depth; the last column is the error of the shaders' reconstruction with the float inverse projection):

| Distance | Reversed (now) | Conventional 0..1 (for comparison) | Float inverse reconstruction error |
| --- | --- | --- | --- |
| 100 | 0.00001 | 0.001 | 0.0000 |
| 7,168 | 0.00075 | 3.06 | 0.0005 |
| 40,000 | 0.0029 | 95.8 | 0.0000 |
| 100,000 | 0.0091 | 598 | 0.0000 |
| 300,000 | 0.0102 | 5,455 | 0.031 |
| 590,000 | 0.0006 | 46,221 | 0.0625 |

All in world units. A far plane of 600,000 is fine for depth: no second depth range, no infinite projection needed. The near clip (default 1) could rise to 10 with the same result.

**Cost of the whole-map view with the baked levels** (Balmora start cell, camera at (-20480, -12288, 25000) looking down at 35 degrees, 1280 x 720, RTX 4070, `hdr` and `pbr` on, terrain tiles levels 1 to 3 and clusters levels 1 and 2, median of the last 150 frames, `[Rendering] frame counters`; the machine was shared with other agents, so wall times are rough; the draws and triangles are the census upper bound):

| `[WorldLod] view distance` | wall ms median / p99 | draws | triangles | resident MiB | device MiB |
| --- | --- | --- | --- | --- | --- |
| 40,000 | 3.46 / 5.09 | 38,175 | 3.0 M | 1,107 | 316 |
| 100,000 | 3.66 / 5.08 | 111,360 | 8.9 M | 1,535 | 492 |
| 600,000 | 4.17 / 13.07 | 29,818 | 6.3 M | 6,897 | 3,047 |

**Review correction (2026-11-03): do not quote the draws column.** 38,175, 111,360 and 29,818 are not monotonic while triangles and memory rise steadily: the census is an upper bound without culling, measured on a machine shared with other agents. The go / no-go rests on the memory columns (resident and device), which are the reliable ones; the wall times are rough.

Frames: `I:\Projects\OpenEden-work\depth\hi_a.png` (40,000), `hi_c.png` (100,000), `hi_b.png` (600,000), ground poses `gr_a.png`, `gr_b.png`. Findings from viewing them:
- **The sky is not clipped by the far plane** (ground pose, 600,000 against 40,000: same sky, same horizon; the dome fits inside the plane).
- **No depth artefacts** (no z-fighting, no flicker) at 600,000, as the table says.
- **The land fog follows the far plane** (`FogManager::configure(viewDistance)`), so at 600,000 the haze is gone: crisp dark terrain to the coast, no atmosphere (stage c's job). At 40,000 and 100,000 the fog hides everything past a few thousand units from this height.
- **Open water far away is speckled with blue and white noise at 600,000** (top corners of `hi_b.png`). Not depth precision (the table). **Unverified hypothesis:** `water/main.frag:97` passes `scene.zFar` as the screen-space reflection's maximum distance, which is now 600,000 (`reflect.glsl`'s stride and jitter follow it). A stage c item: cap the reflection distance, and fade the water to the haze colour with distance.
- **At 100,000 white wavy strips follow some far ridges** (upper right of `hi_c.png`), probably water planes or tile edges; not investigated.

**Go / no-go.**
- Depth and far plane: **go**; nothing to build.
- The whole-map view with today's baked levels: **no-go**: 6.9 GiB resident and 3.0 GiB of device memory (against about 1.1 GiB and 0.3 GiB at 40,000), p99 13 ms (against 5 ms), because everything out to 600,000 is chosen at levels 1 to 3 and nothing is evicted. This is the budget stage d (levels 4 to 6) and the streaming core (task 0060) have to bring down: the target remains the spec's far field of at most 1.5 ms CPU and 150 MiB resident over the 30,000 view.
- `view distance = auto` must not become the default before those two; the cap setting (`max view distance`) is needed from the start.

**Not verified:** a true before/after pixel comparison (not needed, no render code changed); `fast ascent` pop at level boundaries (needs levels 4 to 6); the sky dome radius value (confirmed only by frames); the shadow cascades and the editor viewport at 600,000 (read, not run); the weak-GPU run; the clouds and volumetric fog at 600,000 (not run; both build rays from the projection and have their own reach limits: `volumegpu.hpp` `farDistance(fogEnd)`).

### Stage b, 2026-11-03: the limits taken apart (the commit that adds `components/lod/viewlimits.*`)

**What changed.** The one view distance is now four numbers, computed by one pure function (`Lod::computeViewLimits`, `components/lod/viewlimits.*`) and kept in `RenderManager` (`mViewDistance` = the paging range, `mTileRange`, `mFarPlane`, `mFogRange`, `apps/openmw/mwrender/rendermanager.cpp`, `applyViewLimits`):

| Number | Used for | Before |
| --- | --- | --- |
| tile range | `LodTerrain` and `LodClusters` camera (`camera.mViewDistance`: tiles beyond it are skipped) | the one number |
| paging range (`getViewDistance()`) | `mTerrain->updateView`, `DistantObjects::update`, the map, the preloader (`scene.cpp`) | the one number |
| far plane | `updateProjectionMatrix`, `setDepthRange`, the post-process `far` | the one number |
| fog range | `FogManager::configure` (land fog ends there; stage c gives the fog its own model) | the one number |

Rules (tests in `apps/components_tests/lod/testviewlimits.cpp`): with the world LOD inactive (`enabled`, `[Terrain] distant terrain`, not a bake run) all four are `[Camera] viewing distance`; active with `map view` off all four are the larger of that and `[WorldLod] view distance` (`auto` counts as 40000), exactly the rule of commit `ed53100fd3`; with `map view` on the tile range reaches `view distance` (a number, not capped) or, for `auto`, the diagonal of the box of the loaded world's land cells capped by `max view distance`, the far plane and fog follow the tile range, and the paging range stays at the old number. `auto` is camera independent (the diagonal is the farthest any camera can be from land) so the projection does not change every frame; the land cells come from the ESM store in `RenderManager::setStore`.

New settings (in `files/settings-default.cfg` and so in `defaults.cfg`, which the editor loads): `[WorldLod] map view = false`, `max view distance = 600000`; `view distance` is now text (`40000`, any number, or `auto`), parsed by `Lod::parseViewDistanceSetting`; text that is neither logs one warning and counts as 40000. A Lua `camera.setViewDistance` still sets all four numbers until a setting changes (`mViewLimitsOverridden`).

**Tests, written before the code** (`testviewlimits.cpp`, 21 tests; written from this spec while `viewlimits.hpp` did not exist, so they were never run red: the first build compiled them against the new header and all 21 passed. **Process deviation, recorded by the review of 2026-11-03:** CLAUDE.md asks for an independent test-writer and a failing-first run; this stage had neither (the implementing agent wrote the tests, and the red run was skipped). The review fixes below were done red first): parsing (`auto` in any case, numbers, 15 refused texts), the land extent from cells (empty, one cell, negative and positive, order independent) and its diagonal, the limits in every combination (world LOD off, map view off, `auto` with and without an extent, the cap, a number overriding `auto`, invariants over a grid of inputs, purity), and the tile selection out to 600,000 units (the accepted level is non-decreasing with distance from 20,000 to 600,000 and reaches level 5 at the far end; a tile is skipped beyond the tile range and chosen inside it with no far plane involved; hysteresis at 0.7 holds for a level 4 tile at 60,000 to 560,000; selection is a pure function). Not covered here: tile keys of levels 4 to 6 and the baker's determinism for them (stage d). Windows: `components-tests` 4735, `openmw-tests` 859, `eden-studio-tests` 578, all pass.

**Frames: map view off is the same picture as the commit before.** Method: a second `openmw.exe` was built from the stage-a commit's `rendermanager.{cpp,hpp}` (the old one-number code) with the same settings category, then both were run with the same config copy (`I:\Projects\OpenEden-work\mapview`, a fresh user dir, `--skip-menu --start Balmora --script-run` with `tcl` and, for the high pose, `setpos` to (-20480, -12288, 25000) looking 35 degrees down, frame 300, 1280 x 720, `hdr`, `pbr`, `object clusters`). Differences by ffmpeg (`psnr` average, `blend=difference` maximum of luma):

| Pair | PSNR | max luma difference |
| --- | --- | --- |
| old exe against itself, high pose, `view distance = 40000` (run-to-run noise) | 72.2 dB | 48 |
| old against new, high pose, 40000, map view off | 70.5 dB | 45 |
| old against new, ground pose, 40000 | 68.7 dB | 1 |
| old against new, ground pose, 600000 | 66.5 dB | 1 |
| old against new, `enabled = false` (the world LOD off), second pair | identical (inf) | 0 |
| new with `view distance = auto`, map view off, against old 40000, high / ground | 78.1 dB / 66.9 dB | 19 / 1 |
| old against itself and new against itself, high pose, 600000 | 32.7 / 29.1 dB | 156 / 159 (the tiles load asynchronously: the pose is not repeatable at that distance) |

So the frames are identical within the run-to-run noise of the same binary. **This is a relaxation of the spec's bar of 0.000 percent of pixels differing** (review of 2026-11-03: each pair is one run, PSNR here does not discriminate, and `auto` with map view off scored better than the self pair): only `enabled = false` is byte-identical. They are not byte-identical with the world LOD on (the noise is the terrain, clouds and water animating on real time, and at 600,000 the order tiles finish loading); with the world LOD off the pair is byte-identical. `map view = true` with a number gives the same frame as `map view = false` (the number sets all four).

**Map view on** (`view distance = auto`, map view on, the same high pose, `terrain tile threshold` 8, clusters on): the whole land out to the coast and the second island are drawn, from 25,000 units up, with no fog (the fog end is the 590,000 tile range: stage c adds the haze) and no gap at the edge of the live terrain in this frame. The frame is dark because the pose looks down and the fog no longer lightens it; the lighting is unchanged. The cost with today's levels is the stage-a table (6.9 GiB resident at 600,000): `auto` is not the default. **That figure does not describe this configuration** (paging stays at 40,000 with `map view` on); it was measured in the review fixes below.

**Stale-exe lesson applied:** `eden-studio` was rebuilt after the defaults changed and starts (`defaults.cfg` carries `map view`, `max view distance`). **Not verified:** the Lua tests (run at the next Linux gate), a frame with `[Cells] incremental cell build` on, `view distance = auto` in the editor viewport (the editor has no world LOD), a Lua `setViewDistance` call at run time, weak GPUs, and the launcher (which does not show these settings).

### Stage b review fixes, 2026-11-03 (fresh review of stages a and b, fixes by a separate pass)

The review (read-only) found no blocker: with the world LOD inactive or `map view` off every limit equals the old single number (traced through the projection, the fog configure, terrain and object paging, tile selection and the bake modes). What it found, and what was done:

1. **`auto` went stale.** `RenderManager::setStore` worked the land diagonal out only if `enabled` and `map view` were on at that moment; switching them on later left it 0 and `auto` fell back to 40000. It is now worked out always (a few thousand cells, once per store), and `[WorldLod] enabled` and `[Terrain] distant terrain` also recompute the limits when they change.
2. **`max view distance` below 40000 had no effect** (a test pinned 40000 as a floor under it). toddhow's design intent is that the cap is a hardware limit, so it wins: with `map view` on and `auto`, a cap lowers the tile range and the paging range to `max(camera viewing distance, cap)`; a cap of 0 or less means no cap and then the old number is the floor of the tile range; a number in `view distance` is never capped; with `map view` off the cap plays no part. **The one existing test changed on purpose:** `the_cap_never_lowers_the_reach_below_the_old_one` became `the_cap_wins_over_the_old_number` (the red run before the fix shows it failing: 40000 against 20000).
3. **A numeric `view distance` was not clamped** (`1e30` reached the projection). It is cut to `Lod::maxViewDistanceNumber` (10,000,000: far beyond any map, where float depth is meaningless) with a warning. The warning no longer latches: it repeats when the setting changes to another such text, and again after a good value was set in between.
4. **Lua `camera.setViewDistance`** sets all four limits (`Lod::uniformViewLimits`), cancels `map view` and the haze until a setting changes, and its early-out now compares all four limits (it compared only the paging range, so a call equal to it did nothing while the far plane or tile range differed). A distance that is not finite or not positive is ignored. Documented in `settings-default.cfg`.
5. **Weak tests strengthened.** DepthMath: the reconstruction tolerance is 0.1 unit (the measured error is 0.0625 at 590,000 and under 0.04 elsewhere) and 590,000 was added; the tautological `isfinite || isinf` is gone; the precision table is asserted instead of printed. LodViewLimits: the far end now asserts level 5 exactly (the log claimed it; the test only said 4), the hysteresis search fails loudly if it runs off the end, and `EXPECT_GE(fogRange, 0)` became `fogRange == tileRange`. 29 tests now (was 21): the cap rules, the clamp, `uniformViewLimits` and the all-four comparison.
6. **Red run.** Before the fix the new and changed tests were run against stubs: 7 failed (the clamp, the cap rules, the uniform limits), as intended. A deliberate mutation of `viewZFromDepth` (a factor of 1.001) made the tightened reconstruction and table tests fail (an error of 1 and of 7 units against 0.1), where the old tolerance (about 100 units at 100,000) would have let it through. The mutation was reverted.
7. **Cost of `map view`, measured now** (`view distance = auto` with `map view` against 40000 with it off; Balmora, 25,000 units up looking 35 degrees down, 1280 x 720, RTX 4070, `hdr`, `pbr`, tiles levels 1 to 3 and clusters levels 1 and 2 from the lodtest bakes, median of the last 150 frames, shared machine, quiet window):

| | wall ms median / p99 | GPU ms median | resident MiB | device MiB | draws (census) | triangles (census) |
| --- | --- | --- | --- | --- | --- | --- |
| map view off, 40,000 | 3.46 / 4.96 | 0.085 | 1,121 | 317 | 38,175 | 3.0 M |
| map view on, `auto` (590,000) | 3.78 / 5.16 | 0.095 | 1,780 | 589 | 40,366 | 5.8 M |

So `map view` on with `auto` costs about 0.3 ms of wall time, 660 MiB resident and 270 MiB on the device with today's levels, instead of stage a's 6.9 GiB: the paging range stays at 40,000 here. Quote the memory columns, not the draws. A first run without the LOD packs in the user data folder drew the same 49,745 draws on and off: tiles and clusters need `openeden/lod/*.edenpak` there (the log says "not used" otherwise). Frame: `I:\Projects\OpenEden-work\mapview2\hi_on.png`.
8. **Commit message note:** the message of `6fa7cece57` mentions a "testbodylegacy dangling-reference fix"; its diff is only a clang-format re-wrap of that fix's line (the fix itself is in `e3b7878727`).

**Not verified:** the Lua tests and the Linux gate; the editor viewport with `auto`; a Lua `setViewDistance` call at run time in the game (only its pure part is tested); weak GPUs; `[Cells] incremental cell build` with the new limits.

### Stage c, 2026-11-03: aerial perspective (the haze of the whole-map view)

**What it is.** With `[WorldLod] map view` on (and `enabled`, `[Terrain] distant terrain`, and the new `haze`, default true), the land, objects, tiles, clusters and water fade into the air's colour by how much air the ray crosses, in place of the linear fog end (which stage b had moved out to the tile range, 590,000, so it did nothing). The air's density is `beta * exp(-h / H)` with `h` the height above world z = 0 and `H` the scale height; a straight ray from the camera at height `h0` with direction z `dz` and length `s` crosses the optical depth `beta * exp(-h0/H) * (1 - exp(-s dz / H)) / (dz / H)` (the closed form; a ray that descends to height 0 adds `beta` per unit from there on), and the fog's share is `1 - exp(-depth)`. Thick near the ground, thin at height: from 25,000 units up the ray to the far sea crosses little air.

**One shared function.** Everything that fogs goes through `applyFog` in `files/shaders/lib/view/fog.glsl` (objects and clusters through `default/main.frag`, terrain and tiles through `terrain/main.frag`, water through `water/main.frag`), so the haze is written once, there. Its C++ twin is `components/render/aerialmath.hpp` (`Render::Aerial`: `baseBeta`, `weatherBeta`, `fogDepthFromRange`, `opticalDepth`, `transmittance`, `fogAmount`, `sceneFogRange`, `isAerial`); a test reads the shader's three constants back. The sky keeps its own colours; the cloud and sun shaders do not use `applyFog`. It is GLSL, not Slang: the object, terrain and water pipelines are GLSL and take their fog from a GLSL include; a Slang function could not be included there (decision 0009 applies to new shaders, and this is an edit of an existing include).

**How the shaders are told (no new uniform).** `Scene` cannot grow (past 640 bytes interiors brightened) and `fogColor.w` is the linear-view marker. The scale is stored NEGATIVE (`View::Scene::setFogRange(start, end, aerial)` -> `Render::Aerial::sceneFogRange`): its size is the linear fog's, so the weather's fog depth is read back from the range (`depth = 1 - start / end`; FogManager stores start = range * (1 - depth), end = range). Only the main view's exterior above water sets it (`mRenderModes[Render_Sky]`, not under water, not `[Fog] use distant fog`, not a bake run, no Lua `setViewDistance` override), and only when the shaders were built with it (`fog.settings`: `cAerialHaze`, `cAerialVisibility`, `cAerialScaleHeight`, `cAerialWeatherGain`, read at start, kept as `RenderManager::mAerialShader`). Interiors and under water keep the linear fog. The volumetric fog and cloud compute shaders read `fogScale <= 0` as "no fog", so with the haze on the volumetric fog is off (not supported together yet: the aerial path is checked first in `applyFog`).

**Weather.** Beta is `baseBeta(haze visibility) * exp(gain * clamp(depth - 0.7, 0, 0.42))`: clear, cloudy and overcast (Land_Fog_Day_Depth 0.69 to 0.72) have the base density; rain 0.8, storm and fog 1.0, blizzard 2.8 (capped). Vanilla's linear fog ends at the view distance in every weather and only its start moves, so the settings were calibrated to the vanilla feel (gain 10, cap 0.42: the distance that keeps 10 percent of the light, horizontally at the ground: clear 100,000, rain about 30,000, storm about 5,000, blizzard and ash about 1,500). The first try (gain 12, cap 0.5) made ash storms about 250 units deep: far too thick, seen in a frame, corrected.

**Settings** (`[WorldLod]`, defaults in `files/settings-default.cfg`, and so in `defaults.cfg`): `haze = true`, `haze visibility = 100000` (T = 0.1 for a horizontal ray along the ground in clear weather; 590,000 as first planned was invisible in every frame: a view from 25,000 units up crosses almost no low air, so it came down to 100,000), `haze height = 4000`, `haze weather gain = 10`. Starting values chosen by eye; tune further when toddhow has flown it.

**Tests, written first and run red.** `apps/components_tests/render/testaerialmath.cpp` (14 tests) was written against a stub header whose functions returned wrong values on purpose: 9 of 13 then failed, the other 4 passed vacuously (they compare relations that hold when the density is 0). Then the implementation: one test failed at first, my own bound: at 25,000 units up a horizontal ray gives a fog share of 0.0053, not under 0.001 as I wrote; the closed form is right and the test now compares against it. They cover: T = 0.1 at the visibility, T = 1 at zero distance, T in [0, 1] and falling with distance for 3 densities and many rays, the optical depth against Simpson integration (200,000 steps) for 240 rays within 1 percent (including `dz = 0`, tiny `dz`, up, down, a camera 80,000 up), a descending ray reaching the ground, thinner air from altitude, weather (base density at and below 0.7, rising with depth, capped, a storm sees less than a tenth of clear, a gain of 0), `fogDepthFromRange`, the shader's constants, and "off is exactly vanilla": `sceneFogRange(start, end, false)` returns `start` and `1 / max(0.001, end - start)` bit for bit for 30 combinations and is never marked; with it on the scale is negative with the same size. All 14 pass, with the 121 of `AerialMath*:LodViewLimits*:*Settings*`.

**Frames** (`I:\Projects\OpenEden-work\hazefog`, 1280x720, RTX 4070, `OPENEDEN_QUIET_WINDOW=1`, the baked terrain and object packs of `lodtest/user/openeden/lod`, a config copy). "lin" is the same `map view` with `haze = false` (the linear fog at the tile range). All frames were viewed.
- **From 25,000 units up over Balmora, looking north** (`F_b25k_n`, `h100_b25k_n`): the far ridges and the far sea pale into the sky colour and the horizon blends; `lin_b25k_n` is crisp to the horizon. At `haze visibility = 590000` (`haze_b25k_n`) the two are nearly the same: too weak.
- **From 3,000 units up looking over Balmora** (`h100_b3k_e`): the far trees and the sea at the horizon are lightly hazed, the town is clear.
- **Over the swamp near Vivec, 25,000 up** (`h100_v25k`): depth cueing across the far ridges, nothing washed out.
- **Weather at Seyda Neen** (`G_storm`, `G_ash`, `G_rain`; `F_storm_lin` and `F_ash_lin` for the linear fog under `map view`): the linear fog at the tile range shows NO weather (a storm or an ash storm looks like a clear day with rain or a red sky); the haze shortens the view as vanilla does: the storm hides the far trees and hills, the ash storm hides everything but the near house, rain is a mild haze.
- **Off is unchanged.** `map view` off (`enabled = false`; poses `F_off_ground`, `F_off_b3k_e`): the same exe was run with the OLD `fog.glsl` (the stage b file, copied over the build's resource copy and restored) and the new one: PSNR 37.1 dB (ground pose; NPCs and water move between runs) and 64.6 dB (3,000 up), max luma difference 129 and 28; two runs of the NEW shader against each other give 40.1 dB and 61.7 dB, max 115 and 26. So the old-against-new difference is the run-to-run noise. Not byte-identical, as in stage b (the game animates on real time): "0 pixels differ" cannot be shown with these poses; the code path is identical by the unit test above and by the diff (`mixFog` is the old `mix`; the aerial branch is behind `cAerialHaze`, a compile-time false).

**What looks wrong (not hidden by the haze).**
- **Far water speckle:** open water far away is a dotted white and blue noise out to the horizon in every `map view` frame, haze or not (`lin_b25k_n`, `h100_*`). The haze lowers its contrast near the horizon but does not remove it. It is a water shader problem (not studied here; the unverified hypothesis from stage a is that a water distance uses `scene.zFar`), so it needs its own task.
- **White strips on far ridges** (stage a, at 100,000) were not seen in these poses; not investigated.
- **Volumetric fog and the haze are exclusive** for now (see above).
- **Interiors and under water** keep the linear fog by design; a doorway between an interior and an exterior is a hard cut, as before.
- **Fog depth 0:** an exterior with `fogDepth == 0` stores `end = 3.4e38`, from which the shader reads depth 1 (maximum weather). No Morrowind weather has depth 0; a mod weather with 0 would be hazed as a blizzard. Not handled.

**Not verified.** A frame at 600,000 from 100,000 up (the highest taken was 25,000); weather transitions as a sequence; the horizon colour against far land at dawn and dusk (all frames are noon); water in a storm from altitude; the editor viewport (it never turns `map view` on); the Lua tests and the Linux gate (clang-format 14 was run in Docker on the changed files, the unit tests on Windows only); a weak GPU; the cost of the haze (an `exp` and a few multiplies a fragment on the fogged draws, no measurement taken).

Stage c's acceptance item "from the ground the horizon ends in haze at every weather; storms and ash still shorten the view" is seen at noon at Seyda Neen in rain, storm, ash and blizzard (`G_rain`, `G_storm`, `G_ash`, `G_blizzard`; the blizzard hides everything past the near house). The other items belong to later stages.

### Stage d, 2026-11-03: the baker levels and the frozen cluster layout

**Frozen cluster layout (toddhow's approval of 2026-11-03, item 5).** The draft layout 0 is layout version 1: the header's version field is 1 (`objectClusterPayloadVersion`), the manifest's `layoutVersion` is 1 (`clusterLayoutVersion`), and both readers refuse version 0 with a located error that says the pack must be baked again; the bytes are otherwise the draft's. Two limits grew with it, because the far levels need them: `maxClusterLevel` 3 to 4 and `maxClusterCells` 64 to 256 (a vertex's cell slot is one byte, so 256 is the most the layout can hold; a level 4 tile is 16 x 16 cells). Re-baking is byte-identical: `terrain.edenpak` and `objects.edenpak` baked twice into two user folders have the same SHA-256.

**Pinned tests changed in the freeze (each listed, the S1 pattern).**
- `LodObjectCluster.WriterMatchesHandBuiltBytes` and `ReaderParsesHandBuiltBytes`: the hand-derived bytes of `makeExpectedBytes` hold version 1 (`putU16(b, offVersion, 1)`).
- `LodObjectCluster.RefusesOtherVersions`: the refused versions are 0, 2 and 0xFFFF (1 was refused, 0 is now).
- `LodObjectCluster.RefusesLevelsOutsideOneToThree` became `RefusesLevelsOutsideOneToFour` (0, 5, 15, 0xFFFFFFFF).
- `LodObjectCluster.WriterAndValidatorRefuseLogicallyInvalidClusters`: the case "level 4" became "level 5", and "too many cells" adds 256 cells instead of 64.
- `LodClusterBuild` `validateInput` cases: "level 4" became "level 5".
- `LodClusterBuild.MoreThanSixtyFourDistinctCellsIsAnError` became `MoreThanTwoHundredFiftySixDistinctCellsIsAnError` (a 32 x 32 cell tile at level 4, 256 allowed, 257 refused).
- `LodClusterPack`: the canonical manifest text and `canonicalValue()` say `layoutVersion` 1; `open_refuses_a_manifest_with_layout_version_1` became `open_refuses_a_manifest_with_another_layout_version` (0, 2, 4000000000); the "leading zero on zero" case is now `01`; `manifest_refuses_levels_outside_one_to_three` became `..._one_to_four` (the case "level 4" is now "level 5").

**New tests, written first and run red before the code** (20 failed against the old constants and a stub of `clusterLevelError`, among them `LodTileBakeLevels.the_default_plan_bakes_levels_one_to_six`; the far-lod storage tests failed with garbage values): `LodObjectCluster.LayoutVersionIsOne`, `LevelFourWithTwoHundredFiftySixCellsRoundTrips`, `TwoHundredFiftySevenCellsAreRefused`, `LevelFiveIsRefusedByTheWriterAndTheReader`; `LodClusterPack.manifest_accepts_four_levels_and_the_layout_version_is_one`; `LodClusterBuild.TheLevelErrorsGrowAboutThreeTimesALevel`; `LodTileBakeLevels.the_default_plan_bakes_levels_one_to_six`, `the_far_levels_cover_a_morrowind_sized_world_in_few_tiles` (level 4: 16 tiles, levels 5 and 6: 4 each), `levels_four_to_six_bake_with_a_ground_picture_each`; `LodViewLimits.the_measured_levels_one_to_six_are_chosen_coarser_and_coarser_out_to_the_map_edge` (the real errors below; monotonic out to 600,000 units; level 3 from 77,000, level 4 from 154,000, level 5 from 339,000, level 6 from 410,000); `ESMTerrainFarLod.*` (2). The far-level terrain tests (coverage and level 6 bake) and the selection test passed from the start, because the tile model already supported the levels; they pin that.

**A bug found by the far levels, in inherited code.** `ESMTerrain::Storage::getVertexData` (`components/esm3terrain/storage.cpp`) keeps at least one vertex per cell, so for a lod above 6 (a step of 128 or more vertices; a level 6 tile is read at lod 7) it wrote beyond its arrays: the level 6 tiles got garbage (a tile error of 1.9e28) and the heap was corrupted. It now takes lod 6, whose vertices are the cell corners, and keeps every 2^(lod - 6)th vertex, which is the same grid (tests `ESMTerrainFarLod.*`). Level 5 (step 64) was never affected.

**Terrain levels 4 to 6.** `TerrainBakePlan{}` bakes levels 1 to 6 (593 tiles: 414, 118, 39, 14, 4, 4; 52.0 MB; bake 15 to 17 s; levels 2 and up carry the 256 x 256 ground picture). Geometric error of the tiles in world units, from the bake log (`LOD terrain bake: level N`): level 1 median 190 (max 1136), level 2 489 (1392), level 3 984 (2314), level 4 1973 (3143), level 5 4347 (6791), level 6 5254 (6516). A pack whose manifest has another level list is stale, not corrupt (the manifest comparison already did that; the 571-tile pack of levels 1 to 3 must be baked again). The staleness check at load now covers six levels: the pack is adopted about 10 s after the start (worker thread; it was about 1 s with three levels), because the digest of a level 6 tile reads 4097 x 4097 land samples.

**Object cluster levels 1 to 4** (`Lod::clusterLevelError`: 128, 320, 960, 2900; the baker's `bakerVersion` 13). Bake 40 s, 497 clusters (358, 100, 31, 8), `objects.edenpak` 186,681,880 bytes (was 157 MB). Per level: level 3 keeps 23,358 of 119,122 statics (11.4 M triangles to 2.2 M), level 4 keeps 920 (583 k to 127 k). The survey the baker now logs says what the next levels would keep: **level 5 (error 8700) keeps 1 object, level 6 (error 26100) keeps 0**, and a level 5 tile holds 1,024 cells, which a cluster's one-byte cell slot cannot address. **Recommendation, not a silent choice: stop at level 4**; the far land at levels 5 and 6 is terrain only, which is what toddhow's answer 4 ("far land is basic, it blends into the ground colour") asks for. Merging levels 4 and 5 is not needed.

**Two baker faults found and fixed while measuring.** (1) The bake runs inside a new game where the player starts in a fight and dies after about 10 s; a death stops the game's update and with it the bake, so any bake longer than that stalled for ever (the earlier "hangs"). The object baker switches god mode on at its start; the terrain baker finishes in 17 s and was not affected, but should do the same if it grows. (2) The first bake attempts looked slow with the six-level terrain pack in the user folder; moving the pack aside showed it was the death, not the pack. New logs: progress every 50 tiles, any tile over 2 s, per-level terrain errors, the survey. The measurement poses also need `tgm` and `tcl` (god mode, no collision): a player placed at 25,000 units falls and dies.

**Cost of the whole-map view** (Balmora and Vivec at 25,000 units, 1280 x 720, `map view` on with `auto` against `map view` off with `view distance = 40000`; both with the new packs, last 800 of about 4,000 frames, an RTX 4070, the machine shared with another agent's builds):

| Pose | Setting | Median ms | p99 ms | Resident MiB | Device MiB | Draws | Triangles |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Balmora 25,000 | off | 3.48 | 5.48 | 1,723 | 314 | 38,175 | 3.00 M |
| Balmora 25,000 | on | 3.49 | 5.56 | 1,978 | 410 | 39,363 | 4.30 M |
| Vivec 25,000 | off | 3.63 | 5.37 | 2,028 | 378 | 28,878 | 2.99 M |
| Vivec 25,000 | on | 3.60 | 5.39 | 2,371 | 570 | 29,325 | 5.02 M |

Further poses with the setting on (Balmora 3,000 / 8,000 / 15,000 / 60,000; Vivec 3,000 / 60,000): medians 3.46 to 4.95 ms, p99 5.3 to 7.6 ms, resident 1,976 to 2,287 MiB. **Go / no-go against the stage a budget (at most 1.5 ms CPU and 150 MiB resident over the 30,000 view): the CPU cost is met (0.0 to 0.1 ms); the memory is not: +255 MiB resident at Balmora and +343 MiB at Vivec, +96 and +192 MiB on the device.** The memory is the baked tiles (the six-level pack with pictures) and the level 3 and 4 clusters (2.3 M triangles); stage e is where a hardware tier trades it (`far levels`, `max view distance`, picture size). Stage b's own row (3.78 ms, 1,780 MiB, 589 MiB) was a shorter run on a differently loaded machine: do not compare it with this table.

**Frames viewed** (`I:\Projects\OpenEden-work\mapview3\frames`): `on_bal_25000.png`, `on_viv_60000.png`, `on_bal_60000.png`, `on_bal_15000.png`: terrain complete to the coast and to Solstheim, no hole, no crack, no tile seam seen; clusters stand on the terrain; the haze of stage c blends the sea into the sky. The first set of frames, taken before the pack was adopted, had no far terrain at all (objects floating over haze): that was the 10 s adoption, not a fault, and it is why the runs are 3,500 frames long.

**Not verified.** The Linux gate and the Lua tests; a continuous scripted ascent (the poses are separate runs at 3,000, 8,000, 15,000, 25,000 and 60,000 units, so a pop or crack at a level boundary during a climb is not seen, only the stable frames); clusters at levels 3 and 4 against live objects close up; the weak-GPU run (stage e); the editor with the new `getVertexData` path in a running window (`eden-studio-tests` pass and the editor was rebuilt, not launched); a terrain bake at the 40,000 view while a pack of three levels is still in the user folder (stale, as designed). clang-format 14 was run in Docker over the changed lines of the files I changed (not the other agents' hunks in `vsgengine.cpp`, `main.cpp`, `options.cpp`, `arguments.hpp`).

### Stage e, 2026-11-03: hardware tiers, the memory of the whole-map view, the scripted climb

Method: Balmora, 1280 x 720, an RTX 4070 with 12 GiB, 32 GiB of RAM, the machine shared with other agents' builds, last 800 frames of about 3,500, every game in a quiet window, a copy of the config, the packs of stage d. Scripts and tables: `tools/mapview_check/` (`run_ascent.sh`, `analyze.sh`, `analyze_ascent.sh`, the Lua climb in `data/`), runs in `I:\Projects\OpenEden-work\mapview4`.

**Where the memory of stage d went (the finding that changed the plan).** Stage d left the whole-map view +255 / +343 MiB resident over the 40,000 view, over the 150 MiB budget. Variants at the 25,000 pose, with the stage d code: no LOD 735 MiB; map view off, clusters off 1,590; map view on, clusters off 1,597; far levels 4: 1,050; far levels 5: 1,070; far levels 6: 1,597. So the terrain tiles of levels 5 and 6 cost nothing to draw (device memory unchanged) but the working set rose by 530 MiB, and that came from the **staleness check** of the pack on its worker thread: it sampled the whole land again for each of six levels (levels 5 and 6 build arrays of 4097 x 4097 samples), and the working set stayed at the peak (`working set` log lines: 342 MiB before the check, 1,528 after, 1,574 at adoption; `_heapmin` and `HeapCompact` after the check changed nothing, so the memory is held by something, not by a lazy allocator; what holds it was not found). The check also took ten seconds (the adoption delay of stage d).

**Fix (`lodterrain.cpp`, `TilePack::tileSourceDigest`):** the check now samples only levels 1 and 2 (and no more than `far levels`), tile by tile against the digest each tile was written with (the pack's provenance records). Level 2 already reads every land sample and, as every level from 2 up, every blend map, vertex colour and layer file; a pack baked in one run holds the same land at every level, so levels 3 to 6 are read without a check of their own (documented in the code; a hand-mixed pack of two bakes would not be noticed above level 2). Adoption: 10 s to 1.1 s. Resident at the 25,000 pose: map view off 1,721 to 1,052 MiB; terrain only, six levels, 1,597 to 1,010; on 1,975 to 1,385 to 1,394. The working set after the check is still 890 MiB (342 before): the land records of 1,540 cells stay loaded; that is the base of the LOD, not of the whole-map view. This change also applies with `map view` off (the default LOD path now checks two levels too).

**What the whole-map view costs now (same code, `map view` off against on, 25,000 pose):**

| | Balmora resident / device MiB | Vivec resident / device MiB | median ms |
| --- | --- | --- | --- |
| map view off (40,000) | 1,051 / 297 | 1,321 / 377 | 3.46 / 3.67 |
| tier minimal | 1,248 / 377 (+197 / +80) | 1,531 / 473 (+210 / +96) | 3.54 / 3.61 |
| tier low | 1,305 / 393 (+254 / +96) | 1,590 / 489 (+269 / +112) | 3.47 / 3.72 |
| tier medium | 1,386 / 409 (+335 / +112) | 1,772 / 569 (+451 / +192) | 3.46 / 3.60 |
| tier high | 1,394 / 409 (+343 / +112) | 1,773 / 569 (+452 / +192) | 3.49 / 3.57 |
| terrain tiles only (`object clusters` off), against the same view without map view | +93 / +16 | +126 / +16 | 4.45 against 3.84, 8.71 against 6.91 |

- **The budget of 150 MiB resident is met by the terrain tiles alone (+93 and +126) and is not met with object clusters on at any tier (+197 at best).** The clusters of levels 1 to 4 are the rest: the working set grows about 13 MiB for each built cluster (about 100 to 160 bytes a vertex; 42 clusters = 560 MiB at the Balmora pose); what `Lod::clusterResidentBytes` counts (66 bytes a vertex, 8 an index: 225 MiB at the Balmora pose, 365 at Vivec) is about a third of that. Letting VSG drop the arrays' CPU copies after the transfer (`STATIC_DATA_UNREF_AFTER_TRANSFER`) changed nothing in a trial and was removed. The honest number for the high tier is +340 to +450 MiB; the way to the budget is a lower tier or `object clusters = false`.
- **What did not help, measured:** capping the far cluster pictures (`far texture max size`): 187 textures lost mip levels, 8 MiB before and 1 MiB after (the pictures of the clusters are small; resident 1,975 / 1,979 / 1,980 for caps of 256, 1024 and 128). `far levels` 3, 4, 5 against 6 with the short check: resident 1,393 / 1,386 / 1,383 / 1,385 (levels 4 to 6 cost nothing, they only extend the reach; stage d's earlier 570 MiB difference was the check). The setting stays (it is cheap and harmless); the reach, not the memory, is what a lower `far levels` changes.
- **A hard memory budget is a bad lever and is only a safety valve:** with 64 / 112 / 192 MiB (counted) the blocked clusters leave their cells to the live objects, which cost more draw calls (49,700 against 33,000), and the median rose to 4.6 to 8.8 ms. The tier budgets are 256 / 320 / 512 / 1,024 MiB counted and were never reached at the poses measured (the largest figure held was 365 at Vivec, high tier); `[WorldLod] far memory budget` overrides them.
- **Threshold:** a tile and cluster threshold of 32 px at Vivec gave a median of 6.0 ms (40,915 draws: the live objects cover what the clusters leave); 16 px gave 3.76 ms and saves about 150 MiB resident against 8 px. The minimal and low tiers use 16.

**Tier table (`components/lod/fartier.*`; starting values from the runs above, not properties of the hardware):**

| tier | device memory and system memory | far levels | threshold (at least) | `auto` cap | far pictures | budget counted |
| --- | --- | --- | --- | --- | --- | --- |
| minimal | a software device, unknown, or under 2 GiB / 6 GiB | 3 | 16 px | 80,000 | 128 px | 256 MiB |
| low | 2 GiB / 6 GiB | 4 | 16 px | 150,000 | 128 px | 320 MiB |
| medium | 4 GiB / 12 GiB | 5 | 8 px | 300,000 | 256 px | 512 MiB |
| high | 8 GiB / 24 GiB | 6 | 8 px | 600,000 | 256 px | 1,024 MiB |

The tier is the lower of the two memory tiers; `[WorldLod] tier` (auto, minimal, low, medium, high) forces one; `far levels`, `far texture max size` and `far memory budget` override single values (0 means the tier's). **It acts only with `map view` on**: with it off nothing is read from the device and every number is the setting's own. The effective threshold is the larger of the setting and the tier's, the effective cap the smaller. Logged at start (`World LOD tier: ...`).

**The scripted climb (`tools/mapview_check/run_ascent.sh`):** one run; a global Lua script teleports the player (god mode and no collision from `tgm` and `tcl`) from the start height (2,257 units) to 27,257 over 40 s with a smooth start and stop, holds 5 s and comes back in 40 s, and a player script takes a screenshot every 5 s (25 per run). A window of 85 s of summed frame time (`analyze_ascent.sh`):

| | median | p99 | p99.9 | worst | over 16.7 ms | peak resident / device MiB | peak draws | peak triangles |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| map view on, high | 3.77 | 5.41 | 6.78 | 11.28 ms at 74.5 s | 0 | 1,450 / 442 | 40,552 | 4.56 M |
| map view on, minimal | 3.65 | 5.35 | 7.69 | 25.9 ms at 4.9 s | 2 | 1,294 / 394 | 39,074 | 3.86 M |
| map view off | 3.63 | 5.42 | 6.84 | 12.26 ms at 75.7 s | 0 | 1,096 / 314 | 39,272 | 3.22 M |

No frame over 16.7 ms in the high run and none over 33 ms in any. Frames viewed (`ascent_on_sheet.png` and `ascent_tmin_sheet.png` in the work folder, and frames 3 and 8 of the high run at full size): the ground (a rock close to the camera), the rise through the cluster ring, the whole coast, Solstheim and the sea paling into the haze at the top, and the same on the way down; **no crack, hole, seam or visible pop at a level boundary** at this size. A pop of one cluster would not show in a frame taken every 5 s; the frame-time trace shows no hitch, and the dither of the hand-over was not looked at frame by frame. The first run of the script found that `onFrame`'s `dt` is zero while the game waits in character generation (`--skip-menu`), so the script uses real time.

**Pack-load delay:** 10.1 s to 1.1 s (the log line `the check read the land for levels up to 2`); at far levels 6 all 593 tiles are read and adopted together.

**Weak-GPU run (software device, `far levels = 3`, `texture max size = 512`): NOT RUN.** It needs a Linux build of this change under Docker (llvmpipe, note 0025), and Docker was limited to short clang-format runs for this pass; no Linux build of these files exists. What exists: the tier of a software device is a unit test (`LodFarTier.a_software_device_is_the_minimal_tier_whatever_its_memory`), and the minimal tier was run on the real card (`tier = minimal`: far levels 3, threshold 16, cap 80,000, pictures 128 px; the rows above). Whether the packs baked on Windows pass the staleness check on Linux (float digests across compilers) is unknown: a weak-GPU run may need its own bake.

**Tests (red first, shown failing):** `LodFarTier`, `LodResolveFarLevels`, `LodFarLevelsWithinBudget` (20, 12 of them red against a stub), `LodFarTierSetting`, `LodEffectiveFarSettings`, `LodResolveFarTextureSize` (9, 8 red), `LodClusterBudget` (7, 6 red), `LodTilePack.a_tile_reports_the_source_digest...` (2, 1 red): 38 new tests in `testfartier.cpp` and `testtilepack.cpp`. Windows: `components-tests` 4,947, `openmw-tests` 859, `eden-studio-tests` 578, all pass; `eden-studio` was rebuilt after the defaults changed and starts (a 20 s run, no fatal error).

**Not verified:** the Linux build and gate (nothing of stage e has been built with GCC `-Werror`: watch for shadowing and unused variables in `fartier.cpp`, `lodterrain.cpp`, `lodclusters.cpp`); the weak-GPU run; a machine of another tier (the table was run by naming tiers on the RTX 4070, not on a weaker card); the working set that stays after the check (cause not found); `[WorldLod] tier` with `map view` toggled at run time (the tier is chosen at start); the lighter check against a pack whose level 3 to 6 tiles are stale (not tested); the pictures of far clusters at the hand-over (a 128 px cap on levels 1 and 2 was not looked at close); (`Render::releaseFreedMemory`, tried after the check and changed no number, was removed).

### Stage f, 2026-11-04: close-out

The acceptance table and "Your flight" are above, after the acceptance criteria; the status line says where the task stands. New: `tools/mapview_check/prepare_flight.sh`. It was tested on copies of `I:\Projects\OpenEden-work\lodtest\config` in `I:\Projects\OpenEden-work\flighttest`, never on toddhow's folder:

1. **First run: a fault found and fixed.** The terrain bake wrote its pack (593 tiles, 51,973,008 bytes, all read back), then the object bake's game stopped at start on a settings parse error: the script had appended a second `[Terrain]` and `[WorldLod]` section, and the terrain bake's game, rewriting `settings.cfg` at exit, put every key of both sections into both, so keys appeared twice. The stuck game (started by the script) was stopped. The script now writes each key once, into the first section of its name, and appends a section only when the file has none.
2. **Second run** (same source): exit 0, both packs (terrain 51,973,008 bytes, objects 186,681,880 bytes), the source's SHA-256 unchanged, and after both bakes the copy's `settings.cfg` has one `[WorldLod]` section and no section or key twice. The printed command plus `--skip-menu --start Balmora --auto-quit 600` (quiet window) logged `World LOD tier: high`, `593 tiles of levels 1 to 6 loaded`, `497 clusters`, no settings error, and quit normally.
3. **Windows paths with spaces and backslashes** (`flighttest\My Games\OpenMW` to `flighttest\work two`): exit 0, the same packs, the source unchanged.
4. **The `openmw.cfg` rules** (a source without the build's three lines and with stray `config=` and `user-data=` lines): the copy drops both stray lines, has the engine's data folder ahead of the first `data=` line and the build's `resources=` and pack; nothing was created at the stray paths; the source unchanged; both packs byte-identical to run 3's (SHA-256), which repeats stage d's determinism check of the new levels.

Refusals checked (exit 2, nothing created): a work folder outside `I:\Projects\OpenEden-work`, that folder itself, a work folder inside the config folder, an existing non-empty folder the script did not make. The script had no fresh review.

**Not done in this task** (each is in the table): toddhow's verdict on the frames, the weak-GPU run, a near-field difference image with `map view` on, an "off" frame comparison after `7ebbc09e09`, frames at dawn and dusk, weather changes as they happen, a frame at 600,000 from 100,000 units up, and a test and fresh review of `7ebbc09e09`.

### Stage g, 2026-11-04: far cluster eviction (decision 1, option b) and the weak-GPU run

toddhow decided on 2026-11-04: option (b) on the minimal and low tiers only (medium and high unchanged), and `[WorldLod] map view` stays off by default (to be offered later by the first-run setup prompt as a preset, once the weak-GPU run passes and he has flown it). Built, tested and measured in task 0060's stage log, "Far cluster eviction on the low tiers" (commits `e66a3816fb`, `1eafaa6918`): a cluster past the live paging range is replaceable when the ground beneath it has been drawn for 30 frames, so the budget can bound it; the planner is unchanged; 26 new tests. The default game is unchanged (`map view` off; the code runs only with `map view`, a tier that evicts and a budget). The weak-GPU run (criterion 9) is in the same entry: it starts and draws on llvmpipe with the minimal tier; the Windows terrain pack is refused as stale on Linux although its tiles are byte-identical to a Linux bake (the staleness digest is not portable; bake on the machine that runs). Open: toddhow's flight, the tier budgets (the rule is idle at the default 256 / 320 MiB), whether out-of-view cells count as ground.
