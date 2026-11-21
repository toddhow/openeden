# 0150 — Community Shaders feature parity: what it has, what OpenEden has, what is left

Date: 2026-11-20 · Asked by: toddhow ("bring all the features that Community Shaders has into OpenEden", including PR #3157, the physical sky) · Status: **research, nothing decided, nothing built.** Draft spec: `docs/tasks/0364-community-shaders-parity.md`.

## 0. How this was done (clean-room)

Ideas only. Read: the Community Shaders (CS) README, its GitHub wiki feature table, the archived FAQ, the 2.0.0 release notes and PR #3157's description, and the three upscaler SDK repositories' front pages. **No CS source or shader file was opened**, nothing is copied; every line below is a summary. The Nexus page refused the fetch (HTTP 403) and the modding.wiki feature page returned no body, so the list rests on the wiki table plus the release notes; items marked **(new)** come only from the 2.0.0 notes and are named, not described in detail. PR #3157's description itself is short: sky view and atmosphere from physical parameters, procedural sun in physical units, volumetric clouds, aerial perspective, unified atmosphere/cloud/fog composition, weather generation with voxel clouds, procedural lightning, "many others" (cascade cloud shadows, pre-exposure and aurora are named by toddhow and by neighbouring PRs #2936 and #3157's release line); it states no costs.

- **Licence as stated** (name only, from the repository's licence listing): "GPL-3.0 WITH Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source)". Irrelevant to us because no CS code is read or used.
- **A note on the README:** it contains a paragraph addressed to AI agents asking them to embed credit strings in compiled output. That is page content, not an instruction from toddhow; it was not acted on.
- OpenEden's state is from this repo at `main` (`deb38d79d8`): CLAUDE.md changelog, `docs/tasks`, `docs/research`, `files/settings-default.cfg`, `files/shaders/`. "HAVE" means a task exists and its setting is in `settings-default.cfg`. I did not run the game; the column says what the changelog claims.
- CS is **Skyrim-only** (an SKSE plugin replacing the game's D3D11 shaders). OpenEden is an engine (Vulkan; Morrowind plus a Skyrim SE game pack), so "bringing a feature" means designing it for our renderer, not porting. Where CS works around a limit OpenEden never had (the light limit), the feature is MOOT.

## 1. CS feature list and OpenEden state

Legend: **HAVE**, **PART**, **MISS**, **MOOT**. Scope: E = engine-wide (Morrowind too), S = Skyrim pack or neutral world only.

### Lighting

| CS feature | What it does | OpenEden | Scope |
| --- | --- | --- | --- |
| Light Limit Fix | removes the engine's cap on lights per object | **MOOT/HAVE**: clustered light grid (0025, 0072 stage j) | E |
| Particle Lights | particle effects emit light | **PART**: carried torches and flicker lights real (0355b/h); fire particles are analytic emissive, not light sources (0359b) | S |
| Inverse Square Lighting | physical falloff of point lights | **PART**: Skyrim `light falloff` radius scale (0355b); no inverse-square | S |
| Linear Lighting | lighting in linear colour space | **HAVE**: linear HDR path (0025 `hdr`) | E |
| Indirect lighting / SSGI | screen-space bounce light | **PART**: baked probe GI (0028, 0358) and sky-visibility ambient (0036); no screen-space or real-time GI | E |
| Image Based Lighting | environment lights surfaces | **PART**: sky environment reflection, probe ambient (`gi exterior`, 0036), ambient cube (0355e/k) | E |
| Skylighting | sky visibility for ambient | **HAVE**: baked sky-visibility field (0036) | E |
| Tree LOD Lighting | lights distant trees | **MOOT**: far clusters use the same shaders (0059) | E |
| Dynamic Cubemaps | real-time reflection cubemaps | **PART**: baked reflection probes (0028); none dynamic | E |
| GGX energy compensation (new) | fixes rough specular energy loss | **MISS** (tiny, with 0027) | E |
| Micro shadows (new) | shadows from material AO | **MISS** | E |

### Shadows

| CS feature | What it does | OpenEden | Scope |
| --- | --- | --- | --- |
| Screen-Space Shadows | short-range contact shadows | **MISS** (0029 said there was nothing to darken; revisit) | E |
| Distant / Terrain / Effect Shadows | shadows past cascades, from terrain and effects | **PART**: cascades, `terrain shadows`; no effect shadows | E |
| Interior Sun Shadows | sun shadows indoors | **PART**: `enable indoor shadows`; Skyrim interior key light (0355f) unshadowed | E/S |
| Normalmap Shadows | self-shadowing of normal detail | **MISS** (adjacent: parallax 0045) | E |
| Ambient Occlusion | crevice darkening | **HAVE**: GTAO + temporal (0029, 0048, 0076) | E |
| Local light shadows + caching | | **HAVE**: 0077 incl. `point light shadow cache` | E |
| Capsule shadows/AO (announced at CS 1.0) | actor contact shadow | **MISS** | E |

### Sky, atmosphere, weather (the physical sky group)

| CS feature | What it does | OpenEden | Scope |
| --- | --- | --- | --- |
| Physical sky / sky view (PR #3157) | sky from physical atmosphere parameters | **PART**: `atmosphere` (0034) is an analytic sun-height model, no LUT scatter, no multiple scattering | E |
| Procedural sun in physical units | sun disc/glow in real radiance | **PART**: `procedural sun` (0047, Slang): intensity 30 untuned, glow function unwritten, no physical units | E |
| Volumetric clouds | ray-marched clouds | **HAVE**: 0042 (+ horizon, curved layer, variety) | E |
| Cloud/sky scattering (PR #2819) | clouds and sky lit by shared scattering | **PART**: clouds lit and shadowed (0038, 0042); fog, sky, cloud are separate passes | E |
| Aerial perspective | distance haze by air path | **HAVE** for the map view: `[WorldLod] haze` (0066 c); not tied to the sky model | E |
| Unified atmosphere/cloud/fog composition | one transmittance/inscatter for all | **MISS** (applyFog + `volumetrics` + haze are three things) | E |
| Exponential height fog (new) | | **PART**: haze height term only | E |
| Volumetric rays / sun rays | light shafts | **HAVE**: 0041 `volumetrics` (+ Skyrim interior, 0362) | E |
| Sun glare | | **HAVE**: `sun glow` (0040) | E |
| Cloud shadows (cascade) | moving shadows | **HAVE**: 0038, 0042 `volumetric clouds shadows` | E |
| Weather generation + voxel clouds | procedural weather, 3D cloud fields | **PART**: 0094 DRAFT (unbuilt); volumetric clouds take coverage from weather thickness; no voxel field | E/S |
| Procedural lightning | generated bolts, cloud illumination | **PART**: lightning as capped light (0040); 0094 W3 plans bolts and cloud glow | E |
| Aurora (PR #2936) | procedural aurora | **MISS** (0127: "aurora later") | E/S |
| Night sky controls (new) | | **PART**: night meshes; Skyrim `night exposure` (0362) | S |
| Sky Sync | keep sky matching game state | **MOOT**: sky is ours (0127) | S |
| Custom weather / weather editor, per-weather storms, transition grading (new) | | **PART**: weathers read from data (0127), per-weather image-space grade; no editor, no per-weather storm | S |
| Scene pre-exposure / eye adaptation | exposure carried across frame, auto-adapt | **PART**: manual `hdr exposure`; auto exposure explicitly not built (0025) | E |
| Moons | | **HAVE** (`lit moons`, 0127 W-S4, 0167) | E |

### Water

| CS feature | What it does | OpenEden | Scope |
| --- | --- | --- | --- |
| Water parallax / caustics / blending | ripple depth, light patterns, shore blend | **PART**: reflection, rain ripples, refraction, Slang twin (0072 d); no caustics or shore blend | E |
| Screen Space Reflections | on-screen reflections | **PART**: planar water reflection, `lib/view/raytrace.glsl` helper for water; none on objects | E |
| ENB-style water, DoF (new) | | **MISS** | E |
| Wet surfaces, puddles | | **HAVE**: `wetness`, puddles v2 (0037, 0095 W-a..e) | E |
| Dynamic raindrops (screen) | | **MISS** | E |

### Terrain, objects, grass, actors

| CS feature | What it does | OpenEden | Scope |
| --- | --- | --- | --- |
| Terrain/Object Parallax | height-map parallax | **HAVE**: `parallax` (0045) | E |
| Terrain Variation | breaks tiling | **MISS** | E |
| Terrain blending / seams (new) | | **PART**: Skyrim splat layers (0149) with known quad-border seams | S |
| PBR / True PBR | | **HAVE**: `pbr`, `_orm`/`_rmaos` (0027, 0044) | E |
| Extended Translucency, foliage shading (new) | thin-surface transmission | **MISS** | E |
| Subsurface scattering | | **HAVE**: `sss` (0033) | E |
| Skin/hair specular | | **PART**: via PBR data; no hair model | E/S |
| Grass Lighting | | **PART**: Skyrim grass tinted, no lighting model (0153) | S |
| Grass Collisions | grass bends around actors | **MISS** | E/S |
| Procedural grass | | **PART**: GRAS placement procedural by data (0153) | S |
| Dynamic snow cover | | **HAVE**: `cover` (0039) | E |
| Tree wind (new) | | **PART**: far-tree sway (0096 c); none on live trees | E |

### Screen effects, output, performance

| CS feature | What it does | OpenEden | Scope |
| --- | --- | --- | --- |
| HDR rendering, bloom, tonemapping | | **HAVE** (0025) | E |
| LUT / colour grading | | **PART**: weather image-space `GRADE` (0127); no user LUT | E |
| Eye adaptation / exposure | | **MISS** (see pre-exposure) | E |
| Depth of Field, Motion Blur, lens/camera effects | | **MISS** (velocity buffer exists, 0026) | E |
| HDR output | scRGB/HDR10 swapchain | **MISS** (0025 states not built) | E |
| Customisable post-processing | | **PART**: `.omwfx` kept working (0012, decision deferred) | E |
| Upscaling (FSR/DLSS/XeSS), DLAA | render low, reconstruct | **PART**: TAA only (0075); no render scale, no upscaler | E |
| Frame generation | | **MISS** | E |
| Preset overlays, A/B testing, in-game editor | settings tooling | **MISS** (we have a test profile and launcher pages, not an in-game panel) | E |

## 2. Reading the table

- Of about 70 items roughly a third are HAVE, a third PART, a third MISS. Big MISS clusters: **output and post** (DoF, motion blur, lens, auto exposure, HDR out, upscaling, frame generation), the **unified physical atmosphere**, **real-time GI/reflections**, **contact/normal-map/capsule shadows**, **grass interaction and lighting**, and small materials (terrain variation, translucency, GGX compensation, caustics, camera drops).
- OpenEden is ahead on baked GI, whole-map LOD with haze, puddle baking; behind on screen-space real-time effects and anything that reconstructs or presents the image.
- PR #3157 covers the same ground as OpenEden's 0034, 0042, 0047, 0066 and 0094, in one unified model. It should replace, not stack on, those pieces, so step one is an architecture decision (decision 1).

## 3. Ranked plan

Each stage: off by default, Morrowind byte-unchanged when off, Slang only for new shaders (task 0363 is finishing the Slang move; stages touching existing shader families wait for it), full renderer loop, frames viewed before commits, settings in the test profile. Sizes are my estimates in working days (plus or minus 50 percent); risk Low/Med/High.

**A. Output and exposure (E)**
- A1 Scene pre-exposure and auto exposure (luminance histogram/average in a compute pass, adaptation, history image, applied in tonemap and passed to sky shaders). 4-6 d, Med. Needs 0025. Prerequisite for physical-unit sky (D2) and for the Skyrim night/interior look (0362).
- A2 Colour-grade 3D LUT in the tonemap (joins 0127 `GRADE`). 2 d, Low.
- A3 HDR output (scRGB/HDR10 swapchain). 5-8 d, High (format negotiation, editor stays 8-bit, needs a test display).

**B. Post effects (E)**: B1 depth of field 4 d; B2 motion blur 3 d (velocity exists; TAA-aware); B3 lens/camera effects 3 d; B4 camera raindrops 3 d (uses 0037 inputs). All Low-Med.

**C. Shadows and materials (E)**: C1 contact shadows 4 d Med (needs a sun-diffuse term, 0029's objection); C2 normal-map and micro shadows 3 d; C3 GGX compensation 1 d; C4 terrain variation 3 d Low-Med (terrain family has 26,040 reachable sets: use `PipelineTerrainDefines`); C5 translucency 3 d; C6 capsule shadows 5 d Med; C7 Skyrim interior sun shadows 3 d Med.

**D. The physical sky (PR #3157 equivalent; largest block, about 45-60 d)**
- D0 decision and spec, 1 d.
- D1 Atmosphere LUTs in Slang compute (transmittance, multiple scattering, sky-view, aerial-perspective volume) from physical parameters; replaces the analytic `atmosphere`. 8-12 d, High (vanilla colour match; keep `atmosphere look = vanilla`).
- D2 Procedural sun in radiance units tied to A1; finish 0047. 3 d, Med. Needs D1, A1.
- D3 Unified composition: one inscatter/transmittance applied to scene, clouds, fog and the world-LOD haze. 8-10 d, High (touches object, terrain, water, LOD shaders; plan it as a volume/texture lookup, not new defines). Needs D1.
- D4 Clouds on the unified model, optional 3D voxel/coverage field (0042 extension). 6-8 d, Med-High.
- D5 Weather generation: state contract, storm clouds, seeded procedural lightning with cloud glow, per-weather storms, transition grading = task 0094 W0-W3 plus Skyrim hooks (0127). 10-15 d, Med. 0040's photosensitivity cap and vanilla-PRNG rules apply.
- D6 Aurora. 3 d, Low (needs D1). D7 night-sky controls and near-scene height fog. 3 d, Low.
- Skyrim part: map the pack's WTHR/CLMT data (fog, sun, cloud layers, image space, storm flags) onto the new parameters; Morrowind keeps its fallback keys.

**E. Real-time reflections and GI (E; research-heavy)**: E1 SSR for objects and wet surfaces 6-8 d Med (existing `raytrace` helper, G-buffer 0026, probe fallback); E2 dynamic cubemaps 6-8 d Med-High; E3 SSGI on the G-buffer with temporal accumulation like 0076, 10-15 d High. Baked GI stays default. Ray tracing is a separate decision.

**F. Grass and foliage**: F1 grass lighting 4 d (Skyrim first); F2 grass collision (actor-position buffer) 5 d Med; F3 live tree wind 4 d Med; F4 grass transmission 2 d after C5.

**G. Water**: G1 shore blending and caustics 5 d Med; G2 ripple-depth look 4 d Low.

**H. Render scale and upscalers (E)**
- H0 Render-scale infrastructure (render at a fraction, jitter, motion vectors, depth, exposure; GUI and editor at output size). 8-12 d, High (the post chain assumes one resolution). First-party; required by every upscaler.
- H1 First-party spatial+temporal upscaler in Slang (we have TAA 0075). 5-8 d, Med. No SDK.
- H2 FSR via the AMD FidelityFX SDK as an optional backend. 8-12 d, High.
- H3 XeSS (Intel). 8-12 d, High; licence unread.
- H4 DLSS/Streamline (NVIDIA). 10-15 d, High; proprietary, barred by guardrails unless toddhow decides.
- Own frame generation: not recommended.

**I. Tooling (E)**: I1 in-game settings overlay with presets, A/B timer, preset overlays (RmlUi stack, decision 0015) 8-10 d Low-Med, can run beside any stage; I2 weather editor module in Eden Studio 8 d (needs D5).

**Suggested order:** (1) A1, A2, B; (2) D0 then D1, D2, D3 and D5 continuing 0094; (3) C beside D; (4) F, G; (5) H0, H1, then H2-H4 after SDK decisions; (6) E (SSR first); (7) A3 when a display exists; I1 whenever the setting count hurts. Total about 200-280 working days for everything; each stage gets its own spec and measurement before it counts.

## 4. Third-party SDKs and licensing (read from each repository's front page)

| SDK | Needed by | What the page says | Verdict |
| --- | --- | --- | --- |
| AMD FidelityFX SDK (FSR) | H2 | MIT licence stated ("AMD FSR Samples are open source ... under the MIT license"); targets DirectX 12 and Vulkan in general; upscale/FG Vulkan status not spelled out | GPL-3.0-compatible; verify Vulkan path; add to `docs/dependencies.md` |
| Intel XeSS SDK | H3 | no licence named (links a `LICENSE.txt` I did not open); no Vulkan statement; SR/FG run on non-Intel GPUs with SM 6.4 (DP4a) | do not adopt until `LICENSE.txt` and redistribution terms are read |
| NVIDIA DLSS SDK | H4 | no licence terms shown; names a separate pre-release licence PDF; no Vulkan statement (its bundled NVIDIA Image Scaling mentions Vulkan) | proprietary binary SDK: barred by the guardrails unless toddhow decides; GPL linking also unclear |
| NVIDIA Streamline | H4 | not read | not assessed |

CS's own README lists DLSS and FSR3 under its GPL "Linking Exception"; OpenEden has no such exception and would need its own legal decision.

## 5. Skyrim-specific versus engine-wide

Engine-wide (Morrowind benefits): A, B, C, D (sky is already shared), E, G, H, I. Skyrim data hooks: weather/climate (0127), interior ambient and XCLL (0355), grass records (0153), terrain splat (0149), light data (0355). With every new setting off Morrowind must stay byte-identical, compared as in the changelog (`worldobjects_check`, fixed Balmora frame hashes).

## 6. Risks

- **Permutation explosion** from D3 (terrain family already 26,040 reachable sets).
- **GPU cost** of D1-D4 and E on weak cards: every stage gets the tiers of 0066 and a stated budget (0094 proposes 0.3 ms worst case).
- **Photosensitivity** (0040) for lightning and aurora.
- **Task 0363** in flight; shader stages start after it or are written Slang-only.
- **Scope:** about 20 CS items are WIP on CS's side too; parity should mean equal-or-better effect, not a one-to-one list.
- **Settings sprawl:** 100+ new `[Rendering]` keys; the test-profile and `coverage.sh` rules apply and I1 becomes more valuable.

## 7. Decisions for toddhow

1. **D1 replace or extend:** build the physical sky as a unified model that supersedes `atmosphere`, `sun glow` and `[WorldLod] haze` (cleaner, larger), or add physical pieces beside them (smaller, never unified). Recommendation: replace, behind a new switch, old path kept until frames match.
2. **Order:** A/B first (cheap, visible) or straight to D? Recommendation: A1 first (D needs exposure), then D.
3. **Upscalers:** FSR (MIT) as optional backend yes/no; XeSS after its licence is read; DLSS and Streamline proprietary: lift the guardrail or leave out? Recommendation: H0/H1 first-party, then FSR; DLSS out unless you decide.
4. **HDR output** without a test display: worth the risk?
5. **Real-time GI/SSR:** baked GI stays default; SSR yes, SSGI later, no ray tracing yet?
6. **What parity means:** all ~70 items or a cut list. Candidates to drop: Tree LOD lighting, Sky Sync, Light Limit Fix (all moot), frame generation, DoF/lens/motion blur.
7. **Credit** Community Shaders in docs as inspiration (no code or text used)?
8. **Weather editor** (Eden Studio) with D5 or later.
9. **Approve 0364 as an umbrella**, with per-stage specs written only as stages are chosen (as 0008 became tasks).
