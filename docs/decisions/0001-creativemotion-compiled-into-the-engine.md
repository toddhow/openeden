# 0001 — CreativeMotion is compiled into the engine, not loaded as a plugin

Date: 2026-10-28 · Decided by: toddhow

## Decision
CreativeMotion, OpenEden's animation and procedural motion system, is compiled into the engine instead of being loaded as a plugin. Its source is vendored in `components/creativemotion/` and linked into `openmw-lib`; engine-side adapters (the animation pose driver in `apps/openmw/mwrender/creativemotiondriver.*` and `ActorIkController`) call it directly. `plugins/creativemotion/` is removed.

The decision covers CreativeMotion only. The other feature systems (AdvancedBodies, ComplexClothing, Reflectron) and the compatibility packs stay planned as plugins under `docs/design/openeden-extension-system.md`.

## Why
toddhow's reasons:
- **It needs engine internals.** Animation and IK read and write bone nodes in the OpenSceneGraph scene graph, keyframe controllers and actor physics every frame (task 0007, "How OpenMW plays animations today"). Reaching those through SDK contracts would mean exposing the same engine internals through the host API anyway.
- **It is core to OpenEden.** Animation is groundwork every host needs, like physics, not an optional feature a user might leave out.

## Consequences
- **Source of truth stays in CreativeMotion's repository.** `components/creativemotion/` is a committed copy, chosen over a git submodule (toddhow, 2026-10-28, task 0007 open question 1), first taken from CreativeMotion commit `1fe5a27`. It is never patched in place: a change CreativeMotion needs becomes a CreativeMotion task spec, and the copy is refreshed from that repository with the source commit named in the refreshing commit (task 0007, "What CreativeMotion already offers").
- **CreativeMotion keeps its own boundaries.** Its `core` stays free of engine types (CreativeMotion decision 0004), so its sandbox, tests and replays still work outside OpenEden. Engine types appear only in the OpenEden-side adapters.
- **The plugin system stays, without CreativeMotion as its first client.** The Morrowind compatibility pack (`plugins/compat_morrowind/`) is now the first real plugin (plugin doc, phase 5).
- **The shared physics adapter is gone.** Removing the plugin removed `HostPhysicsWorld`, which ran CreativeMotion's `PhysicsWorld` over the core's `PhysicsService`. Ragdolls in game (CreativeMotion M10) need an in-engine adapter over the same single Jolt world; there must still never be a second Jolt world.
- **The `openeden.animation` contract has no provider.** `sdk/include/openeden/contracts/animation_contract.hpp` stays in the SDK for now; whether to keep it for plugins that want to drive animation, or remove it, is an open question.
- **The rules for engine code apply to the integration.** Every CreativeMotion behaviour in game ships off by default and is verified unchanged when off, as for any other engine change.
