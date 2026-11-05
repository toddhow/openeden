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
