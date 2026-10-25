# Eden Studio: Architecture, Tooling Suite, and Integration Plan

**Status:** Living Design Document & OpenEden Handoff  
**Author:** Todd & CreativeMotion Team  
**Date:** 2026-10-25  
**Target:** OpenEden Core Team & Tooling Workgroup  
**Companion Documents:**  
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
**Eden Studio** elevates OpenEden from an engine fork into a **complete, unified game development studio suite** (comparable to modern Unreal Engine Editor or Godot). Instead of maintaining separate external tools, OpenEden refactors the `openmw-cs` foundation into an extensible, modern **Studio Shell**. 

Each major subsystem—**CreativeMotion** (Animation & Physics), **AdvancedBodies** (Anatomy & Morphs), **ComplexClothing** (Dynamic Garments), and **Reflectron** (Character Creation & NPC Styling)—exposes its own native, dockable workspace inside Eden Studio. All tools share a single 3D viewport, one Jolt physics simulation environment, a virtual file system (VFS), an undo/redo transaction stack, and a central event bus.

---

## 2. Core Shell Architecture

Eden Studio is not a collection of standalone apps wrapped in an installer. It is an extensible, modular workbench host.

```text
+----------------------------------------------------------------------------------------------------+
|                                           EDEN STUDIO                                              |
+----------------------------------------------------------------------------------------------------+
| Menu Bar: File | Edit | View | Subsystems | Simulation | Tools | Build | Window | Help              |
+----------------------+------------------------------------------------------+----------------------+
| Asset Browser (VFS)  | Central 3D Viewport (Shared PBR + Jolt World)        | Context Inspector    |
|                      |                                                      |                      |
| [>] Characters       |   [Live Jolt Ragdoll + Skeleton Preview]             | Selected: Pelvis     |
|   [>] Player.char    |                                                      | - Mass: 14.5 kg      |
|   [>] Guard.char     |   Interactive Handles & Gizmos:                      | - Shape: Capsule     |
| [>] Animations       |   - Joint limit cones/pyramids                       | - Motor: Active      |
|   [>] Locomotion     |   - Ground elevation & contact normals               |   - K_p: 2500 N*m/rad|
| [>] Rig Profiles     |   - Live push impulse arrow (e.g. 40 N*s)            |   - K_d: 180 N*m*s   |
| [>] Garments         |   - Capsule vs Pelvis leashing bounds                | - Arbiter: Dynamic   |
+----------------------+------------------------------------------------------+----------------------+
| Dockable Subsystem Workspaces (Tabs / Floating Windows)                                             |
| [CreativeMotion (Anim & Physics)] [Reflectron & Bodies] [ComplexClothing] [World & Cells] [Audio/Face] |
+----------------------------------------------------------------------------------------------------+
| Timeline & Scrub Controls | Transport Controls [> Play] [|| Pause] [[] Step] | Output Log / Memory   |
+----------------------------------------------------------------------------------------------------+
```

### 2.1 Studio Shell Services
1. **Dockable Workspace Manager:** Modern dockable canvas (built on Qt6 Docking or embedded ImGui docking) enabling customized layout persistence per workflow (Animation Authoring, Level Design, Character Sculpting).
2. **Unified 3D Viewport:**
   - High-fidelity PBR rendering engine (Vulkan / OpenGL).
   - Live Jolt physics debug overlay (colliders, contact points, joint coordinate frames, support polygon convex hulls, center-of-mass indicators).
   - Shared camera controls (Orbit, Flycam, Focus-on-Selection) with configurable simulation time-dilation (0.1x, 0.25x, 0.5x, 1x, 2x).
3. **Project & Asset Virtual File System (VFS):**
   - Seamless transparent overlay supporting base game data, mod directories, and development working directories.
   - Native asset pipeline: glTF/GLB models, OpenEden JSON manifests, sound files (`.wav`, `.ogg`), and ESM/ESP/omwaddon records.
4. **Unified Transaction System:** Standardized undo/redo command stack across all modules with transaction merging and dirty state tracking.
5. **Central Studio Event Bus:** Broadcasts editor events (e.g., `OnActorSelected`, `OnClipScrubbed`, `OnImpulseInjected`, `OnParameterModified`), allowing workspaces to react cohesively.

---

## 3. The Four Character Ecosystem Workspaces

The OpenEden character pipeline is organized into four clearly separated, complementary pillars:

```
+----------------------------------------------------------------------------------------------------+
|                                      EDEN STUDIO & OPENEDEN                                        |
+----------------------------------------------------------------------------------------------------+
                                                  |
         +----------------------------------------+----------------------------------------+
         |                                        |                                        |
+------------------+                    +--------------------+                   +--------------------+
|  CreativeMotion  |                    |   AdvancedBodies   |                   |  ComplexClothing   |
|                  |                    |                    |                   |                    |
| Animation Engine |                    | Body Morphology    |                   | Dynamic Garments   |
| Jolt Ragdolls    |                    | Mesh Morphs        |                   | Auto-Fit to Body   |
| Balance & Sensors|                    | Surface Contracts  |                   | Cloth Simulation   |
| Face Layer (C8)  |                    | Proportions/Scale  |                   | Layer Stacking     |
+------------------+                    +--------------------+                   +--------------------+
         ^                                        ^                                        ^
         | Drives Motion & Limits                 | Body Measurements                      | Drapes Over Body
         +----------------------------------------+----------------------------------------+
                                                  |
                                        +--------------------+
                                        |     Reflectron     |
                                        |                    |
                                        | Unified CC (Game)  |
                                        | NPC Studio Editor  |
                                        | Population Gen     |
                                        | Presets & Export   |
                                        +--------------------+
```

### 3.1 CreativeMotion Workspace (Animation, Physics & Face)
CreativeMotion provides the runtime and tooling for how characters **move, balance, react, and emote**.

#### Key Panels & Features:
1. **Visual Animation Graph Editor (`.animgraph.json`):**
   - Node-based blend tree and state machine canvas.
   - States: Single clips, 1D/2D blend spaces, inertialization nodes, procedural balance/stepping layers.
   - Visual transition rules with live condition debugging (highlighting active transitions and blend weights).
2. **Rig & Motor Limits Inspector (`.rig.json`, `.profile.json`):**
   - 3D visual gizmos rendering collision shapes (capsules, boxes, spheres) aligned to bones.
   - 3D joint limit cones and pyramids (supporting lopsided swing/twist limits as defined in OPAS Rig v2).
   - Motor tuning sliders: Target stiffness ($K_p$ in $\text{N}\cdot\text{m/rad}$), damping ($K_d$ in $\text{N}\cdot\text{m}\cdot\text{s/rad}$), and maximum joint torque.
   - Procedural helper bones inspector (Milestone C6: twist, swing, chain, and breathing multipliers).
3. **Interactive Live Sandbox / Push Dock:**
   - Allows creators to test physical stability directly inside Eden Studio without launching the game.
   - One-click impulse injection: Select a bone (e.g., Pelvis, Spine, Head), specify an impulse vector (e.g., $40\,\text{N}\cdot\text{s}$ forward/lateral), and watch the ragdoll:
     - Apply dynamic balance lean compensation.
     - Trigger autonomous stepping IK to recover base of support.
     - Enter stumble/stagger states if the capture point exceeds safety margins.
     - Drop into knockdown/ragdoll if acceleration thresholds are exceeded, followed by automated get-up blending.
4. **Reconciliation & Arbiter Inspector (Milestone C7 & C1):**
   - Real-time visualization of the gameplay capsule position vs. physical simulated pelvis position.
   - Visual leashing boundaries (soft leash vs. hard leash radii).
   - Arbiter state indicators: Dynamic / Kinematic / Blending / Dormant per body part.
   - Snapshot recording & deterministic playback scrubber (capturing bit-exact `CharacterStateSnapshot` streams).
5. **Facial Expression & Speech Timeline (Immersive Expressions / Milestone C8):**
   - Audio waveform scrubber: Drag and drop dialogue lines (`.wav`/`.ogg`).
   - Phoneme / FACS curve editor: 52 blendshape channels (ARKit / FACS standard).
   - Procedural expression layers: Eye gaze IK targets, involuntary blinks, micro-saccades, and mood/emotion blend sliders (angry, fearful, amused).
   - Direct export to dialogue event metadata and animation clip sidecars.

---

### 3.2 Reflectron & AdvancedBodies Workspace (NPC & Body Studio)
Reflectron is the universal character authoring tool, replacing external BodySlide and RaceMenu workflows with a single runtime-integrated system.

#### Key Panels & Features:
1. **Dual-Mode Authoring:**
   - **In-Game Mode:** Runs as the player character creation interface (PC appearance, race, face sculpt, body proportions).
   - **Eden Studio Mode:** Authoring tool for game developers and modders to design unique NPCs, races, and demographic factions.
2. **Continuous Multi-Axis Morphology Sliders:**
   - Anatomical sliders: Height, muscularity, body fat, torso proportions, leg/arm lengths, shoulder breadth.
   - Facial feature sliders: Jaw width, cheekbone prominence, nose bridge, eye depth.
   - Morphs operate via vertex displacement vectors evaluated directly in shaders or compute passes—no offline mesh baking required.
3. **Demographic Archetype & Population Generator:**
   - Modders define population rules per faction, region, or guild:
     ```json
     {
       "archetype": "NordBlacksmith",
       "muscularity": { "min": 0.70, "max": 0.95 },
       "bodyFat": { "min": 0.25, "max": 0.50 },
       "height": { "min": 1.05, "max": 1.14 },
       "styleProfile": "rugged_laborer"
     }
     ```
   - One-click demographic generator automatically populates whole settlements with anatomically diverse, believable NPCs.
4. **Dynamic Capsule & Rig Auto-Scaling Contract:**
   - When a body morph is applied in Reflectron, CreativeMotion queries the **Body Contract** to rescale physical collision capsules and sensor offsets proportionally, ensuring ragdoll and balance accuracy match the character's visual physique.
5. **Canonical Character Profile (`.character.json`):**
   - Saves character appearance, morphology, and style profiles into clean, human-readable, lightweight JSON files.

---

### 3.3 ComplexClothing Workspace (Dynamic Garments & Draping)
ComplexClothing replaces static armor meshing with dynamic clothing that fits any body geometry automatically.

#### Key Panels & Features:
1. **Runtime Auto-Fit Preview:**
   - Preview garments across the full spectrum of Reflectron body shapes (from minimum to maximum slider extremes) in real time.
   - Clothing geometry conforms to the underlying AdvancedBodies skin surface proxy.
2. **Layer Stacking & Clipping Resolver:**
   - Multi-layer management: Underwear $\rightarrow$ Inner Tunic $\rightarrow$ Mail Cuirass $\rightarrow$ Heavy Plate $\rightarrow$ Cloak.
   - Automatic surface offset and vertex culling for occluded inner mesh regions to ensure zero clipping and optimal rendering performance.
3. **Cloth Simulation Constraints:**
   - Authoring pin constraints, stiffness maps, and aerodynamic drag for skirts, capes, sashes, and robes simulated in Jolt.

---

### 3.4 World & Scene Workspace (Classic OpenMW-CS Modernized)
Retains and expands all traditional world-building capabilities:
- **3D Cell & Exterior Editing:** Terrain sculpting, heightmap editing, object placement, and instant multi-object transforms.
- **Lighting & Atmospherics:** Live PBR light probes, day/night cycle preview, volumetric fog tuning.
- **Dialogue & Quest Graphs:** Visual node graph for branching quest logic, dialogue trees, and script execution.
- **Navmesh Generation:** High-performance automatic navmesh generation using Recast/Detour with live debug path queries.

---

## 4. Technical Integration & Contracts

### 4.1 Physics Backend Integration (Jolt Physics)
1. **Shared Physics World:**
   - CreativeMotion does not spin up a duplicate physics engine. When running inside OpenEden or Eden Studio, CreativeMotion interfaces directly with OpenEden's master `Jolt::PhysicsSystem`.
   - Collision layers and filters are cleanly partitioned (`NON_MOVING`, `MOVING`, `CHARACTER_CAPSULE`, `RAGDOLL_DYNAMIC`, `SENSOR_RAYCAST`).
2. **Fixed-Step Update Contract:**
   - Physics operates at fixed, deterministic timesteps (typically $60\,\text{Hz}$ with configurable sub-stepping).
   - In Eden Studio, the simulation loop can be stepped frame-by-frame, paused, or scrubbed backwards using recorded state snapshots.

### 4.2 Strict Zero-Allocation Guarantee
- In accordance with CreativeMotion's core performance requirements, all simulation and evaluation code executed during the simulation step adheres strictly to **zero heap allocations** (`malloc`/`operator new`).
- Studio inspection tooling uses dedicated diagnostic memory pools or non-hot-path queries so profiling metrics remain unpolluted.

### 4.3 Bidirectional Event Bus (Engine $\leftrightarrow$ Subsystems)
Implemented via the fixed-size event queue (`FixedEventQueue` / `EventBridge`):
- **Engine to CreativeMotion:** `LocomotionInputEvent`, `HitReactionEvent`, `KnockdownEvent`, `OwnershipChangeEvent`, `LeashTeleportEvent`.
- **CreativeMotion to Engine:** `StateEvent` (StaggerStarted, GroundContactLost, KnockdownFell, RecoveredToKinematic), `FootPlantEvent`, `MarkerHitEvent`.

---

## 5. File Formats & Schemas Reference

All assets authored in Eden Studio utilize explicit, versioned, round-trip byte-identical JSON formats:

| Extension | Format Identifier | Owning Subsystem | Purpose |
| --- | --- | --- | --- |
| `.animgraph.json` | `opas.animgraph` (v1) | CreativeMotion | State machines, blend trees, transition conditions, inertialization. |
| `.rig.json` | `opas.rig` (v2) | CreativeMotion | Bone hierarchy, collision shapes, joint limits (cones/pyramids), helper bones. |
| `.profile.json` | `opas.profile` (v2) | CreativeMotion | Motor stiffness/damping, sensor ray layouts, balance/stepping parameters, arbiter bounds. |
| `.style.json` | `opas.style` (v1) | CreativeMotion | Personality styling, posture leans, movement cadence, additive layers. |
| `.scenario.json` | `opas.scenario` (v1) | CreativeMotion | Replay fixtures, automated sandbox verification tests, benchmark scripts. |
| `.character.json` | `openeden.character` (v1) | Reflectron | Player/NPC morphology parameters, facial sculpt, styling references. |
| `.garment.json` | `openeden.garment` (v1) | ComplexClothing | Garment layer metadata, auto-fit offsets, cloth simulation properties. |

---

## 6. Implementation Phasing & Handoff Roadmap

For the OpenEden development session implementing Eden Studio, the recommended phased execution plan is:

### Phase 1: Studio Shell & Embedded Viewport
- Refactor `openmw-cs` core application loop to decouple Morrowind-specific data models from the main window host.
- Integrate modern docking architecture (Qt6 Docking or ImGui).
- Embed high-performance 3D viewport sharing the engine's Vulkan/OpenGL renderer and Jolt debug physics draw pass.

### Phase 2: CreativeMotion Module Docking
- Expose the CreativeMotion C++ runtime into Eden Studio.
- Implement the 3D Rig Inspector: render visual joint limits (cones/pyramids) and collider gizmos directly in the viewport.
- Build the **Push Sandbox Dock**: interactive button array injecting impulses and visualizing balance recovery, stepping, and knockdown transitions in real time.
- Implement the Visual Animation Graph Editor for `.animgraph.json`.

### Phase 3: Reflectron & AdvancedBodies Integration
- Implement the dynamic morphology slider panel.
- Connect slider values to vertex shader morph buffers for live character deformation.
- Wire the **Body Contract**: auto-rescale CreativeMotion physics capsules and foot sensors when body sliders change.
- Implement the Demographic Population Generator panel.

### Phase 4: ComplexClothing Workspace
- Implement garment auto-fitting preview over the dynamic body contract.
- Add garment layering and clipping inspection tools.
- Implement cloth simulation constraint tuning inside the shared Jolt world.

### Phase 5: Dialogue, Speech & Facial Expression Timeline
- Add audio scrubber and waveform track component.
- Integrate phoneme/FACS curve visualizer and emotion blend controllers (CreativeMotion Milestone C8).
- Synchronize head/neck gesturing, eye gaze IK, and lip movements during speech preview.

### Phase 6: Production Hardening & Community SDK
- Validate deterministic round-trip serialization across all `.json` formats.
- Integrate Virtual File System (VFS) mod package export.
- Author creator documentation and sample test fixtures.

---

## 7. OpenEden & CreativeMotion Boundary Guardrails
- **No Direct Coupling:** Modules never link against each other's private implementation details. All communication flows through explicit interface contracts (`core/rig`, `physics/interface`, `EventBridge`, `BodyContract`).
- **Original Content Only:** No proprietary game assets (Bethesda `.nif`, `.hkx`, or `.esm`) are bundled into any repository. Test fixtures use original glTF models and synthetic test data.
- **Clean Separation of Concerns:**
  - *CreativeMotion* determines how entities move, react, and balance.
  - *AdvancedBodies* determines anatomical geometry and surface contracts.
  - *ComplexClothing* determines garment fit and cloth dynamics.
  - *Reflectron* provides the unified authoring interface for creators.
  - *Eden Studio* provides the workbench that brings them all together.
