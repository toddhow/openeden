# anim — the animation runtime (task 0020)

Depends only on `core/math`, `core/rig` and `core/format` (privately `opas::format_json`; `anim/gltf` adds cgltf privately). Never on physics, a game or a framework. Spec: `docs/tasks/0020-c3-animation-runtime.md`; formats decision: `docs/decisions/0005-animation-formats.md`. Full workflow loop, though outside `core/` (toddhow, 2026-10-23).

## Rules
- `anim/clip`: the internal clip model (`Clip`), skeleton profiles (`docs/formats/skeletonprofile.md`) and `ClipBinding`. Every reader produces a `Clip`; every layer above reads only clips and bindings.
- Sampling and root motion allocate nothing; `ClipBinding::create` resolves names and sizes buffers once.
- Clips carry no scale tracks: `math::Transform` has translation and rotation only.
- Quaternions: shortest path before every interpolation, normalized after.
- Time is passed in by the caller (step index × dt); nothing here reads a clock.
- `anim/clip` also holds clip sidecars (`docs/formats/clipsidecar.md`): events per clip beside an animation file.
- `anim/gltf`: the glTF 2.0 reader (`docs/formats/gltfclips.md`). cgltf is private (`opas_cgltf`, compiled once in `src/CgltfImplementation.cpp` without our warnings); `cgltf.h` is included only under `anim/gltf/src`. Readers bake nodes that are not joints into the joints, so nothing above a clip's root rotates.
- `anim/runtime`: players, pose blending, masks, inertialized transitions (C3c), and driven helper bones solver `HelperSolver` with swing-twist decomposition (C6b). Players share bindings through `std::shared_ptr<const ClipBinding>` (one binding serves every character with that skeleton). Buffers and helper rules are resolved and sized at construction; `play`, `advance`, `sample`, `apply`, blending and the inertializer allocate nothing. Fired events point into the binding's clip.
- Rotations are interpolated only through `opas/anim/Interpolate.hpp` (shared slerp), so every layer blends the same way.
- `anim/graph`: the animation graph (C3d, `docs/formats/animgraph.md`): files merge by entity id in load order, never replacing the graph; `GraphInstance` plays one character's graph with a fixed evaluation order per step. Blend samples share one phase and use `advanceClipTime` so their events and root motion match a `ClipPlayer`'s.
- `anim/style`: the style layer (task 0023, C5, `docs/formats/style.md`): `StyleProfile` data model and canonical serializer; `Noise` seeded continuous deterministic noise generator ($C^1$ continuous) and `IdleGenerator` (breathing, sway, micro-fidgets); `StyleModifier` (posture offsets, idle blending, limb-isolated layers: limp, hunch, protect_arm, shiver); `ContextController` (dynamic health, encumbrance, alertness, cold mapping to effective style profiles with smooth transitions). Zero heap allocations on hot paths (`apply`, `update`, `evaluate`); multi-rate invariant (30/60/120 Hz).
