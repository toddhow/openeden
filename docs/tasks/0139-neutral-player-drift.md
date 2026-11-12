# Task 0139: the player drift in a neutral Skyrim exterior (found by experiment)

Reported in task 0138: after arriving at Whiterun's gate the player "drifted" 1,271 units in y in 12 s and fell 200, and
after a `tp` 1,000 units in x. Guessed to be slope physics; not investigated. This task found the cause.

## Cause (proved)

- A per-frame trace (`tools/neutral_session_check`, scenarios `doorsdrift` and `doorssweep`, Lua trace of position,
  ground state and controls) showed the jumps are single-frame position changes with no input (`ctl=0`), not sliding.
- A temporary log in `MovementSolver::move` showed the solver itself moves the player only vertically; the start
  position of the NEXT step had already changed, and `World::moveObject(..., movePhysics=false)` (the physics sync) carried
  it. The only code that moves an actor sideways between steps is `MovementSolver::unstuck` (`mtphysics.cpp` PreStep).
- A temporary log in `unstuck` named the body it was pushing the player out of every time: a Jolt BOX of world bounds
  18279..21584 x -8663..-5363 x -5288..-1981, base `skyrimse:b1108346`, editor id `WRMainRoadPlainsRiver01ACT`, an
  activator whose model (`wrmainroadplainsriver01.nif`) is a river effect with no collidable triangles. Its own-frame
  OBND is -2524,-2009,-1768 .. 781,1292,1539 (a real 3,300-unit cube, not a rotation fault). The penetration depths were
  -1068.7 (the `tp` case, player ends at x = 18250.0, the box face minus the capsule) and -1264.1 (the door-back case, y =
  -8692.6): exactly the drift of the report. Teleports with `onGround` also landed at z = -1980, the roof of that box.
- Why a box: `MWClass::Neutral::insertObject` (N3a-3/N3a-4b) gives an object whose model yields no collision mesh the box of
  its bounds. The shape reader returned null both for "read fine, nothing collidable" (effects, smoke, fire, water,
  markers: 523 of 5,625 objects in the Whiterun window, 0 failed, 0 over the cap) and for "failed".
- Experiment: with the box fallback skipped (env switch, temporary) the player stays at 19315,-7433,-3630 after the `tp`
  and at 19516,-7428,-3643 after the door back (12 s, no movement), and the `unstuck` log shows no depth above 10.

## Fix

`SkyrimShape::modelReadWithNoCollision` / `noteModelReadWithNoCollision` (the reader notes a model that read without a
problem and has no collidable triangles, not over the triangle cap) and the pure `SkyrimShape::standInBoxWanted`;
`Neutral::insertObject` keeps the box only for a failed read, a model over the cap, a Jolt refusal or no model.

## Not changed, for toddhow

- `unstuck` has no limit on how far it moves an actor (1,264 units in one frame here), and it runs on a movement of
  2.7e-5 that survives a teleport (`length2(mMovement) == 0` is the only early out). Inherited from OpenMW's Jolt port.
- Objects that lost their stand-in box (list in the changelog row): effects, markers, water volumes; and a few that look
  solid: `StockadeBarricade01Activator` (18 placements), `CityBannerWhiterun01` (12), `WRJovaskrBanner02` (7),
  `HangingRabbit01` (4), `HangingPheasant02` (3), `DummyPotion` (2). Skyrim collides through `bhk` shapes, which are not read.
