# Body System Design Guide

Status: draft, 2026-10-22. Companion to the Open Procedural Animation System guide and `CLAUDE.md`.

This guide covers everything about characters' bodies: the replaceable body package, geometry-aware animation, layered clothing, and body physics (clothed and unclothed). It is a design document. Nothing here is built yet, and every engine fact that has not been checked against source is marked **verify**.

## 1. Goals

- One body definition per character that every system reads: animation, IK, alignment, physics, clothing, rendering.
- A standard, versioned way to replace a body mesh, so a modder can drop in a body and get a working character.
- Animation that knows the actual geometry of the characters it moves, for accurate contact and alignment.
- A layered clothing system alongside the original clothing system, which keeps working unchanged.
- Body physics that makes characters look alive without endangering the deterministic gameplay simulation.
- Body customization built into the engine, not dependent on external tools.

## 2. Principles

1. **Contract over mesh.** Consumers depend on the body contract, never on a file format or a particular mesh.
2. **Derive what is missing.** A body needs only a skeleton binding and a render mesh. Everything else is authored if provided and derived if not, and tools show which.
3. **Cosmetic physics is one-way.** Soft-tissue and cloth motion never feed back into the skeleton, collision, AI, or saves.
4. **Gameplay sees the rest shape.** Combat, alignment, and collision queries use the rest-shape proxy, not the deformed surface.
5. **Tiered cost.** Full fidelity only near the player or during interactions. Crowds get the cheapest tier.
6. **Tiers must agree.** The cheap and expensive tiers must look like the same body, so handoffs do not pop.
7. **Everything is off by default.** New behavior sits behind settings, and with settings off the engine behaves as before.
8. **Data, not code.** Parameters live in canonical JSON in the same style as the animation system's data, with validators.

## 3. Module layout

All modules are engine-independent, with a host interface for rendering, assets, and time, in the same style as `core/`.

| Module | Depends on | Responsibility |
| --- | --- | --- |
| `body/` | `core/` data types | Body package format, validator, derivation, surface queries, morphs, selection rules |
| `garment/` | `body/` | Garment format, layers, coverage, fit, states, legacy wrapper |
| `softbody/` | `body/` | Cosmetic physics: spring bones, blend-shape drive, cloth, soft-body regions |

The Jolt gameplay world stays in `physics/`. `softbody/` runs in its own cosmetic world or solver, outside the deterministic gameplay simulation (see section 8).

## 4. The body package

A body package is a bundle of data bound to a canonical skeleton (per race or creature type).

| Part | Required | Notes |
| --- | --- | --- |
| Skeleton binding and bone map | Required | Which canonical skeleton it targets, plus a map for differently named or extra bones |
| Render mesh with LODs | Required | Skin weights, UVs, material references |
| Morph set | Optional | Named morph targets, slider ranges, defaults, presets |
| Surface proxy | Optional | Capsules, a low-resolution mesh with BVH, or per-bone signed distance fields |
| Physics profile | Optional | Collision shapes, masses, joint limits, motor defaults for the ragdoll |
| Measurements | Optional | Limb lengths, heights, widths, support-area data for balance and stride |
| Region map | Optional | Which triangles belong to which body region |
| Landmarks | Optional | Named points: grip points, seat contacts, eye position, sockets |
| Soft-tissue profile | Optional | Per-region mass, stiffness, damping, limits (section 8) |
| Metadata | Required | Version, author, license, contract version, age class |

### Required, optional, and derived

Most community meshes ship only the required parts. The engine generates the rest and labels each piece **authored**, **derived**, or **missing**, so tools can show quality and consumers can degrade gracefully:

- Capsules and a low-resolution proxy from the mesh and skeleton
- Measurements estimated from the skeleton and mesh bounds
- Regions inferred from skin weights
- Physics profile generated from measurements, with masses from volume

Derivation is deterministic: same package and parameters produce identical derived data. Derived data is cached on disk, keyed by package hash and contract version.

### Non-humanoids

The contract treats the skeleton as general. Tails, extra limbs, and digitigrade legs are ordinary packages. The balance and support-area logic must not assume two plantigrade feet, and the test suite must include at least one non-humanoid fixture.

## 5. Consumers and the query API

Each system asks questions in terms of the contract:

- **Animation and IK:** measurements, landmarks, joint limits.
- **Alignment:** closest surface point, normal, and bone weights for a world-space point; ray queries.
- **Physics:** collision and ragdoll definition, which the Jolt backend turns into bodies and constraints.
- **Clothing:** region map, surface proxy, fit data.
- **Rendering:** LOD meshes and morph targets.

Surface proxy tiers mirror the LOD scheme: capsules for crowds, the proxy mesh near the player or during interactions. Proxy data is rebuilt when the body changes, not every frame.

### Adapting animation to different bodies

Stride and root motion scale with leg length. IK corrects hand and foot targets a differently proportioned body cannot reach. Retargeting goes through the canonical skeleton, with proportion data in the retarget map. Mass and inertia derive from body volume.

## 6. Selecting and replacing bodies

- A package declares which race, sex, or creature type it replaces, with a priority.
- Each character stores a body definition ID and slider values (a few bytes), never geometry.
- If two packages claim the same target, the highest priority wins and the conflict is written to a log.
- If a package fails validation, the engine falls back to the default body and logs why. A bad mod must not crash a load or break a save.
- A **legacy adapter** wraps an original game body as a package and derives what it lacks, so existing content and the original clothing system keep working.
- Per-game quirks (Morrowind's assembled body parts, later games' unified meshes) live in game-specific loaders that feed the generic body model.

### Validator

A command-line tool plus an in-engine report. Checks:

- Skin weights sum correctly, bones exist, no weights reference unmapped bones
- Bounds and proportions within the range the retargeting can handle
- Every triangle belongs to a region, with no gaps
- Collision shapes cover the skeleton, masses are positive, joint limits agree with the rest pose
- Morph targets reference valid vertices, and extreme slider values do not invert triangles
- Contract version is supported

### Versioning

The contract has a version number. Additive changes stay compatible and breaking changes bump the major version. A conformance suite of reference packages and queries must keep passing on every contract change.

## 7. Layered clothing

The layered system runs alongside the original system, which is kept unchanged.

### Garment model

Each garment is its own object, not part of the character mesh:

- **Slot and layer:** what it is and where it sits in the stack
- **Coverage regions:** which body regions it covers, used for hiding body geometry and resolving conflicts
- **Attachment type:** skinned, skinned with secondary motion, full cloth, or rigidly attached to a bone
- **States:** buttoned or open, sleeves up or down, tucked or untucked, hood up or down
- **Fit data:** how it conforms to a given body
- **Material and condition:** base material plus dirt, wetness, and damage layers

### Layers without poke-through

- Each layer sits slightly outside the one beneath, using a per-garment thickness and the body surface proxy.
- Body regions fully covered by a garment are masked, which avoids z-fighting and saves pixel work.
- Conforming uses authored fit data first, with automatic conforming (cage or radial-basis deformation) as a fallback, plus a penetration clean-up pass with limits.
- Hair, hats, hoods, and masks get explicit coverage and compatibility rules.

### Legacy items

An original item is wrapped as a single-layer garment in its traditional slots. By default a legacy item blocks layered garments from the regions it covers (strict exclusion). Partial overlap needs coverage data the legacy mesh does not have, so it is a later decision (section 11).

### Saves and rendering

Equipped items save as garment IDs and states. Near characters merge static layers into one skinned mesh with a material atlas. Crowds bake the whole outfit once and instance it. Dirt, wetness, and damage are shader parameters or masks, not separate textures per combination.

## 8. Body physics

### The boundary

Soft-tissue and cloth physics is a **cosmetic layer**:

- It is driven by animation and gameplay, and never pushes back on the skeleton, capsules, or AI.
- It runs outside the deterministic gameplay simulation, in its own world or solver.
- Gameplay queries see the rest-shape body.
- It can be dropped when frame time is tight.

### Techniques

1. **Spring bones:** extra bones driven by spring-damper dynamics. Cheap and robust. Good for tails, hair, loose flesh.
2. **Bone-driven blend shapes:** bone velocity and acceleration drive morph weights through a small spring model. GPU-friendly and near-constant cost.
3. **Cloth:** position-based dynamics for loose garments, colliding against rest-shape capsules or the proxy.
4. **Soft-body regions:** XPBD-style simulation with volume and distance constraints on a coarse mesh, anchored to the skeleton. Near-tier only. **Verify** whether Jolt's soft-body support suits this and can run in a separate cosmetic system, otherwise use a dedicated solver.

### Recommended approach by scenario

| Scenario | Approach |
| --- | --- |
| Clothed, far or crowd | None (rest shape) |
| Clothed, mid | Spring bones and blend-shape drive on the garment |
| Clothed, near | Loose garments: cloth against the rest-shape body. Tight garments: blend shapes and spring bones driven from fit data |
| Unclothed, moving around | Bone-driven blend shapes plus spring-bone follow-through |
| Unclothed, in a scene with contact | Soft-body on active regions only, with one-way contact hints from the alignment solver |

For clothed characters, the body underneath is never simulated, because the visible surface is the garment.

### Soft-body tier in scenes

- Only regions near contact or large motion run the solver. The rest stay on the blend-shape tier, which caps cost.
- A coarse mesh of a few hundred particles per region is a reasonable starting point. The visible mesh is embedded in the simulation mesh, or the solver's displacement is applied as a delta over the skinned mesh, with normals recomputed.
- The alignment solver passes contact hints (surface point, depth, direction) into the layer. The layer never pushes the skeleton.
- Each body's active regions collide against the partner's rigid proxy, not the partner's deformed surface.

### Tier handoff

When a scene starts and the soft-body tier takes over, any difference between tiers looks like a pop. Two rules:

1. Initialize the solver from the current displacement and velocity of the cheaper tier, and blend the outputs over a few frames.
2. Calibrate the cheap tier against the expensive one: run the solver offline over a standard set of test motions and fit the blend-shape and spring parameters to reproduce its average behavior.

### Per-region profiles

The soft-tissue profile in the body package defines regions, mass, stiffness, damping, maximum displacement, volume preservation, and coupling to the driving bone. Parameters scale with body sliders, within defined limits, so a new body gets plausible dynamics. Defaults are derived when a package does not supply them.

### Stability

- Reset state on teleport, cell change, and animation snaps.
- Clamp displacement, velocity, and force, and substep at a fixed rate independent of frame rate.
- Cap forces from collisions and animation pops.
- Keep the layer running through ragdoll, fed from the simulated skeleton.
- Handle large time steps by clamping and resetting.

### Settings

Global intensity, an off switch, and per-character scaling. The whole layer is optional, and garments and bodies must look acceptable with it off.

## 9. Adult content gating

The unclothed-physics and scene-contact tiers are intended for adult characters only. The restriction is built into the engine, not left to mods:

- The body package metadata carries an **age class**. The unclothed-physics and scene-contact tiers refuse to run for any actor not marked adult.
- The feature set is **off by default**, behind an explicit setting.
- The feature set is modular, so it can be removed from builds and distributions where it is not wanted.
- Validators reject a package that enables these tiers without an adult age class.

## 10. Compatibility, LOD, and cost

- **Legacy first:** the original body and clothing systems keep working through the legacy adapter. Settings off means vanilla behavior.
- **Saves:** store body definition IDs, slider values, garment IDs, and states. Do not change how existing records are stored.
- **Population variety:** seeded resident NPCs generate body parameters from the same seed, so towns get varied builds with almost no storage.
- **Morph application:** bake each unique body once and cache it, with GPU morphing for the flexible case. Generate mesh LODs after morphing, and recompute normals and tangents.
- **Streaming:** baked bodies, garments, and proxies are streamable assets under the existing memory budgets.
- **Importers:** reading community body and physics formats is a large adoption lever. **Verify** the formats before designing around them, and check licenses before bundling any asset. Bundled content must be original or clearly licensed.
- **Per-tier budgets:** each tier has a recorded per-character cost in the benchmark harness, and the layer drops to a cheaper tier when frame time is tight.

## 11. Open decisions

Each is settled in a numbered decision record before the related milestone starts.

1. **Surface query API scope:** serve only animation and alignment at first, or also replace the collision shapes used by physics and combat hit detection? Sharing one source is cleaner but ties physics determinism to body changes.
2. **Package form:** a self-contained archive (easy to distribute and hash), a folder of files (easy to edit and diff), or both, with the folder as the source form and the archive as the packed form.
3. **Soft-tissue skeleton:** expose extra physics bones in the skeleton (simple, but requires changes to meshes and retargeting) or keep the skeleton unchanged and deform only through GPU morphs.
4. **Soft-body morph reuse:** reuse the blend-shape tier's morph definitions (consistent appearance, simple calibration) or use a separate simulation mesh with a mapping layer (better solver behavior, more to maintain).
5. **Legacy and layered overlap:** strict exclusion by region, or partial overlap, which needs coverage data for legacy meshes.
6. **Cosmetic world:** a separate Jolt world, or a dedicated solver outside Jolt.

## 12. Testing

- **Determinism:** same package and parameters give identical derived data. Same-platform bit-exact, cross-platform within tolerance, consistent with the animation guide.
- **Property tests:** morph then measure, and check limb lengths, volumes, and symmetry within tolerance. Energy does not grow without bound. Displacement stays within limits.
- **Penetration metrics:** clothing-through-body and hand-through-body counts across a sweep of body types.
- **Stress motions:** teleport, spin, fall, and ragdoll transitions, with maximum displacement and settling time recorded.
- **Conformance suite:** reference packages, including a non-humanoid, that every contract change must pass.
- **Allocation rule:** no heap allocation in the per-frame update, with a counter in tests that also hooks Jolt's allocator.
- **Visual review:** a debug view showing proxies, capsules, regions, contact points, and simulation regions over the rendered mesh, plus a repeatable test scene with recorded clips. Fit and motion quality need human judgment.

## 13. Workflow

Use the full loop (spec, independent test-writer, implementer who may not edit tests, fresh reviewer) for the body data model, derivation, morph application, the solvers, and anything with determinism or stability risk. The lighter loop applies only to tasks outside those areas that the owner marks as lighter, such as editor UI and authoring tools. Every change that touches both uses the full loop.

## 14. Milestones

Each milestone has a spec in `docs/tasks`, tests written from the spec, and a changelog row.

### B1. Body contract

- [ ] Package format in canonical JSON, with required, optional, and derived tiers
- [ ] Validator (command line plus report)
- [ ] Legacy adapter for one reference body
- [ ] Conformance suite with a humanoid and a non-humanoid fixture

### B2. Derivation

- [ ] CPU reference derivation of capsules, measurements, regions, and physics profile
- [ ] Disk cache keyed by package hash and contract version
- [ ] Determinism tests

### B3. Morphs and proportions

- [ ] Slider model and CPU reference morph application
- [ ] Skeleton proportion adaptation and automatic physics rig generation
- [ ] Property tests for measurements after morphing

### B4. Surface query API

- [ ] Closest point, normal, weights, and ray queries at each proxy tier
- [ ] Integration with the IK and alignment solver
- [ ] Decision 1 recorded

### B5. Selection and replacement

- [ ] Declaration, priority, and conflict logging
- [ ] Fallback on validation failure
- [ ] Save format for body ID and sliders

### B6. Garments

- [ ] Garment format, slot and layer model, legacy wrapper
- [ ] Layer resolution, coverage, and fit with offsets
- [ ] State system and condition layers

### B7. Cosmetic physics baseline

- [ ] Physics profile in the body contract
- [ ] Spring-bone solver with fixed-step substeps and stability tests
- [ ] Bone-driven blend-shape layer
- [ ] Reset, clamping, and ragdoll handoff

### B8. Clothed physics

- [ ] Tight-fit deformation from garment fit data
- [ ] Cloth tier against rest-shape colliders

### B9. Scene tier

- [ ] Soft-body solver for active regions
- [ ] Tier handoff and calibration against the cheap tier
- [ ] Contact hints from the alignment solver
- [ ] Adult gating enforced and tested

### B10. Render paths, tools, importers

- [ ] Baked and merged render paths with caching
- [ ] Authoring tools, debug view, and importers
- [ ] In-engine customization UI

## 15. Guardrails

- Never invent an engine fact. Cite a source or ask.
- No game assets in the repository. Bundled content is original or clearly licensed.
- Cosmetic physics never writes to the skeleton, collision, AI, or saves.
- Adult gating is enforced in code and in validators, not left to settings alone.
- Anything marked **verify** is checked against source or documentation before it is relied on.
