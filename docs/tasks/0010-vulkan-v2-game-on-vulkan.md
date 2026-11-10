# Task 0010: Track V stage V2 — The whole game on Vulkan

**Status:** CLOSED 2026-11-10 by toddhow (remaining checks are follow-ups, not blockers; listed in `docs/toddhow-checks.md`, "Known open items from Track V"). Earlier status: approved by toddhow (2026-10-29). Depends on task 0009 (V1), done on Windows (commit 7a8e86c27d).
**Design doc section:** `docs/decisions/0002-vulkan-renderer-on-vulkanscenegraph.md`; guide `CLAUDE.md`, Track V.
**Goal:** after the renderer import, make Morrowind fully playable on the Vulkan build: movement, combat, magic, AI pathing with the navigator on, saving and loading, maps, menus and the Lua API, with every existing test gate passing again and the image and performance comparisons against the OSG references judged. OpenEden's own rendering features stay excluded until task 0011.
**Workflow:** full loop.

---

## Context (facts this spec relies on)

- **vsgopenmw's known gaps that V1 inherits** (`VSG_MERGE_PLAN.md`, "Known bugs on master"; `vsgopenmw-roadmap.doc`, "vsgopenmw-known-issues"):
  - movement does not work ("WASD does nothing even in `tcl`"), suspected in the `mwinput`/mechanics glue;
  - the navigator (`components/detournavigator`) is untested on vsgopenmw and its documentation says to run with `[Navigator] enable = false`; OpenEden needs it on (AI pathing, Lua `nearby.findPath` and related integration tests);
  - Lua `Unknown tag 'MENU'` warnings on 0.49 (should disappear with OpenEden's 0.51 Lua, verify).
- **The 32 Lua integration tests** (`scripts/data/integration_tests/test_lua_api`, run by `scripts/integration_tests.py` against the example suite at the revision pinned in `.gitlab-ci.yml`) use `openmw.async`, `camera`, `content`, `core`, `input`, `interfaces`, `menu`, `nearby`, `self`, `types`, `util`, `vfs`, `world`. `openmw.camera` is implemented in `apps/openmw/mwlua/camerabindings.cpp`, which talks to the renderer's camera.
- **`openmw.postprocessing`** (`apps/openmw/mwlua/postprocessingbindings.cpp`) depends on `components/fx`, which vsgopenmw does not build; its fate is decided in task 0012.
- **On Windows the integration tests could not run on the OSG build** because vcpkg's OpenSceneGraph crashes loading the example suite's COLLADA model (guide, Pitfalls). The Vulkan build has no OSG image code, so this may no longer apply; verify.
- **Reference images and performance numbers** come from task 0008 (`tools/render_compare.sh`, `tools/render_scenes.txt`, `docs/research/0002-vsgopenmw-baseline.md`).

## Inputs / outputs

- Fixes on `vulkan` in small commits, each naming the symptom and the cause.
- `docs/research/0004-vulkan-playability.md`: the playthrough checklist below with results, the image comparison table, the performance comparison, and every difference that is accepted (with toddhow's sign-off) or deferred to task 0012.

## Work

1. **Input and movement:** find why movement does nothing after the import, fix it at the cause (no workaround in `movementsolver.cpp` or Jolt code), with a test that reproduces it if it is a logic error (an input-to-movement unit test or a Lua integration test that walks the player and checks the position changed).
2. **Navigator:** run with `[Navigator] enable = true` (the default); make navmesh generation and AI pathing work on the Vulkan build; the navmesh debug draw (`navmesh`, dropped by vsgopenmw) is task 0011's.
3. **Lua API:** every binding OpenEden 0.51 has must exist and work, except `openmw.postprocessing`, which stays registered but returns a clear "not available on this build" error until task 0012; `openmw.camera` works against vsgopenmw's camera.
4. **Integration tests:** all 32 pass in the Linux CI container (guide, Pitfalls: line endings, separate build and test containers); if they now also run on Windows, add that to the guide's Pitfalls as resolved.
5. **Playthrough checklist** with toddhow's Morrowind data, written as repeatable steps: new game through the census office; walk, run, jump, swim, levitate; open doors and travel by silt strider and boat; melee, ranged and magic combat including projectiles over water; NPC pathing through doors and stairs; dialogue, barter, inventory, map and world map, journal; save, quit, load; rest and weather changes; day and night; an interior with many lights; a large exterior with distant terrain.
6. **Image comparison:** run `tools/render_compare.sh` against the OSG references. Differences are expected (vsgopenmw renders differently: clustered lighting, soft shadows, new water); each scene gets a verdict from toddhow ("accepted", "bug", or "parity item for task 0012"), recorded in the research note. Missing objects, wrong materials, black or missing textures, and wrong geometry are bugs, not accepted differences.
7. **Performance:** measure every scene against the OSG baseline from task 0008 on the same machine and settings; record CPU and GPU frame time. A regression over the budget agreed in open question 2 is a bug.

## Constraints

- No change to game rules, physics or AI behaviour to make the renderer work; fixes go where the import broke something.
- OpenEden's excluded features stay excluded (`OPENEDEN_VSG_PENDING_FEATURES=OFF`); their settings stay inert.
- No game assets in the repository.

## Acceptance criteria

- [ ] The player can move, jump, swim and levitate; a test reproduces the movement bug if it was a logic error, and passes.
- [ ] The navigator runs with `enable = true`; NPCs path through doors and stairs; the Lua navigation integration tests (`findPath`, `findPath with checkpoints`, `findRandomPointAroundCircle`, `castNavigationRay`, `findNearestNavMeshPosition`) pass.
- [ ] All 32 Lua integration tests pass on the Vulkan build in the Linux container.
- [ ] `components-tests`, `openmw-tests` and the clang-format check pass (tests excluded in task 0009 stay listed, not silently dropped).
- [ ] Every item of the playthrough checklist passes, or has a recorded bug with a follow-up.
- [ ] Every reference scene has toddhow's verdict recorded; no scene has an open "bug" verdict.
- [ ] Performance per scene is recorded against the OSG baseline and is within the agreed budget.
- [ ] `openmw.postprocessing` fails clearly rather than crashing.

## Out of scope

- OpenEden's features excluded in task 0009 (task 0011).
- Renderer parity items and post-processing, VR, groundcover (task 0012).
- OSG math (task 0013); the editor and tools (task 0014).
- Merging `vulkan` back into the main line (after task 0011, see its spec).

## Open questions for toddhow

1. **Accepted visual differences:** is vsgopenmw's different look (clustered lighting, soft shadows, its water) acceptable as OpenEden's new default, or should V4 aim to match the OSG look first?
   - **Answer (toddhow, 2026-10-29):** vsgopenmw's look is accepted as the new default. Image comparisons judge for bugs (missing or black objects, wrong materials or geometry), not for matching the OSG look; parity work stays in task 0012.
2. **Performance budget:** what regression, if any, is acceptable against the OSG baseline (for example "no scene more than 10% slower in CPU frame time, GPU time free to rise for new features")?
   - **Answer (toddhow, 2026-10-29):** no scene more than 10% slower in CPU frame time than the OSG baseline; GPU time may rise for new features.

## Progress

- **2026-10-29, Lua on every frame:** `Engine::frame` runs Lua for every game state while a game exists, as 0.51 did; Lua pauses during Operation, Loading, LoadingScreen and Video states (0.51 ran them synchronously); `Game::run` uses 0.51's pausing (`DateTimeManager::updateIsPaused`) and advances simulation time. A state pushed by `synchronizedUpdate` (a Lua teleport's ChangeCell) ends the frame before scripts run, as 0.51 changed cells inside `synchronizedUpdate`.
- **2026-10-29, movement, navigator and the magic crash (work items 1 and 2):** one cause. Non-NIF (`.dae`) models had no collision shape on the VSG build (`PhysicsShapeManager::getShape`), so the example suite's player got no physics body (`PhysicsSystem::addActor` returns early) and could not move, the navmesh lacked every `.dae` building, and `PhysicsSystem::addProjectile` dereferenced a null shape when a `.dae` magic bolt launched. Fixed at the cause by porting 0.51's `NodeToShapeVisitor` onto assimp (new dependency, `docs/dependencies.md` section 7), with 16 unit tests in `apps/components_tests/resource/testphysicsshapemanager.cpp`; transforms above a "Collision" node now apply (0.51 skipped them, which only mattered for files that rotate above that node).
- **Windows integration run (2026-10-29):** 31 of 32 Lua integration tests pass with the runner normalising CRLF test data. Remaining: "player with equipped weapon on attack should damage health of other actors": `.dae` models still render and animate as vsgopenmw's fallback object, so the player has no attack animation and no hit is triggered. Rendering and animating `.dae` models on VSG is open work for this task. Linux container run not done yet.
- **Known differences of the `.dae` collision port from 0.51 (fresh review, 2026-10-29):** only the COLLADA importer is built, so other non-NIF formats OSG could read (`.osgt`, `.obj`) get no collision shape and a warning; assimp applies the file's `<unit meter>` scale and converts X_UP and Y_UP files through its own rotation, and neither has been compared with 0.51's OSG reader (the example suite is all Z_UP in metres). Matching 0.51: nodes are named by their COLLADA `name`, and only the first "Collision" node counts.
- **2026-10-29, `.dae` rendering and animation (toddhow asked to work on it):** vsgopenmw never loaded non-NIF models (the vsgXchange model reader is commented out in `resourcesystem.cpp`), so every `.dae` drew as an empty fallback node and `.dae` actors had no skeleton or animations. Following 0.51's `SceneManager::loadNode` and `KeyframeManager` for COLLADA:
  - `vsgAdapters::dae` (`components/vsgadapters/dae/dae.cpp`) builds the structures the NIF reader builds: named `Anim::Transform` nodes, `Anim::Skin` for skinned meshes (assimp's bone offset matrices are the inverse bind matrices, at most four weights per vertex), vsgopenmw pipelines and material data from the COLLADA material (diffuse texture, ambient, diffuse and emissive colours, opacity, two-sided).
  - `vsgAdapters::daeAnimation` (`daeanim.cpp`) returns an `Anim::ControllerMap`. Blender writes each `<animation_clip>`'s samplers from time 0 and places the clip with `start`; the text keys use that timeline (0.51's `OsgAnimationController` subtracted the clip start). assimp ignores clips, so the reader shifts each clip's sampler times by its start (pugixml, compiled into assimp) before assimp reads the file, and merges every animation of a node into one timeline. Text keys come from the `.txt` next to the model, as in 0.51. A model without animations is no animation source.
  - 0.51's COLLADA rules: the first node named "Collision" is not drawn; when the model has a skin, `_` in node, bone and animation target names becomes a space; a model marked `<Description>bodypart</Description>` keeps only its skinned meshes; `alphatest MODE VALUE material` descriptions set alpha testing. The up axis is converted to Z up as for collision (`Resource::sAssimpYUpToZUp`, shared with collision in `resource/assimpscene.cpp`).
  - assimp 6.0.5 bug worked around in `Resource::readAssimpScene`: its skin reader does not skip whitespace at the start of `<vcount>` (Blender writes `<vcount> 2 2 …`), which shifted every vertex's bone weights to the next vertex; the example suite's player stretched into spikes when animated. Found by comparing assimp's weights with the file's (all 22482 vertices now match).
  - The sky keeps unreadable models empty (`mwrender/weather.cpp`): the example suite's clouds and stars are `.osgt`, which the VSG build cannot read, and the world's fallback would now draw the readable `marker_error.dae` with the sky's shaders over the whole view.
  - Result on Windows: all 32 Lua integration tests pass (the weapon attack test needed the player's attack animation); screenshots of the start room and the hub show the buildings, terrain and the animated, skinned player.
  - Known differences from 0.51: the example suite's sky dome is faceted, because the atmosphere shader fades by vertex index parity (`sky/atmosphere/main.vert`, and 0.51's `ModVertexAlphaVisitor`), which assumes the NIF cylinder's vertex order and does not hold for assimp's de-indexed vertices; `.osgt` models (the suite's clouds and stars) are not drawn; COLLADA vertex colours are not used; the player's `hu_male.dds` and the first-person `base_anim.1st.nif` are missing from the example suite itself.
  - Fresh review (2026-10-29): clip shifting now handles nested animations (Blender 2.8 and later) and time sources shared by several samplers, and writes numbers in the C locale. Further differences from 0.51, kept on purpose or still to check: text keys are lowercased and trimmed (as NIF text keys are); an unknown `alphatest` mode disables alpha testing (0.51 used LEQUAL with a warning) and mode names match case-insensitively; a `bodypart` model without skinned meshes reads as nothing, so the error marker is drawn (0.51 kept the whole model); the material name matched by `alphatest` is assimp's `AI_MATKEY_NAME`, not yet compared with OSG's stateset name; when two clips touch, the later clip's first key replaces the earlier clip's last one; textured materials keep assimp's diffuse colour (white in the example suite's files; OSG's choice for other exporters is unverified); every COLLADA node becomes an `Anim::Transform` (the NIF reader drops identity transforms), and "AttachLight"/"BoneOffset" placeholders are not recognised. The sky change only matters when a sky model cannot be read; Morrowind's sky NIFs are unaffected. A large `.dae` is parsed up to four times on its first load (XML for clips, then assimp for animation, model and collision); later reads are cached. The node reader (`dae.cpp`) has no unit tests yet: it needs a `Pipeline::Builder`, and is covered by the integration tests and screenshots only.
