# Task: PBR material textures in the True PBR `_rmaos` layout, on objects and terrain
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 4 (F4); builds on `docs/tasks/0027-rendering-f4-material-model.md` ("Tier 3, optional extra texture files"), `0029-rendering-ao.md` (the AO pass excludes the terrain) and `0037-rendering-wetness-and-puddles.md`.
Goal: let `[Rendering] pbr material maps` read the packed `_rmaos` files of Community Shaders' "True PBR" layout (and still `_orm`) for objects and for terrain layers, with the per-set roughness and specular scales, so toddhow can test the material model with Faultier's PBR ground textures from his Skyrim install, local only.
Inherited code touched, at commit `0bcd7a99cf`: `components/vsgadapters/nif/automaps.*` (`AutoMapSettings`, `findAutoMaps`), `components/vsgadapters/nif/nif.cpp` (`handleAutoMaps`, `createMaterialData`), `components/terrain/layermaps.*` (`findTerrainLayerMaps`, `TerrainMapSettings`), `components/terrain/builder.cpp` (`setLayersWithMaps`), `components/pipeline/terrain.*` (`terrain()`), `components/pipeline/mode.hpp`, `components/pipeline/graphics.cpp` (mode names), `components/render/materialmath.hpp`, `components/render/materialoverrides.*`, `components/vsgutil/readimage.cpp` (`removeSrgb`), `files/shaders/lib/material/pbr.glsl` (`pbrApplyMap`), `files/shaders/lib/material/data.glsl` (`Material::align_dummy2`), `files/shaders/sky/sun/sun.slang` (its copy of `Material`), `files/shaders/default/main.frag`, `files/shaders/terrain/main.frag`, `files/shaders/terrain/bindings.glsl`, `apps/openmw/mwrender/rendermanager.cpp` (terrain map settings), `files/settings-default.cfg` (comments only).
Inputs / outputs: no new setting; `openeden.material` v1 files gain two optional keys per entry, `roughnessScale` and `specularLevel` (old files parse and write exactly as before); new constants and pure functions in `Render::Pbr`; one more terrain texture array (binding 8) and one more storage buffer (binding 9) in the terrain descriptor set, only when the material maps are on; no change to any save, Lua API, extension contract or existing file format.

Status: draft written 2026-11-01 at toddhow's request. Full loop (Track V renderer).

## What the True PBR layout is (verified, not assumed)

The layout is a community convention, not a standard (wiki: `github.com/alandtse/open-shaders/wiki/True-PBR`). For a colour texture `<name>.dds` the set is `<name>_n.dds` (normal), `<name>_p.dds` (height), `<name>_rmaos.dds`, and a JSON in `PBRTextureSets/` with `roughnessScale`, `displacementScale`, `specularLevel` (and, on some sets, `subsurfaceColor`, `subsurfaceOpacity`).

I decoded the top mip of Faultier's AIO files (Python, BC1 decoder in the scratchpad): `_rmaos` are BC1 (DXGI 71, UNORM, linear), 2048 by 2048 with a full mip chain; colour files are BC1 or BC7 with an sRGB DX10 header (DXGI 72, 99); `_n` are BC7 UNORM (98); `_p` are BC4 with the legacy `BC4U` code at 512. Value ranges of `_rmaos` (mean R,G,B,A out of 255):

| Set | R | G | B | A | Reading |
| --- | --- | --- | --- | --- | --- |
| `sdirt01` (dirt) | 207 (156 to 238) | 0 (0 to 20) | 169 (0 to 255) | 255 | rough, no metal, AO varies |
| `sgrass01` (grass) | 230 | 0 | 182 | 255 | rougher still |
| `whstonefloor` | 195 | 0 | 234 | 255 | rough stone, little AO |
| `smetaldet01` (metal) | 105 | 103 (0 to 255) | 105 | 216 | smooth, metallic where G is high |

So R is roughness, G metalness, B ambient occlusion (0 = occluded), as the layout says. The A channel is 255 everywhere in BC1 files (BC1 has one alpha bit), so it can only mean a multiplier of one: **F0 of a non-metal = `specularLevel` (the set's JSON, default 0.04) times A**. That is the reading the data allows; a value of 0.04 in A would make F0 = 0.04 on files that hold 1.0. Where a BC1 file has a punch-through alpha of 0 (the `smetaldet01` sample has 13 percent, with black RGB there), F0 is 0 on those texels; accepted and stated.

## Decisions (recommended first; toddhow can change)

- D1. Naming: `<diffuse name>_rmaos` is the documented default and is looked for first; `<diffuse name>_orm` stays supported (task 0027's proposal, unchanged). A model or terrain layer uses at most one. The `_orm` constants stay as they are (the existing tests pin them); `_rmaos` gets a new constant.
- D2. Per-set scales come in through the existing tier 2 file, not by reading Skyrim's `PBRTextureSets` at run time: that folder is keyed by texture set name from Skyrim's plugins, which Morrowind does not have. `roughnessScale` multiplies the map's roughness (clamped to 0.045 to 1); `specularLevel` sets the dielectric F0 before the map's A multiplies it (it is stored as `reflectance = sqrt(F0 / 0.16)`). A local script converts the Skyrim JSONs into one `openeden/materials/*.json` file.
- D3. Parallax and displacement: `files/shaders/lib/material/parallax.glsl` exists and the terrain already offsets by the alpha of a `_nh` normal map, but Faultier's height is a separate `_p` file in BC4 (legacy `BC4U`). Reading `_p` and `displacementScale` is **not built**; logged here.
- D4. Terrain with `pbr material maps`: the terrain takes the per-pixel path of its normal maps for every layer, looks for `_n` and `_rmaos` next to each layer's diffuse texture (independent of the `[Shaders]` terrain normal map settings), and lights with the material model (GGX over the same light grid and sun, `PBR_LIT`'s functions). A layer with no `_rmaos` gets the tier 1 terrain values (roughness 0.95, metalness 0, F0 0.04, occlusion 1). Terrain does not take tier 2 overrides for roughness or metalness absolutely (only the two scales, per layer texture path); stated.
- D5. Terrain occlusion: the AO pass reads a stored ambient part that the terrain writes as zero, so it cannot occlude the terrain. The `_rmaos` B channel therefore scales the terrain's ambient light directly in the terrain shader (`materialTermsScaled`), in both pbr looks. The AO pass is not changed. With `gi exterior` the scale is applied to the field's result.
- D6. Terrain layer arrays have one size and format (`convertToArrayTexture`: the most common one; a layer of another block-compressed format or size is filled grey). Stock Morrowind land textures are 256 by 256 DXT1 (to be confirmed in the stage log), so a 2048 by 2048 Skyrim file cannot be a layer as it is. The local setup script therefore cuts the matching mip level from each file (a block copy, no recompression) and writes legacy-header files. The terrain shows the PBR layers at the array's resolution; a higher one needs every layer at that size (memory) and is not built.
- D7. sRGB: `vsgUtil::removeSrgb` handles only 8-bit formats. A DDS with a DX10 header in `BC1/2/3/7_*_SRGB` would be converted to linear by the sampler and again by the shader. It is extended to the block-compressed sRGB formats. Stock Morrowind DDS files are legacy DXT1/3/5 (UNORM) and unchanged. `_rmaos` and `_n` files are never sRGB-tagged by their writers; a tagged one is stripped the same way (they must be read linear).

## Constraints
- Off means unchanged: with `pbr material maps` false nothing is looked for, no define, no binding, no byte of the terrain descriptor set differs; the default frame is identical (compared with `shot.sh` and a diff map).
- No GPU, window, network or game data in unit tests: DDS bytes and channel values are generated in the test.
- Threading: material scales are resolved by the NIF loader threads from the `OverrideTable` as today; the terrain builder runs where it ran.
- No texture or mod file enters the repository. The setup is in the scratchpad.
- `Pipeline::Data::Material` keeps its 96 bytes: the dummy float `align_dummy2` carries the roughness scale (0 means 1); it keeps its name because the Slang copy in `sun.slang` is not this task's.

## Acceptance criteria
- [x] `findAutoMaps` returns an `_rmaos` file in preference to an `_orm` file, sets the layout, and finds nothing with the setting off; the old tests pass unchanged.
- [x] `Render::Pbr` decodes an `_rmaos` texel into roughness, metalness, occlusion and specular, and an `_orm` texel as before; `dielectricF0`, `reflectanceFromF0` and the roughness scale are tested, including NaN and out-of-range input.
- [x] `openeden.material` files with `roughnessScale` and `specularLevel` parse with located errors and write back byte for byte; files without them are unchanged.
- [x] `findTerrainLayerMaps` finds `_rmaos` per layer when asked, lists each file once, and a layer without one has index -1.
- [x] `removeSrgb` turns every block-compressed sRGB format into its UNORM twin and leaves other formats alone.
- [x] With the setting on, the debug view `roughness` and `material` on the terrain show the map's values at layers that have one and 0.95 elsewhere; the lit frame draws normally.
- [x] With the setting off, the default frame is identical to before (`diffmap` empty).
- [x] GPU memory and time with the Skyrim sets are measured and reported.
- [ ] Linux gate, whole-tree clang-format, `CI/check_*` scripts, `components-tests`, `openmw-tests`, `eden-studio-tests`, 32 Lua tests pass.
Out of scope: the AO pass; parallax from `_p`; subsurface JSON keys; `_g` glow and `_m`/`_s` maps; layers of another resolution; the editor's rendering; shipping or converting any Skyrim file in the repository; reading Skyrim's JSON folder at run time; the Vanaheimr or other landscape packs (the AIO has no Skyrim "landscape" ground sets, only ground-like architecture sets).

## Stage log

### 2026-11-01, stage 1 (maths, search, sRGB) and stage 2 (shaders, terrain, local setup)

**What changed from the spec as first written.**
- `Material::align_dummy2` keeps its name (the spec said it would be renamed): `files/shaders/sky/sun/sun.slang` has a copy that the Slang test compares, and the sky shaders belong to another task. `data.glsl` only gained a comment; the roughness scale for objects rides in that float (0 = none).
- Stock land textures are 256 by 256 DXT1 with 6 mip levels (read from `Morrowind.bsa`: `tx_bc_moss`, `tx_bc_dirt`, `tx_bc_mud`, `tx_sand_01`, `tx_bc_rock_01`, `tx_bc_grass`, `tx_ai_dirt_01`, `tx_gl_dirt_01`, `tx_bc_scrub`), so D6 holds as written.
- Terrain in the `vanilla` look: the terrain always takes the full GGX lobe (it has no equivalent of the objects' `pbrSpecularScale`); test with `pbr look = physical` so ground and objects agree. Terrain ambient occlusion applies in both looks (decision D5); objects in `vanilla` ignore occlusion.
- `roughnessScale` applies to `_rmaos` on objects but to either layout on terrain (the old `_orm` object path is untouched). Terrain layers also take a tier 2 `reflectance`.
- Shader language (decision 0009, toddhow, 2026-11-01): no new shader file was written; everything here is an edit of existing GLSL (`pbr.glsl`, `default/main.frag`, `terrain/main.frag`, `terrain/bindings.glsl`), which cannot be mixed with a Slang module. Staying GLSL, and why, is the only deviation from 0009.

**Verified (Windows, 2026-11-01).**
- Channels (decoded in Python): `_rmaos` is R roughness, G metalness, B occlusion, A = 255 in every BC1 file (table above). The Windhelm, Solitude and Markarth ground sets are BC1 colour (`_rmaos` BC1, `_n` BC7, `_p` BC4 `BC4U`), the files are 2048 by 2048 (the "4k" is the mod's name).
- vsgXchange maps DX10 BC1/BC7 sRGB to `*_SRGB_BLOCK` (`extern/fetched/vsgXchange/src/dds/dds.cpp`, `kFormatMap`), and `removeSrgb` did not handle them: with a generated DX10 DDS in a unit test the format came back sRGB, and in the game a Faultier wall (`whruinstone`, BC7 sRGB) rendered nearly black on the old executable and normally with the fix (`scratchpad/faultier/out/sbs2.png`).
- Unit tests: `components-tests` 3010, `openmw-tests` 799, `eden-studio-tests` 520 pass; the new files are `render/testrmaos.cpp`, `vsgadapters/testrmaosmaps.cpp`, `terrain/testlayerrmaos.cpp`, `vsgutil/testremovesrgb.cpp` (written from the spec before the code, by me, not by a separate subagent).
- Frames (Seyda Neen, hour 12, `hdr`, `pbr`, physical look): the hill's ground shows the Faultier grass and dirt through `_rmaos` and `_n` (`out/maps_on.png`); the roughness debug view shows the map's variation on terrain and on the house wall (a full-size `_rmaos` and BC7 `_n`, `out/dbg_rough2.png`); an exaggerated override (`roughnessScale 0.1`, `specularLevel 0.16` on `tx_bc_moss` and `tx_bc_grass`) darkens the roughness view on those layers only (`out/sbs5.png`), so the tier 2 scales reach the terrain.
- Off means unchanged: with the setting off, a frame with `hdr` + `pbr` differs from the earlier executable's by 64 pixels over a threshold of 6 (water animation along the bottom), and a default frame by 47. This comparison had to be made with the other task's (0038) uncommitted sky and shadow shaders replaced by HEAD's: with them, a default-settings frame was white (new executable) or had a red sky (the 15:58 executable), whatever my files did (checked by running HEAD's versions of mine too). Reported to toddhow.
- Reviewer (fresh subagent): no bugs. Two leaks fixed (the terrain's layer-flags buffer had different bytes and the scales array was allocated with the setting off: both now masked and conditional); the vanilla-look terrain difference and the `align_dummy2` name are written above.

**Cost (balmora_longview, RTX 4070, 1280 by 720, `tools/benchmark.sh`, GPU main scene, median).** `hdr`+`gbuffer`+`pbr` physical, no extra data: 0.411 ms; with the Faultier folder, maps off: 0.405 ms; maps on: 0.231 ms (a different scene state is possible; the same note 0010 says this method moves 0.1 to 0.2 ms between runs, and the stand-in layers are not in Balmora, so read it as "no measurable cost"). Whole-GPU memory sampled with `nvidia-smi` while the game ran: idle about 2300 MiB, peak 2540 MiB without the data, 2582 MiB with it, maps on or off (no measurable difference from the extra terrain arrays: eight 256 by 256 BC1/BC7 layers). A full-size set (2048 by 2048: BC7 colour 5.3 MB, `_n` 5.3 MB, BC1 `_rmaos` 2.7 MB, all with mip chains) is about 13 MB, held compressed. Loading time was not measured separately.

**Not verified.** The Linux gate and clang-format (below, if run), a window resize and antialiasing 4 with the maps, `_orm` on terrain in a frame (unit-tested only), snow and ash stand-ins in a frame (the snow texture names of Bloodmoon were not looked up), a low-sun frame for visible highlights (all frames are at noon, so the specular lobe is faint), terrain at a resolution above the array's, the editor.

**Not built.** Parallax and displacement from `_p` (D3), `_g` glow, subsurface keys, layers above the array's size (D6).
