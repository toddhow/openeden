# Task 0357: Skyrim streaming, step 1 (navmesh threads and model prefetch)

Design doc section: `docs/research/0146-skyrim-cell-streaming-hitches.md` section 7; plan of task 0323.
Goal: remove the two largest Skyrim cell-crossing hitches (the cold navmesh wait and the main-thread first read of every model) without changing Morrowind.
Inherited code touched: `apps/openmw/mwworld/worldimp.cpp` (the navigator settings in the `World` constructor), `apps/openmw/mwclass/neutral.{hpp,cpp}` (`Neutral::getModelsToPreload`), at `ca52ec4f0b`. Read, not changed: `apps/openmw/mwworld/preloadcell.cpp` (`PreloadCell`), `components/detournavigator/settings.cpp`.
Inputs / outputs: new settings `[Game] neutral preload models` (default true, needs `neutral world`) and `[Navigator] neutral async nav mesh updater threads` (default 4, used only with `neutral world`); `MWWorld::chooseNavMeshUpdaterThreads` (`neutralnavthreads.hpp`).
Constraints: Morrowind never sees either setting's effect (both gated by `[Game] neutral world`); the preloader path of Morrowind classes is not touched.
Acceptance criteria:
- [x] A neutral session gets 4 navmesh updater threads; others keep `async nav mesh updater threads`.
- [x] `Neutral::getModelsToPreload` lists the object's model (what `getModel` returns: nothing for actors, markers, empty models).
- [x] Measured before/after with the 0323 tools (below).
- [x] Morrowind unchanged: `worldobjects_check` 110 objects OK, fixed Balmora frames equal to `n6saves`'s build.
Out of scope: Papyrus flush budget, incremental cell build default (steps 2 and 3), actor preloading, LOD.

## How it works

1. **Threads.** `World::World` builds the navigator settings from `[Navigator]`; with `[Game] neutral world` on the thread count is `[Navigator] neutral async nav mesh updater threads` (default 4, sanitised to at least 1). `settings-default.cfg` keeps `async nav mesh updater threads = 1` for Morrowind. Pure choice: `chooseNavMeshUpdaterThreads(neutral, normal, neutralValue)`.
2. **Prefetch.** `Neutral::getModelsToPreload` was an empty override, so `PreloadCell` never listed a Skyrim model. It now pushes `getModel(ptr)` when `neutral world` and `neutral preload models` are on. Threads: `PreloadCell`'s constructor (main thread, "Constructor to be called from the main thread") calls it through `cell.forEach`; it only reads the object's `NeutralBase` (a node of `Store<NeutralBase>`, an `unordered_map` with stable nodes, not freed in a session; `getModel` returns a view of `mModel`). The worker (`PreloadCell::operate`) then does what it does for Morrowind: `vsg::read(mesh, nodeOptions)` (the node cache and NIF reader that `insertModel` uses, with the Skyrim scene reader installed by `RenderManager`) and `PhysicsShapeManager::getShape`. Both are the managers' thread-safe caches; every failure is caught per model (0 "Failed to preload mesh" lines in all runs). Actors return nothing (the same `isLiveThing` rule as `getModel`), so NPC build cost (`neutralactor.create`) is not prefetched.

## Tests

New file `apps/openmw_tests/mwworld/testneutralstreaming.cpp`, written first and run red (4 of 7 failed: the model list of a static and a tree, the pointer identity with `getModel`, two thread-count tests; the other 3 pin the unchanged cases). Green after: 7 of 7; `*Neutral*:*Settings*:*Extension*:*Skyrim*` of `openmw-tests` 1,202 run, 1,200 pass, 2 data-gated skips. No existing test edited: `NeutralSafetyFixture.the_answers_that_were_already_safe_stay_so` still expects an empty list; it passes because `neutral world` is off in unit tests, which is one more reason for the gate.

## Measurement

`tools/streaming_check/batch57.sh` (new) runs 0323's `rwalk` (real engine movement, 455 units/s, 14 border crossings, incremental build off, play profile) and `rfly` (flight, 1,500 units/s) with ONE build. "Before" = both new settings switched off (`neutral preload models = false`, neutral threads 1: exactly the old behaviour), "after" = defaults. Cold = empty `navmesh.db`; warm = the db of a cold run. The machine was shared. Columns: frames over 25 ms, p99, p99.9, worst frame (ms), whole-run totals of the navigator wait and of main-thread `model.create`.

| Run (walk) | over 25 | p99 | p99.9 | worst | navigator wait | `model.create` |
| --- | --- | --- | --- | --- | --- | --- |
| cold before c1 | 23 | 7.11 | 3220 | 6310 | 26.9 s | 7.8 s |
| cold before c2 | 21 | 5.42 | 2181 | 3953 | 19.0 s | 1.6 s |
| cold before c3 | 23 | 5.43 | 2001 | 3526 | 17.0 s | 2.5 s |
| cold after c1 | 23 | 6.88 | 298 | 3394 | 4.5 s | 0.46 s |
| cold after c2 | 28 | 5.68 | 331 | 1411 | 5.3 s | 4.0 s |
| cold after c3 | 16 | 5.47 | 169 | 468 | 0.8 s | 0.37 s |
| cold threads only c1 | 21 | 5.38 | 224 | 512 | 0.8 s | 1.7 s |
| cold prefetch only c1 | 21 | 5.50 | 2399 | 3256 | 19.7 s | 0.41 s |
| warm before w1 | 21 | 6.56 | 2745 | 4302 | 19.4 s | 2.1 s |
| warm before w2 | 19 | 6.65 | 1963 | 3153 | 14.6 s | 2.9 s |
| warm after w1 | 19 | 8.08 | 384 | 1489 | 0.008 s | 3.4 s |
| warm after w2 | 16 | 6.35 | 181 | 471 | 0.007 s | 0.41 s |

| Run (flight) | over 25 | p99 | p99.9 | worst | navigator wait | `model.create` |
| --- | --- | --- | --- | --- | --- | --- |
| cold before | 42 | 15.2 | 6180 | 8256 | 88.7 s | 17.1 s |
| cold after | 49 | 10.6 | 784 | 1274 | 7.5 s | 2.0 s |
| warm before | 55 | 23.4 | 5323 | 6826 | 85.4 s | 3.0 s |
| warm after | 44 | 10.9 | 1214 | 2416 | 13.7 s | 1.3 s |

Reading it:
- The navigator wait was the whole tail: with 4 threads the cold p99.9 of the walk falls from 2.0 to 3.2 s to 0.17 to 0.33 s and the worst frame from 3.5 to 6.3 s to 0.5 to 3.4 s (one run, c1, still has one 3.3 s wait on the first crossing: 4 threads do not remove it entirely). The cold flight's worst frame goes from 8.3 s to 1.3 s.
- "Warm" with the old single thread did not remove the wait (15 to 19 s): the db of a cold run did not make the one-thread run fast here; with 4 threads it is 8 ms. Not investigated (note 0146 saw a warm win).
- Frames over 25 ms and p99 barely move (they are the many ordinary cell-build frames); what vanished is the multi-second tail.
- Prefetch: `preload.operate` now does 1.8 to 6.7 s of work on the worker per walk (2,928 model reads in 54 cell preloads). The main-thread `model.create` total falls from 1.6 to 7.8 s (three runs before) to 0.4 to 0.5 s in 4 of the 6 runs with prefetch; after_c2 (4.0 s) and after_w1 (3.4 s) were no better, probably because the worker had not finished when the cell was built (c2's worker needed 6.7 s) or the machine was loaded. The nav-threads-only run (1.7 s) shows the run-to-run spread of this number is large; I did not isolate how much of the drop is the OS file cache of earlier runs.

## Morrowind unchanged

- `tools/worldobjects_check` against `I:\Projects\OpenEden-work\n6saves\build`: 111 result lines each, 110 objects, OK.
- `tools/skymap_check/morrowind_check.sh`: the two fixed Balmora frames (`1eb68236352dc472`, `c819db116f4af8a6`) are equal for this build and the reference. (The `1c5d47ad0b5dc32d` hash of other rows is another start configuration, not run here.)
- NOT run: a Morrowind cell-crossing flight before/after (no Morrowind scenario exists in `tools/streaming_check`). By reading: no Morrowind class's `getModelsToPreload`, `PreloadCell` or the preloader changed, the thread count is gated by `[Game] neutral world`, which is false in a Morrowind session.

## What remains of the 0323 plan, in order

2. A pack change to budget the Papyrus flush over frames (60 to 110 ms per crossing, all of what is left with incremental build on and the navmesh warm).
3. Incremental cell build as the default for Skyrim sessions. Actor build (`neutralactor.create`, skeletons, bodies, animations) is not prefetched: a neutral-actor preload is a possible step 1b before this.
4. A shipped or baked navmesh db, a shared scheduler and IO pool (a cold first visit still builds tiles in the background; one cold run still waited 3.3 s with 4 threads).

## Decisions for toddhow

1. 4 threads is the number 0323 measured; a weaker machine may want less or `auto` from the core count.
2. Whether to bake or ship a navmesh db (removes the cold case entirely).
3. The "warm single thread still waits" result contradicts note 0146; a follow-up could look at how `navmesh.db` is keyed.

## Not run

The Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, a fresh review, a Morrowind flight, collision on, more than three cold runs per variant (shared machine).
