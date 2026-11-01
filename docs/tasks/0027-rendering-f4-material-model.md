# Task: Rendering F4 material model and R1 physically based lighting
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 4 (F4), section 5 (R1) and the skin questions of section 2 and R7 tier 1; builds on `docs/tasks/0025-rendering-f2-linear-hdr.md` and `docs/tasks/0026-rendering-f3-stored-gbuffer.md` ("F4 must know" notes).
Goal: give every opaque surface a roughness, metalness, reflectance, occlusion and subsurface flag (derived, overridden by data, or read from an optional texture), light it with a microfacet specular, an energy-conserving diffuse and Fresnel, and store the values where global illumination, ambient occlusion and reflections can read them, all behind one setting that is off by default.
Inherited code touched, at commit `20f635c3d2`: `files/shaders/default/main.frag` (`main`), `files/shaders/terrain/main.frag`, `files/shaders/lib/material/data.glsl` (`Material`), `files/shaders/lib/light/pixellighting.glsl` (`pointLightsPerPixel`), `components/pipeline/material.hpp` (`Material::createDefault`, `isDefault`), `components/pipeline/graphics.cpp` (`Graphics::create(const Options&)`), `components/pipeline/mode.hpp`, `components/pipeline/gbufferblend.*`, `components/pipeline/terrain.*`, `components/render/gbuffermath.hpp` (`extraColorFormats`), `components/render/hdrtarget.*`, `components/render/tonemap.*`, `components/vsgadapters/nif/nif.cpp` (`createMaterialData`, `handleAutoMaps`), `components/vsgadapters/nif/automaps.*`, `apps/openmw/mwrender/rendermanager.cpp` (constructor), `apps/openmw/mwrender/npc.cpp` (body part loading), `components/settings/categories/rendering.hpp`, `files/settings-default.cfg`, and the tests that build a `Pipeline::Data::Material` by aggregate (`apps/components_tests/mwanimation/testclonematerials.cpp`; new fields only appended, existing assertions unchanged).
Inputs / outputs: new `[Rendering]` settings `pbr` (bool, false), `pbr look` (`vanilla` or `physical`, default `vanilla`), `pbr material maps` (bool, false); new `components/render/materialmath.hpp`, `components/render/materialoverrides.*`; `Pipeline::Data::Material` grows from 80 to 96 bytes (one `vec4 pbr`, one named float `subsurface`); one more G-buffer colour output (`material`) only with `pbr` and `gbuffer` both on; JSON files `openeden/materials/*.json` in the VFS (format `openeden.material` v1); no change to any save, Lua API, extension contract or existing file format.

## What the values mean (the contract GI, AO and reflections can rely on)

| Quantity | Range | Meaning | Where it is stored (with `pbr`, `hdr`, `gbuffer` on) |
| --- | --- | --- | --- |
| roughness | 0.045 to 1 | perceptual roughness (GGX alpha = roughness squared) | `normal.w` of the `normal` image (RGBA16F). With `pbr` off, `w` stays 1 where a surface is written; 0 where there is none |
| metalness | 0 to 1 | 0 dielectric, 1 metal | `material.r` |
| reflectance | 0 to 1 | the specular strength of a dielectric, F0 = 0.16 * reflectance^2 (0.5 gives the usual 0.04) | `material.g` |
| occlusion | 0 to 1 | baked ambient occlusion, 1 = open | `material.b` |
| subsurface | 0 or 1 | the surface is skin (or marked so by data) | `material.a` |

`material` is `VK_FORMAT_R8G8B8A8_UNORM`, colour output 4 (after `objectMotion`), valid for the same span as the other G-buffer images, zero where there is no opaque surface. It exists only when `pbr` and `gbuffer` are on; the blend-state count (`1 + outputs`) is fixed at start as before, now 1 + 4. The terrain writes roughness 0.95, metalness 0, reflectance 0.5, occlusion 1, subsurface 0 (no per-layer data; stated).

## The three tiers

**Tier 1, derive from what exists.** Morrowind's NIF material specular colour and glossiness are not read by the renderer for Morrowind-era files (`handleMaterialProperty` in `nif.cpp`, the commented specular lines under a version test; OpenMW does the same), so for stock assets there is nothing per-material to derive from, and tier 1 is: metalness 0 (no metals), reflectance 0.5, occlusion 1, roughness 0.8 for every surface without a specular map. With a specular map (the mods that ship `_spec` files: the map's alpha times 255 is the shininess the existing shader uses, its colour the specular colour) roughness is `roughnessFromShininess(shininess)` and reflectance `reflectanceFromSpecular(colour)`. Skin: the body part type marks it. `ESM::BodyPart::MT_Skin` parts (`mwrender/bodyparts.cpp`) are registered with the override table as model paths with `subsurface = true` when `Npc` attaches them, lowest priority, so heads, hands and bodies of NPCs carry the flag and clothes and armour do not. Creatures and props need tier 2.

**Tier 2, data-driven overrides.** JSON files `openeden/materials/*.json` in the VFS (mods ship them like any data file; files are read in VFS order, later wins). Format `openeden.material`, version 1:
```json
{
  "format": "openeden.material",
  "version": 1,
  "materials": [
    {
      "match": { "texture": "textures/tx_example.dds" },
      "roughness": 0.4,
      "metalness": 1,
      "reflectance": 0.5,
      "occlusion": 1,
      "subsurface": false
    }
  ]
}
```
`match` has exactly one of `texture` (the diffuse texture's resolved path) or `model` (the model's path); paths compare lowercase with forward slashes. Every value is optional but at least one is required; numbers are 0 to 1 (roughness 0.045 to 1); no other keys are allowed. Precedence per value: tier 1, then `model` entries in file order, then `texture` entries in file order (a texture is more specific than a model; later wins). A field an entry does not set falls through to the earlier layer. Errors are located (file, line, field) and a bad file is skipped whole with one log line, never a partial table. The canonical writer emits two-space indent, the keys in the order above, `format` and `version` first, shortest round-trip numbers, LF and a final newline, and `write(parse(x)) == x` byte for byte for canonical `x`. Files are read only when `pbr` is on.

**Tier 3, optional extra texture files (implemented, off, needs toddhow's approval).** Survey, from sources I could read (web search, 2026-11-01): OpenMW's own documentation fixes only the `_n` normal-map and `_spec` specular-map patterns (the settings `normal map pattern` and `specular map pattern`); OpenMW has no roughness or metalness texture of its own. The PBR texture mods and shader packs found (for example the Nexus mod "(OpenMW) PBR for Morrowind Enhanced Textures", mod 57946) reuse `_spec` files and ask the user to switch an interpretation define in a third-party shader pack, so the meaning of a `_spec` file's channels is pack-specific and cannot be guessed. General tools name maps by suffix (`_roughness`, `_metallic`, `_ao`) or pack them (`_arm` is common in game asset pipelines); none is specific to Morrowind. I found no convention that Morrowind mods share, so I do not adopt one silently. **Proposed default, for toddhow to approve or change:** one packed file next to the diffuse texture named `<diffuse name>_orm` (found as `_n` is), channel R occlusion, G roughness, B metalness, the glTF 2.0 metallic-roughness layout, linear (not sRGB) data. It overrides tier 1 and is overridden by tier 2. It is read only with `pbr material maps = true` (default false), and the suffix is a named constant `Render::Pbr::materialMapSuffix`, not a setting.

## Lighting (R1)

Without `pbr` every shader is as before (the defines are not added). With `pbr` (and `hdr`; without `hdr` the game logs a warning and runs without it) the default shader's opaque lit surfaces light per pixel (the path normal and specular maps already use, `pointLightsPerPixel`, over the same light grid and the game's `LightAttenuation_*` falloff, and the sun as now with its shadows) and replace the Phong specular by the microfacet model of `materialmath.hpp`: GGX distribution, Smith height-correlated visibility, Schlick Fresnel; `F0 = mix(0.16 * reflectance^2, baseColour, metalness)`; diffuse colour `baseColour * (1 - metalness)`. Morrowind's lights were never physical, so there are two looks, `pbr look`:
- `vanilla` (default): the diffuse term is the vanilla one (Lambert times the light colour, unscaled), so a surface without specular data keeps the vanilla picture; specular is added only where the surface has a specular colour (a specular map, or a tier 2 override with reflectance above 0.5, i.e. a strength above the vanilla none); metals still tint and darken their diffuse. The light falloff and light grid are the game's, unchanged.
- `physical`: reflectance applies to every surface (a minimum F0 of 0.04, so highlights appear everywhere), the diffuse is multiplied by `1 - Fresnel` of the view angle (energy conserving), the ambient term is scaled by `occlusion` and the metal tint.
Both read roughness, metalness, reflectance and occlusion from the material descriptor (`pbr` vec4) and the tier 3 map. Ambient and emission are unchanged. Water, sky, particles, groundcover and terrain are not changed by R1 (terrain keeps its look; stated).

## Constraints
Determinism: no change. Allocation: nothing per frame; override tables are built at start. Threading: the override table is filled on the main thread before the first model loads and read from loader threads; `markSubsurfaceModel` (main thread, while NPCs attach parts) and `resolve` (loader threads) share a mutex. Off by default and unchanged when off: with `pbr = false` no shader define, descriptor, output or table exists, no model path is looked up, and a frame of the default path and of the HDR path (with and without `gbuffer`) is identical to the commit before this stage by `diffmap` (the 96-byte material struct is the only change that is always present, and no shader reads it). Ownership: `Render::Pbr::OverrideTable` is owned by `RenderManager` (a `std::unique_ptr`) and observed through a raw pointer set into the loaders' options.

## Interfaces (pure parts, for tests written from this spec alone)
Header `components/render/materialmath.hpp`, namespace `Render::Pbr`, header-only, vsg math types only:
```cpp
constexpr float minRoughness = 0.045f; constexpr float defaultRoughness = 0.8f; constexpr float defaultReflectance = 0.5f;
constexpr float terrainRoughness = 0.95f; constexpr std::string_view materialMapSuffix = "_orm";
float roughnessFromShininess(float shininess);       // (2/(n+2))^(1/4), clamped to [minRoughness, 1]; n <= 0 gives 1
float reflectanceFromSpecular(const vsg::vec3& c);    // 0.5 + 0.5 * luma(c), luma = 0.2126 r + 0.7152 g + 0.0722 b, channels clamped to [0,1]; result in [0.5, 1]
float f0FromReflectance(float reflectance);           // 0.16 * r * r
vsg::vec3 specularF0(const vsg::vec3& base, float metalness, float reflectance);   // mix(vec3(f0FromReflectance), base, metalness)
vsg::vec3 diffuseAlbedo(const vsg::vec3& base, float metalness);                   // base * (1 - metalness)
float distributionGGX(float NoH, float roughness);    // a2 / (pi * (NoH*NoH*(a2-1)+1)^2), a = roughness^2
float visibilitySmithGGX(float NoV, float NoL, float roughness);   // height-correlated Smith visibility (includes the 1 / (4 NoV NoL))
vsg::vec3 fresnelSchlick(float VoH, const vsg::vec3& f0);          // f0 + (1 - f0) * (1 - VoH)^5
struct Surface { float roughness = defaultRoughness; float metalness = 0; float reflectance = defaultReflectance; float occlusion = 1; bool subsurface = false; };
Surface clampSurface(Surface s);                      // each value into its range (NaN becomes the default)
vsg::vec4 packMaterial(const Surface& s);             // (metalness, reflectance, occlusion, subsurface ? 1 : 0), each in [0,1]
Surface unpackMaterial(float roughness, const vsg::vec4& material);
enum class Look { Vanilla, Physical }; std::optional<Look> parseLook(std::string_view name);   // "vanilla" or "physical"
bool usePbr(bool settingOn, bool hdrPath);            // true only when both
bool useMaterialOutput(bool pbr, bool gbuffer);       // true only when both
```
`Render::GBuffer::extraColorFormats(bool objectMotion, bool material = false)` appends `materialFormat` (`R8G8B8A8_UNORM`) after `objectMotion` when `material` is true; `(true)` and `(false)` give the lists as before. `Render::GBuffer::DebugView` gains `Roughness`, `Material`, `Subsurface` after `Velocity` (names `roughness`, `material`, `subsurface`); `Tonemap::debugPushConstants` mode numbers follow the enum order.
Header `components/render/materialoverrides.hpp`, namespace `Render::Pbr`: `OverrideValues` (optional doubles `roughness`, `metalness`, `reflectance`, `occlusion`, optional bool `subsurface`), `OverrideEntry` (`kind` Texture or Model, `path`, `values`), `OverrideSet` (`entries`), `OverrideError` (`file`, `line`, `field`, `message`, `describe()` giving `file:line: field: message`), `std::optional<OverrideSet> parseOverrides(std::string_view json, std::string_view fileName, OverrideError&)`, `std::string writeCanonical(const OverrideSet&)`, `std::string normalizePath(std::string_view)`, and `OverrideTable` with `add(const OverrideSet&)`, `markSubsurfaceModel(std::string_view path)`, `Surface resolve(std::string_view modelPath, std::string_view texturePath, const Surface& base) const` applying the precedence above, `bool empty() const`.
`Pipeline::Data::Material` fields: `vec4 pbr` (roughness, metalness, reflectance, occlusion; roughness and reflectance negative mean "derive"), `float subsurface`; `Material::createDefault()` gives `pbr = (-1, 0, -1, 1)`, `subsurface = 0`; `isDefault` still compares bytes.

## Stages (each commits working, default off unchanged)
- **F4a Material inputs into the G-buffer.** Settings, `materialmath.hpp`, the `Material` fields, the `material` output and `normal.w` roughness (default shader, terrain), debug views, tier 1 (specular map values, skin marks from `Npc` through the override table's `markSubsurfaceModel`). With `pbr` the shading is still the old shading.
- **F4b R1 lighting.** The microfacet shading and the two looks. GPU cost measured.
- **F4c Tier 2 overrides.** Parser, writer, table, loading from the VFS, lookup in `nif.cpp`.
- **F4d Tier 3 textures.** `_orm` map unit, mode, auto-map search, behind `pbr material maps`.

## Acceptance criteria
- [ ] `pbr = false`: frames of the default path and of the HDR path (with and without `gbuffer`) equal the commit before this task by `diffmap` to within run-to-run noise (Seyda Neen, ToddTest, Caldera Mages Guild, night, water, an NPC, the menu); no define, descriptor, output or table exists.
- [x] F4a: `gbuffer debug view = roughness` and `material` show the derived values (uniform roughness on stock surfaces, the terrain's 0.95, a different value where a specular map gives one, skin flagged on an NPC in `subsurface`); the scene itself is unchanged by the outputs; the preview and map still draw.
- [ ] F4b: with `pbr`, both looks draw an exterior, an interior with torches, night and an NPC without artefacts (no NaN speckle, no black surfaces); `vanilla` stays close to the vanilla picture on stock surfaces; a surface with a specular map shows a microfacet highlight; the sun and shadows still apply; viewed by eye and judged by toddhow.
- [x] F4c: a test material file changes a chosen texture's roughness and metalness in the debug view and the lit picture; a malformed file is skipped with a located log line; `write(parse(x)) == x` for the canonical sample.
- [x] F4d: with `pbr material maps` and a generated `_orm` file next to a diffuse texture, the debug view shows its values; without the setting nothing is read.
- [x] Cost: `docs/research/0010-material-model-cost.md` has `hdr`, `hdr + gbuffer`, `hdr + gbuffer + pbr` (both looks) GPU times of the main pass over the heavy scenes.
- [ ] Unit tests for the pure parts pass; the Linux gate builds with `-Werror`; the whole tree passes `CI/check_clang_format.sh`; `eden-studio-tests` pass and the editor builds; a fresh reviewer read each stage.
Out of scope: ambient occlusion, reflections and GI themselves; subsurface scattering beyond the flag; per-layer terrain materials; materials for COLLADA models (`dae.cpp` keeps the defaults); the editor's rendering; changing the default look; any Lua API.

## Stage log

### 2026-11-01, cleanup before F4 (own commits)
- **Preview and local map with `hdr` + `gbuffer` on (the F3 "not verified").** Checked: a small Lua player script (`UI.setMode('Interface')` after 240 frames, in the scratchpad) opens the inventory with the character preview and the map, and a frame was captured with the default path and with `hdr` + `gbuffer`. Both draw the character, the items and the local map the same; nothing is broken by the three-entry blend state on the one-attachment passes. (`scratchpad/pbr/tab_gb.png`, `tab_def.png`; no validation layer is installed, so only what the picture shows is claimed.)
- **The default path's flat water: cause found and fixed (commit `20f635c3d2`).** `Reflect::update` built the colour copy's region from the source image's extent, but the window's colour image is a bare swapchain handle whose `vsg::Image` reports extent 0x0 (and format 0, usage 0; a temporary log line printed it). So the blit region was empty, the copy never wrote, and the water reflected and refracted a constant. Debug outputs of the water shader showed the normals were fine and the copied colour was one flat colour. The fix uses `Engine::swapchainExtent()` when the image does not know its size. The window's depth image is a full image and was already right. **This changes the default look:** the default path's water now has waves and reflections, as the HDR path's and the OSG build's always did. toddhow decides whether that stays (it is its own commit and can be reverted alone). Before: `scratchpad/gb/wdbg2.png` (copy shown flat); after: `scratchpad/gb/wfix.png`.

### 2026-11-01, stages F4a, F4b and F4c (Windows, RTX 4070, `build-jolt`)

These three were written together because the shader changes overlap in `main.frag`; they are in one commit.

**What was built, in plain language.** With `[Rendering] pbr = true` (and `hdr`) every surface has five material numbers. For stock Morrowind models they are the tier 1 defaults (rough 0.8, no metal, F0 0.04, no baked occlusion), changed by a specular map where one exists (its shininess becomes roughness, its colour the reflectance), by a `openeden/materials/*.json` file, or by the skin mark that `Npc` puts on the models of skin body parts and heads before it loads them. Lit surfaces are then lit per pixel with a GGX specular, Smith visibility and Schlick Fresnel over the same light grid and falloff; `vanilla` keeps the vanilla diffuse and adds a highlight only where there is specular data, `physical` gives every surface F0 0.04, takes the Fresnel share out of the diffuse and scales the ambient by occlusion. With `gbuffer` also on, roughness is written to `normal.w` and metalness, reflectance, occlusion and the skin mark to a fourth extra output (`material`, RGBA8). The debug views `roughness`, `material` and `subsurface` show them.

**Verified.**
- Off means unchanged: the default path and `hdr` + `gbuffer`, six scenes each (Seyda Neen noon, night, water, an NPC, `ToddTest`, Caldera Mages Guild), current build with the shaders of the commit before against the current shaders (`scratchpad/pbr/offcheck.sh`, `res_base`): noon 774 and 653, night 597 and 218, `ToddTest` 0 and 188 (sum of differences over a 2x subsampled frame), Caldera 166500 and 63980 in a 40x80 pixel patch (torch flicker); the water and NPC scenes differ by the run-to-run noise (sea 296k against a noise floor of 491k between two runs of the same build, NPC 1.74M against 1.70M).
- Viewed: the roughness view shows the terrain light (0.95), objects mid-grey (0.8), the sky black; the material view is uniform (0, 0.5, 1); the subsurface view marks only the NPC's skin (head and hands) (`scratchpad/pbr/f4a_npc_*.png`). A frame with `pbr` in `vanilla` look is within noise of the HDR frame on stock content (noon, sum 10121); `physical` is slightly different on the houses (`b_noon_*.png`, `b_cal_*.png`).
- Tier 2: a generated `openeden/materials/test.json` (44 Seyda Neen exterior models, roughness 0.25, metalness 1) in a data folder made the houses dark copper-coloured with a highlight on the roofs in both looks (`c_noon_vanilla.png`). Metals look dark because nothing yet gives them an environment to reflect; probes and reflections (the GI and reflection tracks) fill that.
- Unit tests: 2525 in `components-tests` (new: `testmaterialmath.cpp`, `testmaterialoverrides.cpp`), 799 in `openmw-tests`.
- The preview and the local map, with `hdr` + `gbuffer`: drawn (see the cleanup entry above); not re-run with `pbr`.

**Found on the way.**
- A new shader file must also be listed in `files/shaders/CMakeLists.txt`, or the build folder does not get it and glslang reports "Failed to load include code" while the game hangs at start with no other message (it took a while to see the line in the log). Added to `apps/openmw/mwrender/CLAUDE.md`.
- `files/shaders/sky/sun/sun.slang` has its own copy of the `Material` struct; `SlangSunTest` caught that it had to follow the new fields.
- Files in `openeden/materials/` are read in file name order (the VFS iterator's order), not in load order of data folders; later files win. The spec said "VFS order"; this is what that gives.
- One game start in the series hung once in the audio device listing (a repeat of the same run worked); not related to this work as far as seen.

**Not verified.** The Linux gate and clang-format (below), the editor build and `eden-studio-tests` (the editor was running, so `eden-studio.exe` and `compat_morrowind.dll` could not be relinked), a window resize with `pbr`, antialiasing 4 with `pbr`.

### 2026-11-01, review of F4a to c, and stage F4d

**Reviewer (a fresh agent that did not write the code) found, and what was done.**
- *Risk, fixed:* at roughness 0.045 the specular peak can exceed what the 16-bit float target holds, and an infinity would spread through the bloom. `pbrSpecularLobe` now clamps the lobe to 256 (`pbrMaxLobe`) and guards the half vector's length.
- *Risk, fixed:* `vsgAdapters::nif::materialTable` was a raw pointer to a table whose owner is destroyed first; it is now a `std::shared_ptr`.
- *Minor bug, fixed:* a `version` such as 4294967297 narrowed to 1 and was accepted; it is read as a 64-bit number now (a new test).
- *Risk, not fixed, stated:* a model is loaded and cached once, so a skin mark added after its first load (for example by a preloader that loaded the part file earlier, without the mark) is not seen. In the frames checked the head and hands were marked, but the order is not guaranteed. A fix needs either marking at cell-preload time or a material that the mark can change later; left for a decision.
- Error line numbers come from text searches, so they are approximate when a path itself contains `"match"`; the texture key is compared with the resolved path of the NIF (`.dds`), so a mod writing `.tga` will not match. Both are now written down here and not changed.
- Nit: `pbr material maps` was documented before it worked; it works from this stage.
- Added tests: odd top levels and versions are errors, `resolveValues` leaves unset values unset and ignores the folder prefix.

**F4d, in plain language.** With `pbr material maps = true` the model loader also looks for `<diffuse name>_orm` next to each diffuse texture (the same search as `_n` and `_spec`), binds it as a new texture unit (16) and adds the define `MATERIAL_MAP`. The shader fills roughness (green), metalness (blue) and occlusion (red) from it only where the material descriptor does not set them (so a tier 2 file wins). The suffix is `Render::Pbr::materialMapSuffix`.

**Verified.** Generated 3816 flat `_orm` files (roughness 0.2, no metal, occlusion 1) in a data folder: the roughness debug view showed every object at 0.2 and the terrain still at 0.95 (`scratchpad/pbr/d_orm_roughness.png`), and the lit frame drew normally (`d_orm_none.png`). Without the setting the files are not looked for (`nothing_is_read_with_the_setting_off`). `components-tests` 2532, `openmw-tests` 799.

**Cost.** `docs/research/0010-material-model-cost.md`: the main pass moved within the noise of this method; wall-clock frame time unchanged (CPU-bound scene).
