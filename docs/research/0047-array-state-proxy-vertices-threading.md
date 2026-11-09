# 0047: Is the shared `proxy` of `vsgUtil::ArrayState::getOrCreateProxyVertices` a data race?

Date: 2026-11-08. Status: finding, no code change (the race is not reachable today). Docs and one header comment only.

## Question

The changelog row of 2026-11-08 (crash in `Terrain::HeightArrayState::vertexArray`) says the other callers of
`getOrCreateProxyVertices` "share one unlocked `proxy` object per batch". Is that a race?

## What the code does

`components/vsgutil/arraystate.hpp`, `ArrayState::getOrCreateProxyVertices(parent, minSize)`: reads
`parent->getObject<vec3Array>("proxy")`; if it is missing or smaller than `minSize` it allocates a new array and stores it with
`parent->setObject("proxy", array)`. `Object::setObject/getObject` go to the object's `vsg::Auxiliary::userObjects`, a plain
`std::map` (VSG 1.1.16, `vsg/core/Auxiliary.h`, `Object.cpp`); the `Auxiliary` has a mutex but `Object::getObject/setObject` do not take it.
The returned array is then written by the caller. So the function is not thread safe: two threads on the same `parent` would
race on the map and on the array contents.

Callers (grep of `components/` and `apps/`, whole tree):

| Caller | `parent` | Used by |
| --- | --- | --- |
| `Terrain::HeightArrayState::vertexArray` (`components/terrain/heightarraystate.hpp`) | the batch's terrain `StateGroup` (`components/terrain/builder.cpp`, lines 780 to 785) | an intersector |
| `Anim::SoftwareSkin::apply(const vec3Array&)` (`components/animation/softwareskin.cpp`) | the `Anim::Skin` | an intersector (`components/animation/skin.cpp` line 173 sets it as the StateGroup's `prototypeArrayState`) |

Both are `prototypeArrayState`s, which VSG's `Intersector` clones and fills while it traverses a `StateGroup`; the proxy is therefore
computed only during an intersection (and again by `vsgUtil::computedHitVertices`, `components/vsgutil/intersectnormal.hpp`, which
says plainly that a proxy is overwritten by later draws).

## Which threads intersect

Every intersection in the tree:

- `MWRender::castRay` and `castCameraToViewportRay` (`apps/openmw/mwrender/intersect.hpp`), reached from `RenderManager::castRay` and
  `castCameraToViewportRay`, whose callers are all in `World` (`worldimp.cpp`: `getFocusObject`, `castRenderingRay`, the touch-target ray,
  the cursor rays). `World` runs on the main thread. Lua's `castRenderingRay` runs inside input handling on the main thread, and
  `asyncCastRenderingRay` only queues an action (`mwlua/nearbybindings.cpp`, `LuaManager::addAction`), which `applyDelayedActions` runs
  from `synchronizedUpdate`, called from the engine frame (`vsgengine.cpp`): main thread. The AI, mechanics and IK rays use the Jolt
  `castRay`, not the scene graph.
- `Preview` (`apps/openmw/mwrender/preview.cpp` line 287): the inventory doll's picking, main thread.
- Eden Studio picking (`apps/opencs/view/render/picking.hpp`): GUI thread.

No worker (cell preloader, terrain paging, `UpdateThreads`, groundcover, LOD workers) builds or runs an intersector; the terrain
preload thread only edits `Paging` views. `Skin` update workers write bone matrices, not the proxy, and the main thread does not
intersect while they run (`Scene::update` ends with `waitIdle`).

## Verdict

**Not a race today: disproved by the call graph.** Every call to `getOrCreateProxyVertices` is made from the main (or editor GUI)
thread, serially, so the "one proxy per batch" is single-threaded scratch. The 2026-11-08 crash was a different fault (a wrong
vertex count, fixed by `decodeChunkGeometry`) and does not point at a race.

It would become a race, and a silent heap or map corruption, if a future caller intersected the scene graph off the main
thread (an asynchronous ray, a worker-thread picking pass, a second window), because both the `std::map` of the parent and the
proxy contents would be shared. No code change was made: a mutex would only protect the map, not the proxy contents that
the caller fills and the hit reads afterwards, so the correct fix for a threaded intersection is a per-intersector (per-thread)
proxy, which needs the intersector API changed; it is a design change for the day a caller exists, not a fix for a present
bug, and a stress test of the "same call pattern" would only demonstrate misuse that nothing performs.

`arraystate.hpp` now states the contract in a comment. Not done: ThreadSanitizer (no race to demonstrate), any build.
