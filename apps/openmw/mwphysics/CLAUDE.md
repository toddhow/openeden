# Physics: `apps/openmw/mwphysics`

Rules and pitfalls for new OpenEden code here. The port itself is inherited from the Jolt branch.

- **Contacts for extensions:** `JoltContactListener` tells a `ContactObserver`, if one is set, about contacts involving an actor or a projectile. It runs on physics threads with the bodies locked, so an observer must only record them. With none set, the listener costs one atomic load.
- **Extensions never change this world:** `getJoltWorld` exists for the read-only `PhysicsServiceImpl` (decision 0003).
- **Actors are kinematic bodies** (`actor.cpp`), and Jolt reports no contact between a kinematic and a static body. Standing on the ground gives no contact event; the actor simulation finds the ground itself.
- **Lengths are Morrowind units,** gravity included (`Constants::UnitsPerMeter`, `physicssystem.cpp`). The extension SDK speaks meters and converts at `PhysicsServiceImpl` and `OMW::ExtensionWorld`.
- **Ragdolls (task 0035, `components/cmphysics/`):** `CMPhysics::HostedJoltWorld` lets CreativeMotion's `PhysicsWorld` interface work on this world (never a second world). Its bodies carry user data 0, because `JoltContactListener` and `getUserPointer` read user data as a `PtrHolder*`; never store an index there. It is not used by the game yet (stage b needs toddhow's answers in the task file). The game's Jolt is v5.5.0 while CreativeMotion's own `JoltWorld` uses a newer commit (5.6.0 dev, `PositionAndVelocity` motors): the adapter has a damping-only workaround for that.
- **Gravity points up:** `PhysicsSystem::PhysicsSystem` sets gravity to +Z. Nothing dynamic is affected today (projectiles use gravity factor 0, actors are kinematic), but a dynamic body with gravity would float up.
