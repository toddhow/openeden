# 0027 - Incremental cell build: moving the cost of a border crossing over several frames

Date: 2026-11-02 · Task 0060 (LOD S6), stage g, second and third moves · Status: measurement and a design with an off-by-default setting. Every number is from a run on this machine on this date.

## 1. What was built

Note 0026 found that the 25 to 100 ms frame at a border crossing is the construction of 400 to 550 objects in one frame. toddhow chose to spread it over frames. Three changes, all measured below:

1. **`[Cells] incremental cell build`** (off by default) and **`[Cells] incremental build budget ms`** (default 3). A border crossing loads the new exterior cells as before (records, land and height field, pathgrid, local scripts registered, window manager, water) but queues their objects (`Scene::insertCell`, `Misc::BuildQueue`, `components/misc/buildqueue.hpp`). `Game::run` calls `Scene::buildPendingObjects` once a frame, which builds objects until the budget is used. Order: nearer cells first; in a cell the plain objects, then the actors, then the navigator pass; nearer to the player first. What "built" and "unbuilt" mean, and every interaction (scripts, physics, picking, paging, unload, teleport, save), is in `apps/openmw/mwworld/CLAUDE.md`.
2. **Race body parts are preloaded** (`Npc::defaultPartModels`, read by `PreloadCell` through the same per-part-type options as the build). `npc.part.read` was 6 ms per hitch and 35 ms for one NPC (note 0026 section 5).
3. The cell the player stands in, and every cell when the load is not a border crossing, is built whole: a teleport is unchanged.

## 2. Method

`tools/benchmark.sh` copies the user's config into a scratch folder with its own user-data folder (the user's real files are never touched). Scenes: `heavy_lod_flight_off` (the fast flight, nothing distant, 15000 view; about 16 crossings of 3 cells) and `heavy_lod_walk_off` (the slow walk). `[Cells] load trace = true`. A **crossing** is one `grid.total` scope after start-up; its **worst frame** is the longest frame from the start of the load to the end of the last `cell.built` of that crossing (with the setting off, just the frame of the load). The start-up and the first 9-cell load are left out. A frame is measured from the end of the previous one (it includes the swapchain wait). Tables are medians over runs; the individual runs are given because **the machine was shared**: other sessions were building and running games during runs 1 to 3, which show as isolated 60 to 3000 ms frames in either setting; runs 4 to 6 of the flight were taken with nothing else running and are the ones the headline numbers use.

## 3. Flight (3 clean runs, 4 to 6; per run)

| | Off | On, budget 3 ms |
| --- | --- | --- |
| Frames over 25 ms | 15, 9, 8 | 0, 0, 0 |
| Worst crossing | 167.7, 82.6, 81.8 ms | 22.9, 21.7, 24.7 ms |
| Mean of the crossings' worst frame | 61.7, 33.6, 33.1 ms | 13.2, 12.6, 13.2 ms |
| Frames over 16.7 ms | 18, 16, 15 | 3, 2, 6 |
| p99 frame | 4.89, 3.89, 3.87 ms | 4.76, 4.32, 4.47 ms |
| p99.9 frame | 12.2, 8.2, 6.9 ms | 10.0, 10.0, 11.5 ms |
| Mean frame | 2.52, 2.09, 2.10 ms | 2.19, 2.07, 2.23 ms |
| Time until a cell is fully built | 0 (built in the load frame) | mean 39.6, 38.6, 41.4 ms; worst 108 to 112 ms |
| Main-thread time spent building, per crossing | 23.0 to 50.0 ms | 24.5 to 26.3 ms |

All six runs (shared machine included): frames over 25 ms, median 10 off and 1 on (off 14, 11, 9, 15, 9, 8; on 2, 2, 8, 0, 0, 0); worst crossing, median 90 ms off and 24 ms on (on: 23.5, 221, 64.4, 22.9, 21.7, 24.7; the 221 and 64 were during other sessions' builds).

Reading it:

- **The criterion of the restated target is met in the clean runs:** worst crossing 22 to 25 ms (target under 35) and no frame over 25 ms (target at most 3 per flight).
- **p99.9 is not improved, it is slightly worse** (6.9 to 12.2 off, 10.0 to 11.5 on). That is the price: one frame of 30 to 80 ms becomes about 10 frames of 6 to 15 ms (3 ms of building on top of a 2 ms frame, plus the one object that overran the budget). The p99 is unchanged. The mean frame is unchanged within noise.
- **The total cost is not smaller, it is moved:** per crossing the main thread spends 23 to 26 ms building with the setting on against 23 with it off in the clean runs (about 10% more: one navigator update guard per frame instead of one per load, and objects built after the cell was added).
- **What is left over 16.7 ms** is one NPC or creature that overran the budget (one build step is not divisible): `npc.updateparts` 2 to 9 ms warm, a levelled-creature spawn 13 to 22 ms (`creature.create`), a cold equipment part.

## 4. Slow walk (3 runs each, 5 or 6 crossings a run, final build)

| | Off | On, budget 3 ms |
| --- | --- | --- |
| Frames over 25 ms | 6, 4, 4 | 0, 11, 2 |
| Worst crossing | 84.7, 57.8, 51.8 ms | 19.8, 42.0, 20.5 ms |
| Mean of the crossings' worst frame | 43.4, 35.9, 34.8 ms | 13.7, 29.5, 14.7 ms |
| p99 / p99.9 frame | 3.6 to 4.3 / 5.0 to 6.5 ms | 3.4 to 3.9 / 5.2 to 11.3 ms |
| Time until a cell is fully built | 0 | mean 37.4, 70.3, 42.2 ms; worst 77 to 126 ms |

Run 2 with the setting on had a 2226 ms frame that no crossing explains (the machine was shared; the run's other crossings are 15 to 40 ms); without it the picture of runs 1 and 3 is the same as the flight's: worst crossing about 20 ms against 52 to 85 ms.

## 5. Interactions checked

See `apps/openmw/mwworld/CLAUDE.md`. Checked by reading the code (file and function named there) and by the tests listed in the stage log of task 0060; not checked in play: scripted quests that touch a just-loaded cell within its first few frames.

## 6. Reproducing

```
OPENEDEN_LOAD_TRACE_FILE=<file> BENCH_FILTER=heavy_lod_flight_off BENCH_FRAMES=40000 \
  BENCH_RENDERING='frame counters = true\n[Cells]\nload trace = true\nincremental cell build = true\nincremental build budget ms = 3' \
  tools/benchmark.sh
```
