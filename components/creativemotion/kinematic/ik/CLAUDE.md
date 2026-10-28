# kinematic/ik — inverse kinematics

Depends only on `core/math` (and later `core/rig`). No physics, game or framework types.

## Rules
- Solvers are pure functions of their inputs: deterministic, no allocation, no global state.
- Every degenerate case (target on the root, pole parallel to the target line, straight chain) has a documented, deterministic fallback; property tests cover them.

## Pitfalls
- Precision near a straight limb: derive angles with `atan2` from the solved bend, not `sqrt(1 - cos²)`. The latter put the mid joint 7e-8 rad off a straight line and failed the joint-limit property test.
- Bend 0 means straight. Reach decreases as bend increases, so `minBendRad` sets the maximum reach.
- Finger curl (`FingerCurl.cpp`, A5c) scans for the first touch, then bisects. The scan step count is sized so the tip moves at most half the target's radius per step (capped at 4096); with a fixed 32 steps, the tip stepped over thin targets. It allocates small vectors, which breaks the no-allocation rule above. toddhow approved per-frame allocation for it on 2026-10-17, as for `alignPair` (A4c-2).
