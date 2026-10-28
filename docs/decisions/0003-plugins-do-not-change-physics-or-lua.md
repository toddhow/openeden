# 0003 — Plugins do not change physics or extend Lua; systems that must, go into the core

Date: 2026-10-28 · Decided by: toddhow

## Decision
- **Physics is read-only for plugins.** A plugin may query the game's Jolt world: cast rays and shapes, read body states, and receive physics events such as hits. It may never create, destroy or move bodies, create joints or collision groups, register collision layers, drive motors, apply impulses or velocities, change motion types, or step the world.
- **No fixed-step phases for plugins.** `FixedStepPrePhysics` and `FixedStepPostPhysics` are removed from the SDK; plugins run only in the per-frame phases. Deterministic stepping is a core concern.
- **No Lua API for plugins.** A plugin cannot register Lua tables (`openeden.<id>`) or call into Lua. It may publish events on the event bus that the core forwards to Lua scripts as events, so mods can react to what a plugin does.
- **Anything that must change physics or extend Lua is merged into the core.** CreativeMotion already is (decision 0001). AdvancedBodies (body morphology, the body contract that rescales physics capsules) and ComplexClothing (garment fitting, cloth simulation in Jolt) are planned as core systems too. Reflectron (character creation and NPC styling) stays planned as a plugin.

## Why
- **The hardest parts of the plugin system are the ones that touch physics and Lua.** Sharing one Jolt world with plugins needs collision-layer bits that the 16-bit object layers cannot provide, fixed-step callbacks that clash with async physics threads, and Lua tables that must respect OpenMW's script contexts and sandboxing (`docs/design/working-plugin-system.md`, decisions 2–4). Keeping both systems in the core removes those problems instead of solving them.
- **The systems that need them are core systems anyway.** Animation, bodies and clothing all drive physics and are groundwork every host needs, the same reasoning as decision 0001.

## Consequences
- **SDK 2.0.** Removing API is a major version change (plugin doc 4.2): `openeden::sdk::kSdkVersion` goes from 1.0 to 2.0. The read-only physics service, the event bus and the other services stay; `ScriptingService` (`sdk/include/openeden/scripting.hpp`), the physics write operations, collision-layer registration, the fixed-step phases and the fixed-step `registerPhaseCallback` overload are removed. `components/plugin` loses `scripting_service`, `collision_layer_registry` and the write side of `physics_service`, with their tests.
- **The plugin system is for lighter extensions.** Compatibility packs (game rules, records, asset readers), UI, data, tools and character authoring (Reflectron) remain plugin territory; anything that changes how the world simulates or what Lua can call is core.
- **Mods keep Lua** through OpenMW's Lua API. Anything mods need from native code is added to the core's Lua API, never by a plugin.
- **The core grows** with AdvancedBodies and ComplexClothing, against the plugin doc's original aim of keeping build times flat by moving features out (plugin doc 1.2). Each still keeps its own engine-independent modules, as CreativeMotion does.
- **Track W shrinks.** Its decisions on collision-layer bits, fixed steps with async physics and Lua contexts are no longer needed; task 0019 trims the SDK, and tasks 0016–0018 drop the removed services.
- **Documents to update:** `docs/design/openeden-plugin-system.md` (sections 2, 4.3, 4.4, 4.10 and the phase list), `docs/design/working-plugin-system.md`, `docs/design/eden-studio-plan.md` (AdvancedBodies and ComplexClothing as core systems), and CreativeMotion's `docs/design/advancedbodies-handoff.md` (AdvancedBodies is now planned inside OpenEden's core).
