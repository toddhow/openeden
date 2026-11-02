# 0026 - Cell-load hitches: where the 25 to 110 ms go

Date: 2026-11-02 · Task 0060 (LOD S6), stage a (measure and attribute) and the first stage g change · Status: measurement. Every number is from a run on this machine on this date.

## 1. Machine and method

- **Machine:** the one of note 0025 (Windows 11, i7-13700K, RTX 4070), `build-jolt` Release, Morrowind with Tribunal and Bloodmoon, no other plugins. The config is a copy of the user's (`tools/benchmark.sh` makes one per run in a scratch folder, with its own user-data folder, so the navmesh cache starts empty every run).
- **Instrument:** `[Cells] load trace = true` (off by default). `Misc::LoadTrace::Scope` (`components/misc/loadtrace.hpp`) records name, begin, end, a value and a thread number into memory; the file `load-trace.jsonl` is written at exit (path: the user-data folder, or the file named by `OPENEDEN_LOAD_TRACE_FILE`). When off, a scope is one relaxed read of an atomic flag. Scopes are in `Engine::loop` (acquire, events, callback, viewer update, record and submit, present, deletion queue), `Engine::frame`, `Game::run`, `World::update`, `Scene::changeCellGrid`, `Scene::loadCell` (each part), `Scene::insertCell`, `addObject` (render, mechanics, physics, Lua), `MWRender::Scene::insertModel`, `insertNPC` and `insertCreature`, `Npc::updateNpcBase` (skeleton, animations, parts, part reads), `RenderManager::updateTerrain` and `update`, `CompileContext::compile`, and on the preload worker `PreloadCell::operate`.
- **Runs:** the flight `heavy_lod_flight_off` (note 0025 section 4: collision off, 1500 up, Athletics 1500, 15000 view, nothing distant) three times before and three after the change; a slow walk `heavy_lod_walk_off` (a new line in `tools/render_scenes.txt`, script `tools/benchmark_data/walk_slow.txt`: collision off, 1500 up, Athletics 120, 7168 view, about 60,000 frames) three times before and three after. The runs are deterministic: the same frames are slow in every run (for example frames 7224, 6187 and 7248 in the three flights, each the same 3 cells and 520 objects).
- A frame is measured from the end of the previous frame, so it includes the wait for the swapchain. The first 700 frames (start-up) are not in the tables; the one 9-cell load right after them (the player placed by the script, 600 to 650 ms) is listed apart.
- The distant-terrain and cluster rows of the spec were not run: no LOD packs were baked for this note, and the hitch is the same code path with or without them (the tiles are not the culprit).

## 2. Where the time goes (flight, before the change, 3 runs, 28 hitches of 3 cells each)

Mean of those 28 hitch frames: 47.2 ms wall (range 25 to 101). Main-thread scopes, mean ms per hitch (nested scopes overlap):

| Scope | Mean ms | Share of 47.2 | What it is |
| --- | --- | --- | --- |
| `insert.objects` (`Scene::insertCell`, all objects of 3 cells) | 35.4 | 75% | everything below except the navigator and the loop |
| `npc.create` (`Npc::updateNpcBase`) | 15.2 | 32% | of which `npc.readanimations` 6.8 (cold reads, up to 44 ms for one NPC) and `npc.part.read` 7.3 (cold body part reads, up to 35 ms) |
| `model.compile` (`CompileContext::compile` of statics) | 6.8 | 14% | about 17 microseconds an object |
| `creature.create` and `creature.compile` (incl. leveled-creature spawns) | 5.7 | 12% | one spawn took 15 ms |
| `obj.physics` (Jolt body creation) | 2.4 | 5% | |
| `model.create` (static model read, cached) | 1.7 | 4% | |
| `load.exterior.total` (land read, height field, navigator height field) | 1.1 | 2% | |
| `grid.unload`, `insert.navigator`, scripts, mechanics | about 1.0 each | 2% each | |
| `loop.record.submit` | 1.0 | 2% | |

Single hitches, worst first: 101 ms (frame 7224 of the flight: 520 objects, one NPC 46.6 ms, of which 41.6 ms was reading the Argonian animations for the first time), 75 ms (557 objects, one NPC 10 to 28 ms), 65 and 63 ms (NPC cold reads 25 to 33 ms each), the rest 25 to 48 ms (356 to 443 objects, no single cause: 3 ms for each NPC built even when warm, 15 ms for a leveled creature spawn).

## 3. The hypotheses

| # | Hypothesis | Verdict | Numbers |
| --- | --- | --- | --- |
| H1 | compile of every object of 3 cells on the main thread | **Part of it, not dominant** | `model.compile` 6.8 ms per hitch (14%), `npc.compile` 2.2, `creature.compile` 1.9: about 11 ms in all, 23% |
| H2 | a blocking model or animation read on the main thread because the preloader had not done it | **Confirmed, the largest single piece** | `npc.readanimations` + `npc.part.read` = 14 ms per hitch (30%); the preload reads animations under another name than the NPC build asks for (section 4) |
| H3 | terrain `updateView` compile | **Refuted for cell crossings** | `terrain.updateview` mean 0.002 ms (max 5.1), `terrain.compile` mean 0.14 (max 9.2, once) |
| H4 | Jolt height field and bodies, navigator | **Small** | `obj.physics` 2.4 ms (5%), `load.exterior.total` 1.1, `insert.navigator` 0.12 per cell, `grid.navigator.update.release` 0.5 |
| H5 | freeing node trees in `advanceFrame` | **Refuted** | `loop.deletion` mean 0.006 ms, max 1.0 ms over 22,000 frames |
| H6 | scripts and Lua on cell load | **Refuted** | `load.scripts.respawn` 0.5 ms, `obj.lua` 0.3, `obj.mechanics` 0.5 per hitch |
| H7 | sky visibility field | **Refuted** | `load.water.ambient.sky` 0.008 ms |
| H8 (new) | `World::update` waits for navigator tiles after a cell load | **Seen once**, in the first 9-cell load after start | `world.navigator.wait` 145 ms in that frame, about 0 in the 3-cell crossings (mean 9.1 over 16 calls) |

The spec's named first suspect (`insertModel` compile) is 14% of a hitch, not the cause. The hitch is **the construction of about 400 to 550 objects in one frame**, of which the NPCs and creatures are the expensive ones (45% together, counting their cold reads), and what can be moved cheaply off the main thread is the cold reading.

## 4. The change (stage g, first move): preload the animation files NPCs read

Cause, from the code: `Npc::updateNpcBase` reads its animations with `mContext.readAnimations(sources)` where `sources` are `[Models] xbaseanim`, the skeleton, the NPC's own `mModel` as written in the record (for example `base_animKnA.nif`) and, for Argonians, `xargonian_swimkna`. The animation options cache by the name as written (`vsg::SharedObjects`; `createAnimationOptions`, `components/resource/resourcesystem.cpp`). The preloader (`PreloadCell::operate`) read `readAnimation(correctMeshPath(model))`, a different name, and never read `xbaseanim` or the swim file, so the first NPC of each kind read them on the main thread: 19 to 42 ms each time (`SLOW anims` lines of a debug run: female skeleton 19 ms, Khajiit and Argonian 25 ms, a later Argonian 42 ms).

Change: `Npc::animationSources` (the list and skeleton, extracted unchanged from `updateNpcBase`) is also called by `PreloadCell` on the main thread for every NPC of the cell and for the four plain kinds; the worker reads those names with `readAnimation` and `readActor` and keeps them referenced in `mPreloadedObjects` like the rest. Nothing is read that was not read before, only earlier and on the preload thread. No setting: with the preloader disabled nothing changes, and the reads are the same calls the main thread made.

Results (same flights, same machine; three runs each):

| | Before | After |
| --- | --- | --- |
| Flight: frames over 25 ms (run 1, 2, 3) | 11, 10, 12 | 13, 11, 9 |
| Flight: p99 ms | 3.65, 3.49, 3.44 | 3.41, 3.52, 3.54 |
| Flight: p99.9 ms | 6.22, 6.58, 6.49 | 6.24, 6.41, 6.46 |
| Flight: worst 3-cell crossing | 100.8 ms | 60.4 ms |
| Flight: mean of the 3-cell hitches | 47.2 ms (28 hitches) | 37.1 ms (11 hitches, run 1) |
| `npc.readanimations` per hitch | 6.8 ms (max 44) | 0.1 ms |
| Walk: frames over 25 ms | 6, 6, 6 | 6, 5, 6 |
| Walk: p99 / p99.9 ms | 3.04 to 3.12 / 4.08 to 4.26 | 3.02 to 3.09 / 4.01 to 4.42 |
| First 9-cell load after start | 649, 650, 652 ms | 626, 615, 589 ms |

The count of hitches over 25 ms did not fall: a crossing costs 25 to 40 ms even when everything is warm. The change removes the long ones (a 95 to 101 ms crossing became 56 ms). The criterion of the spec (at most 3 crossings over 25 ms, worst under 50 ms, p99.9 under 8 ms) is **not met**: p99.9 is already under 8 ms before and after, the worst crossing is 56 to 60 ms, and the count is 9 to 13. The rest cannot be moved by reading earlier: it is the work of building the objects (NPC assembly about 3 ms each when warm, 17 microseconds of compile and 6 of physics per static, leveled creature spawns) on one frame. The next candidates are in section 5.

Checks: components-tests 4469, openmw-tests 830, eden-studio-tests 568 pass; the 32 Lua integration tests pass (Windows, `integration.sh`); the warnings in `openmw.log` of a flight are the same before and after; frames of two interiors were taken but no NPC was in view, so **no picture of an NPC was compared** (the change reads files into caches that the NPC build then finds; it does not change what is built).

## 5. What the next change should be (not done)

1. **Race body parts, heads and hair** (`npc.part.read`, 6 ms per hitch, 35 ms for one NPC): each part type has its own options object and so its own cache (`partBoneOptions`, `animcontext.cpp`); the preload never reads through them. A preload needs the part type of each model (`getBodyParts`, `getHeadModel`) and the same `markSkinModel` call, so it was left as a separate step.
2. **Equipment parts** need the NPC's inventory, which exists only after `ensureCustomData`; creating it earlier changes the order of random numbers (leveled items), so it is not a safe preload.
3. **Spreading the construction of a cell's objects over several frames** (statics first, actors in a later frame, a budget in milliseconds) would give the large win (a 400-object cell is 15 to 30 ms of pure work) but is a behaviour change: scripts and physics see the cell loaded before it is built. It needs a design decision by toddhow.
4. **Compile of statics on the compile thread** (`model.compile`, 17 microseconds an object): the preloader already compiles the models it read; a per-instance compile remains. Needs a look at what the instance's compile adds that the shared model's does not.
5. **Leveled creature spawns** (15 ms for one): the spawn builds the creature's whole animation on the main thread at insert time.

## 6. Reproducing

```
BENCH_FILTER=heavy_lod_flight_off BENCH_FRAMES=40000 \
  BENCH_RENDERING='frame counters = true\n[Cells]\nload trace = true' tools/benchmark.sh
# the walk: BENCH_FILTER=heavy_lod_walk_off BENCH_FRAMES=70000 (the scene line is in tools/render_scenes.txt)
# the trace: <run folder>/userdata/load-trace.jsonl, one JSON object per line:
#   {"n":name,"b":begin_us,"e":end_us,"v":value,"t":thread} or {"f":frame,"b":begin_us,"e":end_us}
```

The analysis scripts were Perl, in the session scratchpad, and are not committed.
