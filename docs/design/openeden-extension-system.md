# OpenEden Extension System: Core Services, Contracts, and Registration Plan

**Status:** Design Document & OpenEden Handoff  
**Author:** Todd & CreativeMotion Team  
**Date:** 2026-10-25  
**Target:** OpenEden Core Team  
**Companion Documents:**  
- [`docs/design/eden-studio-plan.md`](file:///i:/Projects/CreativeMotion/docs/design/eden-studio-plan.md)
- [`docs/design/advancedbodies-handoff.md`](file:///i:/Projects/CreativeMotion/docs/design/advancedbodies-handoff.md)
- [`docs/decisions/0004-total-fork-of-openmw.md`](file:///i:/Projects/CreativeMotion/docs/decisions/0004-total-fork-of-openmw.md)

This document describes the mechanism only: what an extension is, what the core must expose so extensions can work, and how extensions are found, registered, started and stopped. It deliberately does not design any individual extension (AdvancedBodies, ComplexClothing, Reflectron, compatibility packs); those are clients of this system and get their own documents.

> **Decision 0001 (toddhow, 2026-10-28):** CreativeMotion is compiled into the engine, not loaded as an extension, because it needs engine internals every frame and animation is groundwork every host needs. The exception covers CreativeMotion only; every other feature system stays an extension as described here. See `docs/decisions/0001-creativemotion-compiled-into-the-engine.md`. Mentions of CreativeMotion below describe its earlier plan as an extension, kept because the patterns they cite (its physics interface, event queue, debug overlay) still apply.

> **Decision 0003 (toddhow, 2026-10-28):** extensions read physics but never change it, have no fixed-step phases, and have no Lua API; they may publish events the core forwards to Lua. Systems that must change physics or extend Lua go into the core: CreativeMotion, AdvancedBodies and ComplexClothing are planned as core systems; Reflectron stays an extension. The SDK becomes 2.0. See `docs/decisions/0003-plugins-do-not-change-physics-or-lua.md`. Sections 4.3, 4.4 and 4.10 below are marked where the decision changes them.

---

## 1. Executive Summary & Vision

### 1.1 The Problem: A Monolithic Engine That Grows With Every Feature
OpenMW, the base of OpenEden, is one program: every subsystem is compiled into the same executable, and Morrowind's rules and data model are woven through it. That shape becomes a problem for OpenEden's goals:
- **Build times grow with every feature.** Each new system (animation, bodies, clothing, character creation, face) adds to one build that every contributor pays for, even when they only touch one system.
- **Features cannot be removed or swapped.** A system that is compiled in is always present; there is no way to ship without it, replace it, or let users choose.
- **Game rules are not separable.** Morrowind-specific behaviour lives inside the engine, so supporting other Bethesda-style games means editing the engine itself.
- **Systems reach into each other.** Without explicit boundaries, one system calls another's internals, and every change risks breaking a neighbour.

### 1.2 The Solution: A Groundwork Core and Runtime-Loaded Extensions
OpenEden splits into three kinds of parts:
- **The core** is the engine's groundwork only: physics on Jolt, rendering, platform and windowing, input, audio, the virtual file system (VFS) and resources, the world and entity model, the frame loop, the Lua host, saving and loading. It knows nothing about any feature or any particular game.
- **Extensions** are shared libraries the core loads at runtime. Every feature system is an extension, and so is every compatibility pack (the game-specific rules and data readers for one game, Morrowind first).
- **Contracts** are versioned interfaces, published as headers, through which extensions talk to each other. An extension never calls another extension's code directly; it asks the core's registry for a contract and calls that.

The core is built once. An extension builds against the core's published SDK on its own, so adding or changing a feature never rebuilds the engine or the other extensions (toddhow, 2026-10-22: extensions chosen mainly so build times do not grow as modules are added).

---

## 2. Concepts & Terminology

| Term | Meaning |
| --- | --- |
| **Core** | The engine groundwork (section 1.2). Loads extensions, owns the frame loop, the shared physics world, the world model, the VFS, settings and saves. Depends on no extension. |
| **Host** | A program that embeds the core: the game (`openeden`), the editor (Eden Studio), and a headless test host used by CI. All three load extensions the same way. |
| **Extension** | A shared library (`.dll` / `.so`) plus a manifest. Provides zero or more contracts, requires zero or more, and hooks into the core's lifecycle. |
| **Feature extension** | An extension that adds a system that does not change physics or extend Lua (decision 0003), for example character creation (Reflectron); animation, bodies and clothing are core systems. |
| **Compatibility pack** | An extension that teaches the core one game: it implements the **game contract** (records, rules, asset readers such as `.nif`/`.kf` or `.hkx`). One game per session. |
| **Contract** | A versioned C++ interface with a stable id (for example `openeden.body`), published as a header-only package. Providers implement it; consumers look it up. |
| **SDK** | The core's public headers and import library: the host API (section 4) plus the contract headers. The only thing an extension compiles against. |
| **Manifest** | `extension.json` beside the library: identity, version, the SDK version it was built for, contracts provided and required, data folders, settings defaults. |
| **Registry** | The core's table of contract providers, filled during registration and read during resolution. |

---

## 3. Architecture

```text
+----------------------------------------------------------------------------------------------------+
|                        HOSTS: openeden (game) | Eden Studio (editor) | test host                     |
+----------------------------------------------------------------------------------------------------+
                                                  |
+----------------------------------------------------------------------------------------------------+
|                                         OPENEDEN CORE                                              |
|  Extension Loader & Registry | Frame Loop & Phases | Physics (shared Jolt world) | World & Entities    |
|  VFS & Resources | Event Bus | Settings | Save Games | Lua Host | Rendering | Input | Audio | Logs   |
+----------------------------------------------------------------------------------------------------+
        ^ SDK (host API + contract headers): the only thing extensions compile against
        |
+------------------+   +------------------+   +------------------+   +------------------------------+
|  Feature extension  |   |  Feature extension  |   |  Feature extension  |   |  Compatibility pack          |
|  provides A      |   |  requires A      |   |  requires A, B   |   |  provides openeden.game      |
|                  |   |  provides B      |   |                  |   |  (one per game session)      |
+------------------+   +------------------+   +------------------+   +------------------------------+
        \______________________ talk only through contracts in the registry ______________________/
```

### 3.1 Dependency Rules
1. **The core depends on no extension.** It must start, run a world and shut down with zero extensions loaded (an empty scene); the test host proves this in CI.
2. **Extensions depend on the SDK only.** An extension includes core headers and contract headers, never another extension's headers, and never links another extension's library.
3. **Contracts carry no implementation.** A contract package is headers only: abstract interfaces, plain data types and constants. It may depend on the SDK's math and handle types, never on a third-party library's types (the rule CreativeMotion already follows for `physics/interface`).
4. **No cycles.** The graph of "requires a contract that another extension provides" must be acyclic; the loader refuses a cycle (section 6.3).
5. **Game rules live in the compatibility pack.** The core and feature extensions ask the game contract for anything game-specific (races, skeleton names, record types, asset formats); nothing in them names a game.

### 3.2 Precedent in OpenMW
OpenMW already reaches its major subsystems through abstract manager interfaces (`apps/openmw/mwbase/`) held by `MWBase::Environment`. The registry generalizes that pattern: the interfaces become versioned contracts, and the implementations move out of the executable into extensions. The exact files should be checked at the fork's commit before work starts.

---

## 4. What the Core Must Expose (the Host API)

Everything an extension may touch is listed here. If an extension needs something not on this list, the list grows (through a change to the SDK), rather than the extension reaching into core internals.

### 4.1 Identity, Version and ABI Handshake
- **SDK version:** `openeden::sdk::kVersion` (major, minor, patch). The core refuses an extension built for another major version, or for a newer minor version than it has.
- **Entry point:** each extension exports one C-linkage function the loader finds by name (section 6.2). Everything else is reached through what that function returns.
- **ABI strategy (phased):**
  - **Phase A, first-party:** the host API is plain C++ interfaces. Extensions must be built with the same compiler, standard library and build flags as the core; the manifest records the toolchain, and the loader refuses a mismatch. This is fast to build and keeps the code natural for the team.
  - **Phase B, third-party:** a stable C ABI layer (opaque handles and function tables) wraps the same services, so community extensions built with other compilers keep working across core updates. The C++ API stays as a thin header-only wrapper over it.

### 4.2 Service Registry
The heart of the system. During registration an extension offers implementations; during resolution it asks for the ones it needs.

```cpp
namespace openeden::sdk {

struct Version {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
};

// Filled by an extension during registration (section 6.4).
class Registrar {
public:
    // Offers `impl` as the provider of contract C. The extension owns impl and
    // keeps it alive until its stop(); the registry stores a non-owning pointer.
    template <typename C> void provide(C& impl);
    // Event types, phase callbacks, file formats, settings and editor panels are
    // registered here too (sections 4.3 to 4.10).
};

// Read by an extension during resolution.
class ServiceLocator {
public:
    // The provider of contract C with the same major version and at least
    // C::kVersion.minor, or nullptr. Valid from resolve() until stop().
    template <typename C> C* find() const;
    // As find(), but a missing required contract has already failed loading
    // (the manifest lists it), so this never returns nullptr.
    template <typename C> C& require() const;
};

} // namespace openeden::sdk
```

A contract declares its identity next to its interface:

```cpp
namespace openeden::contracts {

class BodyContract { // example shape only; the real contract has its own document
public:
    static constexpr std::string_view kId = "openeden.body";
    static constexpr sdk::Version kVersion{1, 0};
    virtual ~BodyContract() = default;
    // ... queries ...
};

} // namespace openeden::contracts
```

**Versioning rules:**
- A **minor** version only adds: new functions at the end of the interface, new optional fields with defaults. A consumer built for 1.0 works with a 1.3 provider.
- A **major** version may change anything; it is a different contract as far as the registry is concerned. A provider may offer several majors side by side during a migration (`BodyContractV1` and `BodyContractV2`).
- **One provider per contract major** in a session. Two extensions offering the same contract is a load error unless the contract is declared *multi-provider* (for example "file format readers"), in which case consumers receive the list in load order.
- Lookups by id happen once, at resolution. Per-frame calls go straight through the cached pointer: a virtual call, no string lookup, no allocation.

### 4.3 Lifecycle and Frame Phases
The core owns the loop and calls extensions at named phases, in a fixed order:

| Phase | When | Typical use |
| --- | --- | --- |
| `Input` | Once per frame, after input is polled | Read controls, set animation or behaviour parameters |
| `PreSimulation` | Once per frame, before fixed steps | Queue impulses, spawn bodies |
| ~~`FixedStepPrePhysics`~~ | Removed for extensions (decision 0003) | Core systems step physics themselves |
| ~~`FixedStepPostPhysics`~~ | Removed for extensions (decision 0003) | Core systems step physics themselves |
| `PostSimulation` | Once per frame, after all fixed steps | Reconcile capsules, emit gameplay events |
| `Animation` | Once per frame | Sample and blend poses, helper bones, face |
| `PreRender` | Once per frame | Push poses and morphs to render data, debug draw |
| `Ui` | Once per frame | Game UI or editor panels |

- **Order within a phase:** extensions in load order (section 6.3), then each extension's callbacks in registration order. The order is the same on every run, so fixed-step results are deterministic.
- **Threading:** callbacks run on the main simulation thread unless registered as `parallel`, which promises the callback touches only its own data and data passed in; the core may then run them on its job system once that exists.
- **Fixed step:** the core owns the step rate (default 60 Hz, configurable, with sub-steps) and passes `dt` and the step index; extensions never read a clock.

### 4.4 Physics: the Shared Jolt World

> **Decision 0003:** extensions get a read-only query service (ray and shape casts, body states, physics events). The write operations, joints, collision groups and collision-layer registration described below are core-only.
- One physics world per session, owned by the core. Extensions never create their own, and neither does CreativeMotion inside the engine (decision 0001): its physics interface runs over the core's world, never a second one.
- Exposed through a core physics service with the operations CreativeMotion's `physics/interface` already defines: create and destroy bodies and constraints, set motion type, drive joint motors, apply impulses, read states, ray and shape casts, and step listeners (the fixed-step phases above).
- **Collision layers:** extensions register named layers and the pairs that collide during registration; the core assigns the bit values, so two extensions cannot clash over layer numbers.
- Handles, not pointers: bodies and constraints are returned as IDs, looked up when used (the guide's rule for anything a backend can destroy).

### 4.5 World and Entities
- **Stable entity IDs** for actors and objects, valid across frames; never raw pointers to engine objects.
- **Lifecycle events:** entity spawned, despawned, cell loaded, cell unloaded, actor died, actor revived. Extensions create and free their per-entity state on these.
- **Per-entity extension data:** an extension may attach its own component (a fixed-size struct or a handle into its own pool) to an entity by ID; the core stores the slot and frees it on despawn, the extension owns what it points to.
- **Queries:** entities in a cell, near a point, by tag; the actor's skeleton, current pose buffer and capsule.

### 4.6 Event Bus
- **Typed events** declared in contract or SDK headers; extensions register the types they publish and subscribe to.
- **Two delivery modes:** immediate (synchronous, within the publisher's phase) and deferred (queued and delivered at the start of a named phase).
- **Fixed-size queues** sized at registration, so publishing allocates nothing on the hot path; overflow is counted and logged, never silent (the pattern of CreativeMotion's `FixedEventQueue` / `EventBridge`).
- **Deterministic order:** deferred events are delivered in publish order, and subscribers in load order.

### 4.7 VFS, Resources and File Formats
- Extensions read data through the core VFS, which overlays the base game data, the enabled mods in load order, and each extension's own `data/` folder.
- **File format registration:** an extension registers a reader for a format id or file extension (for example `opas.animgraph`, `.glb`, or `.nif` in a compatibility pack). Other extensions load assets through the core, which dispatches to the registered reader.
- Readers return located errors (file, line, field), never throw, matching the project's loader rules.

### 4.8 Settings
- Each extension gets its own settings namespace (`[extension.<id>]`), with defaults declared in its manifest.
- **Every behaviour change ships off by default**, so enabling an extension without changing settings leaves the game as it was (the rule M9 and M12 already set for CreativeMotion's in-game features).
- Settings are typed (bool, number with range, enum, string), so the launcher and Eden Studio can draw them without extension code.

### 4.9 Save Games

> **Decision 0005:** extensions do not affect save data; they are like script extender plugins. An extension cannot write to or read from a saved game. A feature that must live in a save belongs in the core. Extensions hear when a game is loaded or saved through events.

### 4.10 Scripting, Diagnostics and Editor Hooks
- ~~**Lua:** an extension registers Lua tables under its own name (`openeden.<id>`), so mods script it through the existing OpenMW Lua host.~~ Replaced by decision 0003: extensions have no Lua API; event types an extension marks are forwarded by the core to Lua scripts as events. Lua APIs for features come only from the core.
- **Logging and profiling:** a per-extension logger and named profiling scopes, shown per extension.
- **Memory:** extensions may ask the core for frame-scoped scratch memory; the per-step rule stays: no heap allocation in fixed-step or per-frame callbacks.
- **Debug draw:** extensions submit points, lines and labels as data (the `opas::debug::Overlay` pattern); the core's renderer draws them. Extensions never call the renderer directly.
- **Editor hooks:** today an extension registers a dockable workspace, an inspector or a viewport gizmo from its runtime code, in `start()`, through `host.getEditorService()` (`sdk/include/openeden/editor.hpp`; implementation `components/extension/editor_service.*`). They take effect only when the host is Eden Studio; the game and the headless host ignore them, and Eden Studio does not use them yet. The Eden Studio plan (section 4) has a separate editor library per extension that the game never loads, with its own `editor` manifest entry. That is not built and its format is not designed; decision 0007 removes these hooks when the editor API is built (Phase 3), and no code is removed yet.

---

## 5. Extension Packaging & Manifest

### 5.1 Folder Layout
```text
extensions/
  creativemotion/
    extension.json            manifest
    creativemotion.dll     library (creativemotion.so on Linux)
    data/                  the extension's own data, mounted into the VFS
    LICENSE                licence of this extension
```

### 5.2 Manifest Format (`extension.json`)
JSON, versioned, with a canonical writer that round-trips byte for byte, like the project's other authored formats:

```json
{
  "format": "openeden.extension",
  "version": 1,
  "id": "creativemotion",
  "name": "CreativeMotion",
  "extensionVersion": "1.4.0",
  "kind": "feature",
  "sdk": {"major": 1, "minor": 2},
  "toolchain": "msvc-19.44-x64-release",
  "library": {"windows": "creativemotion.dll", "linux": "libcreativemotion.so"},
  "provides": [
    {"contract": "openeden.animation", "version": [1, 1]}
  ],
  "requires": [
    {"contract": "openeden.game", "version": [1, 0]}
  ],
  "optional": [
    {"contract": "openeden.body", "version": [1, 0]}
  ],
  "loadAfter": [],
  "data": "data",
  "settings": [
    {"key": "enabled", "type": "bool", "default": false}
  ],
  "licence": "GPL-3.0-or-later"
}
```

| Field | Rule |
| --- | --- |
| `id` | Lower-case, unique among installed extensions; names its settings namespace |
| `kind` | `feature` or `compatibility` (a compatibility pack provides `openeden.game`) |
| `sdk` | The SDK version the extension was built against (section 4.1) |
| `toolchain` | Phase A only: must equal the core's, or the extension is refused |
| `provides` / `requires` | Contract id and `[major, minor]`; a required contract must be provided by some enabled extension |
| `optional` | Used if present; the extension must work without it (`find()` may return nullptr) |
| `loadAfter` | Ordering hints for extensions with no contract between them; never needed when a contract already orders them |
| `settings` | Typed defaults (section 4.8) |
| `licence` | Recorded for the GPL check (section 9) |

The manifest is read and validated before any library is loaded, so a broken or incompatible extension is rejected without running its code.

---

## 6. Registration & Loading Sequence

```text
 Discover --> Read manifests --> Resolve order --> Load libraries --> Handshake
                                                                          |
     Shutdown <-- Stop (reverse) <-- Run phases <-- Start <-- Resolve <-- Register
```

### 6.1 Discovery
1. The host lists extension folders (the install's `extensions/`, then user and mod-manager folders).
2. The user's enable list (in the OpenEden config, written by the launcher and Eden Studio) says which extensions are on. Installed but disabled extensions are listed, never loaded.

### 6.2 Load and Handshake
Each enabled extension's library is loaded and its one entry point called:

```cpp
// Exported by every extension with C linkage, so the loader finds it by name on
// every platform. Returns nullptr if the extension cannot run on this host.
extern "C" OPENEDEN_EXTENSION_EXPORT const openeden::sdk::ExtensionDescriptor*
openeden_extension_entry(const openeden::sdk::HostInfo* host);

namespace openeden::sdk {

struct ExtensionDescriptor {
    Version sdkVersion;               // must match the manifest
    std::string_view id;              // must match the manifest
    std::unique_ptr<Extension> (*create)(); // the loader owns the returned extension
};

// Implemented by each extension. Owned by the loader from create() until unload.
class Extension {
public:
    virtual ~Extension() = default;
    virtual void registerServices(Registrar& registrar) = 0;   // 6.4
    virtual bool resolve(const ServiceLocator& locator) = 0;   // 6.5; false = cannot run
    virtual void start(Host& host) = 0;                        // 6.6
    virtual void stop() = 0;                                   // 6.7
};

} // namespace openeden::sdk
```

The descriptor's id and SDK version must match the manifest; any mismatch unloads the library and reports why.

### 6.3 Dependency Resolution and Load Order
Done from manifests alone, before any extension code runs:
1. Build the graph: an edge from each extension to the provider of every contract it requires (and, when present, every optional contract), plus `loadAfter` edges.
2. Check every required contract has exactly one provider at a compatible version. A missing provider disables the requiring extension, and then anything that requires it, with a message naming the missing contract.
3. Refuse cycles, naming every extension in the cycle.
4. Topologically sort; ties are broken by extension id, so the order is the same on every machine. **This order is the load order** used for registration, phase callbacks, event delivery and the VFS overlay of extension data.

### 6.4 Register
In load order, the core calls `registerServices`. An extension offers its contract implementations, declares its event types and queue sizes, its phase callbacks, file format readers, settings and editor panels. Registration must not call other extensions: providers further down the order are not registered yet.

### 6.5 Resolve
In load order, the core calls `resolve`. The extension looks up the contracts it needs and caches the pointers. Returning false (for example, an optional feature it depends on behaves unexpectedly) disables the extension and everything that requires it; their registrations are withdrawn.

### 6.6 Start and Run
In load order, `start` is called with the host's services (world, physics, VFS, settings). From then on the core calls the registered phase callbacks every frame and fixed step.

### 6.7 Stop and Unload
In reverse load order, `stop` is called, so a provider outlives every consumer that cached its pointer. Then each extension object is destroyed and the libraries are unloaded, also in reverse order.

### 6.8 Failure Handling
- Every failure names the extension, the stage and the reason, in the log and in the launcher.
- An extension that fails is disabled, together with every extension that requires it; the rest of the session runs. The core never crashes on a bad manifest, a missing library or a failed handshake.
- An exception thrown out of an extension callback is caught at the boundary, logged, and disables that extension at the next safe point (end of frame). No exception crosses the core/extension boundary in either direction.

### 6.9 Hot Reload
Not in the first version. Eden Studio may later reload a single extension with no dependants (stop, unload, rebuild, load, register, resolve, start) for faster iteration; the game host never hot reloads.

---

## 7. Build System & SDK Distribution

### 7.1 Core Build
- The core builds as a shared library (`openeden_core`) plus thin host executables, and installs an SDK: public headers, the import library and a CMake package (`find_package(OpenEdenSDK)`).
- Symbols are hidden by default; only the SDK's API is exported (`OPENEDEN_API`), on Windows and Linux alike.

### 7.2 Extension Build
- An extension is its own CMake project that finds the SDK and calls `openeden_add_extension(<id> SOURCES ... MANIFEST extension.json)`, which builds the library, checks the manifest, stamps the toolchain id and lays out the extension folder.
- **This is where the build time is saved:** an extension rebuilds only itself, against the installed SDK; the core and other extensions are untouched.
- First-party extensions may still live in the main repository for convenience, built as separate targets that link only the SDK, so the boundary is enforced by the build, not by discipline.

### 7.3 CI
- Builds the core and the SDK, then each first-party extension against the installed SDK only (no access to core internals), on Windows (MSVC) and Linux.
- An include check, like CreativeMotion's Jolt and viewer include checks, fails if an extension includes a core-internal header or another extension's header.
- An SDK compatibility check compares the exported API with the last release and fails a minor-version bump that removes or changes anything.

---

## 8. Testing Strategy

| Test | What it proves |
| --- | --- |
| **Empty host** | The core starts, runs a world for N steps and shuts down with zero extensions |
| **Resolution unit tests** | Load order, missing providers, version ranges, cycles, optional contracts, duplicate providers, `loadAfter`, deterministic tie-breaks, all from manifests alone |
| **Lifecycle tests** | Register, resolve, start, stop and unload run in the documented order with test extensions that record their calls; failures disable dependants and nothing else |
| **Contract conformance suites** | One reusable suite per contract that any provider must pass, so a replacement extension is checked against the same behaviour as the original |
| **Determinism** | A scenario with extensions loaded, run twice, gives bit-identical results; the same with extensions enabled in a different discovery order |
| **Allocation** | Phase callbacks and event publishing allocate nothing per step (the project's allocation counter) |

The headless test host loads extensions exactly as the game does, without a window or renderer, so all of this runs in CI.

---

## 9. Licensing and Trust

- **Licence of extensions:** the core is GPL-3.0-or-later (OpenMW is GPLv3). The Free Software Foundation's view is that a main program and plug-ins that make function calls to each other and share data structures form a single combined program, so such plug-ins must be released under GPL-compatible terms ([GNU GPL FAQ, "If a program released under the GPL uses plug-ins, what are the requirements for the licenses of a plug-in?"](https://www.gnu.org/licenses/gpl-faq.html#GPLExtensions)). First-party extensions are GPL-3.0-or-later; the manifest's `licence` field records each extension's terms, and the launcher can warn about unknown ones. A legal review is needed before third-party extension terms are promised.
- **Trust:** a native extension runs with the same rights as the game. The system does not sandbox native code; it only refuses extensions that fail validation. Users should install extensions only from sources they trust, and the launcher should say so. Lua-only mods remain the sandboxed extension route.

---

## 10. Implementation Phasing & Handoff Roadmap

For the OpenEden session implementing the extension system, the recommended order is:

### Phase 1: Registry and Lifecycle, In Process
- Define the SDK skeleton: `Version`, `Registrar`, `ServiceLocator`, `Extension`, `ExtensionDescriptor`, the phase list.
- Implement the registry, dependency resolution and lifecycle with extensions linked statically into a test host, to prove the contracts and ordering before any dynamic loading.
- Resolution, lifecycle and empty-host tests (section 8).

### Phase 2: Dynamic Loading and Manifests
- Manifest format, validation and canonical writer (`openeden.extension` v1).
- Discovery, enable list, library loading and the handshake on Windows and Linux.
- `openeden_add_extension()` and the SDK install; CI builds a sample extension against the installed SDK only.

### Phase 3: Core Services Behind the Host API
- Frame phases and fixed-step callbacks wired into the engine loop.
- Physics service over the shared Jolt world, with collision layer registration.
- Entity IDs, lifecycle events and per-entity extension data.
- Event bus with fixed-size queues.
- VFS mounting of extension data and file format registration.
- Settings namespaces.

### Phase 4: Scripting, Diagnostics and Editor Hooks
- Lua table registration per extension.
- Per-extension logging, profiling and debug-draw submission.
- Editor hooks for Eden Studio (workspaces, inspectors, gizmos).

### Phase 5: First Real Clients
- ~~Move one feature in as an extension (CreativeMotion is the natural first, since it is already engine-independent with a physics interface) and fix whatever the host API lacks.~~ Done in task 0005, then superseded by decision 0001: CreativeMotion is compiled into the engine, and the Morrowind compatibility pack below is the first real extension. The next feature system to arrive (AdvancedBodies, ComplexClothing or Reflectron) is the first feature extension.
- Start the Morrowind compatibility pack: define the game contract, then move Morrowind-specific code out of the engine into it step by step, verifying Morrowind still plays the same.

### Phase 6: Stable C ABI and Community SDK
- The C ABI layer over the same services, the header-only C++ wrapper, and the SDK compatibility check in CI.
- SDK documentation, a template extension and sample contracts for community authors.

---

## 11. Open Questions for toddhow

1. **When to add the C ABI:** from the start (slower to build, safer for the future) or after the first-party extensions work (phase 6, as proposed)?
2. **Where contracts live:** in the core repository with the SDK (simplest), or in their own repository so a contract can change without a core release?
3. **Load order UI:** is the order always computed from contracts (as proposed), or may users override it, as mod managers do for content?
4. **Hot reload:** worth building for Eden Studio, and when?
5. **One game per session:** confirm that exactly one compatibility pack is active at a time, rather than packs layering over each other.
6. **Third-party licence policy:** require GPL-compatible extensions (and say so in the launcher), pending a legal review?

---

## 12. Extension System Guardrails
- **The core stays groundwork.** A feature or a game rule never moves into the core to make an extension's life easier; the host API grows instead.
- **No extension-to-extension coupling.** Only contracts, through the registry. No shared globals, no linking another extension's library, no including its headers.
- **Deterministic by construction.** Load order, phase order and event order are fixed and reproducible; extensions never read clocks or use unseeded randomness.
- **No allocation in hot paths.** Phase callbacks, contract calls made per step, and event publishing allocate nothing.
- **Off by default.** Enabling an extension changes nothing until its settings say so.
- **Fail closed, never crash.** A bad extension is disabled with a clear reason; the core and other extensions keep running.
- **No exceptions across the boundary.** Errors are values or log entries at the extension edge.
- **Original content only.** Extensions ship no proprietary game assets; compatibility packs read the user's own installed game data.
