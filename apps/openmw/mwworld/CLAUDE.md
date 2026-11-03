# The world: `apps/openmw/mwworld`

Rules and pitfalls for new OpenEden code here. This folder is mostly OpenMW's (inherited): keep changes small and in its style. Read the root `CLAUDE.md` first.

## Incremental cell build (task 0060, `Scene`, `[Cells] incremental cell build`, off by default)

With the setting off nothing here changes: `Scene::insertCell` builds every object of a cell inside `loadCell`, as OpenMW does.

With it on, a **border crossing** (the `ChangeCell` that `World::moveObject` pushes, `incrementalLoad = true`) loads the new exterior cells as before (land, height field, pathgrid, local scripts registered, window manager, water, `exteriorCellLoaded`) but does not build their objects. `Scene::insertCell` puts each object in `Scene::mBuildQueue` (a `Misc::BuildQueue<Ptr>`, `components/misc/buildqueue.hpp`, unit tested with fakes) and `Game::run` calls `Scene::buildPendingObjects()` once a frame, before the local scripts. It builds objects until `[Cells] incremental build budget ms` is used (at least one object; after 120 calls a share of the queue, so it always empties). Order: nearer cells first; inside a cell the plain objects (statics, doors, containers, lights, items), then the actors, then the navigator pass; inside each, nearer to the player first. The order never depends on time; only how many fit in a frame does.

"Building" an object is exactly what `insertCell` did: `addObject` for rendering, mechanics, physics, Lua and the extension host, then the second `addObject` for the navigator (with one `UpdateGuard` per frame instead of one per load).

### What an unbuilt object is

An object of an active cell that is waiting is in the same state as a **disabled** object, which OpenMW already supports: no model, no physics body, no mechanics entry, no navigator entry, no Lua `onActive`. It differs in that its record says enabled, so:

| Thing | What happens to an unbuilt object |
| --- | --- |
| Local scripts | skipped by `Game::executeLocalScripts` (`Scene::isPendingBuild`) until the object is built (its navigator pass is the last item, so up to a few frames longer) |
| `Disable`, `Delete`, moving to an inactive cell | `Scene::removeObjectFromScene` forgets it from the queue (`cancelKey`) and the usual removal copes with a never-added object |
| `Enable`, `placeatpc`, a new reference, levelled-list spawns | `World::enable` does nothing for an enabled object; new references are added at once by `addObjectToScene`, which also cancels a queued entry for the same reference (never two adds) |
| Moving between two active cells | `World::moveObject` calls `Scene::updatePendingPtr`, which gives the queued entries the new reference |
| Scripted move, rotate, scale | the build reads the record's position, rotation and scale as they are then (`loadTransform`, `makeDirectNodeRotation`); an object whose angle a script set in the inverse order before it was built gets the direct order |
| `mPhysicsPostponed` | `addPostponedPhysicsObjects` skips queued objects; the build clears the flag and adds the body at the current position |
| Cell unload mid-build, `Scene::clear` (load game, new game) | `unloadCell` calls `cancelGroup`; `clear` empties the queue |
| Teleport, interior door, fast travel, first load, `changeToExteriorCell` with `incremental = false` | never incremental; a non-incremental `changeCellGrid` also builds every cell still waiting (`finishCell`) |
| The cell the player stands in | `changeCellGrid` and `changePlayerCell` call `finishCell`: the player is never in a cell that is not built |
| Saving | the save reads records, not the scene, so it does not care; a game loaded from it starts with an empty queue |
| Picking, activation rays, tooltips | `World::getFocusObject` and `castRenderingRay` use the render scene graph, so an unbuilt object is both invisible and unpickable; its body does not exist either, so rays and bumping agree |
| Object paging and LOD clusters | `RenderManager::setCellObjectsPending(x, y, true)` tells `DistantObjects` the cell is unbuilt: its distant chunk keeps drawing even though the cell is in the active grid, and `staticsReady` is false, so a cluster stays solid until `reportBuiltCells` clears the flag |
| Sound, lights | set up by the build like any object (`Class::insertObject*`), at that time |

### Threads and pitfalls

- Everything above runs on the main thread. `Scene::changeCellGrid` can run on a loading-screen worker, but only for loads that are not incremental, which never queue.
- The build budget uses `std::chrono::steady_clock`. It decides only how many objects fit in a frame.
- A build step can run more than the budget: one NPC is 2 to 10 ms, a cold one up to 50 ms. That is why `Npc::defaultPartModels` and `Npc::animationSources` are preloaded (`PreloadCell`).
- Do not call `addObject` for a queued object by another route: `PhysicsSystem::addObject` asserts that a body does not exist yet.
