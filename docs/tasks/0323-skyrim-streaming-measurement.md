# Task 0323: Skyrim asset streaming, measured

Design doc section: `docs/tasks/0060-lod-s6-streaming-core.md` (S6, stages a and g); research note `docs/research/0146-skyrim-cell-streaming-hitches.md` holds the numbers.
Goal: measure how Skyrim cells stream (a scripted walk and flight across Whiterun's hold, a gate door and an interior door), attribute the main-thread hitches as note 0026 did for Morrowind, and say which unbuilt S6 piece (or Skyrim-specific change) comes first. Measure only.
Inherited code touched: `components/extension/lifecycle_manager.cpp` (`runPhase`: one trace scope), `apps/openmw/mwworld/worldmodel.cpp` (`findNeutralExterior`), `apps/openmw/mwworld/worldimp.cpp` (`doPhysics`), `apps/openmw/game.hpp` (`Game::run`): trace scopes only, all at `c14f282175`.
Inputs / outputs: new scopes `phase.<Phase>`, `neutral.cell.build`, `physics.neutral.actors`, `physics.step`, `ext.neutral.apply`; the pack's `OPENEDEN_PACK_FRAME_TRACE` aid; `tools/streaming_check`.
Constraints: off by default and free when off (one atomic read per scope; the aid is an environment variable); no setting added; nothing fixed.
Acceptance criteria:
- [x] A scripted flight and walk across many exterior borders, plus a gate door and an interior door, run with `[Cells] load trace`, incremental cell build off and on (the walk 3 runs each cold, 2 to 3 warm; the flight 2 each; the doors 2 each).
- [x] Frame times (median, p99, p99.9, frames over 25 ms) and the worst crossing in the note, section 3.
- [x] Attribution of the hitches (section 4), the pack's frame split (section 5), a ranked table and a recommendation (sections 1 and 7).
- [x] A comparison with Morrowind's numbers of note 0026 (section 3).
- [x] With the scopes in, Morrowind unchanged against `integ2`'s build: `tools/worldobjects_check` 110 objects OK and the Balmora frame `1c5d47ad0b5dc32d` for both.
Out of scope: any fix (the navmesh thread default, preloading Skyrim models, the Papyrus flush budget are recommendations, section 7 of the note); LOD; textures as their own cost.

## Stage log

**What was run.** `tools/streaming_check/run.sh` (one run), `batch.sh` (a list of runs, keeps the summaries), `analyze.py` (frame statistics and the mean main-thread time of each scope in the hitch frames), `attrib.py` (shares of hitch time), `skyjoin.py` (joins the pack's `SKYFRAME` lines with a trace), `summarize.py` (table over a results folder). The player is moved by the Lua scripts `stream323` (real engine movement, collision off). A teleport-per-frame mode (`walk` and `fly`) exists in `run.sh` and was NOT used for the results: a teleport is not a border crossing for the incremental build. About 45 runs of 2 to 3 minutes each on a shared machine; the run folders are deleted, the summaries are kept in `I:\Projects\OpenEden-work\streaming\keep`.

**Tests.** `PhaseTrace.aPhaseIsOneScopeNamedLikeItsProfilerScope` and `PhaseTrace.offRecordsNothing` (`apps/components_tests/extension/testphasetrace.cpp`): written AFTER the one-line scope and not run red (a deviation; the second test pins the off state, the first the name). `SkyrimFrameSplit.*` (7) and `SkyrimSectionMarks.*` (3) (`apps/components_tests/skyrimse/testskyrimseframesplit.cpp`): written before `frame_split.hpp` existed (compile-red by construction, not run as failing tests). All 12 new tests pass. No existing test edited.

**Findings that changed the plan.**
1. The first matrix teleported the player every frame in `WhiterunWorld` coordinates: no objects (the wilderness is Tamriel's) and no incremental build (a teleport is not a crossing). Redone with the engine's own movement in Tamriel.
2. Two cold, incremental-off runs were ended by the 5 s freeze monitor (exit 173): in a cold start a single frame is over 4 s (`world.navigator.wait`, then the cell build). `OPENMW_DISABLE_CRASH_CATCHER=1` finished the later runs.
3. The "warm" navmesh run uses the `navmesh.db` of the first cold run on the same route; the first attempt failed on an empty field in the batch spec (no run happened) and was redone.
4. One cold, incremental-on run filled the 4,000,000 event cap of the load trace (its window was lost) and was repeated.

**Not run.** The Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, `openmw-tests`, a fresh review; the whole `components-tests` (only the new tests and the `LoadTrace` and lifecycle tests); World LOD; a run with collision on; a weaker machine.

**Decisions for toddhow (note 0146, section 7).** (1) The default of `[Navigator] async nav mesh updater threads` for a neutral world (1 now; 4 removed the wait in one cold run), and baking or shipping a navmesh db. (2) A task to let `MWClass::Neutral::getModelsToPreload` list the Skyrim model (the first S6 piece). (3) A pack change to budget the Papyrus flush over frames. (4) Whether the incremental build becomes the default for Skyrim sessions (only together with 2 or 3). No existing test needs a change.
