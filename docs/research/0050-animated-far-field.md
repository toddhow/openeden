# Research 0050: animated far field (static LOD objects, frozen distant creatures)

Status: DRAFT, 2026-11-09. Docs only; nothing built, run or measured. Task: `docs/tasks/0096-animated-far-field.md`.
Origin: toddhow: "Objects in the distance shouldn't be static. For instance, the moving effect on the fence doesn't play in the LOD object. Also creatures in the distance don't move." Screenshot: the Ald-ruhn city wall seen from below, translucent blue-grey wall panels between pillars; animated in vanilla, static in the distance.

Marking: facts are cited as file and function at the current tree. **RECALLED-VERIFY** marks anything not read this session. No game data was opened, so which models carry which controllers is NOT known here; stage a of the task measures it.

## 1. Two separate problems

1. **Far objects are static.** Both far paths (baked clusters and live object-paging chunks) draw objects without running any controller.
2. **Far creatures do not move.** An actor farther than `[Game] actors processing range` from the player is skipped by the mechanics loop, so its animation is never advanced. It is not a rendering-cost cut; it is the same range that gates AI.

## 2. How far objects are drawn today

Three layers (task 0059, `apps/openmw/mwrender/CLAUDE.md` "World LOD object clusters"):

- **Live cells** (active grid, `Constants::CellGridRadius = 1`, `components/misc/constants.hpp`, i.e. 3x3 cells): real `MWAnim::Object`s. `Scene::update` (`apps/openmw/mwrender/scene.cpp`) runs one update operation per object cell and actor cell; an object's `autoPlay` group is advanced by `MWAnim::Object::update` (`components/mwanimation/object.hpp`: `autoPlay.update(dt)`; controllers with `hints.autoPlay` are added to it). This is where the wall's effect plays when near.
- **Object paging chunks** (`DistantObjects`, `apps/openmw/mwrender/distantobjects.cpp`, `buildChunk`): one `CullGroup` per cell, each static, activator or door a `MatrixTransform` over a `DistanceLOD` over the model node read by `context.readNode`. **Nothing here links controllers**: the file says so for particles ("Nothing here runs the particles' controllers (MWAnim::create does for placed objects)", above `HideParticles`). The prototype is a shared read of the model; there is no `MWAnim::Object`, no `autoPlay`, no update. So **every object in a paging chunk is a frozen rest pose**, including ones with a UV, flip or alpha controller. (Whether the VSG adapter bakes any time-driven state into the node itself, so that something still moves, is RECALLED-VERIFY: `components/vsgadapters/nif/nif.cpp` creates controllers as separate objects, `handleUVController` ~line 1397, `handleGeometryControllers` ~1455, `handleFlipController` ~805, so I expect not.)
- **Baked clusters** (`components/lod/nifflatten.cpp`, `clusterbuild.cpp`, `apps/openmw/mwrender/lodclusters.cpp`): merged geometry per cell, texture + alpha-test flag + cutoff per piece (`SourcePiece`). No time, no per-piece parameters; the shader only knows the LOD fade (`LOD_FADE`, `dither8` in `default/main.frag`).

## 3. What the baker keeps and drops (nifflatten.cpp / clusterbuild.cpp)

`NifFlattener::visit` (nifflatten.cpp, around line 136):

| Feature | Handling | Citation |
| --- | --- | --- |
| Hidden node, collision root, avoid node, "Bounding Box" | skipped | `visit`, first lines |
| Any **node or geometry with `mController` set** (includes `NiUVController` and `NiGeomMorpherController` on geometry, keyframe controllers on nodes) | `mTraits.mAnimated = true`, model excluded from clusters (`ExcludeReason::Animated`) | `visit`, `classifyObject` (clusterbuild.cpp ~303) |
| `NiBSAnimationNode` | animated, excluded | `visit` |
| `NiBillboardNode` | animated, excluded | `visit` |
| Particles (`NiParticles`, `NiParticleSystem`, `NiBSParticleNode`) | excluded | `visit` |
| Skinned geometry | excluded | `addGeometry` |
| `NiSwitchNode` | only the initial child | `visit` |
| `NiLODNode` | only the first child | `visit` |
| Alpha blend (not tested) | cutout if ordinary src/inv-src blend, textured, material alpha >= 0.99, not translucent-looking by name; else excluded (`mAlphaBlended`) | `addGeometry`, `Lod::textureLooksTranslucent` |
| Lights | no case in `visit`: not carried into clusters (RECALLED-VERIFY) | `visit` |
| **Controllers on properties** (`NiFlipController` on `NiTexturingProperty`; `NiAlphaController` / `NiMaterialColorController` on `NiMaterialProperty`) | **not checked**: `visit` tests `node.mController` only; `applyProperty` reads material alpha, alpha property and texture name, never a property's `mController` | `visit`, `applyProperty`; the engine itself reads them at `nif.cpp` ~1001 (`searchController<NiFlipController>(texprop.mController, ...)`) and ~1100 (`NiAlphaController`) |

Consequence, **two distinct ways a wall panel ends up static**, which cannot be told apart without the data:

- (i) the effect is a `NiUVController` on the geometry (scrolling water-like texture) -> detected -> model excluded from clusters -> drawn by the live paging chunk -> frozen (section 2).
- (ii) the effect is a property-level controller (flip texture, alpha pulse, material colour) -> **not detected** -> the model is flattened into the cluster as a static piece.

Translucent blended panels are excluded from clusters as AlphaBlended when the material alpha is below 0.99 or the texture name looks translucent; otherwise they are baked as cutouts. The screenshot's translucent panels therefore most likely come from the paging chunk, with the pillars from the cluster. **RECALLED-VERIFY.**

Which vanilla meshes: I did not verify any name. I recall the Ald-ruhn wall using `ex_ald_ruhn_wall...` style names and Ghostgate's barrier being an effect, but the survey tool (task stage a) must list them by controller kind from the user's own data. Other candidates to check (all RECALLED-VERIFY): banners and flags, braziers and torches (particles, light flicker), waterwheels and windmills (keyframe), sails and ropes, kelp and grass (Morrowind has few sway models), swamp trees (static), lava and water-like panels (UV controllers).

## 4. Why distant creatures and NPCs do not move

Cited, `apps/openmw/mwmechanics/actors.cpp`, `Actors::update`:

- `actorsProcessingRange = Settings::game().mActorsProcessingRange` (default 7168, sanitised to 3584..7168: `components/settings/categories/game.hpp`).
- Around line 1696-1710, in the loop that decides which actors animate: `inRange = isPlayer || dist <= actorsProcessingRange || alwaysActive`. If `!inRange`: `world->setActorActive(ptr, false)` (which only calls `MWPhysics::Actor::setActive`, `worldimp.cpp` `setActorActive`) and **`continue`**: the actor's `CharacterController` is not updated that frame, so its animation is not advanced and the pose stays as last drawn. This is case (a): outside the processing range -> frozen.
- Around lines 1576-1621 AI, combat and magic are likewise gated by `inProcessingRange`. What `updateActor` still does out of range is RECALLED-VERIFY (stats, not animation).
- Case (c): actors exist only in loaded cells. The active grid is 3x3 cells; a cell is 8192 units, so an actor in a neighbouring cell can be up to about 12,000 units from the player: loaded, drawn, but beyond 7168, hence frozen. **Past the active grid there are no actors at all**: `DistantObjects` takes statics, activators and doors (`getModel`, `isDistantObjectType`; that creatures are excluded is RECALLED-VERIFY).
- Case (b), animation skipped when not visible: the same loop notes "vsgopenmw no longer sets node masks with it", so I found no visibility-based skipping; culling is VSG's. RECALLED-VERIFY for `Anim::Cull*`.

Root cause: (a), with (c) deciding what exists to be seen. The processing range (7168) is smaller than the active grid's reach (about 12,000), and animation shares the AI gate.

## 5. Design options

### Far objects

- **A. Animated models live beyond range, with animation LOD.** The paging chunks need controllers linked (new; `buildChunk` has none) and a per-chunk update at a reduced rate by distance. UV and flip controllers write per-state-group descriptors or material data, so each animated object needs its own StateGroup copy and loses merging (estimate +1 draw per animated object): fine for hundreds, bad for thousands (survey decides). Needs the flattener to detect property controllers (section 3 (ii)) so those models are excluded.
- **B. Shader-driven effects in clusters.** Store per piece group an effect id and parameters (UV scroll velocity, alpha pulse amplitude, frequency and phase, flip strip rate, sway amplitude); a time uniform from game time, never wall clock. This **changes the frozen layout version 1** (`components/lod/objectcluster.*`), so it needs **layout 2**: the reader accepts 1 (all effects none) and 2, the baker writes 2, `bakerVersion` and the digest change, and a re-bake of `objects.edenpak` (`openmw --bake-lod-objects`) is required to gain effects (version 1 packs keep working unchanged, so no forced re-bake). Freezing a new layout is a decision for toddhow (decision 0010, task 0059). Cost estimate: one small parameter block per piece group, one uniform, a few ALU per fragment in the cluster pipeline only; GPU cost negligible (estimate under 0.05 ms). Exact for linear UV scroll; non-linear keyframed curves are approximated by their mean rate.
- **C. Hybrid (recommended):** B for parametric kinds (UV scroll, alpha pulse, flip, sway keyed by position and a per-piece amplitude), A for the few keyframe, morph, particle and light ones (windmills, waterwheels, braziers), bounded by a count and distance budget. Sway reads wind from the weather plan (`Render::WeatherState`, `docs/tasks/0094-weather-system.md`) when present and a fixed default otherwise, so it works without that plan.

### Far creatures

- **D. Decouple the animation range from the AI range (recommended).** Behind a setting off by default (`[Game] far animation range`, 0 = off = code path identical), actors beyond `actorsProcessingRange` but inside the animation range or loaded grid get an animation-only tier: idle loops only, no AI, movement, physics, stats or scripts, a lower update rate by distance (time stepped by accumulated dt so loops stay continuous), skinning skipped when culled. Text keys must be ignored in this tier (sounds, hits, loot, footsteps). Cost estimate: tens of microseconds per actor per update; a few dozen creatures in the 3x3 ring means estimate 0.1 to 0.5 ms CPU; measure.
- **E. Far actor impostors with baked animation.** Heavy (atlas bake per creature, memory, new pipeline) and there are no actors past the active grid anyway. Not recommended.
- **F. Larger live cell radius.** `CellGridRadius` is a constant; raising it loads more cells, physics, scripts and AI and changes behaviour. Very expensive. Not recommended.

Showing creatures beyond the active grid at all would need a new far-actor proxy: out of scope (open question 4).

## 6. Risks

- **Determinism and saves:** far animation derives from game or simulation time, never wall time; nothing is saved; the animation-only tier must not touch AI, physics, stats, script or sound paths. A mod script that reads animation state of a far actor would see it change; what Lua and mwscript expose is RECALLED-VERIFY.
- **Hidden gameplay hooks:** `CharacterController` text keys trigger sounds, hits and loot; the tier suppresses them.
- **Mods:** unusual controllers; a count budget with fall back to static.
- **Layout 2** is a project decision as well as code.
- **Perf:** animated chunk objects lose merging (A); keep a budget.
- Interplay: weather wind (0094) only for sway amplitude; puddles (0095) none.

## 7. Recommendation and estimates

1. Measure first (stage a): the section 3 claims, what the user's data has, how many pieces and objects are involved. Nothing below is sized until then.
2. Make the flattener see property controllers, so every effect model is excluded or parameterised rather than baked static (changes the bake, needs a re-bake).
3. B for UV, alpha and flip; then sway; A for the rest; D for creatures. Each off by default.

Estimates (mine, plus or minus 50%): stage a 2 days, b 5, c 4, d 5, e 5, f 3.

## 8. RECALLED-VERIFY list

- Which models have which controller kinds and counts; exact names of the Ald-ruhn wall panels and Ghostgate barrier.
- Whether the wall panels are excluded as AlphaBlended (paging) or baked (property controller).
- Whether the nif adapter puts any animated state into the shared prototype node.
- The exact scope of the `inRange` gate in `Actors::update` and the 0.9 x range fade near line 1268.
- Whether `DistantObjects` ever includes creatures (I believe not).
- Any `Anim::Cull*` visibility-based animation skipping.
- Which time source the cluster and sky shaders use.
- What Lua and mwscript expose of a far actor's animation state.
