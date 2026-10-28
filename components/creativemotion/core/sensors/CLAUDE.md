# core/sensors — balance senses

Depends on `core/math`, `core/rig`, `core/motor` and `physics/interface` only. Spec: `docs/tasks/0017` (stage M5a).

## Rules
- `BalanceSensor` holds body handles and buffers sized at creation; `update(world)` reads the bodies and casts the foot rays and allocates nothing (`opas_alloc_tests`). It never stores a world or ragdoll pointer.
- Feet are rig data: the profile's `feet` list (profile version 2). A box foot tests its four corners lowest in the world (any orientation: rolled, upside down); a capsule foot tests the points under its two end centres, a sphere foot the point under its centre.
- `create` checks the ragdoll was built from this rig (same skeleton, each body on the same bone), since masses and feet are read from the rig by index.
- Foot contact is by rays (toddhow's choice): a 3 cm ray below each foot point, started 1 cm above it inside the foot (which it ignores); contact if the surface is within 2 cm. Named constants in `BalanceSensor.cpp`.
- The capture point is the linear inverted pendulum's, `c + v·sqrt(h/g)` (Pratt et al. 2006), with `h` above the mean contact surface. The margin is the signed distance to the support hull, negative inside, +infinity without support.
- The sensor reads state only, never dt, so its output is independent of the step rate (tested).
- A reading is `valid` only if every body was read (a stale handle makes it invalid, with no support); non-finite foot points are never contacts, and a non-finite capture point has margin +infinity. Behaviors must check `valid` and treat +infinity as "not supported".
- `centreOfMass` (CentreOfMass.hpp) is the one definition, also used by the viewer on recordings.

## Pitfalls
- `convexHull` sorts its input in place and needs `hull` capacity `points.size() + 1` to stay allocation-free.
- A body's position is its centre of mass only because our shapes are centred (the Jolt backend's capsule is a `RotatedTranslatedShape` with zero translation); an off-centre shape would need the centre of mass from the backend.
- Rays ignore only the foot they start in; a ray can hit the other foot or a shin if they are below it.
- A sphere foot gives one contact point and a capsule foot at most a line, so a rig whose feet are all spheres or capsules has a support area with no inside: the margin is never negative and balance (M5b) would never read as supported. Use box feet until the spec says otherwise.
