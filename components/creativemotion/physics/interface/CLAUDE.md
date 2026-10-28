# physics/interface — the physics boundary

Headers only. This is what keeps `core` portable to OpenMW or any other host.

## Rules
- Standard-library includes only. No Jolt, OpenMW or rendering types in any signature. `tools/check_jolt_includes.py` enforces the Jolt part in CI.
- Plain value types (`Vec3`, `Quat`, descriptions) use `float` to match the single-precision Jolt build; units are in field names where ambiguous (`massKg`, `stiffnessNmPerRad`).
- Callers hold handles (`BodyId`, `JointId`), never pointers. A handle to a destroyed object stays stale forever (generation counter).
- Changing a signature here changes every backend; update `docs/tasks/0001-m1-physics-interface.md` (or the design doc once exported) in the same change.

## Pitfalls
- `JointMotor` with stiffness and damping both zero means "motor off", not "infinitely weak motor".
- `setJointTarget` is relative to the pose the joint was created in, not to world space.
- Creating or destroying bodies/joints inside a `StepListener` callback is refused (`CalledDuringStep`).
- `setJointPositionSteps` and `setBodyDamping` (task 0018) refuse during a step, unlike the older `setJointMotor` and `setJointTarget`, which drivers call from `onPreStep`. They are for one-off changes such as death, made between steps.
