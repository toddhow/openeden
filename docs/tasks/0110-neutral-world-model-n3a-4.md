# Task: The neutral world model, stage N3a-4a (Skyrim SE static meshes drawn in the game)

Design doc section: `docs/design/neutral-world-model.md` sections 10.4 (R1), 10.6 (N3a-4), 10.8 (R1 scoping) and 10.13 (as built); decision 0016
Goal: with `[Game] neutral world` on and the Skyrim SE masters and archives in the VFS, the objects of a neutral interior (the Whiterun inn is the acceptance cell) are DRAWN from their `.nif` files as static, textured meshes through the engine's existing pipeline and material path, cached per model path like Morrowind's models, and everything else (Morrowind, setting off) is unchanged.

**Scope decision (recorded here; the design doc allows it):** 10.6 lists "rendering and collision" under N3a-4 and 10.8 ends "its two halves can be separate task specs". This spec is the RENDER half (N3a-4a). Collision stays the boxes of N3a-3. Visual-triangle shapes from the model (10.8 piece 2, an estimated 3 to 5 days) are **N3a-4b and are NOT built here**; the faithful Havok route stays its own task.

## Where the code lives (the layer rules decide)

`CI/layer_rules.txt` lets the engine layer include only the base and engine layers and the exception list may not grow, so no engine file (`components/vsgadapters`, `components/pipeline`, `components/resource`) may include `components/nifscene`. The split is therefore:

- `components/nifscene/header.*` and `flatten.*` (**formats layer**, pure: no VSG, Vulkan or engine type): the header test and the scene-to-flat-shapes policy.
- `components/vsgadapters/nif/nif.{hpp,cpp}` (engine): a format-blind hook, `nif::sceneReader` (two `std::function`s: `accepts(first bytes)` and `read(whole file)`); the class peeks 80 bytes, rewinds, and hands an accepted file on. It includes no format header.
- `apps/openmw/mwrender/skyrimmodel.{hpp,cpp}` (**host layer**, which may include both): `buildNode` (the VSG half), `makeSceneReader` (the pair of functions), `Counters`, `describeCounters`.
- `apps/openmw/mwrender/rendermanager.cpp` sets `nifReader->sceneReader` only when `[Game] neutral world` is on.

## Inherited code touched (at commit `432bbce161`)

- `components/vsgadapters/nif/nif.cpp` `nif::read` and `nif.hpp`: the hook above; with no reader set (the default) the file reaches the Morrowind reader and is refused as before. `nif::getFeatures` and everything else unchanged.
- `apps/openmw/mwclass/neutral.cpp` `Neutral::insertObjectRendering` (was empty): `renderingInterface.getObjects().insertModel(ptr, model)`; `isLiveThing` no longer includes `Item` (items have models from this stage on).
- `apps/openmw/mwworld/scene.cpp` `Scene::insertCell`: one more Info line for a neutral cell, the Skyrim model counters.
- `apps/openmw/mwrender/rendermanager.cpp`: three lines setting the reader.
- `components/CMakeLists.txt` (`nifscene` gains `header flatten`), `apps/openmw/CMakeLists.txt` (`mwrender/skyrimmodel`), `apps/openmw_tests/CMakeLists.txt`, `apps/components_tests/CMakeLists.txt`.
- `tools/neutral_session_check/check.sh`: the Skyrim mesh, texture and misc archives in the session's `fallback-archive` lines, an optional screenshot argument (`<frame>` or `look`), `SETTINGS_EXTRA`; `luatest/scripts/n3a4/look.lua`.
- One existing test line edited (listed in `docs/toddhow-checks.md` section 3): `apps/openmw_tests/mwworld/testneutralcellstore.cpp` `NeutralClass.a_marker_model_and_the_live_things_have_no_model`: `Item` leaves the list of kinds that have no model (a new test, `every_kind_but_actors_and_leveled_lists_has_its_model`, pins the new rule).

## Design

- `NifScene::Flat::flatten(const Scene&, Limits)` -> `FlatModel`: node world transforms baked into the vertices (the model is static), hidden nodes and `EditorMarker` subtrees dropped, skinned shapes dropped, shapes with no shader property dropped, effect-shader shapes and **refraction shapes (flags 1 bits 15 and 16: heat shimmer, found by looking at the first frames: a rainbow slab over the fire)** dropped (both counted as "effect"), UV offset and scale baked, vertex colours kept only with the shader's Vertex_Colors flag (alpha only with Vertex_Alpha), missing or bad normals replaced by area-weighted ones, triangles with an index outside the shape or a non-finite / beyond-1e7 vertex dropped and counted, caps on nodes / shapes / vertices / triangles, shapes with the same `MaterialKey` merged (a merged shape never exceeds `mMaxMergedVertices`). The `MaterialKey`: diffuse path (texture set slot 0), double sided (flags 2 bit 4), vertex colours, alpha blend / test with the property's codes and threshold, clamp mode, shader alpha.
- `SkyrimModel::buildNode(FlatModel, BuildContext)`: per shape one `vsg::StateGroup` with a pipeline from `Pipeline::Builder::graphics->getOrCreate` (modes VERTEX, NORMAL, [COLOR], [TEXCOORD], [DIFFUSE_MAP], MATERIAL; cull none for double sided; blend factors from the NIF codes; alpha test mode; no depth write for blended shapes), a descriptor set with the diffuse `DescriptorImage` (`vsgUtil::readOptionalImage` through the texture options, so DDS BC1/3/5/7 and the texture cap work as for Morrowind) and a material (alpha, alpha test cutoff), one `VertexIndexDraw` with `uint32` indices, `DepthSorted` for blended shapes without NoSorter, `createCullNode` / `addLeafCullNodes` on the root. `OPENEDEN_NIFSCENE_TRACE=1` writes one stderr line per shape.
- The model cache is the existing one: `Scene::insertModel` -> `MWAnim::createObject` -> `Context::readNode` -> `vsgUtil::readNode` with the node options' `SharedObjects`.
- **Not in this slice (decisions):** normal / glow / environment / height maps; emissive; specular; additive effects (effect shaders); skinned actors and worn armour; animation controllers; lights from the model (N3a-5); LOD shapes; texture-set slots beyond 0; collision from the model (N3a-4b).

## Acceptance criteria

- [x] `flatten`: every policy line above is a named test; hostile and odd inputs (non-finite or huge numbers, indices outside, parent cycles, caps, empty scenes, missing texture set) never throw or index outside an array and are counted. Tests written first and run red against a stub (47 of 52 failed against it; the other 5 assert absence or empty results); 6 single-line mutations each failed named tests (one escaped, the double-sided bit, and got a literal pin).
- [ ] `nif::read` with a scene reader turns a generated Skyrim file into one `VertexIndexDraw` with `uint32` indices and the expected streams; without one it is refused; a missing texture is counted and the shape still draws; a truncated file gives no node and is counted; a model that draws nothing is an empty group, not a missing model.
- [x] In game (`tools/neutral_session_check/check.sh`, the five Skyrim masters only, `--start WhiterunBanneredMare`): the inn interior is recognisable (timber, counter, benches, rugs, food, wall hangings) with textures; draws, triangles, frame time, model counters and textures loaded / missing reported; frames viewed.
- [x] Morrowind unchanged with the setting off and on: `tools/worldobjects_check` dump byte-identical (110 lines) and a fixed-time Balmora frame with the same SHA-256 against the executable of the parent commit.
- [x] Layer rules stay at 104 exceptions; the `CI/check_*.sh` that need no Docker pass; test files registered.

Out of scope: collision from the model (N3a-4b), lights (N3a-5), skinned things, animation, other texture maps, doors / actors / items as live things, saves.

## Stage log

(written as the stage was run; see the end of this file)
