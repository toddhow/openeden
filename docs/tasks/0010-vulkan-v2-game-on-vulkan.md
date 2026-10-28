# Task 0010: Track V stage V2 — The whole game on Vulkan

**Status:** draft, awaiting toddhow's approval. Depends on task 0009 (V1).
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
2. **Performance budget:** what regression, if any, is acceptable against the OSG baseline (for example "no scene more than 10% slower in CPU frame time, GPU time free to rise for new features")?
