# Physics: `apps/openmw/mwphysics`

Rules and pitfalls for new OpenEden code here. The port itself is inherited from the Jolt branch.

- **Contacts for extensions:** `JoltContactListener` tells a `ContactObserver`, if one is set, about contacts involving an actor or a projectile. It runs on physics threads with the bodies locked, so an observer must only record them. With none set, the listener costs one atomic load.
- **Extensions never change this world:** `getJoltWorld` exists for the read-only `PhysicsServiceImpl` (decision 0003).
- **Actors are kinematic bodies** (`actor.cpp`), and Jolt reports no contact between a kinematic and a static body. Standing on the ground gives no contact event; the actor simulation finds the ground itself.
- **Lengths are Morrowind units,** gravity included (`Constants::UnitsPerMeter`, `physicssystem.cpp`). The extension SDK speaks meters and converts at `PhysicsServiceImpl` and `OMW::ExtensionWorld`.
