# Eden Studio: Architecture, Editor Extension API, and Integration Plan

**Status:** Living Design Document & OpenEden Handoff  
**Author:** Todd & CreativeMotion Team  
**Date:** 2026-10-25  
**Target:** OpenEden Core Team & Tooling Workgroup  
**Companion Documents:**  
- [`docs/design/openeden-plugin-system.md`](openeden-plugin-system.md) (how plugins are found, registered and started; this document builds the editor side on it)
- [`docs/design/advancedbodies-handoff.md`](file:///i:/Projects/CreativeMotion/docs/design/advancedbodies-handoff.md)
- [`docs/Open Procedural Animation System - Living Design Doc.md`](file:///i:/Projects/CreativeMotion/docs/Open%20Procedural%20Animation%20System%20-%20Living%20Design%20Doc.md)
- [`docs/decisions/0004-total-fork-of-openmw.md`](file:///i:/Projects/CreativeMotion/docs/decisions/0004-total-fork-of-openmw.md)

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
OpenEden's engine core is groundwork only; every feature system is a plugin loaded at runtime (see the plugin system document). **Eden Studio follows exactly the same split:**
- **Eden Studio itself contains only the tools for what the engine core owns:** the shell, the viewport, the asset browser, world and scene editing, project and plugin management. It has no animation editor, no body sliders, no clothing tools and no face timeline of its own.
- **Feature tools come from the feature plugins.** Each plugin that wants tooling ships an **editor extension** alongside its runtime. When Eden Studio loads the plugin, it loads the extension too, and the extension tells Eden Studio which windows, panels, inspectors, gizmos, menus and asset editors it needs through the **Editor Extension API** (section 3).
- **Game-specific editors come from the compatibility pack.** Record types, dialogue and quest data belong to one game, so the game's compatibility pack supplies their editors through the same API.

Why: these features are not baked into the engine, so they must not be baked into the editor either. A feature that is missing from a session (a plugin not installed or not enabled) must leave no dead menus or broken panels behind; a new plugin must be able to bring its own tooling without anyone changing or rebuilding Eden Studio; and a plugin's tools must move with it, versioned and shipped by the team that owns the feature.

---

## 2. Core Shell Architecture

Eden Studio is an extensible workbench host: a small, fixed shell plus whatever extensions the enabled plugins provide.

```text
+----------------------------------------------------------------------------------------------------+
|                                           EDEN STUDIO                                              |
+----------------------------------------------------------------------------------------------------+
| Menu Bar: File | Edit | View | Simulation | Plugins | Window | Help  (+ menus added by extensions)  |
+----------------------+------------------------------------------------------+----------------------+
| Asset Browser (VFS)  | Central 3D Viewport (Shared Renderer + Jolt World)   | Context Inspector    |
|                      |                                                      |                      |
| (asset types are     |   Built-in: camera, selection, transform gizmos,     | Built-in: entity     |
|  listed by whichever |   physics debug draw                                 | transform, name,     |
|  plugins registered  |   Extension gizmos and overlays drawn on top         | tags                 |
|  them)               |                                                      | + sections added by  |
|                      |                                                      |   extensions         |
+----------------------+------------------------------------------------------+----------------------+
| Dockable Workspaces: [World & Scene (built-in)] + one tab per loaded extension's workspace          |
|   e.g. [CreativeMotion] [Reflectron & Bodies] [ComplexClothing] [Game Records (compatibility pack)] |
+----------------------------------------------------------------------------------------------------+
| Timeline & Transport [> Play] [|| Pause] [[] Step] (+ tracks added by extensions) | Output Log     |
+----------------------------------------------------------------------------------------------------+
```

### 2.1 What Eden Studio Provides Itself (Built In)
These exist with zero plugins loaded, because they mirror the engine core:
1. **Shell and Docking:** the main window, dockable workspace manager (Qt6 docking or embedded ImGui docking) and per-workflow layout persistence.
2. **Unified 3D Viewport:** the engine's renderer, shared camera controls (orbit, fly, focus on selection), selection, transform gizmos, and the Jolt physics debug overlay (colliders, contacts, joint frames). Simulation time dilation (0.1x to 2x).
3. **Project & Asset VFS Browser:** base game data, mod folders and working folders as one overlay. The browser itself knows no feature formats; it lists and opens assets through the asset types extensions register (section 3.6).
4. **World & Scene Workspace:** the engine-level parts of classic OpenMW-CS: cell and exterior editing, terrain sculpting, object placement and multi-object transforms, lighting and atmospherics, navmesh generation with Recast/Detour.
5. **Transaction System:** one undo/redo stack for the whole editor, with transaction merging and dirty-state tracking; extensions push their own commands onto it.
6. **Simulation Control:** play, pause, single step and scrub of the shared world, with recorded snapshots for stepping backwards.
7. **Studio Event Bus:** editor events (selection changed, asset opened, simulation stepped, parameter changed) that the built-in tools and extensions publish and subscribe to.
8. **Plugin Manager:** lists installed plugins and their extensions, enables and disables them, shows load errors (section 4).

### 2.2 What Eden Studio Does Not Provide
Eden Studio contains **no feature-specific code**. Specifically, none of the following are part of Eden Studio:
- Animation graph editing, rig and motor inspection, push testing, arbiter and reconciliation views (supplied by the CreativeMotion plugin).
- Body morph sliders, population generation, character profiles (supplied by AdvancedBodies and Reflectron).
- Garment fitting, layering and cloth constraint authoring (supplied by ComplexClothing).
- Facial expression and speech timelines (supplied by the plugin that owns the face layer).
- Game-specific record editors, dialogue and quest graphs (supplied by the active compatibility pack).

If one of these tools needs something Eden Studio cannot yet do, the Editor Extension API grows to offer it; the tool itself never moves into Eden Studio.

---

## 3. The Editor Extension API

The Editor Extension API is the set of services Eden Studio exposes so that an extension can describe its tools and run them. It is to the editor what the plugin system's host API is to the engine: the only thing an extension compiles against, versioned, and shared by all extensions.

### 3.1 How an Extension Is Shaped
```cpp
namespace openeden::editor {

// Implemented by each editor extension. Owned by Eden Studio from creation
// until the extension is unloaded.
class EditorExtension {
public:
    virtual ~EditorExtension() = default;
    // Describe every window, panel, menu, gizmo and asset type to the editor.
    // Called once, after the extension's runtime plugin has started.
    virtual void registerUi(EditorRegistrar& registrar) = 0;
    // Called before the extension is unloaded, after its windows are closed.
    virtual void shutdown() = 0;
};

} // namespace openeden::editor
```

The extension holds no global state the editor does not know about: every window it creates, every command it adds and every asset type it handles is registered through `EditorRegistrar`, so Eden Studio can show, hide, restore and remove them cleanly.

### 3.2 Workspaces, Panels and Windows
- **Workspaces:** a named tab grouping panels for one workflow (for example "CreativeMotion"). Extensions declare a workspace with a default layout; users rearrange it and the layout is saved per user.
- **Panels:** dockable windows inside a workspace or floating. An extension registers a panel with an id, a title, a default dock position and a draw callback; Eden Studio owns the window, the extension owns the panel's contents.
- **Modal dialogs and wizards:** for create-new and import flows.
- **Lifecycle callbacks:** opened, closed, focused, and "selection changed" for panels that follow the selection.

### 3.3 UI Toolkit
One toolkit for every extension, so all tools look and behave alike:
- Standard widgets: buttons, sliders with units, numeric fields, colour pickers, lists, trees, tables, tabs, search fields.
- **Property editing:** an extension describes an object's fields (name, type, unit, range, tooltip) and Eden Studio draws the editor for it, including multi-selection and undo.
- **Specialised canvases:** a node-graph canvas (for state machines, quest graphs), a curve editor (for keys and blend curves), a timeline with tracks, and an embedded secondary viewport for previews.
- Extensions never create native windows or link a UI library of their own; the toolkit is the only route to the screen.

### 3.4 Commands, Menus, Toolbars and Shortcuts
- An extension registers **commands** (id, label, icon, handler, enabled-when condition) and places them in menus, toolbars and context menus (on assets, entities or viewport selections).
- Default keyboard shortcuts are declared with the command; conflicts are reported in the plugin manager and resolved by the user.

### 3.5 Viewport Extensions
- **Gizmos:** interactive handles an extension attaches to a selected entity or bone (for example joint limit cones, impulse arrows, capsule bounds), with drag callbacks that push undoable commands.
- **Overlays:** points, lines, labels and shapes submitted as data each frame and drawn by the viewport (the same overlay-as-data pattern the engine uses for debug draw).
- **Picking:** an extension may register pickable items, so clicking a gizmo or overlay selects what it represents.
- **Preview scenes:** an extension may open a secondary viewport with its own isolated world (for example a single character on a floor), sharing the renderer.

### 3.6 Asset Types
- An extension registers the asset types it owns: format id and extension, icon, how to create a new one, which panel opens it, and optionally a thumbnail generator and a preview.
- The asset browser shows, filters and opens assets through these registrations. An asset whose type no loaded extension handles is listed as a plain file and opened read-only as text.
- Saving goes through the owning extension's writer, so files stay in their canonical, round-trip form.

### 3.7 Inspector Sections
- When an entity, bone or asset is selected, Eden Studio builds the context inspector from the built-in section (transform, name, tags) plus every section an extension registered for that kind of selection.
- Each section is a property description (section 3.3) or a custom draw callback.

### 3.8 Undo, Transactions and Dirty State
- Every change an extension makes to project data is a **command** pushed onto Eden Studio's single undo stack (do, undo, merge-with-previous).
- Extensions mark the assets they changed as dirty; Eden Studio handles save prompts and save-all.

### 3.9 Simulation and Timeline Access
- Extensions read the shared world's state and may inject inputs (for example an impulse) through engine contracts, but play, pause and step remain Eden Studio's controls, so every tool sees the same simulation.
- Extensions may add **timeline tracks** (keys, events, audio waveforms, state bands) and react to scrubbing.

### 3.10 Editor Event Bus
- Extensions subscribe to editor events (selection changed, asset opened or saved, simulation stepped, project loaded) and publish their own typed events for other extensions.
- Extensions never call each other directly; cross-feature tooling (for example a body panel that shows physics capsules) goes through runtime contracts and editor events, exactly as at runtime.

### 3.11 Settings, Diagnostics and Background Work
- **Settings pages:** an extension registers preference pages drawn from typed settings, as in the plugin system.
- **Log and notifications:** a per-extension log channel in the output panel, and non-blocking notifications.
- **Background tasks:** long operations (baking, batch export, population generation) run as cancellable tasks with progress shown by Eden Studio, never on the UI thread.

---

## 4. Packaging, Loading and Lifecycle of Editor Extensions

### 4.1 One Plugin, Two Libraries
A plugin that provides tooling ships its editor extension as a **separate library** beside its runtime library:

```text
plugins/
  creativemotion/
    plugin.json                  manifest (declares both libraries)
    creativemotion.dll           runtime: loaded by the game, Eden Studio and the test host
    creativemotion.editor.dll    editor extension: loaded only by Eden Studio
    data/
```

- The **game never loads editor code**, so it carries no UI toolkit, no editor dependencies and no editor-only memory.
- The editor library links the runtime library's public contracts and the Editor Extension API; it never reaches into the runtime library's internals.
- A plugin with no tooling simply has no editor library; Eden Studio still loads its runtime so the world behaves as in the game.

The manifest gains an optional entry naming the editor library and the Editor API version it was built for:

```json
"editor": {
  "library": {"windows": "creativemotion.editor.dll", "linux": "libcreativemotion.editor.so"},
  "editorApi": {"major": 1, "minor": 0}
}
```

(The plugin system document's manifest and section 4.10 should be updated to match this two-library model.)

### 4.2 Loading Sequence in Eden Studio
1. Eden Studio starts the engine core and loads the enabled plugins exactly as the game does (discover, resolve, register, start).
2. For each started plugin with an `editor` entry, in the same load order, Eden Studio loads the editor library, checks its Editor API version, creates the extension and calls `registerUi`.
3. Eden Studio builds menus, workspaces, the asset browser and the inspector from the built-in tools plus everything registered.
4. On shutdown, or when a plugin is disabled from the plugin manager, its windows are closed, `shutdown` is called, its registrations are withdrawn and the library is unloaded, in reverse load order.

### 4.3 Failure Handling
- An editor extension that fails to load or register is disabled on its own; its runtime plugin keeps running, and the rest of Eden Studio is unaffected. The plugin manager shows the reason.
- If a runtime plugin is disabled (missing contract, failed resolve), its editor extension is not loaded.
- An exception from an extension callback is caught at the boundary, logged, and the extension's windows are closed.

### 4.4 Versioning
- The Editor Extension API has its own major and minor version, separate from the engine SDK's, so the editor can evolve without forcing engine plugin rebuilds and vice versa.
- Minor versions only add; a major version change requires extensions to be rebuilt.

---

## 5. Example: Workspaces the Planned Plugins Will Provide

The workspaces below are **not part of Eden Studio**. They are what the planned plugins are expected to provide through the Editor Extension API, listed here to show what the API must be able to express. Each plugin's own design documents own the detail.

```
+----------------------------------------------------------------------------------------------------+
|                          EDEN STUDIO SHELL (built-in tools + Editor Extension API)                 |
+----------------------------------------------------------------------------------------------------+
                                                  |  loads editor extensions shipped by plugins
         +----------------------------------------+----------------------------------------+
         |                                        |                                        |
+------------------+                    +--------------------+                   +--------------------+
|  CreativeMotion  |                    |   AdvancedBodies   |                   |  ComplexClothing   |
|  editor ext.     |                    |   editor ext.      |                   |  editor ext.       |
| Anim graph, rig, |                    | Morphs, surface    |                   | Fit preview,       |
| push sandbox,    |                    | contract views     |                   | layers, cloth      |
| arbiter, face    |                    |                    |                   | constraints        |
+------------------+                    +--------------------+                   +--------------------+
                                                  |
                                        +--------------------+          +--------------------------+
                                        |     Reflectron     |          |  Compatibility pack      |
                                        |  editor ext.       |          |  editor ext.             |
                                        | NPC studio, pop.   |          | Game records, dialogue,  |
                                        | generator, presets |          | quests                   |
                                        +--------------------+          +--------------------------+
```

### 5.1 CreativeMotion Extension (Animation, Physics & Face)
Expected to register:
1. **Visual Animation Graph Editor** (`.animgraph.json` asset type, node-graph canvas): states, blends, transitions with live condition debugging.
2. **Rig & Motor Limits Inspector** (`.rig.json`, `.profile.json` asset types, inspector sections, viewport gizmos): collision shapes, joint limit cones and pyramids, motor stiffness ($K_p$ in $\text{N}\cdot\text{m/rad}$), damping ($K_d$ in $\text{N}\cdot\text{m}\cdot\text{s/rad}$) and maximum torque, helper bone multipliers.
3. **Push Sandbox Panel** (commands, impulse gizmo, preview scene): inject an impulse at a bone and watch balance, stepping, stumbling, knockdown and get-up in the shared simulation.
4. **Reconciliation & Arbiter Inspector** (overlays, timeline tracks): gameplay capsule against simulated pelvis, leash bounds, per-body-part arbiter states, snapshot playback.
5. **Facial Expression & Speech Timeline** (timeline tracks, curve editor, audio waveform): phoneme and FACS curves, gaze targets, blinks, emotion blends.

### 5.2 AdvancedBodies & Reflectron Extensions (Body & NPC Studio)
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

Reflectron's in-game character creation is a runtime feature of the Reflectron plugin, not an editor extension; its editor extension adds the NPC studio on top.

### 5.3 ComplexClothing Extension (Garments & Draping)
Expected to register:
1. **Auto-Fit Preview** (preview scene): garments across the range of body shapes.
2. **Layer Stacking & Clipping View** (panel, overlays): layer order and culled regions.
3. **Cloth Constraint Authoring** (`.garment.json` asset type, gizmos): pins, stiffness maps, drag.

### 5.4 Compatibility Pack Extension (Game Records)
The active compatibility pack is expected to register the editors for its game's data: record types, dialogue and quest graphs (node-graph canvas), and its game's asset types. With no compatibility pack loaded, Eden Studio still edits cells, terrain, objects, lighting and navmesh through its built-in World & Scene workspace.

---

## 6. Technical Integration & Contracts

### 6.1 Shared Engine, Shared World
- Eden Studio runs the real engine core and the real runtime plugins, so what a tool shows is what the game does.
- There is one Jolt physics world, owned by the core. Plugins do not create their own; their editor extensions inspect and drive it through the plugins' runtime contracts.
- Physics runs at a fixed, deterministic step (typically $60\,\text{Hz}$ with configurable sub-steps); in Eden Studio the simulation can be paused, stepped and scrubbed through recorded snapshots.

### 6.2 Runtime Code Stays Allocation-Free
- Simulation code executed during a step keeps its zero-heap-allocation guarantee inside Eden Studio, exactly as in the game.
- Editor extensions may allocate freely in UI code, but must not make runtime hot paths allocate: inspection uses separate diagnostic queries or snapshots.

### 6.3 Runtime and Editor Event Buses
- **Runtime events** (between the engine and plugins, for example a plugin's state and hit events) use the engine's fixed-size event queues, described by each plugin's contracts.
- **Editor events** (section 3.10) are a separate bus inside Eden Studio. Editor extensions may listen to runtime events to display them, but runtime code never depends on editor events.

---

## 7. File Formats & Schemas Reference

Eden Studio itself owns only its own files:

| Extension | Format Identifier | Owner | Purpose |
| --- | --- | --- | --- |
| `.edenproject.json` | `openeden.project` (v1) | Eden Studio | Project folders, enabled plugins, VFS overlay order. |
| `.layout.json` | `openeden.layout` (v1) | Eden Studio | Saved workspace and panel layout per user. |
| `plugin.json` | `openeden.plugin` (v1) | Plugin system | Plugin manifest, including the optional `editor` entry. |

Every other format is registered by the plugin that owns it (section 3.6) and appears in Eden Studio only when that plugin's editor extension is loaded. For example, as the planned plugins define them:

| Extension | Format Identifier | Registered By | Purpose |
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
- Refactor the `openmw-cs` application loop to separate the window host from Morrowind-specific data models (those move toward the Morrowind compatibility pack).
- Integrate the docking architecture (Qt6 docking or ImGui).
- Embed the 3D viewport sharing the engine's renderer and the Jolt debug draw pass.
- Load the engine core and runtime plugins inside Eden Studio exactly as the game does.

### Phase 2: Built-In Core Tools
- Asset browser over the VFS, transaction system, simulation controls, editor event bus.
- World & Scene workspace: cells, terrain, object placement, lighting, navmesh.
- Plugin manager (list, enable, disable, show errors).

### Phase 3: Editor Extension API
- Define and publish the Editor Extension API (sections 3.1 to 3.11) with its own version.
- Load editor libraries from plugin manifests (section 4), with failure isolation.
- Build a small sample extension in the OpenEden repository that exercises every part of the API (a panel, a command, a gizmo, an asset type, an inspector section, a timeline track), as the reference for plugin teams and as a test.

### Phase 4: First Real Extension
- The CreativeMotion team builds its editor extension (starting with the rig inspector and push sandbox) against the published API; every gap they hit is fixed by extending the API, not by adding CreativeMotion code to Eden Studio.

### Phase 5: Further Plugin Extensions (owned by the plugin teams)
- AdvancedBodies and Reflectron, ComplexClothing, the face layer and the Morrowind compatibility pack each ship their own extension on their own schedule. Eden Studio's work in this phase is limited to API additions they need.

### Phase 6: Production Hardening & Community SDK
- Editor API compatibility checks in CI; documentation and a template extension for community plugin authors.
- VFS mod package export.

---

## 9. Boundary Guardrails
- **The editor mirrors the engine.** Eden Studio contains tools only for what the engine core owns; every feature's tooling ships with its plugin as an editor extension.
- **No feature code in Eden Studio.** If an extension needs a capability, the Editor Extension API grows; the feature's tool never moves into the editor.
- **No game code in Eden Studio.** Game-specific editors come from the active compatibility pack.
- **The game never loads editor code.** Editor extensions are separate libraries loaded only by Eden Studio.
- **No direct coupling.** Extensions never link each other or call each other; they use runtime contracts and editor events.
- **Missing plugins leave no traces.** With a plugin disabled, its menus, panels, gizmos and asset types are absent, not broken.
- **Original content only.** No proprietary game assets (Bethesda `.nif`, `.hkx` or `.esm`) in any repository; compatibility packs read the user's own installed game data.
- **Clean separation of concerns:**
  - *CreativeMotion* determines how entities move, react and balance, and provides the tools for it.
  - *AdvancedBodies* determines anatomical geometry and surface contracts, and provides the tools for it.
  - *ComplexClothing* determines garment fit and cloth dynamics, and provides the tools for it.
  - *Reflectron* provides character creation in game and the NPC studio in the editor.
  - *Eden Studio* provides the workbench and the Editor Extension API that brings them all together.
