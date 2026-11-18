# 0146 - Skyrim cell streaming: where the main thread loses time

Date: 2026-11-18 · Task 0323 (follows research notes 0026 and 0028, task 0060 LOD S6) · Status: measurement. Every number is from a run on this machine on this date. Nothing was fixed: the only code changed is trace scopes and a developer aid (section 6).

## 1. Short answer

On Windows with the five Skyrim SE masters, a scripted run across Whiterun's hold that crosses 14 cell borders at 455 units a second (a horse's pace) loses its time like this, in the frames over 25 ms:

| Rank | Cost (main thread) | Cold navmesh, one updater thread (the default) | Warm navmesh | What it is |
| --- | --- | --- | --- | --- |
| 1 | `world.navigator.wait` | **81 %** of hitch time, 1 to 6.6 s a crossing | 0.2 % | the blocking wait for navmesh tiles after a cell grid change (inherited, `World::update`) |
| 2 | cell build in one frame (`insert.objects`) | 14 % | **60 %** | model reads (`model.create`), actor builds (`neutralactor.create`), Jolt bodies (`obj.physics`) |
| 3 | the pack's Papyrus frame (`phase.GameSimulation`) | 2 % | **20 %** (100 % of what is left with incremental build on) | script attaches and their first events on the new cells |
| 4 | `loop.record.submit` + `compile.vsg` | 2 % | 13 % | recording the new objects' command buffers |
| 5 | the pack's cell build (`neutral.cell.build`) | 1 % | 5 to 7 % | `SkyrimWorldView::loadCell`, merged over the load order |
| - | grass, terrain ensure, weather, Lua, mechanics, physics step | under 1 % each | under 2 % each | not a hitch cause |

The first-time read of a model is most of the cell-build item: 88 to 92 % of `model.create` time sits in the slowest 5 % of calls (the first read of each distinct model; the median call is 5 microseconds, a cached template), and `MWClass::Neutral::getModelsToPreload` is an empty override (`apps/openmw/mwclass/neutral.hpp`), so **the cell preloader never reads a Skyrim model** (`preload.model` was never recorded in any run; `preload.land` and `preload.animations` were).

Recommendation (section 7): the first piece is not on the 0060 S6 list: the navmesh wait (a setting and a bake). After that, the first S6 piece is **prefetch ahead of the camera, in its cheapest form: let the existing preload worker read the Skyrim models** (S6 stage d without the new scheduler); next a Skyrim-specific pack change (spread the Papyrus flush over frames); then the shared scheduler and IO pool (stages b and c), once prefetch has more to schedule. GPU upload slicing and mip streaming are not what this machine's hitches are made of.

## 2. Machine and method

- Windows 11, i7-13700K, RTX 4070, RelWithDebInfo build of branch `streaming-0323` (`c14f282175` plus the scopes of section 6). **The machine was shared**: three to four other `openmw.exe` and build processes ran at times, so the run-to-run spread is large (the cold navigator wait of the same scenario: 19 s, 24 s, 30 s, 61 s in four runs). Numbers are given per run where they differ much.
- The game: `game=skyrimse`, the five masters, `[Game] neutral world`, `neutral actors`, idles, locomotion, combat, spell effects, object animations, process levels, inventory, weather, world materials, Havok collision, tree sway, terrain layers and grass on; the pack settings `actor packages`, `inventory`, `papyrus`, `creatures` on (a play session without UI, HUD or menus). **World LOD was off** (no baked tiles or clusters), so LOD costs are not in these tables. `[Sound] master volume = 0`, `OPENEDEN_QUIET_WINDOW=1`, `OPENEDEN_FIXED_FRAME_TIME=1/60` (simulation time is fixed, frames run as fast as the default `framerate limit = 300` lets them, so **the 3.3 ms floor of the median is the frame limit, not work**; a steady frame is about 2.3 ms of main-thread work).
- The scenarios (`tools/streaming_check/run.sh`, Lua in `tools/neutral_session_check/luatest/scripts/stream323`):
  - `rwalk`: the player starts in Tamriel at (19320, -7433) just outside Whiterun's west gate, 50 real seconds after the process starts (the pack's records and the Papyrus host are read on workers and need that long), then moves along (0.8, 0.6) for 90 simulation seconds with the engine's own movement, collision off and 400 units up, at 455 units a second (Athletics 133 on the stub player): **14 border crossings**; the grid is 5 x 5 cells of 4096 units; about 91 cell loads, 5,300 objects and 60 actors built in the window.
  - `rfly`: the same line at 1,162 units a second, 1,500 up (Athletics 600): 20 crossings. A very fast horse or a dragon: an upper bound.
  - `gate`: the Bannered Mare, its front door to Whiterun's street, the gate door to Tamriel, and back (real seconds; the door scripts of `tools/neutral_session_check`); `hop`: the Dawnstar barracks and jail doors, interior to interior.
  - **Why the player moves by the engine's own movement and not by teleports:** a Lua teleport is not a border crossing for the incremental cell build (`World::moveObject` sets `incrementalLoad` only for movement); the first matrix of runs (a teleport every frame) built every crossing whole in both settings, which is why `incremental cell build = on` showed no `incremental.build` scope in it. Those runs are not used.
  - Whiterun's city cells belong to `WhiterunWorld`, the wilderness to `Tamriel` (`WhiterunWorld` has objects only in its own 7 cells and borrows Tamriel's land), so the walk starts in `Tamriel`.
- Settings compared: `[Cells] incremental cell build` off and on (budget 3 ms); cold navmesh (a fresh `navmesh.db`) and warm (the db the first cold run wrote, same route; the OS file cache is warm in both); `[Navigator] enable = false`; `[Navigator] async nav mesh updater threads = 4` (default 1).
- Measuring: `[Cells] load trace = true` (note 0026), `tools/streaming_check/analyze.py` and `attrib.py` (the window starts at the frame where the walk starts; hitch = a frame over 25 ms; a frame is measured from the end of the previous one, as in 0026). **Two early cold, incremental-off runs were ended by the 5 s freeze monitor (exit 173) before `OPENMW_DISABLE_CRASH_CATCHER=1` was set; all three cold, incremental-off walks that completed had a frame over 4 s.** The killed runs were repeated and are the ones in the tables.

## 3. Frame times (walk, 455 units/s, 5,578 frames in the window)

| Run | median | p99 | p99.9 | max | frames over 25 ms | mean hitch |
| --- | --- | --- | --- | --- | --- | --- |
| cold, incremental off (3 runs) | 3.96 / 3.74 / 3.75 | 9.8 / 5.7 / 5.7 | **3,404 / 2,652 / 2,656** | **6,630 / 4,416 / 4,238** | 25 / 23 / 17 | 1,419 / 1,094 / 1,444 ms |
| cold, incremental on (3 runs) | 3.94 / 4.05 / 3.90 | 64 / 39 / 12 | 2,849 / 6,695 / 2,322 | 7,221 / 10,298 / 5,480 | 125 / 98 / 29 | 311 / 698 / 823 ms |
| warm navmesh, off (2 runs) | 3.78 / 3.87 | 5.8 / 6.3 | 249 / 280 | **579 / 646** | 17 / 18 | 179 / 208 ms |
| warm navmesh, on (2 runs) | 3.96 / 3.92 | 16 / 43 | **93 / 179** | **187 / 479** | 31 / 94 | 67 / 83 ms |
| warm, off, with the pack frame trace (3 runs) | 3.80 / 3.84 / 4.00 | 6.0 / 6.1 / 8.7 | 446 / 276 / 436 | 2,658 / 621 / 899 | 18 / 15 / 19 | 476 / 205 / 273 ms |
| warm, on, with the pack frame trace (3 runs) | 3.80 / 3.97 / 4.00 | 13.9 / 13.4 / 14.2 | 85 / 86 / 94 | 114 / 119 / 145 | 30 / 35 / 34 | 61 / 59 / 61 ms |
| cold, navigator off (off / on) | 3.97 / 3.93 | 12 / 94 | 813 / 450 | 4,461 / 2,244 | 32 / 150 | 485 / 138 ms |
| cold, 4 navmesh threads (off / on) | 3.93 / 3.86 | 6.9 / 89 | 1,276 / 791 | 2,439 / 3,508 | 25 / 132 | 624 / 158 ms |
| fast flight (1,162 u/s), cold, off (2 runs) | 3.94 / 4.01 | 23 / 22 | 6,431 / 7,594 | 8,627 / 12,261 | 54 / 52 | 2,228 / 2,630 ms |
| fast flight, cold, on (2 runs) | 4.35 / 4.37 | 133 / 128 | 6,003 / 6,873 | 7,338 / 9,316 | 248 / 262 | 512 / 536 ms |

(All times in ms unless said. One warm trace run has a 2.7 s frame, a first-time load the other warm runs did not have.)

**Worst crossing:** cold 4.2 to 10.3 s; warm, incremental off 0.58 to 0.90 s; warm, incremental on 0.11 to 0.48 s.

Morrowind (note 0026, same machine class, Balmora flight with nothing distant): the 3-cell crossing costs a mean of 37 to 47 ms, worst 60 to 101 ms, 9 to 13 frames over 25 ms in the whole flight, p99.9 6.2 to 6.6 ms; the walk: 6 frames over 25 ms, p99.9 4.1 to 4.4 ms; the first 9-cell load 590 to 650 ms. **A Skyrim crossing (5 to 9 new cells) is 4 to 6 times a Morrowind one when everything is warm (mean 180 to 280 ms), and 25 to 40 times (mean hitch) with the navmesh cold, 100 times at the worst frame.**

Findings on the incremental build (`[Cells] incremental cell build`, note 0028):
- **Warm navmesh: it works.** p99.9 falls 3 to 4 times (249 and 280 ms to 93 and 179 ms; the traced runs 276 to 446 ms to 85 to 94 ms) and the worst frame from 580 to 900 ms to 110 to 190 ms (one run 479 ms). It makes more slow frames (31 to 94 against 17), as note 0028 found (one long frame becomes several of 60 to 90 ms).
- **Cold navmesh: it does nothing for the multi-second stalls** (they are the navigator wait, not the build), and p99 rises (12 to 64 ms against 6 to 10 ms) because the spread build then waits for tiles in more frames.
- What is left with it on and the navmesh warm is **the pack's Papyrus frame** (section 5).

## 4. Where the time goes (shares of the wall time of the frames over 25 ms; the lines nest)

Walk, 455 units/s. Sums over the runs of each kind (cold off 3 runs, 65 hitch frames, 85 s; cold on 3 runs, 252 frames, 131 s; warm off 2 runs, 35 frames, 6.8 s; warm on 2 runs, 125 frames, 9.9 s); `attrib.py`.

| Scope | cold, off | cold, on | warm, off | warm, on |
| --- | --- | --- | --- | --- |
| `world.navigator.wait` | **81 %** | **82 %** | 0 % | 0 % |
| `grid.total` (cell grid change) | 15 % | 1 % | 65 % | 3 % |
| `incremental.build` (the same work, spread; contains `obj.render`) | - | 11 % | - | 65 % |
| `obj.render` (object insert) | 13 % | 11 % | 49 % | 64 % |
| `model.create` (first read of each model) | 9 % | 7 % | 21 % | 46 % |
| `neutralactor.create` | 4 % | 4 % | 23 % | 17 % |
| `model.compile` | 0 % | 0 % | 4 % | 1 % |
| `obj.physics` (Jolt bodies) | 1 % | 0 % | 10 % | 1 % |
| `neutral.cell.build` (the pack; inside `grid.getexterior`) | 1 % | 1 % | 7 % | 4 % |
| `terrain.neutral.ensure` | 0 % | 0 % | 1 % | 7 % |
| `phase.GameSimulation` (the pack's frame) | 2 % | 3 % | **20 %** | **13 %** |
| `loop.record.submit` | 1 % | 1 % | 9 % | 2 % |
| `compile.vsg` | 1 % | 0 % | 4 % | 1 % |
| `neutralgrass.pack` | 0 % | 0 % | 1 % | 1 % |
| `run.physics` (pack AI and combat update, step) | 0 % | 0 % | 4 % | 3 % |

The lines nest, so the columns are not additive. With incremental build on, a warm hitch frame is mostly one build slice that is longer than its 3 ms budget because a single actor or a single first model read is not divisible (`neutralactor.create` is 16 ms warm and 27 ms cold per actor; a first `model.create` is up to 60 ms warm and 190 ms cold).

Ablations on the cold navmesh (one run each): **navigator off** leaves the cell build as the hitch (p99.9 813 ms, worst 4.5 s, `model.create` 35 %, one `neutral.cell.build` of 1.3 s); **four updater threads** removes the wait (`world.navigator.wait` 7 ms in the run against 19 to 61 s) and leaves p99.9 1.3 s, worst 2.4 s, the cold model reads (`obj.render` 72 %). So the wait is a throughput limit of the one default updater thread over Skyrim's collision (a tile is Recast over terrain and thousands of statics); **a warm db removes it entirely** (0.2 %).

Per object, warm: inserting a cell costs 59 ms and 1.0 ms an object (91 cell loads, 5,278 objects, 4,426 `model.create` calls, 60 actors); cold 125 ms a cell and 2.2 ms an object. Morrowind (0026): about 0.08 ms an object. `model.create`: median 5 microseconds, p90 0.3 ms, p99 8 ms (warm) to 40 ms (cold); the slowest 5 % of calls are 88 % (warm) and 92 % (cold) of its time, the first read and conversion of each distinct model (`NifScene::flatten` and the textures it asks for), not the instancing. Textures were not separated from it (no scope in the reader).

Fast flight (1,162 u/s) is the same picture 2 to 3 times as bad: cold off 74 % navigator wait, cold on 84 %.

## 5. The pack's own frame (`phase.GameSimulation`)

The pack cannot use the engine tracer (its own copy of the flag), so `OPENEDEN_PACK_FRAME_TRACE=1` (section 6) times it itself. Warm, 3 runs each of off and on: in every run 12 frames of 5 ms or more hold nearly all of the 1.5 to 1.6 s the pack's frame took in 19,000 frames (the other frames cost 0.08 ms); **those frames are the Papyrus host's frame, not the rest of the pack (99.9 %)**, 60 to 110 ms a crossing and 500 to 850 ms at the first. Inside it (the `pk2` runs, off and on): `flushPending` (the attach of the scripts of the new cells' references, 40 to 340 ms a crossing, about **70 % of it**), `mHost.tick` (the first events and the scripts they start, 20 to 23 %), `processPlace` (the location-change story event: the story manager walks the quests, 90 to 100 ms at the first crossing, 6 to 8 %). In the gate walk the first visit to the city is 1.1 s of tick and 0.35 s of flush. With incremental build on and the navmesh warm the remaining hitches are these 60 to 110 ms Papyrus frames: the build is spread, they are not.

## 6. What was added

- Scopes (one atomic read when off): `phase.<Phase>` in `LifecycleManager::runPhase` (a game pack's callbacks run in `GameSimulation`, so the pack now appears in the trace), `neutral.cell.build` (`WorldModel::findNeutralExterior`: the pack's `loadCell`), `physics.neutral.actors`, `physics.step`, `ext.neutral.apply`. Tests `PhaseTrace.*` (2, written after the code).
- `OPENEDEN_PACK_FRAME_TRACE=1` (developer aid, not a setting; listed in `docs/debug-environment-variables.md`): the pack logs `SKYFRAME` (the Papyrus frame against the rest, absolute steady-clock microseconds, the clock of the engine trace) and `SKYPAPYRUS` (flush, scenes, pendingstory, place, applies, story, tick, scenes.ambient) for any frame of 5 ms or more, and a `SKYFRAME summary` at stop. `game_packs/skyrimse/include/game_skyrimse/frame_split.hpp`; tests `SkyrimFrameSplit.*` and `SkyrimSectionMarks.*` (10, written before the header).
- `tools/streaming_check/` (`run.sh`, `batch.sh`, `analyze.py`, `attrib.py`, `skyjoin.py`, `summarize.py`, `env.sh`) and the Lua `scripts/stream323`, `stream323*.omwscripts`.

## 7. Recommendation

1. **Not an S6 piece: the navmesh wait.** It is 81 % of the hitch time of a first visit and one setting from gone: `[Navigator] async nav mesh updater threads` defaults to 1 and the main thread blocks on `requiredTilesPresent` after every grid change. Options for toddhow: a default of 4 for a game pack's neutral world; baking or shipping a navmesh db (the warm runs show what it buys); not waiting for tiles beyond the player's own for a neutral exterior.
2. **First S6 piece: prefetch ahead of the camera (stage d), in its cheapest form.** Give `MWClass::Neutral::getModelsToPreload` the model so the existing `PreloadCell` worker (it already reads `preload.land` and `preload.animations` for these cells) reads the Skyrim models before the crossing: the first read of each distinct model is 88 to 92 % of `model.create`, 21 % of a warm hitch and about 70 % of the cell build cold. Whether the preload read (`vsgUtil::readNode`) shares the cache and the Skyrim scene reader with `createObject` was not checked and is the first thing the task has to settle. Actor builds (`neutralactor.create`, 23 % warm) are the next slice of the same work (the preloader reads the animations of neutral actors but not their skeleton or parts).
3. **A Skyrim-specific change that comes before the scheduler: budget the pack's Papyrus flush** (attach the new cells' scripted references over several frames, as the incremental build does for objects). It is 20 % of a warm hitch and all that incremental build leaves; it is pack-only (`PapyrusDriver::flushPending`) and does not touch the core.
4. **The shared scheduler and IO pool (stages b and c) after that,** when prefetch asks for hundreds of models and several readers: the existing single preload thread is enough for the first step.
5. **GPU upload slicing (stage e) and mip streaming from disk (stage h) are not first.** `loop.record.submit` and `compile.vsg` together are 13 % of a warm hitch (2 % cold), and texture reads were not measured apart from `model.create`.
6. **Make the incremental build the Skyrim default only together with 1 or 3.** Warm, it cuts the worst crossing from about 600 ms to about 150 ms; cold, it does nothing.

## 8. Not measured, and limits

- World LOD (tiles, clusters), so no LOD attribution; texture reads as a cost of their own; GPU time; frames with HUD, menus or a UI; movement with collision (the player flew with collision off, so no stuck walks and no terrain-following); a player body; saves; a weaker or Linux machine.
- The matrix is not three runs everywhere: the warm rows have 2 or 3, the ablations 1, the doors 2; the machine was shared and the variance is large.
- Doors (`gate`, `hop`, 2 runs each): loading a town's cells through a door is **8 to 10 s** the first time (Mare to Whiterun street 9.5 s: `obj.render` 5.6 s, navigator 1.7 s; street to Tamriel 7.8 s; Tamriel back to the street 9.6 s with 1.7 s of Papyrus); interior to interior (Dawnstar barracks and jail) 0.6 to 2.0 s, nearly all `world.navigator.wait`. Incremental build does not apply to doors (a teleport builds whole). The frame after the first world load also has a 2 s Papyrus frame (the host starting its quests).
