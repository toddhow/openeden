# Skyrim SE: the full implementation plan

Status: DRAFT, for toddhow. Docs only: nothing was built, run or changed to write it.
Date: 2026-11-11. Author: an agent working from the repository at `339c3b78f8` (main).

**How to read this.** Plain language. A term is explained the first time it appears. Every estimate is in *working days of one agent-assisted developer*, as a range, and is **rough: treat every number as plus or minus 50 percent** (more where marked "unknown"). A fact about Skyrim that was not checked in this repository's code or in the user's installed data is marked **RECALLED-VERIFY**: it comes from memory and must be checked before work starts on it. Facts about the repository are cited by file and section, or by file and function.

Sources: mostly the `CLAUDE.md` changelog rows, with a few files read directly; the other documents named here were not all opened when this draft was written, so check a claim against its source before relying on it. Documents referred to: `CLAUDE.md` (guide and changelog rows), `docs/design/game-packs.md`, `docs/design/neutral-world-model.md` (sections 5, 9, 10), decisions 0013 to 0016, tasks 0081 to 0086, 0093, 0094, 0097, 0098, 0099, 0102 to 0113, research notes 0036, 0043, 0044, 0045, 0046, 0051, and a skim of `game_packs/skyrimse`, `extensions/compat_skyrimse`, `components/nifscene`, `components/script_papyrus`, `components/worldmodel`.

---

## 1. Goal, and what "full Skyrim" means

"Full Skyrim" is not one finish line. This plan proposes **levels of done**, each a place toddhow can stop and still have something real. Every level keeps the Morrowind invariant (section 3.6): with the new settings off, Morrowind plays exactly as before.

| Level | Name | What you can do | Milestones (section 5) |
| --- | --- | --- | --- |
| L0 | Viewer | Look at Skyrim data in Eden Studio | done (task 0086) |
| L1 | Walkable world | Walk through lit interiors, through doors, across Whiterun's exterior with terrain, water and sky | M-S1, M-S2 |
| L2 | Living world | NPCs stand, idle, walk their routes and talk | M-S3, M-S4 |
| L3 | Playable | Fight, cast, loot, trade; a first quest runs from an alternate start | M-S5, M-S6 |
| L4 | Base game | A full playthrough of Skyrim.esm's main and side content, saving and loading | M-S7 |
| L5 | Complete | Dawnguard, Hearthfires, Dragonborn | M-S8 |
| L6 | Ecosystem | Real mod lists (plugins, BSA/BA2 archives, loose files, MO2 layouts); script extender (SKSE) expectations met in tiers | M-S9 |

**Honest size.** Adding the workstreams of section 4 gives roughly **800 to 1,450 working days** to L6 and roughly **480 to 830** to L3 (section 5.2). That is several developer-years even with agents. Skyrim is a large game; the plan's job is to make each slice demonstrable so the work can stop or change course early.

**Out of scope, or a later tier.** SKSE-style native plugins are out of scope until decision 0014's tiered extender policy reaches them (section 4.20). Creation Club content, VR, console versions and a bit-for-bit copy of Bethesda's engine behaviour are not goals: where Skyrim's rules are not written down we match observed behaviour on the user's data, and say where we guessed.

---

## 2. What exists today

All rows come from the guide's changelog and the repository. "Evidence" is what the changelog says was shown.

| Area | State | Evidence | Task or commit |
| --- | --- | --- | --- |
| Plugin files (TES4 family, ESM/ESP/ESL) | Done: header, masters, flags, GRUP structure, compressed records, light "FE xxx" ids, localised strings | Five official masters: 1,178,642 records, 0 failed; 535-plugin MO2 list parses with 1 damaged record | 0083, 0086 (`components/contentsource`, `game_packs/skyrimse/src/records.cpp`) |
| Record decoding | Partial: WRLD, CELL, REFR, ACHR, STAT, NPC_, ARMO, WEAP, MISC, LVLI, LVLN, more base types (ACTI, CONT, DOOR, FLOR, FURN, LIGH, MSTT, TREE, TACT); owner, lock, teleport (XOWN, XLOC, XTEL) | Layouts verified on installed data; 100 `Skyrim*` tests | 0086, 0099 N4 (decoding half) |
| Archives | BSA v105 (LZ4) and Fallout 4 BA2 listed and read through VFS factories | 93 Skyrim BSAs, 181,654 files, 0 refused | 0083 stage 3 (`components/archivekinds`) |
| Strings | `.STRINGS`, `.DLSTRINGS`, `.ILSTRINGS` read | 11,638 localised names found | 0086 |
| NIF (Skyrim SE meshes) | Reads `BSTriShape`, `BSDynamicTriShape`, `BSSubIndexTriShape`, packed vertices, skin partitions, shader and texture-set records into `Nif::Scene`. No `bhk` collision in the scene | 197 of 197 vanilla files; 4,452 of 4,456 body files | 0082 (`components/nifscene`) |
| World index | Lazy cells over the load order, later plugin wins, deleted placements removed | 73,723 cells, 42,061 base forms in 2.0 s; 2,786 cells (522,288 objects) in 0.8 s | 0086 (`world_view.cpp`) |
| Neutral world model | Frozen at 1.0 (types, source interface); core store; `Ptr`-based engine consumption of one interior; `[Game] neutral world` off by default | 694 of 697 Bannered Mare objects inserted; Morrowind frames unchanged | 0099, 0100, 0102, 0103, 0105, 0107, 0109 |
| Rendering of Skyrim statics | Done for static meshes: node transforms baked, textures through the VFS, alpha test/blend, double sided. No lights, no shader types beyond diffuse | Bannered Mare drawn: 207 models, 356 draws, ~101,000 triangles, 0 textures missing | 0110 N3a-4a |
| Collision | Done from visual triangles (one Jolt mesh per model); markers and some models fall back to boxes. Not Havok data | Player stands on the floor, stopped by the bar counter and chairs; stairs and doors **not shown** | 0110 N3a-4b |
| Lights | **In progress** (N3a-5) | not yet | 0110 / design doc 10.6 |
| Early index | **In progress** (N3a-3c, draft): start the pack's index before the engine's content load | draft | 0113 |
| Papyrus VM | Reader and validator for `.pex`; VM core (objects, states, events, arrays, structs, heap, budget); scheduler and latent calls; save state; 22 pure natives; **not wired into the game** | Every installed Skyrim SE script (15,231) parses; 15,155 of 15,162 game scripts run to the end against recorder natives; pure natives cover 4.67 percent of native call sites | 0093 (`components/script_papyrus`) |
| ObScript (Fallout 3 and New Vegas scripts) | Not Skyrim; same family of work | | 0092 |
| Rules (gameplay) | **Null**: the pack's `start()` registers the probe and fails with "Skyrim SE rules are not implemented" unless the neutral world consumes the source; `GameRules` has only crafting | | 0084, 0086 |
| Game-pack tier, save chunks, bindings | Built: `kind: "game"`, SDK 4.0, save chunk codec (not yet hooked into `.omwsave`), Lua descriptor service A to C | | 0084, 0097 |
| Skeleton knowledge | Measured: XP32 and ZeX extended skeletons, role tables, inverse-bind corrections; mapping design | Research note 0036; `components/body` skeleton map | 0070 |
| Eden Studio | Skyrim cell viewer: real meshes, mean-colour textures; reads the neutral store | Frames of the inn, Dragonsreach, Whiterun | 0086 stage e, 0102 |
| UI system | Decision 0015 accepted (RmlUi first, own Lua component library); first slice drawn over the game; input, scaling done; Lua UI compatibility harness | Debug panel drawn; 82 `UiRml*` tests | 0104, 0111, 0112 (nothing Skyrim-specific) |
| Weather | Planned (0094) and several pieces exist for Morrowind (atmosphere, volumetric clouds, cirrus, wetness, lit precipitation) | | 0094 and others |
| Animation | CreativeMotion compiled into the engine; ragdolls for dead NPCs (humanoid rig, first slice); IK, inertial locomotion | | 0006, 0007, 0106, 0108 |
| Facial speech curves | `components/face`, format and driver for the stock head; engine wiring done behind a setting | | 0071 |
| Havok data | Not read. `components/nif/physics.*` parses Morrowind-era Havok records; whether it reads Skyrim's `bhkCompressedMeshShape` and MOPP data is **unchecked** | | 0110 section 10.8 |

**One-line summary.** Skyrim SE today is a *file reader and a static-scene viewer in the engine*: it reads everything it needs to show one interior, draws and collides with it, and has no rules, no actors, no scripts running, no exteriors, no sound and no UI.

---

## 3. The architecture end-state

### 3.1 The tiers (decision 0013, `docs/design/game-packs.md` section 2)

```text
hosts (openeden, Eden Studio) --> core
game pack (game_skyrimse)  --> SDK + contracts (kind "game": may write save chunks, own record types, run the script language, drive mechanics)
compat pack (compat_skyrimse, an extension) --> SDK only (data conventions)
common compat pack (formats) --> SDK only (ESM/ESP/TES4 family, archives, NIF of any version, glTF, script runtimes)
core --> no pack, no extension, ever
```

**Plain meaning.** The *core* is groundwork that names no game: physics (Jolt), rendering (Vulkan/VSG), audio, input, the frame loop, files. A *game pack* is one game's rules. A *compatibility pack* adapts one game's data conventions. The *common compatibility pack* is the game-neutral file readers.

### 3.2 What lives where

| Piece | Home | Why |
| --- | --- | --- |
| TES4-family plugin reader, BSA/BA2, NIF scene, `.pex` reader, Papyrus VM, ObScript runtime, string tables, DDS/BC readers | **common compat pack / formats libraries** (`components/contentsource`, `archivekinds`, `nifscene`, `script_papyrus`) | Used by Skyrim SE, Fallout 4, and Eden Studio. No Skyrim rules |
| Rendering, physics (Jolt), audio mixer, VFS, frame loop, input, the neutral world store, Ptr-based runtime objects, the UI toolkit (RmlUi + Lua component library), animation runtime (CreativeMotion), navigator (recast/detour), weather rendering, LOD baker | **core** | Game-neutral machinery. A feature never moves into the core to help a pack; the host API grows instead (CLAUDE.md architecture rules) |
| Skeleton and bone names, `.nif` conventions, unit scale, body-part naming, game-specific shader flag meanings, FormID/EditorID conventions | **compat_skyrimse** (extension, `openeden.game`) | Data conventions; same role as `compat_morrowind` |
| Records and what they mean: actor values, perks, conditions, combat, magic, AI packages, dialogue, quests, story manager, crime, crafting, economy, the save chunk, the Papyrus *natives*, the UI *screens* | **game_skyrimse** | Rules; also the only tier allowed to write saves |
| Lua namespaces `core.skyrimse.*` | game pack, through the descriptor service (decision 0014, task 0097) | Lets Lua UI and mods read Skyrim state |

### 3.3 How the engine pieces fit

- **Neutral world model** (decision 0016, `neutral-world-model.md`). Each pack feeds a game-neutral description of cells and objects (`WorldCell`, `WorldObject`, a double-precision transform, optional owner, lock, teleport components). The engine keeps `Ptr` (a handle to a placed object, the engine's universal pointer-like type) as the runtime handle; a new record type `NeutralBase` and class `MWClass::Neutral` let a Skyrim object be a `Ptr` without a Morrowind record behind it (N3a-2, N3a-3). Morrowind itself never goes through the neutral model (adopted answer 6).
- **`Ptr` and `MWWorld` classes.** For Skyrim, actor and item behaviour is *not* implemented by inheriting Morrowind's `MWClass::Npc`. Plan: Neutral objects carry a typed *pack payload*, and the game pack supplies behaviour through the `Class`-like hooks and `MechanicsHooks` of `game-packs.md` section 5.5, extended as needed (additive SDK changes). This is the largest design decision for actors (section 7, D3).
- **`ESMStore`.** Stays Morrowind's store. Skyrim records live in the pack's own store, built from `Content::Plugin` records; the engine's ESM4 experimental path (cc9cii's reader inherited from OpenMW) is retired at N7. We use upstream OpenMW's ESM4 work only as a reference (adopted 2026-11-10, task 0101 stays DRAFT; note 0051).
- **Saves.** Our own save format through the pack's save chunks (decision 0013). The engine's session identity table maps neutral objects to stable ids (N6).
- **Scripts (Papyrus).** The VM is a common-pack runtime (decision 0014); the game pack supplies *world natives*, the event wiring and the script-state chunk. Lua is for mods and our UI; Papyrus is for Skyrim's own scripts.
- **UI.** Skyrim menus are Scaleform (Flash `.swf`). We do **not** run SWF. Every menu is rebuilt in RmlUi documents plus our Lua component library (decision 0015), reading state through `core.skyrimse.*` namespaces.
- **Rendering.** Skyrim's shaders become pipelines in our renderer (Slang/GLSL, PBR-leaning, HDR, TAA, volumetrics already exist for Morrowind); NIF shader types map to material parameters.

### 3.4 What changes in the core (additive, in order)

1. A way for a pack to **start before the engine's content load** (N3a-3c, option A).
2. `LightComponent` and more neutral components (N3a-5, additive SDK 1.x).
3. **Exterior cells and terrain in the neutral model** (the model has no terrain yet; R7 of the design doc).
4. **Actor and item typed handles with pack payloads** (N5).
5. Mechanics hooks wide enough for combat, magic and AI (extension of `game-packs.md` 5.5, probably SDK 4.1 or 5.0; decision needed, D3).
6. Animation graph hosting (CreativeMotion graphs for another skeleton).
7. A conditions evaluation service shared by dialogue, packages, perks and magic (pack-side, not core).

### 3.5 What stays outside the core on purpose

Rules (damage formulas, perks), records meaning, quest logic, Papyrus natives, UI screens: all in `game_skyrimse`. If a feature seems to need the core, the host API grows instead.

### 3.6 The Morrowind invariant

Every step: with `[Game] neutral world` off and `game=morrowind`, behaviour and frames equal the previous commit (compared by the existing `worldobjects_check` dump and fixed-time frame SHA-256, as the N3a rows did). Every new setting is off by default, sits in `tools/testprofile/profile.cfg` or `exclude.txt`, and any change to an inherited file needs a task-spec reason.

---

## 4. Workstreams

Format per workstream: **Scope / formats / approach / depends on / legal / risks / milestones / estimate**. "NN" in estimates means working days. The estimate column covers everything needed for L4 (base game) unless noted. DLC-only additions are in M-S8.

### 4.1 World: interiors (mostly done)

- **Scope.** Interior cells (CELL with DATA interior flag), reference placement, lighting template (LTMP), cell flags (e.g. "can't wait", "public"), encounter zones (ECZN), acoustic spaces (ASPC), image spaces (IMGS). **Formats:** CELL, REFR, ACHR, LGTM, XCLL (interior lighting), XCLC, XEZN.
- **Approach.** Extend the existing neutral cell load (N3a); add the interior lighting block to the model as an additive component; apply fog/ambient/directional from XCLL (layout RECALLED-VERIFY, must be checked on data).
- **Depends on.** N3a-5 lights; N4 doors.
- **Legal.** None beyond reading the user's own data.
- **Risks.** Cell *streaming granularity*: Skyrim interiors can have thousands of references (the inn has 697; large interiors more).
- **Milestones.** Lit inn with correct ambient (M-S1).
- **Estimate.** 8 to 15 beyond the in-progress N3a-5/6/7 (which are budgeted separately in M-S0).

### 4.2 World: doors and cell transitions

- **Scope.** XTEL teleports, locked doors (XLOC levels, keys), load doors, interior-to-exterior, cell attach/detach with the player, "load screen" content (LSCR).
- **Approach.** N4 engine half: door class on top of `MWClass::Neutral` using the already-decoded `TeleportComponent`; reuse the engine's cell-change machinery, driven by neutral cell lookups (`findCell` by EditorID and by grid). Verified: all 2,204 master doors lead back (changelog).
- **Depends on.** N3a-3 (cell in the engine), N4 decoding (done).
- **Risks.** Persistent references and cell attach order; worldspace-to-interior parent links (door destinations name a REFR; its cell must be found: the index already records the door's cell).
- **Milestones.** M-S1: a door between two interiors works.
- **Estimate.** 5 to 10.

### 4.3 World: exteriors and worldspaces

- **Scope.** WRLD (worldspaces), exterior CELL grid with LAND (heightmap VHGT, normals VNML, vertex colours VCLR, texture layers BTXT/ATXT/VTXT), LTEX/TXST ground textures, the quad structure (four quadrants per cell), exterior placed objects, persistent references of a worldspace (the index already maps them to squares), worldspace parent/climate/water data, land height ranges.
- **Approach.** (1) Put terrain into the neutral model as a new additive component or source method (R7): a height grid, normals, colours, per-quadrant texture layer lists and blend. (2) Feed the existing terrain system (`Terrain::Storage`, built on Morrowind's 65x65 cell grid per cell; Skyrim LAND is 33x33 vertices per cell, RECALLED-VERIFY) through a neutral `TerrainSource`. (3) Stream cells around the player using the existing exterior-cell loader against neutral cells. (4) Skyrim cell size is 4096 game units with a different vertex spacing than Morrowind's 8192 (RECALLED-VERIFY: Morrowind's cell is 8192 units: `Constants::CellSizeInUnits`).
- **Depends on.** M-S1; terrain texture layers need the ground-texture pipeline (diffuse/normal/_p parallax/_msn); weather and sky (4.5) for a believable exterior.
- **Legal.** None.
- **Risks.** The terrain system assumes Morrowind's cell size and layout in many places (`Terrain::Storage`, `ESMTerrain`, LOD tile baker); the world LOD baker (task 0055 and the tile pack) reads Morrowind's `ESMStore`. This is the single largest *engine-structure* risk in the world track, hence the wide range.
- **Milestones.** M-S2: Whiterun's exterior with terrain.
- **Estimate.** 30 to 55.

### 4.4 World: navmesh

- **Scope.** NAVM records (Skyrim ships a pre-built navigation mesh per cell with portals, edge links and door links) versus generating with recast.
- **Options.** (a) **Import NAVM** triangles/edges into the engine's navigator: matches the original's paths, doors and special links (ledges, doors); needs the NAVM format (RECALLED-VERIFY: vertices, triangles with edge links, cover edges, door triangles) and a path planner over triangle edges rather than recast's tiles. (b) **Generate with recast** from our collision geometry (what OpenMW already does via `components/detournavigator`): no new format, but results differ from the original (door tolerance, steep stairs), and Skyrim's collision is visual triangles in the first pass (4.8) which recast may navigate poorly (risk noted in design doc 10.8).
- **Recommendation.** Start with (b) for M-S3 (the engine already has it, and it needs no format work); read NAVM as an *optional input to restrict or validate* later (decision D8). Do (a) only if (b) breaks on real stairs and furniture.
- **Depends on.** Collision (4.8), doors.
- **Legal.** Reading a game data format from the user's files is the same as reading any record. No assets copied.
- **Risks.** Generated meshes vs. Skyrim's authored ones (dungeons, Whiterun gates); performance of tiled generation at exterior scale.
- **Estimate.** (b) 10 to 20; add (a) 20 to 35 if wanted.

### 4.5 World: water, weather, sky, climate, image spaces

- **Scope.** WATR (water types: colours, fresnel, wave params, shallow/deep, reflectivity), cell water height (XCLW), worldspace water; WTHR (weather: cloud layers, colours by time of day, fog, sun glare, precipitation, wind, thunder), CLMT (climate: weather list with chances, sun/moon textures, time-of-day timing), LGTM, IMGS (image space: HDR, cinematic, tint), IMAD (modifiers), Skyrim "sun glare", moons (Masser/Secunda RECALLED-VERIFY for names), aurora.
- **Approach.** Map to the engine's existing weather and atmosphere systems (task 0094 plans the weather system; Morrowind's `WeatherManager` is the model). Add a neutral `WeatherSource` interface in the SDK; the pack decodes WTHR/CLMT and drives a weather state contract (storminess, cloud base, rain rate) into the existing atmosphere, volumetric cloud and wetness systems. Water: the engine's water shader already exists; add per-type parameters.
- **Depends on.** 0094 W0 (weather state contract); exteriors (4.3).
- **Legal.** None.
- **Risks.** WTHR is a large record with many layers (cloud textures per layer, RECALLED-VERIFY: up to 32 layers); mapping to volumetric clouds is design work.
- **Milestones.** M-S2: sky and a sun/moon cycle with climate weather; later: full weather types and transitions.
- **Estimate.** Water 8 to 15; sky/weather/climate 20 to 35; image spaces 5 to 10. Total 33 to 60.

### 4.6 World: lights, shadows, LOD, occlusion

- **Lights (N3a-5, in progress).** LIGH records (radius, colour, fade, flags: flicker, pulse, shadow-casting types), `LightComponent` additive SDK. Hundreds of lights in a cell: the clustered light grid (task 0072) caps 50 per cluster and keeps the first 50 by index (a decision noted there); Skyrim interiors are lit by many small lights. **Estimate beyond N3a-5:** 5 to 10 (flicker/pulse, shadow casters, light *fade* distance, "no shadows" flags).
- **LOD.** Skyrim ships *pre-built* LOD: terrain LOD (`.btr` and `.bto` object LOD meshes, `LODSettings`, tree LOD billboard atlas; RECALLED-VERIFY names and layouts) produced by xLODGen/DynDOLOD-style tools for mod lists and by Bethesda for the base game. Our own world LOD baker (tasks 0055 to 0066, 0059, 0096) bakes terrain tiles and object clusters from the live data. **Options:** (a) read Skyrim's LOD files clean-room from the user's data (format RECALLED-VERIFY; they are NIF-wrapped meshes, so partly `nifscene`); (b) **use our own baker on the neutral model** (recommended: it already exists, is format-neutral and handles mod lists without per-list LOD generation). (b) needs the baker to read the neutral model instead of `ESMStore` (a refactor of `lodobjectbaker`, `LodTerrain`). **Estimate:** (b) 15 to 30; (a) as a fallback for huge mod lists later 15 to 25.
- **Occlusion.** Skyrim's room markers/portals (XPRM, XPOD, XRMR, occlusion planes OCPL; RECALLED-VERIFY) are an optimisation. Our renderer culls by frustum and distance; the interior sets are small enough at first. Defer; **estimate** 5 to 10 if profiling shows the need.
- **Risks.** Light counts vs. frame budget; LOD baker's Morrowind assumptions.
- **Estimate (4.6 total).** 25 to 50.

### 4.7 Rendering: shader types, materials, textures

- **Scope.** `BSLightingShaderProperty` shader types (RECALLED-VERIFY: 0 default, 1 env map, 2 glow, 3 parallax, 4 face tint, 5 skin tint, 6 hair tint, 7 parallax occlusion, 8 multitexture landscape, 9 LOD landscape, 10 snow, 11 multilayer parallax, 12 tree anim, 13 LOD objects, 14 sparkle snow, 15 LOD objects HD, 16 eye, 17 cloud, 18 LOD noise, 19 LOD landscape noise, 20 LOD landscape blend; Fallout 4 differs). `BSEffectShaderProperty` (fire, magic, VFX with falloff, soft particles), decals (`BSShaderProperty` flags, projected decals), texture set slots (diffuse, normal, glow/skin, height, environment, environment mask, subsurface/inner layer, backlight/specular), flags (vertex colours, double sided, alpha test/blend, skinned, soft lighting, rim lighting, back lighting, refraction, model-space normals).
- **Textures.** BC1/BC2/BC3/BC4/BC5/BC7 DDS (already: BC7 decoder `bc7decode.hpp`, mip streaming, array conversion); normal maps in tangent space (green = up convention, RECALLED-VERIFY), model-space normals (`_msn`) for some meshes, `_s` specular/gloss in Skyrim SE (RECALLED-VERIFY), `_g` glow, `_sk` skin, `_e` environment, `_em` environment mask.
- **Approach.** One mapping table *shader type -> engine pipeline modes* in `compat_skyrimse` data (a JSON, canonical writer) with a PBR-leaning default: Skyrim's lighting is Blinn-Phong-ish; we convert (smoothness/specular -> roughness/F0 approximation) behind a setting. Skin/hair/eye/tint use the existing subsurface-skin (`sss`) and tint mechanisms; multilayer parallax and parallax occlusion map onto the existing parallax-occlusion shader (task 0045); snow maps to the cover system (0039). Vertex animation (trees, flags) maps to the sway system (0096 stage c). Effect shaders need a particle/effect pipeline; fire and magic VFX are NIF particle systems (`NiParticleSystem`, `BSStripParticleSystem`, controllers) that nifscene does not yet cover (RECALLED-VERIFY: not in `Nif::Scene`).
- **Depends on.** N3a-4a (done: diffuse+alpha), the VSG renderer.
- **Legal.** Skyrim's shader *binaries* are never read; we do not use them. Visual parity is by observation of output.
- **Risks.** Fidelity vs. effort: "how close to Skyrim's look must the first pass be?" is open (design doc 10.8). Particle/VFX volume is large.
- **Milestones.** M-S1 (diffuse+normal+alpha+lights), M-S2 (terrain layers), M-S3 (skin/hair/eye), M-S5 (effects), M-S7 (the rest).
- **Estimate.** Material mapping and normal/spec/glow/env 15 to 25; skin/hair/eye/tint 8 to 15; parallax/snow/multilayer 8 to 15; effect shaders + particles 15 to 30; decals 5 to 10. Total 51 to 95.

### 4.8 Facegen

- **Scope.** NPC faces: generated head NIFs under `meshes\actors\character\FaceGenData\FaceGeom\<plugin>\<formid>.nif`, tint masks `textures\actors\character\FaceGenData\FaceTint\<plugin>\<formid>.dds`, plus the race head parts (HDPT), tint layers (RACE/NPC_ `TINT`, `QNAM`, `NAM9`), and for the player (and un-baked NPCs) runtime morphs from FaceGen coefficient data (`.tri` files and `FaceGenData` in NPC_, RECALLED-VERIFY). Facegen for vanilla NPCs is *pre-baked in the BSAs*; mod NPCs need baked facegen from the CK (usually shipped).
- **Approach.** Phase 1: load baked head meshes and tint mask, composite skin tint in the shader (`BSLightingShaderProperty` type 4 face tint). Phase 2 (later, only if wanted): player/character creation with `.tri` morph targets (a clean-room reader from the data; the `.tri` morph format is documented by modding tools, RECALLED-VERIFY).
- **Depends on.** Skeleton (4.10), shaders (4.7), NPC placement (N5).
- **Legal.** User-supplied assets only.
- **Risks.** Mod NPCs without baked facegen render with default heads (an acceptable fallback); race menu fidelity is a later, large item.
- **Estimate.** Phase 1: 8 to 15. Phase 2: 25 to 45 (optional, part of "player character creation" in D6).

### 4.9 Physics: Havok collision (`bhk`) data

- **The problem.** Skyrim collision lives in `bhk*` blocks inside NIFs (`bhkCollisionObject`, `bhkRigidBody`, `bhkCompressedMeshShape`, MOPP trees, constraints). These are Havok data formats.
- **Legal and clean-room position.** The guardrails forbid the Havok SDK and decompiled code. They do **not** forbid reading a data block from the user's own file by our own code written from inspection of the data and from public format descriptions of the *file layout* (the NIF block layout is documented by community schema files such as `nif.xml`; its licence was a GPL-3.0 repository at the pinned commit per note 0044, so it is an *oracle* we compare against, not a thing we copy). We never include Havok headers or linked libraries. RECALLED-VERIFY: whether a `bhkCompressedMeshShape`'s compressed data (chunks with quantised vertices and indices) is fully described by the schema; the MOPP *code* (a bytecode for fast queries) is not needed to build a mesh from the chunks.
- **Options.** (A) Visual triangles (what N3a-4b does today): free, loses stairs/clutter fidelity and collides with decoration. (B) **Read `bhk` shapes into Jolt shapes** (boxes, spheres, capsules, convex hulls, compressed meshes -> mesh shapes): the best fidelity, the schema is the oracle. (C) Bake a convex hull or simplified collision from visual geometry per model (like our LOD baker), cached.
- **Recommendation.** Keep (A) as the default now; add (B) as its own task (3 stages: primitives and convex; compressed mesh; rigid body properties and constraints). Do (C) only for models with no `bhk` data. `bhkRigidBody` mass/friction/restitution feed Jolt body creation for dynamic objects (see 4.12).
- **Depends on.** `nifscene` (needs the blocks added), Jolt shape builder (`components/nifjolt`).
- **Risks.** Havok units (1 unit = 7 game units? RECALLED-VERIFY, commonly cited) and the many shape types; fuzzy filtering (collision layers/groups map to Jolt layers, with `CollisionFilterInfo` bit layout RECALLED-VERIFY).
- **Estimate.** (B): 25 to 50. (C): 8 to 15.

### 4.10 Physics: ragdolls, furniture, markers, dynamic objects

- **Ragdolls.** CreativeMotion already has a humanoid ragdoll rig and `RagdollHost` for dead NPCs (task 0106); Skyrim's skeleton bone names differ (`NPC Pelvis [Pelv]`, etc., from note 0036: both games wrap every bone in zero-offset nodes). Needs the Skyrim skeleton profile (`opas.skeletonprofile` v2 or the `*.skeletonmap.json` of task 0070), per-race rigs (humanoid, then creature skeletons: wolves, dragons, spiders), and `bhkRagdollConstraint` data from the skeleton NIF as an optional source of limits (RECALLED-VERIFY; else CreativeMotion's own limits).
- **Furniture and markers.** FURN with `BSFurnitureMarkerNode` (sit/sleep/lean/work positions, entry/exit directions), idle markers, `BSBehaviorGraphExtraData`. Needed for packages (sitting NPCs, beds) and the player using furniture.
- **Dynamic objects** (MSTT, activators with `bhk` rigid bodies, clutter): moved props are common in Skyrim; save only changed state.
- **Estimate.** Ragdoll humanoid skeleton and creatures 15 to 30; furniture and markers 8 to 15; dynamic clutter and havok-like properties 8 to 15. Total 31 to 60.

### 4.11 Actors: the skeleton, body, equipment and first person

- **Skeleton.** Research note 0036: XP32 (4 skeletons, 542 to 649 bones) and ZeX (374 bones); Skyrim SE skinned `BSTriShape` shapes store 0 vertices (vertices are in `NiSkinPartition`; filled in `nifscene`); the zero-offset wrapper nodes; per-role rest correction (rotations up to 163 degrees) and axis conventions. Vanilla skeleton: the pack's contract names four bones (head, spine, root, and one more) in `compat_skyrimse`. **Approach:** use the `skeletonmap.json` design (task 0070) with a Skyrim table shipped as pack data; stock body on any failure.
- **Body and equipment.** ARMO/ARMA (armour addons: per-race, per-sex models), biped object slots (30 to 61: head, hair, body, hands, forearms, amulet, ring, feet, calves, shield, tail, long hair, circlet, ears, ..., RECALLED-VERIFY for each number), slot conflicts and hiding rules, outfit (OTFT) resolution, template NPC inheritance (TPLT with use flags), race body parts, weight morphs (`_0.nif` and `_1.nif` body pairs blended by NPC weight; RECALLED-VERIFY), armour rating/weight classes (rules, 4.15). Visual assembly has a precedent: the AdvancedBodies merged body (task 0069) merges legacy Morrowind parts; Skyrim is *simpler* (one body mesh + addons) and the merged-body machinery can be reused for performance.
- **First person.** Skyrim has separate first-person arm skeletons/meshes (`1stPerson` skeleton and `_1st` models) and weapon positions; the engine already has first-person handling for Morrowind's `.1st`; conventions come from `compat_skyrimse` (`GameConventions`, move 1 of task 0085, which already named the first-person suffix as a pack value).
- **Depends on.** NIF skin (done), N5 (actors as typed handles), facegen (4.8).
- **Risks.** Mod-list body replacers (CBBE/HIMBO/3BA) with extended skeletons (note 0036): most common mod bodies need the skeleton map; a missing map must degrade to the stock body, not crash.
- **Estimate.** Skeleton + map + body 12 to 25; equipment/ARMA/outfit/slots/templates 15 to 25; first person 8 to 15; weight morph 4 to 8. Total 39 to 73.

### 4.12 Actors: animation (`.hkx`) and behaviour graphs

- **The problem.** Skyrim animation = Havok *packfiles* (`.hkx`): skeleton (`hkaSkeleton`), animations (`hkaSplineCompressedAnimation` typically, RECALLED-VERIFY), and the **behaviour graph** (`hkbBehaviorGraph`: a state machine of clip generators, blend nodes, variables and events, drives idle/walk/combat/magic). Skyrim also has separate `Character` and `Project` files. Havok owns the file formats and the runtime that evaluates them.
- **Legal and clean-room position.** We never use the Havok SDK, its headers, or any decompilation (guardrails). Packfile *container* layout is publicly described by third-party tools and is simply a header + sections + fix-up tables (RECALLED-VERIFY; exact version "hk_2010.2.0-r1" for Skyrim LE and a 64-bit variant for SE, and tagfile format for newer). Reading user-supplied data with a reader we write from inspection is allowed; we do not ship Bethesda assets. Reading behaviour graphs is much larger than reading clips because the semantics (event/variable ids, transitions, blend trees, ragdoll, IK nodes) are defined by the Havok runtime and are not otherwise documented; a faithful implementation would be a large reverse-engineering effort whose provenance is hard to prove clean.
- **Options.**
  - **O1: our own graph, no behaviour files.** CreativeMotion's graph and style layer (tasks 0053, 0067), driven by Skyrim *clips*. Needs only clip data (O2).
  - **O2: convert clips with a user-run converter.** A tool that runs on the user's machine, reads `hkaSplineCompressedAnimation` (and the skeleton) from the user's `.hkx` files, and writes our own format (glTF or `opas` clips). Same pattern as the bakers: derived data in the user's folder, never committed or shipped.
  - **O3: read behaviour graphs and run them.** Faithful but a very large unknown, and the main risk of the whole animation workstream.
  - **O4: hybrid.** O1+O2 for the vanilla humanoid and creature set; O3 only as a stretch for mod animations (OAR/DAR replacers choose clips by condition, which our graph can honour as data, see `open-animation-replacer` references in 4.20).
- **Recommendation.** **O1 + O2 now; O3 not planned.** The player-visible result is idle/walk/run/turn/attack/block/cast/sneak/swim/ride/death implemented as our graph with Skyrim's clips and event names (`IdleStart`, `attackStart`, ... RECALLED-VERIFY) for compatibility with animation mods that only replace clips. This means mods that replace *behaviour graphs* (Nemesis/FNIS-generated) will not work; mods that replace *clips* (via OAR, DAR) can. Decision D4.
- **Depends on.** Skeleton map (4.11), CreativeMotion graph hosting for Skyrim skeletons, ragdoll (4.10).
- **Risks.** The spline-compressed animation decoder (bit-packed with quantisation; RECALLED-VERIFY exact scheme), root-motion extraction (Skyrim clips carry root motion via a separate extract: RECALLED-VERIFY), annotation tracks (event times: "weaponSwing", "footLeft", "HitFrame"), clip counts (thousands).
- **Estimate.** Packfile reader + spline decoder + converter 25 to 45; Skyrim locomotion/combat graph on CreativeMotion 25 to 45; creatures/dragons/mounts 15 to 30; events/annotations 5 to 10. Total 70 to 130. O3 would add 60 to 150 (unknown) and is not included.

### 4.13 Gameplay rules: actor values, perks, leveling, derived stats

- **Scope.** Actor values (AVIF: health/magicka/stamina, regeneration, carry weight, skills (18), resistances, skill advancement, `ActorValue` indices), races (RACE: starting values, abilities, body data), classes (CLAS), birthsigns do not exist in Skyrim (standing stones are shrine activators; RECALLED-VERIFY), perks (PERK: effects such as "modify actor value", "add leveled list", "mod damage", with conditions; entry points RECALLED-VERIFY ~100 types), perk trees (UI from `skilltree` data), leveling (skill XP, level thresholds, player level scaling of leveled lists and NPCs), difficulty settings, GMST settings (~thousands of GMSTs: formulas read their constants).
- **Approach.** `game_skyrimse` implements an `ActorValueSet` and a **formula library** keyed to GMSTs read from data; perk entry points are implemented as a registry (each entry-point kind a function), evaluated by the conditions service (4.16). Values are pinned by tests derived from documented formulas on small cases; where the UESP-style formulas are not verified in code they are RECALLED-VERIFY (and must be cross-checked against observed numbers on the user's game before being relied on). Avoid copying text from wikis; formulas are mathematical facts, but we cite the data (GMST name) and check on data.
- **Depends on.** N5, conditions, UI.
- **Estimate.** Actor values, race/class, derivation 12 to 20; perks (entry points + effects) 15 to 25; leveling and scaling 6 to 12. Total 33 to 57.

### 4.14 Gameplay rules: combat

- **Scope.** Melee (weapon reach, speed, stagger, power attacks (held), sneak attacks, backstabs, dual wield, unarmed, bash), archery (draw time, zoom, projectile physics: speed/gravity/arc by PROJ, arrow types, bow draw animation), blocking (shield, weapon, timed block, bash, armour interaction), damage resolution (weapon damage, perks, armour rating mitigation (a cap), critical hits, resistances), staggering/knock down, death (ragdoll, essential/protected flags), projectiles (PROJ, EXPL explosions), hit reactions (hit events, stagger anim), weapon enchant discharge on hit, poisons (applied to weapons), shouts as attacks, kill moves (cinematic; defer).
- **Approach.** Hit detection with Jolt (`castShape`/body vs. weapon-reach capsule or the NIF `bhk` weapon shapes) driven by animation annotation events; projectile bodies as Jolt bodies owned by the core and driven by pack rules; the pack decides damage; core owns bodies (decision 0003 stays: extensions read physics; **game packs** drive mechanics, decision 0013).
- **Depends on.** Animation graph (4.12), ragdoll/death, actor values, magic.
- **Risks.** Feel: Skyrim combat is animation-event-driven; timing sloppiness shows immediately.
- **Estimate.** Melee/block/damage 20 to 35; archery/projectiles 12 to 20; stagger/death/hit reactions 8 to 15; power/sneak/crit/dual 8 to 15. Total 48 to 85.

### 4.15 Gameplay rules: magic

- **Scope.** SPEL (spell types: spell, disease, power, lesser power, ability, poison, enchantment, potion, ingredient: RECALLED-VERIFY), MGEF (magic effects; **archetypes**: value modifier, peak value modifier, dual value modifier, script, absorb, stagger, light, invisibility, cloak, summon creature, paralysis, reanimate, banish, cure, disarm, demoralize, frenzy, calm, turn undead, soul trap, vampirism, werewolf, bound weapon, guide, darken... on the order of 40 archetypes, RECALLED-VERIFY), ENCH (enchantments with cost/ charge; `ENIT`), casting (hands, concentration vs. fire-and-forget, delivery: self, touch, aimed, target actor, target location), area (EXPL), duration/magnitude, scaling by skill/perks, magicka cost and casting time, spell tomes, **shouts** (SHOU/WOOP words of power, cooldown, dragon souls, unrelenting force), standing stones, cloak/absorb/reflect, summon limits, soul trap and soul gems, crafting (4.18), effect shaders (EFSH) and art (ARTO), hit effects, sounds.
- **Approach.** A magic effect runtime in the pack: archetype handlers register by kind; an `ActiveEffect` list per actor with deterministic ticks (no wall-clock); visual effects use the effect-shader pipeline (4.7). The engine's Morrowind magic (`MWMechanics::Spells`, `ActiveSpells`) is not reused: it is Morrowind-shaped (spell list, magnitude ranges); neutral `Active effects` live in pack payload.
- **Depends on.** Actor values, conditions, animation (cast anims), VFX.
- **Risks.** Archetype breadth; scripted effects call Papyrus (`OnEffectStart`, `OnEffectFinish` on ActiveMagicEffect scripts), so Papyrus integration (4.19) must be present for scripted spells.
- **Estimate.** Core spell/effect/casting/ENCH 30 to 50; archetypes (full) 15 to 30; shouts 8 to 15; summons/commands (calm, frenzy, command) 8 to 15. Total 61 to 110.

### 4.16 Gameplay rules: conditions (CTDA)

- **What they are.** A *condition* is a small test attached to many records (dialogue lines, perks, spells, AI packages, quests, idle animations, scenes): "player's Speech skill >= 50 and the speaker is in faction X". Each is a CTDA subrecord: operator, comparison value (literal or a global), a **function index** (e.g. `GetActorValue`, `GetInFaction`, `GetQuestVariable`, `GetStage`, `HasPerk`, `GetRandomPercent`...), up to two parameters, a run-on target (subject, target, reference, combat target, linked ref, quest alias), and flags for OR chaining.
- **Scale.** Several hundred functions (RECALLED-VERIFY: on the order of 300 to 400); the engine ships them as compiled code; we need each as a function in the pack. Many are trivial reads once the runtime state exists; some need subsystems (GetIsPlayerInCombat, GetDistance, IsInInterior, GetCurrentWeather). Frequency matters: a handful cover most uses; a data survey (like note 0046's native-call survey) will rank them.
- **Approach.** (1) A **survey tool** over the installed data: count function uses by record type, parameter types and run-on (aggregates only, per repository practice). (2) A condition engine (`components`? no: pack): parse CTDA into an AST at load, evaluate against a `ConditionContext`, with a registry of functions; each function a unit test from a hand-derived case. (3) Unimplemented functions: logged once, evaluate as **false** (strict mode errors in tests), so content degrades rather than breaks.
- **Depends on.** Almost every other rules area; start early and grow (the survey ranks work).
- **Risks.** Size and the long tail; semantics of several functions are subtle (e.g. `GetIsID` vs `GetIsRace`; "subject/target" in different record contexts).
- **Estimate.** Engine + top 80 functions 15 to 25; remaining functions as other workstreams need them (budgeted there) plus tail 15 to 25. Total 30 to 50.

### 4.17 Gameplay rules: inventory, items, economy

- **Scope.** Containers (CONT, inventory lists with leveled lists), item types (WEAP, ARMO, AMMO, MISC, BOOK (skill books, spell tomes, notes), INGR, ALCH, KEYM, SCRL, SLGM), weight and carry weight, equip rules and slots, favourites, item condition does not exist (tempering/improvement: smithing), value, barter (VMAD: merchants' gold, buy/sell multipliers from Speech and perks, "is vendor" faction lists), stealing (owner flags), container respawn, pickpocketing, books and lock-picking minigame (UI), trading UI.
- **Crafting:** alchemy (ingredient effects: first/second discovered, effect combination), smithing (forge/tempering/crafting recipes: COBJ constructible objects with conditions, workbench keyword), enchanting (disenchant, learn effects), cooking (Hearthfires), recharging.
- **Approach.** Reuse `GameRules::makeCraftingRules` (task 0084 I1) as a *shape* (the pack implements the Skyrim variant: `sdk/include/openeden/game/crafting.hpp`), not Morrowind's code; recipes come from COBJ records evaluated by conditions.
- **Depends on.** Actor values, perks, UI (4.22).
- **Estimate.** Inventory/containers/equip 10 to 18; barter/economy/stealing 8 to 12; alchemy/smithing/enchanting 15 to 25; books/lockpicking/misc items 5 to 10. Total 38 to 65.

### 4.18 Gameplay rules: crime, factions, stealth, followers, horses, transformations

- **Crime and law.** Bounties per hold (crime factions), witnesses, guards, arrest/pay/resist/jail, theft, trespass, assault, murder, pickpocket, fines, "Thieves Guild" interplay. Morrowind has a precedent module (`JusticeRules`, task 0098 move 7) which proves the shape, not the rules.
- **Factions.** FACT (ranks, relations, crime gold values, vendor lists, "crime group" flag), reputation-like state via aliases/globals and Papyrus.
- **Stealth and detection.** Sneak skill, detection level (visibility factor based on light, movement, armour, line of sight to eyes; `GetDetected`), noise (sound events from footsteps/combat), detection state (hidden/caution/danger/detected), pickpocketing, sneak attack damage multipliers; AI process uses it (4.19).
- **Followers.** Follow packages, `Follower` alias system, commands (wait, carry, fight), combat behaviours, "essential" companions.
- **Horses.** Mount/dismount (animation), horse as a rideable actor with its own skeleton and behaviour set; DLC adds dragon mounts.
- **Werewolf and vampire.** Race swaps with their own skeleton, abilities, hunger, transformation spell/animation; perk trees (UI); vampire lord (Dawnguard).
- **Estimate.** Crime/factions 15 to 25; stealth/detection 12 to 20; followers 8 to 15; horses 8 to 15; werewolf/vampire 15 to 25. Total 58 to 100.

### 4.19 AI

- **Packages (PACK).** Package types (find, follow, escort, eat, sleep, sandbox, patrol, guard, dialogue, activate, use item, travel, wander, flee, use weapon...), schedules (day/hour/duration: PSDT), locations (`PLDT`: reference, cell, near-player, in-radius), targets, conditions, flags (must complete, may sleep...), procedure trees (PTDA/package data), package *stacks* priority; **sandbox** behaviour (use furniture, idles, conversation), **combat styles** (CSTY: aggression, ranged/ melee preferences, dodge, flanking), AI *process levels* (high/middle-high/middle-low/low: simulation fidelity by distance to player; also "off-screen simulation" of schedules), detection (4.18), pathfinding on the navmesh (4.4), dialogue and idles (conversations between NPCs, "IdleMarkers").
- **Approach.** Pack-side `AIProcess` owns the package stack, evaluates conditions at tick (cheap checks first), emits movement targets to the core navigator, animation requests to CreativeMotion, and actions to mechanics. Process levels bound cost: low-level actors teleport along schedules or are simply "somewhere in their destination".
- **Depends on.** Navmesh, animation, conditions, furniture, N5 (actors as typed handles), time (game hour/day).
- **Risks.** Emergent complexity and performance (Skyrim exteriors have dozens of active actors); debugging tools (Eden Studio views: package inspector).
- **Estimate.** Core package types + procedures 25 to 40; sandbox/furniture/idles 12 to 20; combat styles and combat AI 10 to 20; process levels/schedules 10 to 20; follower/travel/escort 6 to 12. Total 63 to 112.

### 4.20 Dialogue and quests

- **Dialogue.** DIAL (topics) and INFO (responses), conditions on each INFO, choices, "Say once", "random", link-from/link-to topics, speaker, voice type, response data (emotion, sound/voice file reference `.fuz`/`.lip`), scripts (fragments: Papyrus code attached as `VMAD`), dialogue **branches** (DLBR), camera and conversation framing, "player dialogue" (choices with `SKIP`...), persuasion/ bribe/ intimidate (speech checks), barter/training/travel services from dialogue, shared info, "Dialogue views" (DLVW). Lines-per-NPC selection rule (first INFO in order whose conditions pass; RECALLED-VERIFY; order by priority/ form order in the topic).
- **Quests (QUST).** Stages (INDX) with log entries (CNAM), objectives (QOBJ/ QSDT targets), aliases (ALST/ ALID/ ALFR: forced reference, find matching reference, created reference, location; fill conditions, flags (optional, unique, essential), alias scripts), priority, quest data (DNAM flags: start-game-enabled, run once, allow repeated stages, ...), event-driven start (SMQN/SMBN/SMEN: **story manager** nodes with event-type triggers (`ADDEVT` kinds like "Kill actor", "Enter location", "Close door") and conditions), scenes (SCEN: phases, actions (dialogue, package, timer), conditions; the Helgen intro and many quests are scenes), **radiant quests** (Dynamic quests: Radiant location/ NPC fill via `SMQN` plus quest templates; random locations and the `RadiantQuest` framework), quest targets for the compass/map.
- **Papyrus integration** (see also 4.21): quest/alias/stage fragment scripts, `OnInit/OnUpdate/OnStageSet`, script properties filled from VMAD (fill by form/ alias/ int/...), event dispatch for `OnHit`, `OnDeath`, `OnLocationChange`, `OnTriggerEnter`, `OnActivate`, `OnItemAdded`... (hundreds of event hooks; RECALLED-VERIFY; the survey of used events from installed `.pex` will rank).
- **Approach.** A quest runtime in the pack: stage state per quest, alias fill engine (conditions), objective tracking, a story manager event queue, a scene runner (phases/actions timeline). A deterministic event queue feeds Papyrus events.
- **Depends on.** Conditions, AI packages (scene actions), Papyrus wiring, dialogue UI/audio, saves (quest state is a major save chunk).
- **Risks.** Aliases and story manager semantics are the least documented part of Skyrim's game logic; correctness shows as "quest never starts" bugs. The base game has ~2,000 quests (RECALLED-VERIFY; the data will tell).
- **Estimate.** Dialogue runtime 20 to 35; quest stages/objectives/journal 15 to 25; aliases 15 to 25; story manager 12 to 20; scenes 15 to 25; radiant 12 to 25. Total 89 to 155.

### 4.21 Papyrus integration

- **What exists.** `components/script_papyrus`: reads and validates `.pex`; a VM with objects, states, events, arrays, structs, casts, mark-and-sweep heap, instruction budget, scheduler for latent natives, versioned script-state blob; 22 pure natives. All 15,231 installed Skyrim game scripts parse and nearly all run to the end against recorder natives (note 0046). Not connected to any game.
- **To wire.**
  1. **World natives.** About 686 vanilla natives (note 0046); **106 give 90 percent of Bethesda's native call sites** (89.75 percent in the Skyrim SE sample). Categories: Actor, ObjectReference, Form, Quest, Game, Debug, Utility, Math (done), Spell/MagicEffect/Perk/Faction, Cell/Location/WorldSpace, Weapon/Armor/Ingredient/Potion/Book, Input/UI, Sound/Music, Weather, Keyword, Alias/ReferenceAlias/LocationAlias, Scene, Package, Race, Outfit, Quest, ActorBase, FormList, Container, Door, Light, Furniture, Hazard, Projectile/Explosion, Shout, WordOfPower, Message, Enchantment, MiscObject... Each native needs the right subsystem (this is why Papyrus is "after" the gameplay workstreams, not before).
  2. **Event dispatch** from the engine (OnInit, OnLoad, OnCellAttach/Detach, OnActivate, OnHit, OnDeath, ...). Fixed ordering and deterministic delivery (rules from the guide: no wall-clock, no unordered iteration).
  3. **Properties filling** (VMAD at load): the object's script properties from the record; aliases; fragments (quest stage fragments, dialogue INFO fragments (begin/end), scene fragments, package fragments, perk fragments, topic info `OnBegin/OnEnd`).
  4. **Script state in saves** (`serialise`/`restore`, built in stage e); attach keys validated against the record store.
  5. **Latent natives and the game clock.** `Utility.Wait` in game time vs real time; `RegisterForSingleUpdate`; script `GoToState`; `Utility.WaitMenuMode`. The scheduler exists; time sources come from the game.
  6. **Threading.** Papyrus runs on the main thread in a fixed phase of the game pack; natives that touch the world run there; long scripts hit the instruction budget and are resumed (stage d).
- **Script extender expectations** (decision 0014 tiers): vanilla natives; **tier 1: built-in compatible natives** for the widely used SKSE-added functions with a no-op or implemented behaviour (the survey of installed mods found 17.9 percent of mod native call sites go to non-vanilla functions: SKSE and libraries; 1,506 distinct, note 0046); **tier 2: strict mode** for tests; native SKSE DLLs are out of scope (extender policy).
- **Depends on.** Everything that natives touch; saves; quests.
- **Estimate.** Event + properties + fragments wiring 15 to 25; top 106 natives 25 to 40; remaining natives (a long tail, grown on demand) 40 to 80; SKSE tier 1 (top ~50 by usage) 10 to 20; tests and gated real-data runs 8 to 15. Total 98 to 180.

### 4.22 Audio

- **Sound.** SNDR (sound descriptors: output model, category, looping, sound files, variance, attenuation), SOUN (legacy), 3D positional audio, reverb/acoustic spaces (ASPC), footstep sets (FSTP/FSTS: by material), material sounds (MATT), ambient sound regions (REGN) and cell ambient, combat/hit sounds from weapon/armour records, spell sounds, activator sounds. The engine's sound manager (OpenAL via the inherited `MWSound`) is Morrowind-shaped; the pack drives neutral sound requests through a `SoundService` contract (additive).
- **Music.** MUSC/MUST (tracks, types: explore/combat/dungeon/town), cell/ location/ combat music selection, crossfades.
- **Voice.** `Sound\Voice\<plugin>\<voicetype>\*.fuz` (a container of an `.xwm` audio and a `.lip` lip-sync file; RECALLED-VERIFY container layout) and `.xwm` (xWMA, a Microsoft format over WMA; **codec options:** decode with ffmpeg's wma decoder (ffmpeg is already a dependency of the video player; licence of the pinned build must be checked per guardrails: LGPL vs GPL configuration) or write a clean xWMA decoder (the XWMA container is documented by Microsoft; the WMA codec is patent-encumbered historically, RECALLED-VERIFY). `.wav`/`.mp3`/`.ogg` loose files for mods are already easy.
- **Lip sync.** `.lip` (phoneme/viseme timing; RECALLED-VERIFY format) -> facial curves (task 0071's `opas.facecurves` v1 format has an importer for Rhubarb/CSV; a `.lip` importer is the clean fit). Default fallback: loudness-driven mouth (already working).
- **Depends on.** Dialogue, AI, combat.
- **Legal.** Codec licence check (D9); no audio from the game ever committed.
- **Estimate.** Sound descriptors/3D/ambient/footsteps/reverb 15 to 25; music 6 to 12; voice (`.fuz`/`.xwm` decode) 8 to 18; lip sync 6 to 12. Total 35 to 67.

### 4.23 Saves

- **Approach.** Our own format through the game pack's save chunks (decision 0013; the codec exists, the hook into `.omwsave` does not, task 0084 I4 built a `GPCH` record path), plus N6: the session identity table (which neutral object is which), a changed load order detected and refused (or repaired), and pack chunk categories: world changes (moved/disabled/deleted references, container contents, door state, cell flags), actors (stats, packages, inventory, active effects, AI state), quests/aliases/stages/globals, story manager state, Papyrus VM state (stage e blob), player state, crime, weather and time.
- **Option (needs toddhow, D7).** `.ess` import: Skyrim's own saves (a compressed, change-form-based format; RECALLED-VERIFY) would let a user continue an existing playthrough. This is a large undertaking (change forms for every record type, global data tables, Papyrus VM state translation) and is **not recommended**; our VM state is not Bethesda's. If wanted: a one-way importer, later, as a separate project (estimate 60 to 120, unknown).
- **Risks.** Save size/time at Skyrim density; versioning; a mod-list change mid-playthrough.
- **Estimate.** 25 to 45 (N6 included at 5 to 8 of it).

### 4.24 UI

Skyrim's UI is Scaleform: SWF files plus ActionScript. We **rebuild every menu** in RmlUi documents with the Lua component library (decision 0015). Layout, text and icons are the author's work from observing the original; no Bethesda artwork is committed (fonts and icons come from the user's installed game data at run time, through the VFS as the `font engine` in `components/ui_rml` already handles bitmap and TrueType faces).

| Screen | Notes | Est. |
| --- | --- | --- |
| HUD (health/magicka/stamina bars, compass with quest markers, crosshair with activate text, enemy health, shout cooldown, sneak eye, subtitles, notifications, hotkey icons) | Low risk; needs compass data from quests | 10 to 18 |
| Main menu, pause, options, loading screens (LSCR with 3D model rotation, tips) | New game, load/save, settings (reuse existing MyGUI settings for core options until the end) | 10 to 18 |
| Inventory/container/barter/gift/loot windows (list + item card, favourites, categories, 3D item preview) | 3D preview needs render-to-texture (note 0045 notes it as new work) | 20 to 35 |
| Magic menu (spells, shouts, powers, active effects; favourites), hotkey wheel | | 10 to 18 |
| Skills / perks (constellations: 3D-ish star charts with perk nodes, zoom, rotation) | Distinctive visual; a custom UI element on RmlUi geometry | 12 to 22 |
| Map (world map and local map with markers, fast travel, weather/fog-of-war; rendering of the world map, see below) | | 15 to 30 |
| Dialogue (list of choices, subtitle, camera controls), lock-picking minigame, crafting menus (smithing, alchemy, enchanting), training, books/notes (readable pages), journal/quests (objectives, quest tracking), messages/ tutorial hints, console (existing) | | 30 to 55 |
| Character creation (race menu, face sliders: ties to 4.8 phase 2) | optional | 15 to 35 |

- **World map rendering.** Skyrim's world map is a rendered top-down view with fog of war (a map camera render of the loaded world, `Map` rendering; the engine already has a local/world map renderer for Morrowind (`MWRender::GlobalMap`, `LocalMap`) and a task 0073 fixed exterior local maps); for Skyrim the map is drawn from a baked top-down texture or generated by our LOD map camera (worldspace map data MNAM is in WRLD: bounds and cell offsets, RECALLED-VERIFY).
- **Depends on.** UI U1 to U3 (input done; fonts; list virtualisation), Lua descriptor service A to C (done), Lua UI compatibility (the Skyrim UI is **not** built on the Morrowind Lua UI mods' compatibility path; it uses the new library).
- **Total.** About 120 to 230 (components library growth included).

### 4.25 Strings and localisation

Already read (`.STRINGS` files, string-id resolution, 11,638 names found). Remaining: all UI text from `Skyrim_English.txt`-style interface string files (`interface\translate_*.txt`, UTF-16, RECALLED-VERIFY) plus language selection (`sLanguage`), fonts per language (fontconfig: Skyrim's `fonts_*.swf` files hold fonts in SWF form; we need TrueType or bitmap fallback: Czech/Polish/Russian/CJK handled through RmlUi font fallback, note 0045). **Estimate:** 4 to 10.

### 4.26 Mod support

- **Plugin load order** (`plugins.txt`, `loadorder.txt`, ESL light plugins "FE xxx", masters sorting, `Skyrim.esm` implicit ordering; already built: `LoadOrder`/`FormIdResolver`, 535-plugin list parses with 1 failure).
- **Archives.** BSA v105 done; BA2 (Fallout 4 style) already read; archive invalidation (loose files override archive files: VFS order is by data folder then archive order, matching Skyrim's `sResourceArchiveList` plus per-plugin same-name BSA loading, RECALLED-VERIFY).
- **MO2 compatibility.** The launcher Mods page (task 0057: profile model, layers, virtual file layout); houseCARL-like tooling is outside this repository.
- **Mod record features** commonly hit by mod lists: leveled-list mergers (Bashed Patch), FormList tweaks, quest mods using SKSE natives, Papyrus script mods, animation replacers (OAR/DAR: condition-based clip replacement: honour via our graph's replacement table), SkyUI MCM (Flash-based; **cannot run**: provide an own settings page via Lua descriptors; the Mod Configuration Menu API (`SKI_*` scripts) would need a compatibility layer, tier 2), SPID/KID/SkyPatcher INI distribution (runtime patchers that rewrite records at load: could be supported by running the same INI grammar in the pack, tier 2), ENB/ReShade (not applicable), body mods (extended skeletons: 4.11).
- **SKSE expectations (tiered, decision 0014).** Tier 0: vanilla natives only. Tier 1: built-in implementations of the most-used SKSE/library natives (PapyrusUtil, JContainers, ConsoleUtilSSE-like, MCM Helper); tier 2: INI distributors and MCM; tier 3 (not planned): loading real SKSE DLLs.
- **Estimate.** Load order/ESL/archives/loose edge cases 8 to 15; compat for common frameworks (tier 1-2) 25 to 45; testing against a real mod list (with gated checks) 15 to 30. Total 48 to 90.

### 4.27 Tooling

- **Eden Studio.** Cell viewer exists. Add: record browser with cross-references (quests, scripts), condition inspector (show why a condition passes), package inspector, navmesh overlay (imported NAVM as a debug view), animation clip/graph preview, Papyrus script inspector (running instances, event queue), world-model viewer for exterior cells and LOD, save inspector. Uses the Editor Module API 1.2/1.3 gaps (event bus, selection, background tasks) already built.
- **Validation tools.** The gated real-data checks (`tools/skyrimse_records_check`, `tools/papyrus_check`, `tools/skyrimcell_check`) continue; add survey tools (conditions, native calls, events, NIF block types) producing aggregate-only reports under `I:\Projects\OpenEden-work`.
- **Test harnesses.** Scripted scenes in the quiet window (`OPENEDEN_QUIET_WINDOW`); a Skyrim test profile variant (`tools/testprofile/variants/skyrim.cfg`); determinism tests (a scenario twice gives equal results); golden recordings (never edited without toddhow).
- **Estimate.** 25 to 50 across the plan.

### 4.28 Workstream totals (to L4/L5 as stated per row; excluding M-S0 in-progress items)

| # | Workstream | Days |
| --- | --- | --- |
| 4.1 | Interiors | 8 to 15 |
| 4.2 | Doors, cell transitions | 5 to 10 |
| 4.3 | Exteriors, worldspaces, terrain | 30 to 55 |
| 4.4 | Navmesh (recast first) | 10 to 20 (+20 to 35 if NAVM import) |
| 4.5 | Water, weather, sky, image spaces | 33 to 60 |
| 4.6 | Lights beyond N3a-5, LOD, occlusion | 25 to 50 |
| 4.7 | Shaders, materials, effects | 51 to 95 |
| 4.8 | Facegen | 8 to 15 (+25 to 45 optional) |
| 4.9 | Havok collision (`bhk`) | 25 to 50 (+8 to 15 hull bake) |
| 4.10 | Ragdolls, furniture, dynamics | 31 to 60 |
| 4.11 | Skeleton, body, equipment, first person | 39 to 73 |
| 4.12 | Animation (`.hkx` clips, graph) | 70 to 130 |
| 4.13 | Actor values, perks, leveling | 33 to 57 |
| 4.14 | Combat | 48 to 85 |
| 4.15 | Magic | 61 to 110 |
| 4.16 | Conditions | 30 to 50 |
| 4.17 | Inventory, items, economy, crafting | 38 to 65 |
| 4.18 | Crime, factions, stealth, followers, horses, transformations | 58 to 100 |
| 4.19 | AI | 63 to 112 |
| 4.20 | Dialogue and quests | 89 to 155 |
| 4.21 | Papyrus integration | 98 to 180 |
| 4.22 | Audio | 35 to 67 |
| 4.23 | Saves | 25 to 45 |
| 4.24 | UI | 120 to 230 |
| 4.25 | Strings | 4 to 10 |
| 4.26 | Mod support | 48 to 90 |
| 4.27 | Tooling | 25 to 50 |
| | **Sum** | **~1,020 to 1,890** (with overlap/shared work saved when sequenced, the milestone estimates in section 5.2 are **800 to 1,450**) |

The sum of rows is deliberately larger than the milestone totals because several rows share work (e.g. weather with 0094, LOD with the existing baker, UI components shared with Morrowind's UI work). The milestone figures in section 5 are the plan's estimate; the row sum is a ceiling for planning risk.

---

## 5. Sequencing and milestones

### 5.1 Dependency graph

```text
M-S0 finish in-progress (index 3c, lights 5, N3a-6/7)
   |
   +--> M-S1 lit interior + door  [N4 engine, lights, materials, interior lighting]
           |
           +--> M-S2 Whiterun exterior [exterior neutral cells+terrain, water, sky/weather, LOD-lite, streaming]
                   |
                   +--> M-S3 NPCs stand/idle/path [N5 actors, skeleton+body+facegen, clip converter, CM graph,
                   |        navmesh (recast), packages(min), process levels]
                   |        |
                   |        +--> M-S4 talk [conditions engine, DIAL/INFO, dialogue UI, voice audio, lip sync]
                   |                 |
                   |                 +--> M-S5 combat [actor values, melee/archery/block, magic basics, death/ragdoll, combat AI, HUD]
                   |                          |
                   |                          +--> M-S6 first quest [quests/aliases/story mgr/scenes, Papyrus wired, inventory+barter UI,
                   |                                  |            journal UI, saves(N6)] 
                   |                                  +--> M-S7 base game playthrough [everything else + polish + perf]
                   |                                           |
                   |                                           +--> M-S8 DLCs
                   |                                           +--> M-S9 mod ecosystem
   Parallel lanes (any time after M-S1): shaders/effects (4.7), Havok collision (4.9), tooling (4.27), strings (4.25), UI library growth (4.24)
   Critical path: M-S1 -> exteriors -> actors(N5)+animation(4.12) -> conditions/dialogue -> combat/magic -> quests+Papyrus -> saves
```

### 5.2 Milestones

| ID | Slice (demonstrable) | Acceptance criteria | Increment | Cumulative |
| --- | --- | --- | --- | --- |
| **M-S0** | Finish what is in progress: early index (0113), lights (N3a-5), Lua/mechanics safety (N3a-6), N3a-7 acceptance | Bannered Mare lit by its own lights; time-to-cell from the early index measured; Morrowind frames equal; written in-game steps in the stage log | 8 to 14 | 8 to 14 |
| **M-S1** | Walk a lit interior and leave through a door | Interior lighting block applied; lights flicker; stairs walkable (collision: stairs shown, which the 4b row says is *not yet*); a load door to another interior works both ways; locked doors with keys; frames viewed; Morrowind unchanged | 14 to 25 | 22 to 39 |
| **M-S2** | Walk Whiterun's exterior with terrain and sky | Exterior cell grid streams; terrain with texture layers; water; time of day and climate weather; statics, trees (sway), LOD tiles from our baker; leave and enter an interior from the street; frame time within a stated budget at a stated GPU | 55 to 100 | 77 to 139 |
| **M-S3** | NPCs stand, idle and path | NPC_ placements appear with body, head, outfit; idle clips (converted); locomotion graph on CreativeMotion; packages: sandbox/wander/travel/sleep; navmesh path across Whiterun; schedules by hour; process levels | 85 to 150 | 162 to 289 |
| **M-S4** | Talk to an NPC | Conditions engine with ranked top functions; DIAL/INFO selection; dialogue UI; subtitles + voice (`.fuz`/`.xwm`); lip sync; speech checks; training/trade hooks stubbed | 70 to 120 | 232 to 409 |
| **M-S5** | Combat | Actor values, melee, bow, block, power attack, stagger, death/ragdoll, hit sounds, combat AI + combat styles, basic spells (fire bolt, healing, ward), HUD | 120 to 200 | 352 to 609 |
| **M-S6** | The first quest from an alternate start | Quest runtime, aliases, story manager, scenes, objectives, journal UI; Papyrus wired with the top 106 natives + events + fragments; inventory, container, barter UIs; saving and loading with quest and VM state (N6). Start point: an alternate-start/ Riverwood-or-Whiterun scenario (Helgen intro is scripted-scene heavy; start with a fixed point and test quests that begin there, then the intro as its own scene-runner test) | 130 to 220 | 482 to 829 |
| **M-S7** | Full playthrough of the base game | Remaining records and natives; crafting; perks/skills UI; shouts and dragons; crime; followers; horses; transformations; radiant quests; map; music; polish and performance; every quest startable and completable in a test run of the main questline and the guild questlines | 200 to 380 | 682 to 1,209 |
| **M-S8** | DLCs | Dawnguard (vampire lord, crossbows, Soul Cairn), Hearthfires (building, adoption, cooking), Dragonborn (Solstheim, ash weather, mounts, new shouts) | 60 to 120 | 742 to 1,329 |
| **M-S9** | Mod ecosystem | A real MO2 list of N plugins loads; common frameworks tier 1 and 2; clip-replacer mods; mod bodies | 60 to 120 | 802 to 1,449 |

(The cumulative column is the plain sum of increments. The increments are smaller than the section 4 row sum because work shared across milestones is counted once.)

**Reading the numbers.** At 220 working days a year, 800 days is nearly four years of one developer; agent assistance is built into the days. toddhow can stop at M-S2 (a walkable, lovely world), M-S4 (a world with talking people) or M-S6 (a playable slice).

---

## 6. Risks and unknowns, ranked

1. **Havok formats (animation and collision).** Behaviour graphs are not realistically readable clean-room; clip decoding and `bhk` shapes are readable but need a sizeable, carefully documented effort. Mitigation: O1+O2 for animation; visual-triangle collision stays the default; read `bhk` as an upgrade.
2. **Scale of conditions and Papyrus natives.** Hundreds of CTDA functions and ~700 natives, each needing a subsystem; the long tail decides whether quests complete. Mitigation: survey tools ranking real use; strict-mode tests; unimplemented = false/no-op with one log line.
3. **Quest machinery semantics (aliases, story manager, scenes).** The least documented rules; wrong behaviour shows as dead quests. Mitigation: build with the data in hand, test with scripted scenarios against known outcomes on the user's own save-free runs; use the Papyrus scripts' own behaviour as the oracle (they call these).
4. **Performance at Skyrim's density.** Exterior cells with hundreds of statics, grass, many lights, and tens of actors with Papyrus and AI; our engine is CPU-bound in draw count (note 0025). Mitigation: LOD clusters from our baker, instancing, light grid, AI process levels, early profiling at M-S2.
5. **Legal and provenance.** No Havok SDK, no decompilation, no Bethesda assets; the audio codec (xWMA/ffmpeg configuration), `nif.xml` as oracle only, fonts from the user's data. A single careless copy damages the project; keep clean-room notes per format (as note 0044 and 0046 do).
6. **Scope.** Four years of work; every milestone is a stopping point. The cure is milestone discipline and refusing "while we are here" additions.
7. **Engine structure assumptions.** Terrain, LOD baker, weather, maps assume Morrowind's cell size and `ESMStore`; the neutral model fixes the data, but each consumer needs a refactor (4.3, 4.6).
8. **Actor model.** Deciding between "Skyrim actors as `MWClass::Neutral` with pack payload" and "extend `MechanicsHooks`" affects 4.13 to 4.19 (D3).
9. **Mod-list reality.** Real lists load hundreds of plugins with conflicts and SKSE-dependent scripts; compatibility is a long tail.
10. **Maintenance of RECALLED-VERIFY facts.** Many numbers here (shader type ids, slot numbers, archetype counts, record layouts) must be checked on the user's data before use; each stage spec must list them.

---

## 7. Decisions toddhow must make

| # | Decision | Options | Recommendation |
| --- | --- | --- | --- |
| D1 | **Target level and stopping points** | L3 / L4 / L5 / L6 | Commit to L1 to L2 now (M-S1 to M-S4); re-decide at M-S4 with real numbers |
| D2 | Start point for the first quest | Helgen intro as is / alternate start (fixed location, skip intro) / both | Alternate start for M-S6; the intro scene as a separate test after the scene runner works |
| D3 | **Actor/item model** | (a) `MWClass::Neutral` + pack payload; (b) extend `MechanicsHooks` (SDK 4.1/5.0); (c) a new Skyrim-only `Class` family | (a) with (b) additions; write a short decision record before N5 |
| D4 | **Animation route** | O1+O2 (own graph + converted clips) / O3 (read behaviour graphs) | O1+O2; accept that behaviour-graph mods do not work; record this as a decision |
| D5 | Collision route | Visual triangles only / `bhk` read / hull bake | Keep visual triangles now; `bhk` read as a task after M-S2 |
| D6 | Player character creation (race menu, facegen morphs) | Defer / minimal (pick race/sex/preset) / full | Minimal at M-S6; full only after M-S7 |
| D7 | Save compatibility | Own format only / one-way `.ess` importer | Own format only; revisit after M-S7 |
| D8 | Navmesh | Recast only / NAVM import / both | Recast first; NAVM import if recast fails on real stairs |
| D9 | Voice codec | ffmpeg decode (licence check) / own xWMA decoder / require pre-converted audio | ffmpeg if the pinned build is LGPL-clean (verify); else own decoder |
| D10 | Fidelity of Skyrim look | Match original shading as closely as possible / PBR-leaning remap | PBR-leaning remap behind a setting; match for skin/hair/eyes |
| D11 | SKSE tiering | Tier 0 only / tier 1 / tier 2 (INI patchers, MCM) | Tier 1 at M-S9; tier 2 on demand |
| D12 | Where UI text and fonts come from | Only user data / bundle open fonts | User data first; an open font fallback (licence checked) for missing glyphs |
| D13 | Process: full loop for rule areas | Full loop (separate tests + reviewer) / lighter loop | Full loop for engine, SDK, saves, Papyrus wiring; lighter loop for tools and survey scripts (needs toddhow's marking per spec) |
| D14 | `ESMStore` ESM4 experimental path | Retire at N7 / retire sooner | N7 as planned |
| D15 | Alternative route: contribute shared pieces upstream | No (OpenMW is a reference only) | Keep adopted answer: reference only |

---

## 8. What to do next (ordered; task numbers TBD)

1. **Finish M-S0.** Land N3a-3c (task 0113, option A), N3a-5 lights, N3a-6, N3a-7 acceptance. (Existing tasks; no new number.)
2. **TBD-A: Skyrim data survey tools** (aggregate-only, like `papyrus_check`): conditions by function/run-on, Papyrus native calls and events used by installed scripts, NIF block and shader-type frequencies, record-type counts per worldspace. Lighter loop, 6 to 10 days. This ranks every later estimate.
3. **TBD-B: N4 engine half**: door classes and cell changes (locked doors, keys, load doors); M-S1 criterion.
4. **TBD-C: interior lighting and Skyrim material mapping v1** (XCLL, shader types 0 to 5, normal maps, glow, alpha, double sided) with a decision on look fidelity (D10).
5. **TBD-D: exteriors in the neutral model** (research note first: terrain component design R7; then the neutral terrain source and cell-size audit of the terrain system, LOD baker and maps). Largest structural task of M-S2.
6. **TBD-E: weather/sky source** (WTHR/CLMT decode and the 0094 state contract; coordinate with task 0094).
7. **TBD-F: Havok clip converter spike** (research note + tool): packfile container reader and spline-compressed decoder on 5 vanilla clips, output our format; decide O2 viability before sinking time (clean-room log).
8. **TBD-G: decision record on the actor model** (D3) and an N5 plan with a detailed design (typed handles, payload, skeleton/body/equipment, the facegen path).
9. **TBD-H: conditions engine v1** with the top ~80 functions from the survey, unit-tested; usable by dialogue and packages.
10. **TBD-I: Papyrus wiring v1**: event dispatch, property filling from VMAD, top natives for the first quest slice, save state through the pack chunk; gated by an alternate-start test quest.

Each draft is a spec toddhow approves; full loop for engine/SDK/saves/Papyrus; every new setting off by default and in the test profile; Morrowind unchanged at every step.
