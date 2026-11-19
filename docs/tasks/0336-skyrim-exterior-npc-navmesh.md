# Task 0336: neutral NPCs walking through Skyrim exteriors on the navmesh

Branch `exteriornav-0336` from local `main` `3190b2b6ca`. Same hand wrote tests and code, tests first and run red (counts
below); no fresh reviewer; Windows only; local commits, not pushed. Behind two NEW off-by-default settings
`[Game] neutral actor walk through` and `[Game] neutral actor long paths` (both need `[Game] neutral actors`; both in the
test profile). Files: `apps/openmw/mwworld/neutralnavigation.*`, `apps/openmw/mwphysics/{neutralmover,neutralpathfollower,
physicssystem}.*`, `apps/openmw/mwworld/{scene,worldimp}.cpp`, `tools/skyexterior_nav_check/`.

## 1. The premise was wrong: the exterior navmesh for an NPC agent exists

Tasks 0134, 0305/0319 (Durak), 0332 (Erandur) and 0331 said "the exterior navmesh for an NPC-sized agent does not exist".
Measured (Riverwood, `@Tamriel,21941,-44792`, `GuardRiverwoodImperial01` sent 6,300 units by the pack's `actor test goto`,
`OPENEDEN_NEUTRAL_ACTOR_TRACE=1`; new trace line `NACTNAV` with the raw `DetourNavigator::Status` and the navigator's pending
jobs, and for a blocked step `refusal wall|slope|no-floor` with the record of the body):

| What was seen | Cause (measured) |
| --- | --- |
| first 8 to 11 asks `NotReady` ("polygon for start position is not found", jobs 625 to 1,374), then a 12 waypoint **Success** path of 6,500 units | the r22 cylinder agent has its own navmesh, built tile by tile on the worker: about 4.5 s from the goal to the first path (jobs 1,300). Not a missing navmesh. |
| `blocked` 127 units short of waypoint 2, `refusal wall body b756327` | `TreeSwordFernCluster05`, a fern. The navigator leaves out Flora, Tree, Item, Light kinds and the stand-in boxes (task 0134) but the mover's Jolt sweeps still hit their bodies. A pine (`TreePineForest03`) did the same to guards 02 and 03 at their camp. |
| (after that fix) `blocked ... refusal slope normal.z 0.628`, then 0.467, 0.43, 0.235 | the mover refuses ground whose probed normal z is under 0.64; recast's voxels (6.8 units, climb 34) pass lumpy rock faces (`RockS03`, `FirewoodPileLarge01`) that are steeper than that. |
| goal 13,000 units away: `polygon for end position is not found`, 20 s of `NotReady`, `NoPath` | the navmesh is built round the player (the loaded grid); a goal beyond it has no polygon. This is Erandur's 11,000 unit goal. |
| with `[Game] neutral havok collision` the same guard rose onto a 40 unit object and was `blocked edge ... no-floor` | a different collision body set (not changed here; the quests' profile does not use it) |

So the root cause is one thing seen three ways: **the navigator and the mover do not see the same world**. The navmesh has
no plants, trees, lights, outdoor items or stand-in boxes and passes slopes the mover refuses, and it exists only round the
player.

## 2. What is built (the mover agrees with the navmesh; the path may be asked in parts)

- `neutral actor walk through` (`MWWorld::neutralActorWalksThrough`, `PhysicsSystem::noteNeutralWalkThrough`, `JoltSweeper`
  `walkThrough` set): a walking neutral actor's sweeps and probes leave out the bodies the navigator leaves out (the set is
  made in `Scene::addObject` where the navigator decision is made, erased in `PhysicsSystem::remove`). The player, rays and
  every other body still collide with them. **Decision 1: ALL trees pass, big ones too** (the navmesh has no trees at all, so
  a path runs through a trunk; the alternative is trunk-only collision from `bhk` data plus trunks in the navmesh).
  While a path is followed the floor limit is `kPathMinFloorNormalZ` 0.35 (about 70 degrees) instead of 0.64 (the idea of
  task 0332's `pathStepDown`: the navmesh vouches for the ground); a 0.2 face is still refused.
- `neutral actor long paths` (`Follower::setLongPaths`, the path finder lambda in `worldimp.cpp`): when the navigator says
  the goal's polygon is missing, the lambda asks for the farthest point of the line to the goal that has one (fractions
  0.85 .. 0.12, `longPathPulls()`) and answers `Partial`; the follower walks a partial path that ends at least 200 units
  nearer to the goal, then asks again from its end (the navmesh grew while it walked), at most 64 parts, and ends `NoPath`
  when a part gets it no nearer (a cliff, the sea).
- Trace aids (no setting): `NACTNAV raw status ...`, `NACTWALK ... refusal ... body ...`, `NeutralMover::Result::mRefusal`.

## 3. Tests (written first, run red)

- `testneutralwalkthrough.cpp` (`NeutralWalkThroughRule` 5, `NeutralLongPathPulls` 1, `NeutralWalkThrough` 4): **5 of the
  first 11 failed** against a rule that always said no and a sweeper that ignored the set (the other 6 pin absence, the
  unchanged collision, or the instrumentation that was written before the test: `aBlockedStepNamesTheWallItHit` passed at
  once). The pull-fraction test was written after the code.
- `testneutralpathlong.cpp` (`NeutralPathLong`, 6): 2 of 6 failed against a follower that ignored the option (4 pin absence;
  `theNumberOfPartsIsBounded` passed vacuously against the stub because it ended `NoPath` at once).
- `testneutralpathslope.cpp` (`NeutralPathSlope`, 6): 2 of 6 failed against a stub (a first version of the fake sweeper
  climbed, so its "refused" tests were fixed before the red run counted).
- No existing test edited.

## 4. In game (`tools/skyexterior_nav_check/run.sh`; quiet window, volume 0, fixed frame time, `actor packages` off so only
the test goal drives; Lua `scripts/nav336`)

- Default settings: the guard stops at the fern after 3,900 units (`blocked`, `refusal wall body b756327`).
- Walk through only: 6,300 units over the road, then `blocked ... refusal slope normal.z 0.628` (a rock).
- Both settings, goal 13,000 units east (`GuardRiverwoodImperial01 -> (32000,-44000,0)`): partial path of 12 waypoints, the
  guard walks from (18592,-47104) to (24932,-45873), **about 6,500 units in 92 simulation seconds (75 to 82 units/s)**, and
  is stopped by a rock face of normal z 0.235 (a real cliff). A goal 3,300 units away (`GuardRiverwoodImperial02 ->
  (22400,-41000)`) walked 6,300 units and stopped at a rock slope 0.405 (before the path floor limit was lowered; not rerun).
- **Three runs in parallel give identical lines** (goal, waypoint and walk samples of the guard: md5 `27578e831cf3`; the
  number of `NotReady` answers before the first path depends on worker timing and is left out of the md5).
- Frame: `I:\Projects\OpenEden-work\followers\nav336\cam2\user\screenshots\screenshot003.png` (viewed: the guard on the
  Riverwood road among ferns, the bridge behind).

## 5. Cost

See section 9 (the task 0323 harness, walk mode, play profile).

## 6. Decisions for toddhow

1. All trees pass for a walking NPC (the navmesh has none); trunk-only collision is the alternative.
2. The path floor limit 0.35 (about 70 degrees) while a path is followed: an estimate from rock faces (0.63, 0.47, 0.43 stopped, 0.235 still refuses).
3. Both settings are off by default; the quests (0305, 0319, 0331, 0332) can switch them on in place of their teleports and direct walks.

## 7. Not done / not run

Durak's Whiterun walk, Erandur's real 11,000 unit escort and Viarmo's Blue Palace walk were NOT re-run (they need the whole
quest chain and a player who walks beside the leader; the mechanism they need is built and shown with a guard). The Linux
gate, clang-format 14, clang-tidy, the Lua tests, a fresh review. Cliff-like rock faces still stop a walk (normals under
0.35); another actor's capsule can stop a walk as before; the 4.5 s first-path wait is the navmesh build of the r22 agent,
not reduced here.
