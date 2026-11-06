# Eden Studio: Architecture, Editor Module API, and Integration Plan

**Status:** Living Design Document & OpenEden Handoff  
**Author:** Todd & CreativeMotion Team  
**Date:** 2026-10-25  
**Updated:** 2026-10-31  
**Target:** OpenEden Core Team & Tooling Workgroup  
**Companion Documents:**  
- [`docs/design/openeden-extension-system.md`](openeden-extension-system.md) (how extensions are found, registered and started; this document builds the editor side on it)
- [`docs/design/advancedbodies-handoff.md`](file:///i:/Projects/CreativeMotion/docs/design/advancedbodies-handoff.md)
- CreativeMotion's `docs/Open Procedural Animation System — Living Design Doc.md`: a local export that is not committed (the root `CLAUDE.md` says exported copies of the living design docs are never committed), so there is no link
- [`docs/decisions/0004-total-fork-of-openmw.md`](file:///i:/Projects/CreativeMotion/docs/decisions/0004-total-fork-of-openmw.md)

> **Decision 0003 (toddhow, 2026-10-28):** AdvancedBodies and ComplexClothing are planned as core systems, like CreativeMotion (decision 0001), because they change physics; their editor modules are registered by the core, not by extensions (decision 0007 says how: separate editor libraries that Eden Studio loads from a fixed list of core systems, not from extension manifests). Reflectron stays an extension. See `docs/decisions/0003-plugins-do-not-change-physics-or-lua.md`.

> **Decision 0002 (toddhow, 2026-10-28):** the renderer moves to Vulkan on VulkanSceneGraph, with no OpenGL fallback and no abstraction layer. The editor's viewport follows it (Track V stage V6, task 0014); section 8, Phase 1 says where that stands. See `docs/decisions/0002-vulkan-renderer-on-vulkanscenegraph.md`.

> **Decision 0005 (toddhow, 2026-10-31):** extensions do not affect saved games, so an editor module has no saved-game data to edit either. Everything the tools author is Studio project data: project files, layouts and the assets that editor modules write (sections 3.6 and 3.8). The decision does not limit those writers. See `docs/decisions/0005-plugins-do-not-affect-saves.md`.

> **Decision 0006 (toddhow, 2026-10-31):** the engine's plugins are called extensions ("plugin" now means an ESP or ESM file), and what this plan first called editor extensions are editor modules. See `docs/decisions/0006-engine-plugins-are-extensions.md`.

> **Decision 0007 (toddhow, 2026-10-31):** Eden Studio's editor design. Core systems' tools are editor libraries that Eden Studio loads from a fixed list of core systems; the plan's separate editor library stays and the SDK's runtime-side hooks go in Phase 3; Studio keeps the V6 `SceneHost` for its viewport and adds the Jolt world and the extension host beside it. See `docs/decisions/0007-eden-studio-editor-design.md` and section 10.

> **Update 2026-11-01.** Built since: the editor's Jolt world with Play, Pause and Step and a debug draw pass (task 0030; it holds test bodies only, the cell's objects are not in it yet); the Editor Module API 1.0 as a first subset (task 0031: panels, commands, gizmos as overlay data, asset types, inspector sections, timeline tracks, undoable commands; no settings pages, background tasks, event bus, picking or labels), the module loader with failure isolation, the `editor` entry in `extension.json`, the core list and a sample module; and CreativeMotion's first module, `creativemotion_editor` (task 0032), with the Rig & Motor Limits Inspector of section 5.1. The old runtime-side hooks are still in the SDK (task 0031 stage C is waiting for toddhow). Phases 3 and 4 below are therefore partly done.

> **Update 2026-11-06 (status refresh asked for by toddhow; no design changed).** Eden Studio is no longer "almost nothing": the shell, the viewport, the Jolt world, the extension host with its Extension Manager, the Editor Module API (now **1.1**) and two core editor modules exist, on the `vulkan` branch. Per phase of section 8:
> - **Phase 1 (shell and viewport):** built. `eden-studio` (the renamed `openmw-cs`, `eden-studio.cfg`, `eden-studio-tests`) has the Eden Studio layout (Asset Browser, viewport, Context Inspector, workspace tabs, Output Log) beside the Classic one, renders cells through `CSVRender::SceneHost` on VulkanSceneGraph, runs a Jolt world with Play, Pause and Step and a debug draw pass (it holds test bodies and the CreativeMotion ragdoll adapter, not the cell's objects), and hosts runtime extensions with an Extension Manager (task 0024, stages A to E; task 0030). Its command line is fixed (it ignored `--config` and `--user-data` since 2013), and `OPENEDEN_QUIET_WINDOW=1` opens every window off-screen for unattended checks. Not done: toddhow's hands-on list (task 0014, "Check list for toddhow"), and Morrowind-specific data has not moved into the compatibility pack.
> - **Phase 2 (built-in core tools):** partly. The VFS Asset Browser and the single undo stack exist; the inherited OpenMW-CS world editing (cells, terrain, placement) works but is untouched; navmesh generation, the Studio event bus and scrubbing are not built. A read-only **Mesh view** opens `.nif` and `.dae` models with overlays, node tree, textures and animation (task 0043), and a LOD tile viewer (`--open-lod-pack`) exists. NIF editing is a draft (task 0046).
> - **Phase 3 (Editor Module API):** first subsets built. API **1.0** (task 0031): panels, commands, gizmos as overlay data, asset types, inspector sections, timeline tracks, undoable commands, the module loader with failure isolation, the core list, a sample module, and the removal of the old `EditorService` hooks (SDK 3.0). API **1.1** (2026-11-05, toddhow): filled triangles (`OverlaySink::triangle`), `EditorContext::userDataPath()`, a module undo stack of its own (`CSMDoc::UndoRouting`: a module edit no longer marks the ESM document modified) and per-module panels and Asset Browser filtering. **Gaps still open** (sections 3.2, 3.3, 3.5, 3.7, 3.10, 3.11; from tasks 0032 and 0069 stage f): picking and dragging of overlay data, labels, bone and entity selection, dialogs and canvases, the Studio event bus, settings pages and background tasks.
> - **Phase 4 (first real module):** the CreativeMotion rig and motor limits inspector exists (`creativemotion_editor`, task 0032). The push sandbox (draft task 0065) waits on in-engine ragdoll work; the animation graph, arbiter and face tools are not started.
> - **Phase 5 (further modules):** the AdvancedBodies body package panel exists (`advancedbodies_editor`, task 0069 stage f: opens a body package, a `.edenpak` or the merged body cache, lists the validator's findings, draws regions filled and weld groups). ComplexClothing, Reflectron, the face timeline and the compatibility pack's module are not started.
> - **Phase 6 (hardening and community SDK):** not started.
>
> **Recommended next step (for toddhow's approval, task 0091 draft):** close the Phase 3 gaps that two modules already hit, as Editor Module API 1.2, before any more modules are written against 1.1. Order: bone and entity selection with picking of overlay items (needs it: rig joint limits, push sandbox), labels, then the event bus, settings pages and background tasks. Then the push sandbox (task 0065), then Phase 2's cell objects in the Jolt world. Every API addition keeps older modules loading (new virtual functions at the end with defaults, as 1.1 did).

> **What exists today (2026-10-31, before the update above).** Almost none of this plan is built. The SDK has an `EditorService` with three abstract hooks (workspace, inspector and gizmo) that an extension registers from its runtime code; it is inactive in the game and the headless host, and nothing in Eden Studio uses it yet (`sdk/include/openeden/editor.hpp`). These hooks (`EditorService`, `EditorWorkspace`, `EditorInspector`, `EditorGizmo`) were removed in SDK 3.0 (task 0031 stage C, approved by toddhow 2026-11-01), now that the Editor Module API exists. Everything else in section 3 (panels, commands, asset types, timeline tracks and so on) is plan, and the loading of separate editor libraries in section 4 is plan too. Section 8, Phase 3 is unbuilt. Section 2.1 says where the editor application stands. The three questions this plan used to carry are decided (decision 0007, section 10).


---

## 1. Executive Summary & Vision

### 1.1 The Problem: Legacy Bethesda Tool Fragmentation
For two decades, developing or modding for Bethesda-style modular RPGs has suffered from severe toolchain fragmentation. Creators and developers must juggle a bewildering array of disparate, unintegrated utilities:
- **World & Scene:** Creation Kit / OpenMW-CS (dialogue, cell layout, records).
- **Meshes & Collision:** NifSkope, Blender with custom plugins, havok/hkx export scripts.
- **Animation & Graphs:** Nemesis, FNIS, Pandora, or manual Havok behavior XML editing.
- **Body & Armor Customization:** External BodySlide & Outfit Studio applications requiring batch-building thousands of pre-baked static meshes to disk.
- **Facial Lip-Sync:** Legacy FonixData / FaceFX or manual phoneme generators.

This workflow is brittle, steep for creators, prone to mod conflicts, and incapable of live, holistic interactive feedback.

### 1.2 The Solution: Eden Studio (Editor of Eden)
**Eden Studio** elevates OpenEden from an engine fork into a **unified game development studio** (comparable to the Unreal Engine Editor or Godot). Instead of separate external tools, OpenEden refactors the `openmw-cs` foundation into an extensible, modern **Studio Shell**: one application with a shared 3D viewport, one Jolt physics environment, the virtual file system (VFS), an undo/redo transaction stack and a central event bus.

### 1.3 The Key Principle: The Editor Mirrors the Engine
OpenEden's engine core is groundwork only. Feature systems are either core systems (CreativeMotion, and the planned AdvancedBodies and ComplexClothing; decisions 0001 and 0003) or extensions (Reflectron, the compatibility packs; see the extension system document). **Eden Studio follows the same split:**
- **Eden Studio itself contains only the tools for what the engine core owns:** the shell, the viewport, the asset browser, world and scene editing, project and extension management. It has no animation editor, no body sliders, no clothing tools and no face timeline of its own.
- **Extension tools come from the extensions.** Each extension that wants tooling ships an **editor module** alongside its runtime. When Eden Studio loads the extension, it loads the editor module too, and the editor module tells Eden Studio which windows, panels, inspectors, gizmos, menus and asset editors it needs through the **Editor Module API** (section 3).
- **Game-specific editors come from the compatibility pack.** Record types, dialogue and quest data belong to one game, so the game's compatibility pack supplies their editors through the same API.
- **Tools for core systems are editor modules too (decision 0007).** CreativeMotion is compiled into the engine, and AdvancedBodies and ComplexClothing are planned as core systems, so none has an extension to ship an editor module with. Each keeps its tools in a separate editor library, and Eden Studio loads those from a fixed list of core systems, not from extension manifests. The game never loads them. Below, an extension's editor module and a core system's differ only in how Studio finds them.


Why: extension features are not baked into the engine, so they must not be baked into the editor either. A feature that is missing from a session (an extension not installed or not enabled) must leave no dead menus or broken panels behind; a new extension must be able to bring its own tooling without anyone changing or rebuilding Eden Studio; and an extension's tools must move with it, versioned and shipped by the team that owns the feature.

---

## 2. Core Shell Architecture

Eden Studio is an extensible workbench host: a small, fixed shell plus whatever editor modules the enabled extensions provide.

```text
+----------------------------------------------------------------------------------------------------+
|                                           EDEN STUDIO                                              |
+----------------------------------------------------------------------------------------------------+
| Menu Bar: File | Edit | View | Simulation | Extensions | Window | Help  (+ menus added by modules)  |
+----------------------+------------------------------------------------------+----------------------+
| Asset Browser (VFS)  | Central 3D Viewport (Shared Renderer + Jolt World)   | Context Inspector    |
|                      |                                                      |                      |
| (asset types are     |   Built-in: camera, selection, transform gizmos,     | Built-in: entity     |
|  listed by whichever |   physics debug draw                                 | transform, name,     |
|  extension added     |   Module gizmos and overlays drawn on top            | tags                 |
|  them)               |                                                      | + sections added by  |
|                      |                                                      |   editor modules     |
+----------------------+------------------------------------------------------+----------------------+
| Dockable Workspaces: [World & Scene (built-in)] + one tab per loaded editor module's workspace          |
|   e.g. [CreativeMotion] [Reflectron & Bodies] [ComplexClothing] [Game Records (compatibility pack)] |
+----------------------------------------------------------------------------------------------------+
| Timeline & Transport [> Play] [|| Pause] [[] Step] (+ tracks added by editor modules) | Output Log     |
+----------------------------------------------------------------------------------------------------+
```

### 2.1 What Eden Studio Provides Itself (Built In)
These exist with zero extensions loaded, because they mirror the engine core:
1. **Shell and Docking:** the main window, dockable workspace manager (Qt docking; see the note below) and per-workflow layout persistence.
2. **Unified 3D Viewport:** the engine's renderer, shared camera controls (orbit, fly, focus on selection), selection, transform gizmos, and the Jolt physics debug overlay (colliders, contacts, joint frames). Simulation time dilation (0.1x to 2x).
3. **Project & Asset VFS Browser:** base game data, mod folders and working folders as one overlay. The browser itself knows no feature formats; it lists and opens assets through the asset types editor modules register (section 3.6).
4. **World & Scene Workspace:** the engine-level parts of classic OpenMW-CS: cell and exterior editing, terrain sculpting, object placement and multi-object transforms, lighting and atmospherics, navmesh generation with Recast/Detour.
5. **Transaction System:** one undo/redo stack for the whole editor, with transaction merging and dirty-state tracking; editor modules push their own commands onto it.
6. **Simulation Control:** play, pause and single step of the shared world. Scrubbing backwards from recorded snapshots is planned; nothing records snapshots today (section 6.1).
7. **Studio Event Bus:** editor events (selection changed, asset opened, simulation stepped, parameter changed) that the built-in tools and editor modules publish and subscribe to.
8. **Extension Manager:** lists installed extensions and their editor modules, enables and disables them, shows load errors (section 4).

**Where the editor stands today.** These facts are true on the `vulkan` branch (at `ca9f6283d8`), not in the tree this document is committed in:
- The editor (`apps/opencs`) renders through VulkanSceneGraph in a Qt viewport (vsgQt v0.5.0, Qt 6.6.3, `docs/dependencies.md`): Track V stage V6, task 0014, stages C1 to C9, pending toddhow's hands-on checks (task 0014, "Check list for toddhow").
- Its model layer is the `csmodel` library (`apps/opencs/CMakeLists.txt`), which `openmw-cs-tests` links without any view code (`apps/opencs_tests/CMakeLists.txt`).
- Docking is Qt: `SubView` derives from `QDockWidget` (`apps/opencs/view/doc/subview.hpp`). OpenEden's own code has no ImGui, so "Qt6 docking or ImGui" is settled as Qt.
- The viewport is `CSVRender::SceneHost` (`apps/opencs/view/render/scenehost.hpp`), the editor's own render-only counterpart of the game's `MWRender::RenderManager`, which is bound to the game's world types. (Update 2026-11-06: the editor now has a Jolt world with Play, Pause and Step and an extension host beside it; see the update at the top.)
- So "Eden Studio runs the real engine core" (section 6.1) is a goal, not today's state.
- **Mesh viewer (task 0043, 2026-11-01):** the viewport also opens a model from the virtual file system (`.nif`, `.dae`) in a read-only Mesh view, with view modes, overlays (collision, bounds, skeleton, node axes, grid), a node tree, properties, textures and animation. It belongs to Eden Studio itself, like the cell viewport: it shows what the engine core loads and knows no game records. Editing NIFs is a later task.


### 2.2 What Eden Studio Does Not Provide
Eden Studio contains **no feature-specific code**. Specifically, none of the following are part of Eden Studio:
- Animation graph editing, rig and motor inspection, push testing, arbiter and reconciliation views (CreativeMotion is a core system, decision 0001; its tools are an editor module on the core list, decision 0007).
- Body morph sliders, population generation, character profiles (AdvancedBodies is planned as a core system, decision 0003, with an editor module on the core list, decision 0007; Reflectron is an extension and supplies its own).
- Garment fitting, layering and cloth constraint authoring (ComplexClothing is planned as a core system, decision 0003, with an editor module on the core list, decision 0007).
- Facial expression and speech timelines (supplied by whichever system owns the face layer; section 5.1 expects CreativeMotion to).
- Game-specific record editors, dialogue and quest graphs (supplied by the active compatibility pack).

If one of these tools needs something Eden Studio cannot yet do, the Editor Module API grows to offer it; the tool itself never moves into Eden Studio.

---

## 3. The Editor Module API

The Editor Module API is the set of services Eden Studio exposes so that an editor module can describe its tools and run them. It is to the editor what the extension system's host API is to the engine: the only thing an editor module compiles against, versioned, and shared by all editor modules.

### 3.1 How a Module Is Shaped
```cpp
namespace openeden::editor {

// Implemented by each editor module. Owned by Eden Studio from creation
// until the editor module is unloaded.
class EditorModule {
public:
    virtual ~EditorModule() = default;
    // Describe every window, panel, menu, gizmo and asset type to the editor.
    // Called once, after the editor module's runtime extension has started.
    virtual void registerUi(EditorRegistrar& registrar) = 0;
    // Called before the editor module is unloaded, after its windows are closed.
    virtual void shutdown() = 0;
};

} // namespace openeden::editor
```

The editor module holds no global state the editor does not know about: every window it creates, every command it adds and every asset type it handles is registered through `EditorRegistrar`, so Eden Studio can show, hide, restore and remove them cleanly.

### 3.2 Workspaces, Panels and Windows
- **Workspaces:** a named tab grouping panels for one workflow (for example "CreativeMotion"). Editor modules declare a workspace with a default layout; users rearrange it and the layout is saved per user.
- **Panels:** dockable windows inside a workspace or floating. An editor module registers a panel with an id, a title, a default dock position and a draw callback; Eden Studio owns the window, the editor module owns the panel's contents.
- **Modal dialogs and wizards:** for create-new and import flows.
- **Lifecycle callbacks:** opened, closed, focused, and "selection changed" for panels that follow the selection.

### 3.3 UI Toolkit
One toolkit for every editor module, so all tools look and behave alike:
- Standard widgets: buttons, sliders with units, numeric fields, colour pickers, lists, trees, tables, tabs, search fields.
- **Property editing:** an editor module describes an object's fields (name, type, unit, range, tooltip) and Eden Studio draws the editor for it, including multi-selection and undo.
- **Specialised canvases:** a node-graph canvas (for state machines, quest graphs), a curve editor (for keys and blend curves), a timeline with tracks, and an embedded secondary viewport for previews.
- Editor modules never create native windows or link a UI library of their own; the toolkit is the only route to the screen.

### 3.4 Commands, Menus, Toolbars and Shortcuts
- An editor module registers **commands** (id, label, icon, handler, enabled-when condition) and places them in menus, toolbars and context menus (on assets, entities or viewport selections).
- Default keyboard shortcuts are declared with the command; conflicts are reported in the extension manager and resolved by the user.

### 3.5 Viewport Modules
- **Gizmos:** interactive handles an editor module attaches to a selected entity or bone (for example joint limit cones, capsule bounds, or arrows that show an impulse a core system applied), with drag callbacks that push undoable commands.
- **Overlays:** points, lines, labels and shapes submitted as data each frame and drawn by the viewport (the same overlay-as-data pattern the engine uses for debug draw).
- **Picking:** an editor module may register pickable items, so clicking a gizmo or overlay selects what it represents.
- **Preview scenes:** an editor module may open a secondary viewport with its own isolated world (for example a single character on a floor), sharing the renderer.

### 3.6 Asset Types
- An editor module registers the asset types it owns: format id and file extension, icon, how to create a new one, which panel opens it, and optionally a thumbnail generator and a preview.
- The asset browser shows, filters and opens assets through these registrations. An asset whose type no loaded editor module handles is listed as a plain file and opened read-only as text.
- Saving goes through the owning editor module's writer, so files stay in their canonical, round-trip form.

### 3.7 Inspector Sections
- When an entity, bone or asset is selected, Eden Studio builds the context inspector from the built-in section (transform, name, tags) plus every section an editor module registered for that kind of selection.
- Each section is a property description (section 3.3) or a custom draw callback.

### 3.8 Undo, Transactions and Dirty State
- Every change an editor module makes to project data is a **command** pushed onto Eden Studio's single undo stack (do, undo, merge-with-previous).
- Editor modules mark the assets they changed as dirty; Eden Studio handles save prompts and save-all.

### 3.9 Simulation and Timeline Access
- Editor modules read the shared world's state, physics included, through the same read-only queries extensions have (decision 0003). They do not change physics: only core systems' tools may, and only through the core (CreativeMotion's push sandbox qualifies because CreativeMotion is core). Play, pause and step remain Eden Studio's controls, so every tool sees the same simulation.
- Editor modules may add **timeline tracks** (keys, events, audio waveforms, state bands) and react to scrubbing.

### 3.10 Editor Event Bus
- Editor modules subscribe to editor events (selection changed, asset opened or saved, simulation stepped, project loaded) and publish their own typed events for other editor modules.
- Editor modules never call each other directly; cross-feature tooling (for example a body panel that shows physics capsules) goes through runtime contracts and editor events, exactly as at runtime.

### 3.11 Settings, Diagnostics and Background Work
- **Settings pages:** an editor module registers preference pages drawn from typed settings, as in the extension system.
- **Log and notifications:** a log channel for each editor module in the output panel, and non-blocking notifications.
- **Background tasks:** long operations (baking, batch export, population generation) run as cancellable tasks with progress shown by Eden Studio, never on the UI thread.

---

## 4. Packaging, Loading and Lifecycle of Editor Modules

*Planned, not built. See the status note at the top. The separate editor library is the design (decision 0007).*

### 4.1 One Extension, Two Libraries
An extension that provides tooling would ship its editor module as a **separate library** beside its runtime library. For illustration, with the Morrowind compatibility pack (`extensions/compat_morrowind/`, whose real manifest names a runtime library only):

```text
extensions/
  compat_morrowind/
    extension.json                  manifest
    compat_morrowind.dll            runtime: loaded by the game, Eden Studio and the test host
    compat_morrowind.editor.dll     editor module: loaded only by Eden Studio (hypothetical; not built)
```

- The **game never loads editor code**, so it carries no UI toolkit, no editor dependencies and no editor-only memory.
- The editor library links the runtime library's public contracts and the Editor Module API; it never reaches into the runtime library's internals.
- An extension with no tooling simply has no editor library; Eden Studio still loads its runtime so the world behaves as in the game.

**What exists and what is planned.** The extension manifest has no `editor` entry today (`components/extension/manifest.hpp` and `manifest_io.cpp` define none), and nothing loads an editor library. What exists is the SDK's `EditorService` (`sdk/include/openeden/editor.hpp`): an extension registers a workspace, an inspector or a gizmo from its runtime code, in `start()`, through `host.getEditorService()`. It is inactive in the game and the headless host. The extension system document says the same in section 4.10. The separate editor library, a manifest entry that names it and the Editor API version it was built for, and the loading in section 4.2 are planned; their format is not designed. Decision 0007 kept this design and the hooks above were removed in SDK 3.0 (task 0031 stage C); the `editor` manifest entry now exists. Editor modules of core systems have no manifest: Eden Studio loads them from a fixed list of core systems.


### 4.2 Loading Sequence in Eden Studio
1. Eden Studio starts the engine core and loads the enabled extensions exactly as the game does (discover, resolve, register, start). The editor has no extension host yet; Phase 1 adds one beside the `SceneHost` (decision 0007).
2. For each started extension with an `editor` entry, in the same load order, Eden Studio loads the editor library, checks its Editor API version, creates the editor module and calls `registerUi`. Editor modules of core systems are loaded the same way, from a fixed list of core systems (decision 0007).
3. Eden Studio builds menus, workspaces, the asset browser and the inspector from the built-in tools plus everything registered.
4. On shutdown, or when an extension is disabled from the extension manager, its windows are closed, `shutdown` is called, its registrations are withdrawn and the library is unloaded, in reverse load order.

### 4.3 Failure Handling
- An editor module that fails to load or register is disabled on its own; its runtime extension keeps running, and the rest of Eden Studio is unaffected. The extension manager shows the reason.
- If a runtime extension is disabled (missing contract, failed resolve), its editor module is not loaded.
- An exception from an editor module callback is caught at the boundary, logged, and the editor module's windows are closed.

### 4.4 Versioning
- The Editor Module API has its own major and minor version, separate from the engine SDK's, so the editor can evolve without forcing engine extension rebuilds and vice versa.
- Minor versions only add; a major version change requires editor modules to be rebuilt.

---

## 5. Example: Workspaces the Planned Systems Will Provide

The workspaces below are **not part of Eden Studio**. They are what the planned systems are expected to provide through the Editor Module API (CreativeMotion, AdvancedBodies and ComplexClothing are core systems, whose editor modules Eden Studio loads from a fixed core list, decision 0007), listed here to show what the API must be able to express. Each extension's own design documents own the detail.

```
+----------------------------------------------------------------------------------------------------+
|                          EDEN STUDIO SHELL (built-in tools + Editor Module API)                    |
+----------------------------------------------------------------------------------------------------+
                                                  |  loads editor modules shipped by extensions
         +----------------------------------------+----------------------------------------+
         |                                        |                                        |
+------------------+                    +--------------------+                   +--------------------+
|  CreativeMotion  |                    |   AdvancedBodies   |                   |  ComplexClothing   |
|  editor module   |                    |   editor module    |                   |  editor module     |
| Anim graph, rig, |                    | Morphs, surface    |                   | Fit preview,       |
| push sandbox,    |                    | contract views     |                   | layers, cloth      |
| arbiter, face    |                    |                    |                   | constraints        |
+------------------+                    +--------------------+                   +--------------------+
                                                  |
                                        +--------------------+          +--------------------------+
                                        |     Reflectron     |          |  Compatibility pack      |
                                        |  editor module     |          |  editor module           |
                                        | NPC studio, pop.   |          | Game records, dialogue,  |
                                        | generator, presets |          | quests                   |
                                        +--------------------+          +--------------------------+
```

*CreativeMotion, AdvancedBodies and ComplexClothing are core systems (decisions 0001 and 0003), not extensions; the boxes show what each tool set needs from the API. Their editor modules are loaded from a fixed core list (decision 0007).*

### 5.1 CreativeMotion Module (Animation, Physics & Face)
Expected to register:
1. **Visual Animation Graph Editor** (`.animgraph.json` asset type, node-graph canvas): states, blends, transitions with live condition debugging.
2. **Rig & Motor Limits Inspector** (`.rig.json`, `.profile.json` asset types, inspector sections, viewport gizmos): collision shapes, joint limit cones and pyramids, motor stiffness ($K_p$ in $\text{N}\cdot\text{m/rad}$), damping ($K_d$ in $\text{N}\cdot\text{m}\cdot\text{s/rad}$) and maximum torque, helper bone multipliers.
3. **Push Sandbox Panel** (commands, impulse gizmo, preview scene): ask the core to apply an impulse at a bone and watch balance, stepping, stumbling, knockdown and get-up in the shared simulation. Changing physics is allowed here only because CreativeMotion is a core system (decisions 0001 and 0003); an extension's editor module could not.
4. **Reconciliation & Arbiter Inspector** (overlays, timeline tracks): gameplay capsule against simulated pelvis, leash bounds, per-body-part arbiter states, snapshot playback.
5. **Facial Expression & Speech Timeline** (timeline tracks, curve editor, audio waveform): phoneme and FACS curves, gaze targets, blinks, emotion blends.

### 5.2 AdvancedBodies & Reflectron Modules (Body & NPC Studio)
Expected to register:
1. **Morphology Sliders** (panel, property editing, live viewport preview): anatomical and facial sliders evaluated as vertex morphs.
2. **Population Generator** (panel, background task): demographic archetype rules per faction or region, for example:
   ```json
   {
     "archetype": "NordBlacksmith",
     "muscularity": { "min": 0.70, "max": 0.95 },
     "bodyFat": { "min": 0.25, "max": 0.50 },
     "height": { "min": 1.05, "max": 1.14 },
     "styleProfile": "rugged_laborer"
   }
   ```
3. **Character Profiles** (`.character.json` asset type): appearance, morphology and style references.
4. **Body Contract Views** (overlays): how capsules and sensors rescale with the body, read through the runtime body contract.

Reflectron's in-game character creation is a runtime feature of the Reflectron extension, not an editor module; its editor module adds the NPC studio on top.

### 5.3 ComplexClothing Module (Garments & Draping)
Expected to register:
1. **Auto-Fit Preview** (preview scene): garments across the range of body shapes.
2. **Layer Stacking & Clipping View** (panel, overlays): layer order and culled regions.
3. **Cloth Constraint Authoring** (`.garment.json` asset type, gizmos): pins, stiffness maps, drag.

### 5.4 Compatibility Pack Module (Game Records)
The active compatibility pack is expected to register the editors for its game's data: record types, dialogue and quest graphs (node-graph canvas), and its game's asset types. With no compatibility pack loaded, Eden Studio still edits cells, terrain, objects, lighting and navmesh through its built-in World & Scene workspace.

---

## 6. Technical Integration & Contracts

### 6.1 Shared Engine, Shared World
- **Goal:** what a tool shows is what the game does. Decision 0007 gets there through the Jolt world and the extension host, not through the game's renderer: Eden Studio keeps the editor's own render-only `CSVRender::SceneHost` for its viewport, and Phase 1 adds the Jolt world and the extension host beside it, read-only at first. Today (on the `vulkan` branch at `ca9f6283d8`) the editor has the `SceneHost` and neither of those. Whether Studio later converges with the game's renderer is decided later.
- There is one Jolt physics world, owned by the core. Extensions do not create their own and never change it: they, and so their editor modules, only read it through the physics query service (`sdk/include/openeden/physics.hpp`, decision 0003). Only core systems' tools may change physics, and only through the core.
- **Stepping** (at `0703c40e59`): the game's physics step defaults to 1/60 s (`mPhysicsDt` in `apps/openmw/mwphysics/physicssystem.cpp`; the `OPENMW_PHYSICS_FPS` environment variable overrides it). `PhysicsTaskScheduler::calculateStepConfig` (`apps/openmw/mwphysics/mtphysics.cpp`) caps the fixed steps per frame (at most 10) and falls back to a variable step when fixed steps would fall behind, so the step is fixed only while the game keeps up. Stepping belongs to the core (decision 0003). Pausing and single steps in Eden Studio are planned; so is scrubbing backwards from recorded snapshots, and nothing records snapshots today.


### 6.2 Runtime Code Stays Allocation-Free
- Simulation code executed during a step keeps its zero-heap-allocation guarantee inside Eden Studio, exactly as in the game.
- Editor modules may allocate freely in UI code, but must not make runtime hot paths allocate: inspection uses separate diagnostic queries or snapshots.

### 6.3 Runtime and Editor Event Buses
- **Runtime events** (between the engine and extensions, for example an extension's state and hit events) use the engine's fixed-size event queues, described by each extension's contracts.
- **Editor events** (section 3.10) are a separate bus inside Eden Studio. Editor modules may listen to runtime events to display them, but runtime code never depends on editor events.

---

## 7. File Formats & Schemas Reference

Eden Studio itself owns only its own files:

| File extension | Format Identifier | Owner | Purpose |
| --- | --- | --- | --- |
| `.edenproject.json` | `openeden.project` (v1) | Eden Studio | Project folders, enabled extensions, VFS overlay order. |
| `.layout.json` | `openeden.layout` (v1) | Eden Studio | Saved workspace and panel layout per user. |
| `extension.json` | `openeden.extension` (v1) | Extension system | Extension manifest. The `editor` entry in section 4.1 is planned and is not in the format today. |

Every other format is registered by the extension that owns it (section 3.6) and appears in Eden Studio only when that extension's editor module is loaded. For example, as the planned extensions define them:

| File extension | Format Identifier | Registered By | Purpose |
| --- | --- | --- | --- |
| `.animgraph.json` | `opas.animgraph` (v1) | CreativeMotion | State machines, blends, transition conditions, inertialization. |
| `.rig.json` | `opas.rig` (v2) | CreativeMotion | Bone hierarchy, collision shapes, joint limits, helper bones. |
| `.profile.json` | `opas.profile` (v2) | CreativeMotion | Motor stiffness and damping, balance and stepping parameters. |
| `.style.json` | `opas.style` (v1) | CreativeMotion | Personality styling, posture, cadence, additive layers. |
| `.scenario.json` | `opas.scenario` (v3) | CreativeMotion | Replay fixtures and sandbox tests. |
| `.character.json` | `openeden.character` (v1) | Reflectron | NPC and player morphology, face sculpt, style references. |
| `.garment.json` | `openeden.garment` (v1) | ComplexClothing | Garment layers, fit offsets, cloth properties. |

All formats are explicit, versioned JSON with canonical writers that round-trip byte for byte.

---

## 8. Implementation Phasing & Handoff Roadmap

For the OpenEden development session implementing Eden Studio, the recommended phased plan is:

### Phase 1: Studio Shell & Embedded Viewport
*Part of this is built, on the `vulkan` branch (Track V stage V6, task 0014, stages C1 to C9; pinned here at `ca9f6283d8`). The tree this document is committed in does not have it.*
- Separate the window host from Morrowind-specific data models (those move toward the Morrowind compatibility pack). **Built in part:** the editor's model layer is the `csmodel` library (`apps/opencs/CMakeLists.txt`), which `openmw-cs-tests` links without any view code (`apps/opencs_tests/CMakeLists.txt`). Morrowind-specific data has not moved.
- Docking: **Qt, as built.** `SubView` derives from `QDockWidget` (`apps/opencs/view/doc/subview.hpp`). Nothing in `apps/` or `components/` uses ImGui except vendored `components/creativemotion` CMake files. The earlier "Qt6 docking or ImGui" choice is settled.
- Viewport: **built, and it stays Studio's viewport (decision 0007); not shared with the game's renderer.** The editor renders through VulkanSceneGraph in a Qt viewport (vsgQt v0.5.0, Qt 6.6.3, `docs/dependencies.md`) through `CSVRender::SceneHost` (`apps/opencs/view/render/scenehost.hpp`). That is the editor's own render-only counterpart of the game's `MWRender::RenderManager`, which is bound to the game's world types (`setStore(MWWorld::ESMStore)`, `configureAmbient(MWWorld::Cell)` in `apps/openmw/mwrender/rendermanager.hpp`). It is pending toddhow's hands-on checks (task 0014, "Check list for toddhow").
- **Phase 1 scope (decision 0007):** keep the editor's `SceneHost` for the viewport and add, beside it, a Jolt world (with its debug draw pass in the viewport) and the extension host that loads runtime extensions as the game does, read-only at first. Both are built (2026-11-06 update at the top). Whether Studio converges with the game's renderer is decided later, not now.


### Phase 2: Built-In Core Tools
- Asset browser over the VFS, transaction system, simulation controls, editor event bus.
- World & Scene workspace: cells, terrain, object placement, lighting, navmesh.
- Extension manager (list, enable, disable, show errors).

### Phase 3: Editor Module API
*Status 2026-11-06: API 1.1 built (task 0031; the hooks removal, stage C, is done: SDK 3.0); section 3.2 dialogs, 3.3 canvases, 3.5 picking and labels, 3.7 bone and entity selection, 3.10 events, 3.11 settings and tasks are not built. Next: task 0091 draft (API 1.2).*
- Define and publish the Editor Module API (sections 3.1 to 3.11) with its own version.
- Load editor libraries from extension manifests (section 4), and for core systems from the fixed core list, with failure isolation. Per decision 0007 this is the only way tools are added: remove the runtime-side hooks in `sdk/include/openeden/editor.hpp` (`EditorService`, `EditorWorkspace`, `EditorInspector`, `EditorGizmo`) in this phase, with their implementation (`components/extension/editor_service.*`) and tests.
- Build a small sample editor module in the OpenEden repository that exercises every part of the API (a panel, a command, a gizmo, an asset type, an inspector section, a timeline track), as the reference for extension teams and as a test.

### Phase 4: First Real Module
*Status 2026-11-01: the rig and motor inspector exists (task 0032); the push sandbox needs the in-engine ragdoll adapter (M10); the animation graph, arbiter and face tools are not started. Gaps the first tool met are listed in task 0032.*
- CreativeMotion's tools (starting with the rig inspector and push sandbox) are built against the published API; every gap they hit is meant to be fixed by extending the API, not by adding CreativeMotion code to Eden Studio. CreativeMotion's editor module is a separate library on the core list, so that guardrail holds (decision 0007).

### Phase 5: Further Editor Modules (owned by the extension teams)
- AdvancedBodies and Reflectron, ComplexClothing, the face layer and the Morrowind compatibility pack each ship their own editor module on their own schedule (AdvancedBodies and ComplexClothing are planned core systems, so their editor modules join the core list, decision 0007). Eden Studio's work in this phase is limited to API additions they need.

### Phase 6: Production Hardening & Community SDK
- Editor API compatibility checks in CI; documentation and a template editor module for community extension authors.
- VFS mod package export.

---

## 9. Boundary Guardrails
- **The editor mirrors the engine.** Eden Studio contains tools only for what the engine core owns; extension features' tooling ships with the extension as an editor module; core systems' tooling ships as editor modules loaded from a fixed core list (decision 0007).
- **No feature code in Eden Studio.** If an editor module needs a capability, the Editor Module API grows; the feature's tool never moves into the editor.
- **No game code in Eden Studio.** Game-specific editors come from the active compatibility pack.
- **The game never loads editor code.** Editor modules are separate libraries loaded only by Eden Studio (planned; section 4).
- **No direct coupling.** Extensions never link each other or call each other; they use runtime contracts and editor events.
- **Missing extensions leave no traces.** With an extension disabled, its menus, panels, gizmos and asset types are absent, not broken.
- **Original content only.** No proprietary game assets (Bethesda `.nif`, `.hkx` or `.esm`) in any repository; compatibility packs read the user's own installed game data.
- **Clean separation of concerns:**
  - *CreativeMotion* (core, decision 0001) determines how entities move, react and balance.
  - *AdvancedBodies* (planned core system) determines anatomical geometry and surface contracts.
  - *ComplexClothing* (planned core system) determines garment fit and cloth dynamics.
  - *Reflectron* (an extension) provides character creation in game and the NPC studio in the editor.
  - *Eden Studio* provides the workbench and the Editor Module API that brings them all together.
  - The tools for the three core systems are editor modules on a fixed core list, not Eden Studio code (decision 0007).


---

## 10. Decisions on the editor design

Decided by toddhow on 2026-10-31 (record: `docs/decisions/0007-eden-studio-editor-design.md`). These were open questions; all three took the recommended option.

- **Core systems' tools (was (a)):** CreativeMotion, AdvancedBodies and ComplexClothing have their editor tools as separate editor libraries (editor modules). Eden Studio loads them from a fixed list of core systems, not from extension manifests. The game never loads them, and "no feature code in Eden Studio" (sections 2.2 and 9) still holds.
- **Editor design (was (b)):** the plan's design stays: a separate editor library that the game never loads. The runtime-side hooks in `sdk/include/openeden/editor.hpp` (`EditorService`, `EditorWorkspace`, `EditorInspector`, `EditorGizmo`) were removed in SDK 3.0 (task 0031 stage C).
- **The world Studio runs (was (c)):** Eden Studio keeps the editor's own render-only `SceneHost` from Track V stage V6 for its viewport. Phase 1 adds the Jolt world and the extension host beside it, read-only at first. Whether Studio converges with the game's renderer is decided later.
