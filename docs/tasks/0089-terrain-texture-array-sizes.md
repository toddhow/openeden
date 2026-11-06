# Task: Terrain layer arrays with mixed sizes and formats, and the start-up block (task 0089)

Status: findings and one small fix; the main fix needs toddhow's decision (options below).
Design doc section: none (`components/render/CLAUDE.md`, "The runtime texture downscaler", "Terrain arrays stay one size").
Goal: land textures of a texture mod (Landscape Remastered) draw as themselves, and the game starts without a main-thread block of 5 s or more.
Inherited code touched: `Terrain::Builder::setLayersWithMaps` (`components/terrain/builder.cpp`), `vsgUtil::convertToArrayTexture` (`components/vsgutil/arraytexture.hpp`), `vsgUtil::readArrayImages` (`components/vsgutil/readimage.cpp`).
Out of scope here: an encoder, a BC7 decoder, per-format arrays (decisions below).

## Findings (2026-11-05, RTX 4070, the `main` test profile with `00 core` and `02` of the mod, Faultier data3, full profile)

The task text said the mod has 4096 BC3 `_nh` and 8192 BC3 grass. The files say otherwise (`I:\Projects\OpenEden-work\texmod\ddsinfo.py`): `00 core` has 116 `_nh` normal+height maps at 2048 x 2048 **uncompressed RGBA8 with 12 mips (22.4 MB each)** and one at 1024 (`tx_ma_lava03_nh`); diffuse maps at 2048 (70 DXT1, 43 DXT5, 7 uncompressed) and 6 at 4096 uncompressed; `grass/` is 6 diffuse and 6 `_n` at 4096 uncompressed.

### 1. The 5 s block is the terrain layer build at start-up

- `[Cells] load trace` plus scopes in `setLayersWithMaps` (the scopes `terrain.layers.*` stay in the code): 133 layers, 96 normal maps. The whole call is **2.1 to 2.8 s on a warm file cache, 6.1 s in one run** (`read normals` 1.3 to 5.1 s: 93 x 22.4 MB = 2.1 GB read and decoded on the main thread, with other work on the machine). It runs once, inside the first frame of a new game (`frame.game.run`), so no frame is drawn before it. A frame over 5 s is what the freeze monitor ends (exit 173 in the first, cold run; `openmw-freeze.dmp`).
- Not the owner: `render.groundcover.distant.lod` (compile of distant groundcover and LOD clusters). In one run (a cold shader and LOD cache) it took 3.9 s and 2.3 s in two frames about a second apart (three `compile.vsg` calls of 0.9 to 2.2 s on the main thread), and `loop.record.submit` 1.1 to 1.8 s; in the next runs it did not appear. It is a second, separate stall of the same size; its three callers now have their own scopes (`render.update.groundcover`, `.distantobjects`, `.lodclusters`) so the next occurrence names its owner. Not investigated further.
- LOD terrain tiles did not build a second set of layer arrays in this profile (one `terrain.layers.total` per run).

### 2. Two kinds of layer are lost

- **Normal maps (the log lines):** the normal array keeps the most common size and format (here 2048 RGBA8). `tx_ma_lava03_nh` (1024 RGBA8) and three maps from the other packs (`tx_dirtroad_01_n`, `tx_bm_rock_02_n`, `tx_bm_rock_03_n`: probably the three format-145 (BC7) images read; the key was not printed next to each path) were dropped with "differs in size or format". Their layers are drawn without a normal map.
- **Diffuse maps, much larger (found now):** `convertToArrayTexture` takes the most common key of the 133 diffuse layers, BC1 2048 (93 layers), and fills every layer that cannot be copied or decoded **grey**: all 35 BC3 layers, the uncompressed ones, and every land texture the mod does not replace (Bloodmoon and vanilla textures at their own size). **79 of 133 layers are grey-filled** (`!convertToArrayTexture(i=..., name=...): ... using a grey fill` in the log; 79 unique names). In the Seyda Neen frames (`I:\Projects\OpenEden-work\texmod\frames`) the land is partly red; whether that is these grey layers or something else was not investigated (it is the same with `texture max size terrain = 256`, where 40 layers are still grey).

### 3. What the texture cap does

| Setting | `terrain.layers.total` | grey diffuse layers | normal maps dropped |
| --- | --- | --- | --- |
| none | 1.8 to 4.1 s (6.1 s once, before the change) | 79 | 4 |
| `texture max size = 1024` (global, sequential read) | 4.8 s | 79 | 4 |
| `texture max size terrain = 1024` | 2.5 s | 79 | 3 |
| `texture max size terrain = 512` | 0.41 s | 79 | 3 |
| `texture max size terrain = 256` | 0.42 s | 40 | 3 |

(Runs after the change keep the 1024 map, so 3 are dropped; before it 4.) A cap of 512 or lower removes the start-up block, and 256 also halves the grey layers (the vanilla 256 BC1 and the mod's BC1 then meet), at the price of the mod's resolution. 2048 changes nothing (the mod is 2048). A cap of 1024 shrinks the uncompressed maps on the CPU, which costs as much as it saves.

## What changed

1. `vsgUtil::arrayLayerConvertible` (`arraytexture.hpp`): a pure rule, kept in step with `convertToArrayTexture`'s branches, for which layers an array takes exactly: the same format at another size when uncompressed with 4 bytes a texel (resampled), BC1/BC2/BC3 in an RGBA8 array (decoded and resampled). The builder's three pre-filters (normal, material, height) use it, so **`tx_ma_lava03_nh` (1024 RGBA8) is no longer dropped**; BC7 stays dropped. Only a layer whose key is rarer than the reference's is kept, so the array picks the same reference as the builder.
2. `vsgUtil::readArrayImages` reads its files on up to 8 threads (`arrayReadThreadCount`; result order and the first-failure-in-path-order exception kept). Measured: **no gain for the uncapped read** (1.2 to 3.6 s against 1.4 to 2.8 s with one thread: disk and memory bound, and noisy while other work runs); the capped read (CPU shrink) went from 4.3 s to 2.2 s (`= 1024`).
3. Trace scopes `terrain.layers.total/.read.normal/.read.diffuse/.arrays` and the three `render.update.*` scopes.
4. Tests: `apps/components_tests/vsgutil/testarraylayers.cpp` (the layer rule and the reader's concurrency tests were red against stubs first).

**Problem 1 is not fixed**: the build is still one synchronous call of 2 s or more on the first frame, and 6 s when the disk is busy.

## Options for toddhow (decisions)

A. **Start-up block.** (a) Document the cap as the workaround (`texture max size terrain = 512` or `256`, or `texture memory budget`), nothing else. (b) Build the layer arrays on a worker thread during the new-game loading screen (the world and terrain exist only after the store is set; a design change in `RenderManager::setStore`). (c) Keep the window's message loop alive during the read (a pump callback; hides the freeze, not the time). (d) Auto-cap terrain when the normal array would exceed a size (for example 1 GiB), as `texture memory budget = auto` does for the objects; today `auto` left the terrain uncapped on the 12 GiB card.
B. **Grey diffuse layers.** A format policy is needed; each costs something. (1) Transcode BC3 to BC1 (the colour block is a BC1 block; lossless for colour) and drop top mips of larger BC1: fixes the 35 BC3 layers and any larger layer, no encoder. (2) A BC1 encoder for the rest (smaller vanilla layers upscaled, uncompressed layers): about 150 lines, lossy, parallelisable, about 1 to 2 s more at start-up unless cached. (3) An RGBA8 array of the largest size: simple and exact, 133 x 2048 x 2048 x 4 x 1.33 = 2.8 GiB of video memory, and a 4096 layer makes it 11 GiB. (4) One array per format and size with a layer flag selecting it: no loss, a shader and descriptor change. (5) Bake a cap-sized pack at first start into derived data (like the LOD bakes): one size and format for every layer, the mod's textures converted once.
C. **Normal maps.** A BC7 decoder (about 200 lines) would admit the three BC7 maps into the RGBA8 normal array; or keep them dropped (they are three of 96).

## Not verified

Linux gate; the Lua integration tests; clang-tidy; the groundcover/cluster compile stall (seen once, not isolated); the red land in the frames; a frame showing the 1024 `_nh` layer (no lava ground near the start; a unit test covers the rule and the log line for lava03 is gone); a run on a cold file cache after the change (the first, cold run was the 5 s one before it).

## Stage log, second round (toddhow's decisions of 2026-11-05)

Decisions: (1) build the layer arrays on a worker thread; (2) transcode BC3 to BC1 losslessly; find why the land is red; report what stays grey (no encoder yet).

### The land was red because of the grey fill itself

`convertToArrayTexture` fills a layer it cannot convert with bytes of 0x80. In a BC1 array a block of 0x80 bytes has both endpoints 0x8080, which is RGB565 r 16, g 4, b 0: **(131, 16, 0), a dark red**. All 79 grey-filled layers of the BC1 array were drawn red, not grey (the test `BlockConvert.the_old_byte_fill_of_bc1_was_red` decodes it). In an uncompressed array 0x80 is a real grey, which is why only the mod's compressed array showed it. Evidence: the same code, the same frames with the fill replaced by a valid grey block (`vsgUtil::BlockConvert::fillGrey`: BC1 endpoints 0x8410, BC2/BC3 opaque alpha) show no red (`frames/after_final.png` against `frames/before_base.png`). Not a swizzle and not a wrong-format read.

**The fix is written but not switched on:** `convertToArrayTexture` still fills with 0x80, because three existing tests pin those bytes (`VsgUtilArrayTextureTest.MostCommonSizeWinsOverFirstLayer`, `.MostCommonCompressedFormatWins` and `.LayerWhoseLayoutPointsPastDataSizeIsGrey`, through the helper `expectBc1LayerGrey` in `testarraytexture.cpp`, which expects every byte 0x80 in a BC1 layer). I may not edit them: with toddhow's approval, change that helper to accept a grey BC1 block (endpoints 0x8410, indices 0) and replace the `std::memset` in the grey branch of `convertToArrayTexture` with `BlockConvert::fillGrey(properties.format, ...)` (a comment at the line says so); the disabled test `BlockConvertArray.DISABLED_a_bc7_layer_is_a_grey_block_not_red` then runs. The frames `after_final.png` (no red) were made with the fill switched on in a build that was then reverted; the committed build still draws the 44 remaining layers red.

### BC3 to BC1

`vsgUtil::BlockConvert::bc3BlockToBc1` (`components/vsgutil/blockconvert.hpp`): the last 8 bytes of a BC3 block are a BC1 colour block that is always read in four colour mode; a BC1 block is in four colour mode only when endpoint 0 is greater than endpoint 1. Copied as it is when it is; otherwise the endpoints are swapped and every index's low bit flipped (0<->1, 2<->3: the same four colours, byte xor 0x55); equal endpoints become index 0 everywhere. Every mip level is converted (`convertToArrayTexture`, when the array is BC1 and a layer is BC3 of the same size with the levels the array has). Alpha is dropped. Terrain diffuse alpha is read only for the layer's specular strength when the layer was found as a `_diffusespec`-pattern file (`terrain/main.frag`, `SPECULAR_FLAG`); such a layer would lose that alpha. Before it was grey (red), so nothing is lost, but it is not specular either. The test decodes 3000 random BC3 blocks (a seventh with equal endpoints) and the transcoded BC1 and checks every texel's RGB is identical. 14 tests in `testblockconvert.cpp`; red first (6 failed against the stubs), green after.

### What stays grey (mod + Faultier data3 + vanilla, `main` profile): 44 of 133 diffuse layers (79 before)

| Layers | What | Count |
| --- | --- | --- |
| Vanilla and Bloodmoon textures the mod and Faultier do not replace (in the BSAs) | DXT1 at their own size (the load shows 34 layers with 9 mip levels, 256 x 256, and 5 with 6 levels, 32 x 32) | 37 |
| Faultier data3 | DXT1 256 x 256 | 5 |
| the mod | uncompressed RGBA8 2048 | 2 |

So a BC1 encoder (decode, upscale to the array size, encode, with the mips regenerated) would cover all 44; 42 of them are upscales of a 256 layer, which would be as soft as nearest or bilinear upscaling. The alternative that needs no encoder is the other direction: the texture cap at 256 brings every layer to the vanilla size and leaves only the RGBA8 ones. The BC7 normal maps (3) stay dropped. Not built, as decided.

### The worker thread

`Terrain::Builder::setStorage` now only collects the layer names, the resolved paths, the maps found next to them and the material table's values for every layer (the calling thread: they use the VFS and the material table), then starts `std::async(launch::async)` for the reads, the arrays and the descriptors (`buildLayersWithMaps`; the plain branch the same). `Builder::create` (which every batch goes through, from any thread) calls `waitForLayers()`, a no-op once the build is done, which rethrows what the build threw; `~Builder` and a second `setStorage` wait first. The main thread does not wait on its own account: `RenderManager::updateTerrain` returns while `mTerrain->layersReady()` is false (the terrain follows in the first frame after the build, and the loading screen keeps drawing: 900 to 1,900 frames before the first terrain). Threads: the build touches only members nothing else reads until `waitForLayers()` returned (the descriptors), reads through `vsg::read` with the shared options as the model loader threads do, and creates VSG objects but compiles and uploads nothing (the compile is the main thread's, as before). The LOD tiles' own builder is the same code on its worker; `DeletionQueue` is not involved (the build owns what it creates and the descriptors go to the batches by `ref_ptr`).

**Still blocking the main thread:** the first exterior local map request (`Map::requestExteriorMap` -> `Paging::updateViewForCell` -> `Builder::createBatch`) comes from the cell grid change (`grid.window.changecell`) and waits for the build: 0 to 1.6 s of the 2 to 5 s the build takes (it is not retried if skipped: `LocalMapBase::updateMap` asks once). Deferring it needs a retry in the map code (not done).

### Measurements (load trace; frames = main thread; mod = Landscape Remastered 00 core + 02 + Faultier data3; the same executable with `TMP_SYNC_LAYERS` making the build synchronous = before)

| Run | Longest main-thread frame | Main thread waiting on the build | Build (`terrain.layers.total`, worker) | Last frame over 200 ms ends at |
| --- | --- | --- | --- | --- |
| mod, synchronous x3 | 3.04, 3.93, 2.46 s (the build itself) | 2.85, 3.74, 2.26 s | 2.3 to 3.7 s | 7.4, 6.6, 5.2 s |
| mod, worker x5 | 2.15 s (first), then 2.72, 1.07, 2.29, 2.0 s | 0, 0.17, 0.67, 0.90, 1.62 s (map request) | 2.8 to 5.6 s | 4.0 to 12.3 s |
| Faultier only (no mod), synchronous x2 / worker x2 | 0.79, 0.78 / 0.78, 0.78 s | 17 to 173 ms / 0 to 19 ms | 5 to 173 ms | the same |
| pure vanilla, synchronous x2 / worker x2 | 0.76, 0.77 / 0.75, 1.02 s | 15, 5 ms / none | 5 to 15 ms | the same |

The worst main-thread frame with the mod went from 2.5 to 3.9 s (6.1 s once before, in a disk-bound run) to 1.1 to 2.7 s, none of it the build except the map request's wait; what remains in those frames is cell loading (`grid.total` up to 1.3 s, `insert.objects`) and `loop.record.submit` (1.2 to 1.5 s): other existing stalls. The build itself got longer on the worker (2.8 to 5.6 s against 2.3 to 3.7 s), because it now runs beside everything else that loads and the machine was shared; the time to a settled scene is not worse (last frame over 200 ms at 4.0 to 12.3 s against 5.2 to 7.4 s) but it varies widely between runs, so no gain in total load time is claimed. Vanilla is not slower (the build is 5 to 170 ms and the frames equal). The terrain appears when the build ends instead of with the first frame.

Frames viewed: `I:\Projects\OpenEden-work\texmod\frames\` `before_base.png` (red land), `after_final.png` (no red; the ground is the transcoded and grey-filled layers).

### Not verified (second round)

Linux gate; the Lua tests; clang-tidy; the async build has no unit test (the builder needs a device: it is shown by the trace only); a failing build (a missing texture) now throws from `create` on whichever thread builds the first batch instead of from `setStorage`; the editor (`apps/opencs`) uses the same Paging/Builder and was not run; a run where the mod's grey layers matter (the lava ground) in a frame; the player seeing the terrain appear late at the end of a loading screen (no frame of the first seconds was viewed); a cold file cache.

## Stage log, third round (2026-11-06: the BC1 encoder, the BC7 decoder, resizing; the map request no longer waits)

### Counts before (the build of the second round, mod `00 core` + `02` + Faultier data3, profile copy `I:\Projects\OpenEden-work\bc1\prof`, new game in Seyda Neen)

One Info line per layer build: `Terrain layer arrays: diffuse 133 (...); normal ...; dropped ...`. Before (`OPENEDEN_TERRAIN_REENCODE=0`, a test aid that switches the new paths off): diffuse 133 = 54 copied, 35 BC3 transcoded, **44 grey**; normal 93 of 96 (3 dropped); material 1 and height 1 dropped. The reasons (the log now prints each layer's `VkFormat` and size): the 44 grey diffuse layers are 39 BC1 at 256x256 (vanilla and Bloodmoon textures the mod does not replace, Faultier) and 5 uncompressed RGBA8 at 2048 (the mod); the three dropped normal maps are BC7 (format 145: `tx_dirtroad_01_n` 2048, `tx_bm_rock_02_n` and `_03_n` 256) in an RGBA8 2048 array; the material map is a BC1 2048 in a BC1 256 array, the height map a BC4 512 in a BC4 256 array.

### What was built (test-first; red run: 32 of 42 new tests failed against stubs, the 10 that passed were trivial size and copy cases)

- `components/vsgutil/bc7decode.hpp` (+ two partition tables as `.inc`): a full BC7 decoder, modes 0 to 7. The partition tables were READ OUT of the D3D11 reference decoder with blocks that show each texel's subset (`I:\Projects\OpenEden-work\bc1\parts.py`, `d3dbc7.cpp`); the anchor tables are from memory and checked by the same oracle. Test: 4096 pseudo-random blocks of each of the 8 modes hash to the D3D11 values (WARP and the RTX 4070 agree), plus hand-built blocks for modes 1, 3, 4, 5, 6 and the reserved mode. It matched the oracle on the first run.
- `components/vsgutil/bc1encode.hpp`: bounding-box diagonal start, two least-squares refinements, RGB565 quantisation, nearest of four palette entries; deterministic, no allocation. PSNR on synthetic images: gradient 37.9 dB (floor 37.0), two-colour checker above its 40 dB floor, uniform noise 13.6 dB (floor 13.0; a one-line fit of isotropic noise is near 12.5 dB). 2048 x 2048 of a synthetic image in 80 ms on one thread (RelWithDebInfo). The test floors were set after the first measurement (the first guesses 38 and 20 were wrong for this kind of image).
- `components/vsgutil/rgbaresize.hpp`: separable resize, box average on an axis that shrinks, linear between texel centres on an axis that grows, integer maths, one rounding.
- `vsgUtil::ArrayConvertOptions` / `ArrayConvertStats`, `convertToArrayTexture(images, options, stats)`, `arrayLayerConvertible(layer, reference, options)`: **opt-in** (`reencode`, default false), so the three tests that pin the 0x80 fill and the BC7-is-not-convertible tests are untouched; no existing test was edited. With it, a BC1 array and an RGBA8 array take every layer that decodes to RGBA8 (BC1, BC2, BC3, BC7, RGBA, BGRA, RGB, BGR with 8 bits): decoded, resized to the array's size and, for a BC1 array, encoded with its own mip chain (box halving, the array's level count), on up to 8 threads (one layer per job, each writes only its own bytes; the same result for 1 and 4 threads: test). Layers that already fit keep their copy and the lossless BC3 transcode (same bytes with and without the option: test). A layer that still cannot be read gets a real grey block (`BlockConvert::fillGrey`), not 0x80. `encodeToBc1 = false` (the builder's normal, material and height arrays) lets only BC1 sources into a BC1 array, since those arrays carry data in alpha (its test was written after the code).
- Builder: all arrays use the option (`OPENEDEN_TERRAIN_REENCODE=0` turns it off), the pre-filters use `arrayLayerConvertible` with it, the drop messages name both formats and sizes, and one Info line per build counts copied, transcoded, encoded, decoded, resized and grey layers per array, the dropped maps and the times.
- Array formats are unchanged: diffuse BC1 as before, normal, material and height keep the array's own format. **Memory change: none** by construction (same arrays and bytes; layers that were filled now hold pictures); the driver's `heapUsage` was not measured.

### After (3 runs, same profile)

`diffuse 133 (copied 54, transcoded 35, encoded 44, grey 0); normal 96 (copied 92, decoded 3, resized 1, grey 0); dropped normal 0, material 0, height 1`. The one layer still dropped is the BC4 `tx_dirtroad_01_p` (512) in a BC4 256 array: BC4 needs its own encoder, not asked for. Diffuse conversion 1.25 to 1.35 s on the worker (0.10 s before), whole build 6.5 to 6.8 s (5.7 s before; the worker builds beside everything else that loads). The longest main-thread frame is the first frame at start-up, 1.9 to 2.0 s, as before (1.9 s); nothing in the layers blocks the main thread. 42 of the 44 re-encoded diffuse layers are 8 times upscales of 256 textures, as soft as bilinear upscaling.

### Part 2: the exterior local map no longer waits

`Map::requestExteriorMap` returns (without clearing `needUpdate`) while `Paging::layersReady()` is false; the map window asks again every frame while a cell has no map texture (`LocalMapBase::updateRequiredMaps`), so no retry code was needed. A run logs `Local map: 119 exterior map requests were deferred until the terrain layers were ready; cell (-2, -9) is rendered now`; the load trace shows `terrain.layers.wait` only on a worker (0.3 to 2.8 s) and none on the main thread (before this change: 1.25 to 1.45 s on thread 0). The exterior HUD minimap shows no picture in this build with or without the change (an open fault from earlier tasks), so the drawn map could not be viewed; that it is rendered is the log line and the unchanged code after it.

### Frames

Seyda Neen (frame 1500 and 3500) and a straight-down view of Ald'ruhn's ground with and without the option (`I:\Projects\OpenEden-work\bc1\al0.png`, `al1.png`): no grey or red in either, they differ in a few pixels; those spots use layers that already fit, so they do not show the change. A frame of the lava ground the mod replaces was NOT obtained (the start cells reachable by `--start` were over water or flat ash). The evidence for the 44 layers is the counts and the unit tests, not a frame of the ground.

### Not verified, decisions

Not run: the Linux gate (GCC -Werror), the Lua tests, clang-tidy, `openmw-tests`; the editor (same Builder); a cold file cache; a mod with 4096 diffuse layers in game (the resize and encode of that size is in tests only). BC7: random blocks hit every mode and partition in practice, not proven for all. Decisions for toddhow: (1) the reference size is still the most common one, so a mod that covers under half the layers makes the array 256 and shrinks its textures; (2) a BC4 encoder for the last height map; (3) 8x upscaled 256 layers cost 2.8 MB each in the array, a per-layer scale in the shader would avoid it (a shader change); (4) `OPENEDEN_TERRAIN_REENCODE=0` is a test aid, not a setting.

## Stage log, fourth round (2026-11-06: BC4 height layers of another size join the height array; toddhow: "bc4 yes")

Built test-first: `components/vsgutil/bc4.hpp` (decoder for both modes, deterministic encoder that tries the 8-value mode on min/max and the 6-value mode on the min/max of the texels that are neither 0 nor 255, two least-squares refinements each, lowest error wins; no allocation), `components/vsgutil/r8resize.hpp` (one-channel resize, equal to `RgbaResize` on four equal channels: test), and in `arraytexture.hpp` a BC4 branch: `arrayLayerConvertible` accepts a BC4 layer of any size in a BC4 array with `reencode`; `convertToArrayTexture` decodes, resizes, rebuilds the mip chain (box halving, the array's level count) and encodes (`detail::encodeBc4Layer`, same worker threads); a BC4 layer that cannot be read gets a real grey block (128, 128, index 0) instead of 0x80 bytes (only with `reencode`). Layers that fit are copied as before (test: same bytes with and without the option), BC1/BC2/BC3/BC7 and RGBA arrays are untouched, BC4 does not enter a BC1 or RGBA array and other formats do not enter a BC4 array (tests). The builder needed no change: its height pre-filter already asks `arrayLayerConvertible`, and the stats line counts the BC4 layer as `encoded`. 25 new tests (`testbc4.cpp`, `testbc4array.cpp`); red run against stubs: 20 failed, 5 passed (the negative cases); after: all pass; whole `components-tests` 6630 pass (run on the build before clang-format and before three PSNR floors were set to their final values, see below).

Measured PSNR (64x64 synthetic): gradient 53.2 dB (floor 52), two-value checker exact (floor 90), uniform noise 30.4 dB (floor 29). One layer 512 to 256 with 9 levels: decode, resize and encode 2.3 ms on one thread (`bench.cpp` in the work folder, cl /O2).

Real data (`I:\Projects\OpenEden-work\bc4\prof`, copy of the bc1 profile: Landscape Remastered 00 core + 02 + Faultier data3, Seyda Neen start): `OPENEDEN_TERRAIN_REENCODE=0` (before): `dropped normal maps 3, material maps 1, height maps 1`, height 60 layers. Now, 3 runs: `dropped normal maps 0, material maps 0, height maps 0`, **height 61 (copied 60, encoded 1)**, material 61 (encoded 1, a BC1 map the earlier rounds already handled), builds 8.8, 7.8 and 7.1 s on the worker (the third round measured 6.5 to 6.8 s; the machine was shared, the BC4 layer's own cost is 2.3 ms, so the difference is noise and not attributable). Longest main-thread frames in the three traces: 1.9 s (first frame at start-up, as in round three), 1.4 s, 1.6 s; `terrain.layers.wait` only on the worker (48 and 74 ms on the main-thread-less trace lines, as before). The off run (`REENCODE=0`) is not a like-for-like timing baseline (it also skips the 44 diffuse re-encodes).

Frames: `bc4\shots\down_Balmora.png` (350 units up, parallax on in the profile): terrain draws with no grey or red; I could not tell that the spot uses `tx_dirtroad_01` and my low oblique camera script pointed into the player model, so **a frame that shows the dirt road's parallax was not obtained**.

BC5 (two-channel normals): the decode and encode are two BC4 blocks (R then G) per 16-byte block, so `Bc4::decodeBlock`/`encodeBlock` do most of it; not built because it also needs a two-channel resize, a `block128` branch in `convertToArrayTexture` (valueSize 16 is BC2/BC3/BC5/BC7 by format) and a decision on the normal array's Z reconstruction; in the mod data all normal maps are BC7 or RGBA8, so nothing in the measured set uses it.

Not verified: Linux gate (GCC -Werror), Lua tests, clang-tidy; the final clang-formatted tests were not re-run (another session's half-edited shader and `testmorrowindunchanged.cpp` stopped `components-tests` from building after the format; the compiled tree before it passed all 6630); a frame of the dirt road's parallax. Decision for toddhow: none new; BC5 only if a mod with BC5 normals turns up.
