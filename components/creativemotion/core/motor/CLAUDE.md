# core/motor — ragdoll building and the motor solver

Depends on `core/math`, `core/rig` and `physics/interface` only (never Jolt). Spec: `docs/tasks/0013`.

## Rules
- A `Ragdoll` holds handles, never pointers into the world; every call takes the world. The host calls `destroyRagdoll` before destroying the world.
- `driveTowardsPose` and `RagdollDriver::onPreStep` must not allocate: buffers (`Ragdoll::poseModel`, the driver's pose) are sized at build/construction. `opas_alloc_tests` enforces it with counting `operator new`.
- Joint targets come from one formula, `jointRotation`: J⁻¹ · A_rest · (A⁻¹ · B) · B_rest⁻¹ · J. Use it for targets and for measuring, so the two always agree.
- Joint frame = Jolt's constraint space: X = twist axis, Y = plane × twist, **Z = plane axis**. Swing about Y is limited by the plane swing range, about Z by the normal swing range (`JointDesc`). This frame and identity = rest pose hold for cone and pyramid joints alike (task 0014), so `jointRotation` needs no change for lopsided limits.
- double (core) ↔ float (physics) conversion happens only in `src/Conversions.hpp`.

## Pitfalls
- M1's interface comment had the frame's Y and Z swapped (Y = plane axis). It is corrected; code written against the old comment builds wrong targets for any swing.
- Stiff, underdamped motors on light limbs keep oscillating at 60 Hz in Jolt (2000 N m/rad with 60 N m s/rad: arms 0.8 rad off after 8 s), while 2000/200, 600/60 or the same gains at 240 Hz settle. Keep damping in proportion when raising stiffness.
- Tracking lags a moving target by about (damping / stiffness) × target speed, because motors aim at a fixed pose with zero target velocity. Lower damping helps at 60–120 Hz but makes 30 Hz oscillate; the sample profile (450/30) is the measured compromise. Feeding the target's velocity to the motor (a later task) removes the lag.
- Jointed neighbours share a collision group with collision off between them; other pairs (an arm and the trunk) still collide, which limits poses. Tests of the target formula turn self-collision off (`disableSelfCollision`).
- `makeLimp` (task 0018) is death: motors off, and with the profile's `death` section fewer joint position steps and more body damping. It does not remove the ragdoll's driver; the host must, because setting joint targets every step keeps a ragdoll awake. A kinematic root is skipped (it has no damping).
