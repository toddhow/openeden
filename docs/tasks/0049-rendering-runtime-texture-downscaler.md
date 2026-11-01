# Task: Rendering, a runtime texture downscaler (texture size cap and memory budget)
Design doc section: `docs/research/0018-world-lod-and-streaming.md` and `docs/design/world-lod-and-streaming.md`, sections 5.7 (budgets: "first raise the minimum texture mip globally") and 5.8 (texture mip streaming); `docs/design/eden-archive-format.md`, sections 8 (KTX2, per-mip chunks), 11 (derived data cache) and 14 (streaming requirements); builds on `docs/tasks/0044-rendering-pbr-rmaos-textures.md` (high-resolution PBR sets, the terrain array's size limit) and `docs/tasks/0045-rendering-parallax-height-maps.md`. `docs/decisions/0010-rendering-and-studio-answers-2026-11-01.md`, answer 11 ("raise the array size later at a memory cost, and build a runtime texture downscaler").
Goal: let the player (or the engine, from the graphics card's memory) say "no texture bigger than N texels", and have every opted-in texture loaded at no more than that size without editing any file, by dropping the top mip levels of a texture that has a mip chain and by shrinking one that has none, so that a 2048 or 4096 PBR texture pack runs on a card that cannot hold it, and so that the terrain's layer arrays can later be made bigger than 256 without running out of memory. Off by default and frame-identical when off.
Inherited code touched (to be confirmed at the commit that starts each stage): `components/vsgutil/readimage.hpp` and `readimage.cpp` (`readOptionalImage`, `readImage`, `removeSrgb`), `components/vsgutil/imageio.*` (`readImageFromMemory`), `components/vsgutil/arraytexture.hpp` (`detail::mipLevels`, `convertToArrayTexture`: reads the `MipmapLayout` or `properties.mipLevels`), `components/resource/resourcesystem.cpp` (the `textureOptions` and `imageOptions`: `findFileCallback`, `sharedObjects`, the readers), `components/vsgadapters/nif/nif.cpp` (the calls at `handleSourceTexture` and `addMap`), `components/vsgadapters/dae/dae.cpp` (line 363), `components/terrain/builder.cpp` (lines 140, 172, 234: the layer reads), `components/vsgadapters/mygui/texture.cpp` (line 90: the GUI, which must stay exempt), `apps/openmw/mwrender/{water,rain,waterripples,weather}.cpp` (their own reads, exempt), `components/render/devicereport.*` (`memory.heapN`, `memory.deviceLocalMiB`), `components/settings/categories/rendering.hpp`, `files/settings-default.cfg`, `apps/components_tests/CMakeLists.txt`.
Inputs / outputs: new `[Rendering]` settings (below); a new pure part `components/vsgutil/texturecap.hpp` (the decisions) and `components/vsgutil/textureresize.hpp` (the data operations on a `vsg::Data` in memory); `readOptionalImage` and `readImage` gain an optional category argument that defaults to "no cap", so every existing call behaves as before; no change to any save, Lua API, extension contract, or file on disk.

Status: draft, written 2026-11-01 at toddhow's request ("we can build a runtime texture downscaler"). Nothing is built. Full loop (Track V renderer) when it starts.

## Why, and what it replaces

High-resolution texture sets are common in mods (the local Faultier setup has 2048 by 2048 colour files and 512 by 512 `_p` height files, task 0045; True PBR sets have a colour, a normal and an `_rmaos` file per surface). A 2048 by 2048 BC7 texture with its mip chain is about 5.3 MiB; a 4096 one is about 21 MiB; an RGBA8 file with no compression is four times that. A busy exterior cell can touch hundreds of textures. Today the only way to cut that is to edit files by hand: the local setup cuts the 256 level of the terrain layers with a block copy so that the terrain's array accepts them (`apps/openmw/mwrender/CLAUDE.md`: "a 2048 by 2048 file must be cut to a 256 level first"). The downscaler is that hand edit, done by the game at load, with the user's number.

**Array limits today** (task 0044, `convertToArrayTexture`): the terrain layer arrays keep the most common size and format, and a layer of another size or format is drawn grey. A downscaler that brings every layer to a common cap makes the "most common size" the cap, so mixed-size land texture sets work, and raising the cap later (decision 0010, answer 11) is a settings change with a known memory cost, not a file edit.

## Design, in plain language

**What a mip chain is, and why it matters here.** A texture is stored as a stack of pictures: the full size one, then one half as wide and tall, then half again, down to one texel. The graphics card picks the level that matches how big the surface looks on screen. Most game textures (all of Morrowind's `.dds` and most mod ones) ship with this chain already made. To make a texture "smaller" you do not have to shrink any picture: you simply do not upload the top levels. A 2048 texture with its chain, loaded without the 2048 level, **is** a 1024 texture with a chain, exactly as if the author had made it so, and nothing is recomputed. That is the main path: **drop top mip levels**. It is exact (no new blur, no colour change), cheap (a copy), works for block-compressed formats (BC1 to BC7) without decoding, and it is also what the later mip streaming does (design doc 5.8).

**When there is no chain.** TGA, PNG, BMP and JPG files, and some DDS files, are a single picture. VSG then makes the mip levels on the GPU when the image is uploaded (`properties.mipLevels == 0` means "generate", see `detail::mipLevels`). For those, an uncompressed single-level image can be shrunk on the CPU by averaging 2 by 2 blocks of texels (a box filter) as many times as needed. A **block-compressed image with only one level** cannot be shrunk without decoding and re-encoding it, which needs an encoder (BC7 encoding is slow and an encoder library is a dependency to be licence-checked, `CLAUDE.md` guardrails); the first version leaves such a texture at full size and counts it in a report (how many, how many bytes), because stock Morrowind textures are small anyway and mod textures almost always have mips. A later stage may add decode, resize and re-encode, if the report says it matters.

**Where it happens.** In the one place all image reads already pass through: `vsgUtil::readOptionalImage` and `readImageFromMemory` (`apps/openmw/mwrender/CLAUDE.md` already requires "read images only through them", for the sRGB reason). They gain a *category*, and the decision "how big may this be" is made from the category and the settings. Nothing else in the engine needs to know.

**Categories (the opt-in).** A call that does not give a category is **not capped**, so the UI, the HUD, the loading screen, water, rain, ripples, the sun, the moons, the font and every other texture that does not opt in stay exactly as now ("not touching UI/HUD textures"). Callers that opt in:

| Category | Callers (verify at the stage commit) | Notes |
| --- | --- | --- |
| `Terrain` | `Terrain::Builder` (`builder.cpp` layer, normal and material reads) | Has its own cap setting; the cap is applied to all layers of an array together |
| `Object` | the NIF and COLLADA readers (`nif.cpp` `handleSourceTexture`, `addMap`; `dae.cpp`) | Includes the extra maps (`_n`, `_rmaos`, `_p`) of the same object; they follow the diffuse texture's cap, with a floor so a map is never larger than its diffuse after capping |
| `Actor` | not separable by the loader | The loader does not know a model is a creature or a body part, and deciding that by path prefix is a game rule (the compatibility pack's job, not the engine's). v1 has one `Object` cap for models and actors alike; a later stage can accept a per-model hint from the scene code (`Scene`, where the object is attached) |
| `Ui` | `vsgAdapters::mygui::texture` | Never capped: no setting exists for it |
| `Effect` | water, rain, ripples, sky, particles | Never capped in v1 |

**How big.** Three sources, in order of precedence: a per-category setting, the global `texture max size`, and a memory budget. The result is a power of two (or 0, meaning no cap). A texture whose longest side is already at or under the cap is untouched. Otherwise the number of levels to drop is `ceil(log2(longestSide / cap))`, but never so many that the image falls below one block (4 by 4 for block-compressed formats) or below 1 texel, and never past the end of the chain; if the chain is shorter than the drop (a single-level image) the shrink path above is used.

**The memory budget mode.** `texture memory budget` set to `auto` takes the device report's `memory.deviceLocalMiB` (sum of the device-local heaps, `components/render/devicereport.cpp`, lines 313 to 326, already collected at start) and works out a cap such that the *expected* texture memory fits: budget = a fraction of device-local memory (starting value 35%, the rest being render targets, geometry, shadow maps, the GI bakes and the OS), then the cap is chosen by the table below. It is a static choice made once at start, from the device and the other settings (resolution, MSAA, `hdr`), and logged ("texture cap 1024 from 8 GiB device memory"). A *dynamic* budget (counting live texture bytes and lowering the cap when over) is a later stage and is where this feature meets mip streaming (design doc 5.7: "first raise the minimum texture mip globally"; the downscaler's cap is that global minimum mip, and the same function answers it).

| Device-local memory | Object cap | Terrain cap |
| --- | --- | --- |
| under 3 GiB | 512 | 256 |
| 3 to 6 GiB | 1024 | 512 |
| 6 to 10 GiB | 2048 | 1024 |
| over 10 GiB | no cap | 2048 |

(The numbers are a starting guess and are **not measured**; stage d measures real use on the test machine and corrects the table; they are named constants with a comment saying so, per the coding standard on tuning values.) `auto` never raises a cap above what the user set explicitly.

**What the cap does to quality.** A capped texture looks softer up close. Normal and `_rmaos` maps lose fine detail, so the object rule above keeps them in step with their diffuse texture; a height map (`_p`, task 0045) has a smaller footprint already and is not capped below 256.

**Colour and data pitfalls (the correctness part).**

- **sRGB and linear.** `vsgUtil::removeSrgb` turns sRGB 8-bit formats into UNORM on read because vsgopenmw's shaders work on the stored values (`apps/openmw/mwrender/CLAUDE.md`: "vsgXchange 1.1 tags stb_image textures as sRGB"). The shrink must therefore run **after** `removeSrgb` and average the *stored* values, which is exactly what the GPU's own mipmap generation does for a UNORM image, so the result matches the GPU-generated levels the engine has today. It must not "fix" gamma by averaging in linear light: that would make the shrunk texture differ from today's smaller mips. If a format is still an `_SRGB` format at the point of shrinking (an unhandled one), the shrink refuses and leaves the texture alone (and says so in the report).
- **Block-compressed sRGB** (DX10 DDS tagged `BC7_SRGB`, handled in task 0044) goes through the same function first, so mip dropping sees UNORM.
- **Normal maps.** Averaging unit vectors shortens them. After the box filter the shrunk `_n` is re-normalised (decode `xyz * 2 - 1`, normalise, encode); the alpha channel (height or specular in Morrowind's `_nh` files) is averaged plainly. Which texture is a normal map is known only to the caller, so the category call carries a *kind* (`Colour`, `Normal`, `Data`), set by the NIF reader's `addMap` and the terrain builder. Dropping mip levels needs no kind (the authored levels are already right).
- **Cutouts (alpha-tested textures: leaves, fences).** Averaging alpha makes cutouts thin out and disappear. The shader already has `coveragePreservingAlphaScale` (`default/main.frag`) for the hardware-generated chain; for the CPU shrink, the alpha is rescaled so that the share of texels above the material's cutoff is preserved (the standard coverage-preserving mip rule). The cutoff is a material property the reader knows, so the shrink takes it as an optional argument (default 0.5). Mip dropping needs none.
- **Premultiplied or mask-in-alpha textures** (the moon's `tx_mooncircle`, UI) are in exempt categories.
- **Non-power-of-two sizes.** Level sizes follow `max(1, floor(size / 2))` per level, as in `detail::mipLevels`.
- **Arrays.** The terrain builder applies one cap to all layers of an array, then the existing "most common size" rule runs; if the cap brings two layers of different original sizes to the same size they now join the array instead of being grey.

**Cache interaction.**

- The in-memory cache: `Resource::ResourceSystem`'s `textureOptions->sharedObjects` (a `vsg::SharedObjects`, `resourcesystem.cpp`, line 85) shares a loaded image by file name and options. The capped image is what gets shared under the file name, so two users of one file get the same capped object; the cap is read once at start, so a setting change takes effect after restart (stated in the settings comment). **Verify** how `SharedObjects` keys images (by path and by the `Options` object) before stage c, so that an `Object` read and a `Terrain` read of the same file with different caps do not collide; if they would, the cap is part of the key.
- The disk cache of the archive design (`docs/design/eden-archive-format.md`, section 11): a derived texture is stored in the cache keyed by *source hash plus tool version* (plus here: target size and algorithm id). The first build has no disk cache: dropping levels is a copy, and the read of the whole file still happens (the file is read in full and then cut), so load time is not faster, only memory is lower. The CPU shrink of a single-level image costs real time per texture, which a disk cache would hide; the cache key is defined in stage a so the later cache is compatible, but not built here.
- Streaming (`docs/design/world-lod-and-streaming.md`, 5.7 and 5.8): its texture budget wants "raise the minimum mip globally" under memory pressure, and per-texture mip streaming wants the same operation (create the image with fewer levels). The downscaler is designed as the static first version of both: `TextureCap::levelsToDrop(info, cap)` is the function the streamer will also call, and the `KTX2` pack layout (per-mip chunks, tail mips first) will let a capped load skip reading the dropped levels, which today's DDS reading cannot.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | Primary method | Drop top mip levels of textures that have a chain (exact, cheap, compressed formats work); CPU box-filter shrink only for uncompressed single-level images; single-level block-compressed images stay full size and are reported | Always decode, resize and re-encode (needs an encoder, slow, lossy for compressed formats) |
| D2 | Where | Inside `readOptionalImage` / `readImageFromMemory` with an opt-in category argument, default = no cap | A separate texture manager (a second place images pass through); caps applied at the descriptor creation sites |
| D3 | Settings | `[Rendering] texture max size` (0 = no cap, else a power of two from 16 to 8192; default 0), `texture max size terrain` and `texture max size objects` (0 = use the global one), `texture memory budget` (`off`, `auto`; default `off`), `texture downscale report` (bool, false: writes a summary to the log at exit) | One setting only |
| D4 | Defaults | All off/0: nothing changes for anyone who does not set them | `auto` on by default (surprising for people with large cards; softer textures) |
| D5 | Categories | `Terrain`, `Object` (models and actors together), `Ui` and `Effect` never capped | Actors capped separately (needs a game-side hint, later) |
| D6 | Maps | Extra maps follow their diffuse texture's cap; height maps floor at 256; normal maps renormalised after a CPU shrink | Cap every file independently |
| D7 | Apply order | After `removeSrgb`; stored values averaged (matches today's GPU mips); refuse on an unhandled sRGB format | Average in linear light |
| D8 | The `auto` budget | Static, from the device report's device-local memory and a table of caps (a guess, measured and corrected in stage d); dynamic budget is a later stage tied to streaming | Dynamic from the start |
| D9 | Cutouts | Coverage-preserving alpha on the CPU path; mip dropping untouched | Plain average |
| D10 | Raising the terrain array above 256 | The downscaler provides the cap; the array size itself (`convertToArrayTexture` takes the most common size) needs no change; the memory cost is documented per layer count (below) | Fixed array size with every layer resized to it (a separate change) |

## Memory arithmetic (so the cost is stated, not guessed)

For a texture whose top level is `W by H` texels with bytes-per-texel `b` (BC1: 0.5; BC3, BC5, BC7: 1; RGBA8: 4), the whole chain is about `W * H * b * 4/3` bytes. A terrain layer array holds `N` layers of one size, so it costs `N * W * H * b * 4/3` per array, and the terrain has several arrays (colour, normal, material, height). Example for 300 layers in BC7 (colour only): 256 by 256 is about 25 MiB, 512 by 512 about 100 MiB, 1024 by 1024 about 400 MiB, 2048 by 2048 about 1.56 GiB. (The numbers are arithmetic from the formula, to be put in a test and in the settings comment, not measured values; the stock game has far fewer than 300 distinct land textures, **verify** the count for the user's setup with the report setting.) `Render::TextureBudget::bytesOfChain(width, height, format, levels)` computes it and is what the report and the tests use.

## The maths and the data operations (stage a), no GPU

`components/vsgutil/texturecap.hpp`, namespace `vsgUtil::TextureCap`, pure functions on numbers:

- `int sanitiseCap(int value)`: 0 stays 0; otherwise clamp to 16 to 8192 and round down to a power of two.
- `int levelsToDrop(uint32_t width, uint32_t height, int levelsInChain, int blockSize, int cap)`: 0 if `cap == 0` or the longest side is at or under the cap; otherwise `ceil(log2(longest / cap))`, never leaving a level smaller than one block or one texel, never more than `levelsInChain - 1`; returns the remainder to shrink separately via `levelsToShrink`.
- `int capFromBudget(uint64_t deviceLocalMiB, Category)`: the table above, as named constants with the source comment.
- `uint64_t bytesOfChain(...)` as above.
- `Category effectiveCap(Category, const Settings&)`: the precedence of the settings.

`components/vsgutil/textureresize.hpp`, namespace `vsgUtil::TextureResize`, on `vsg::Data` held in memory (no Vulkan device needed to create or read them):

- `vsg::ref_ptr<vsg::Data> dropMipLevels(const vsg::Data& source, int levels)`: a new `Data` with the lower levels only: the same pixel format and block size, `width()/height()` of the new top level, the matching `MipmapLayout` (for block-compressed images read by vsgXchange) or `properties.mipLevels` reduced, and the bytes of the retained levels copied. `levels == 0` returns the source itself (same pointer), which is what keeps the off path identical.
- `vsg::ref_ptr<vsg::Data> shrinkUncompressed(const vsg::Data& source, int levels, Kind kind, float alphaCutoff)`: for `R8`, `R8G8`, `R8G8B8`, `B8G8R8`, `R8G8B8A8`, `B8G8R8A8` UNORM (the formats `removeSrgb` produces); box filter per level; `Kind::Normal` renormalises; coverage-preserving alpha for formats with alpha when `alphaCutoff` is given; any other format returns null (not shrinkable).
- `struct Report { uint32_t dropped, shrunk, leftBig; uint64_t bytesBefore, bytesAfter; }` and a thread-safe accumulator (an atomic or a mutex; loaders run on several threads, `apps/openmw/mwrender/CLAUDE.md`), written to the log if `texture downscale report` is on.

Generated test images only; generated DDS bytes as in task 0045 for the reader path.

## Stages

- **a. Maths, data operations and tests**, no engine change: the two headers, `testtexturecap.cpp` and `testtextureresize.cpp`, written from this spec first by a separate test-writing agent.
- **b. Wiring.** The category and kind arguments on the read functions, the settings and their sanitising, the opt-in at the NIF and COLLADA readers and the terrain builder, the report; with all settings at their defaults a read returns the same `Data` pointer as before.
- **c. Terrain arrays and object maps.** The common cap for a terrain array, the map-follows-diffuse rule, `SharedObjects` key check; frames: a 2048 set capped to 1024, 512, 256 compared with the uncapped frame and with the hand-cut 256 setup (looked at; the difference is softness, not colour).
- **d. The `auto` budget.** The device-report figure, the table, a measurement of real texture memory on the test machine (how: the sum of `bytesOfChain` over loaded images, plus the driver's budget if `VK_EXT_memory_budget` is available, which the device report does not list today: **verify** and add it to the report if wanted), table corrected, a note in `docs/research/` with the GPU named.
- **e. CPU shrink quality.** Normal renormalising, cutout coverage, the report of single-level compressed textures left at full size, with real files from the local setup (not committed).
- **f. Later, not in this task:** the dynamic budget and mip streaming (world LOD spec), the disc cache of shrunk textures (archive design section 11), decode-resize-encode of single-level compressed textures, a per-model actor hint.

## Constraints

- Off means unchanged: with every new setting at its default, `readOptionalImage` and `readImageFromMemory` return exactly what they returned before (a unit test that the same `Data` pointer comes back when no cap applies), and frames equal the commit before by `diffmap` (Seyda Neen noon and night, the guild interior, Balmora).
- UI and effect textures are never changed: a test that a call without a category does not go through the resize code at all.
- Determinism: the output is a pure function of the input image, the cap and the kind; no randomness, no threads inside the filter, same bytes on every run.
- Threading: reads happen on loader threads (the NIF reader, terrain builder and preloader); the functions allocate their own result and share no state except the report accumulator.
- Allocation: loading is not a per-frame path; but the filters must work level by level without whole-image temporary copies bigger than one level plus the result.
- No heap use per frame: nothing runs per frame; it is all at load.
- No game files in the repository; generated images only.
- The editor (`SceneHost`, the mesh viewer of task 0043) reads textures through the same functions with no category, so it is unaffected; a per-document cap may be added later if toddhow wants it.
- Works with `hdr`, `gbuffer`, `pbr material maps`, parallax, antialiasing 0 and 4.

## Acceptance criteria

- [ ] `TextureCap` tests: `sanitiseCap` limits and rounding; `levelsToDrop` for square, non-square, non-power-of-two, block-compressed (never below one block), short chain, cap 0, cap larger than the texture; `capFromBudget` for each band and at the edges; `bytesOfChain` against hand-computed values.
- [ ] `TextureResize` tests: `dropMipLevels` of a generated BC1/BC7 and RGBA8 image with a full chain returns the right sizes, `MipmapLayout` and bytes (the retained bytes equal the source's lower levels exactly); level 0 returns the same pointer; `shrinkUncompressed` of a known pattern gives the box average, keeps a uniform image uniform, renormalises a normal map, preserves the alpha-test coverage within a stated tolerance, and returns null for an unsupported format; a texture left unshrinkable is counted in the report.
- [ ] A generated DDS (legacy header and DX10 header, BC1, BC7, RGBA8 with mips) read through `readOptionalImage` with a cap has the expected size and format, and with no cap is identical to the uncapped read.
- [ ] With all settings default, frames equal the commit before by `diffmap`; the UI is identical with caps set.
- [ ] With `texture max size = 1024`, the Faultier wall (2048) loads as 1024 and looks softer, not different in colour or darker (frames viewed); the log report lists counts and bytes before and after.
- [ ] The terrain layers, mixed sizes, with a common cap form one array and none is grey (frame).
- [ ] The cost note: load time with and without the cap on the same save, and texture memory before and after, with the GPU named.
- [ ] Linux gate (`-Werror`), whole-tree clang-format, `check_no_osg.sh`, `components-tests`, `openmw-tests`, `eden-studio-tests` and the 32 Lua tests pass; a fresh reviewer read each stage with extra attention to sRGB handling and to thread safety of the report.
Out of scope: mip streaming and the dynamic budget, a disk cache of derived textures, re-encoding compressed textures, KTX2 or Basis, GPU-side resizing, frame-time adaptive quality, geometry or LOD budgets, a per-actor cap, UI scaling, the editor's own texture settings.
