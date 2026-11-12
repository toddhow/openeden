# Task: Neutral actors, milestone M-S3 design and first slice (a physics body for every Skyrim NPC)

Status: **APPROVED** under toddhow's blanket "go with your recommendations" of 2026-11-11 (decision D3 of the implementation plan: option (a) `MWClass::Neutral` plus pack payload, with the (b) additions; decision 0017 option B). Full loop (D13). Branch `skyactors`. Task number chosen as the next free one on 2026-11-12; another branch may have taken 0133 too, renumber at merge.
Design doc section: `docs/decisions/0017-neutral-actors-and-items.md` sections 2, 4, 5 (stage N5-3 row); `docs/design/neutral-world-model.md` N5; `docs/design/skyrim-se-implementation-plan.md` 4.10, 4.11, 4.19, 7 D3.
Goal: (1) write down how a neutral actor becomes a physics actor and a mechanics actor without Morrowind records, in ordered stages up to wander/sandbox AI on navmesh paths; (2) build the first slice: a Jolt capsule for every neutral NPC (height from the race, radius a named constant) that stands on the floor under gravity and that the player collides with, behind the existing `[Game] neutral actors`. No AI, no movement.
Inherited code touched: `apps/openmw/mwphysics/physicssystem.{hpp,cpp}` (new members `addNeutralActor`, `updateNeutralActors`, `getNeutralActorFall`; two lines in `remove` and the destructor's clear), `apps/openmw/mwworld/worldimp.cpp` (`World::doPhysics` calls the update before `stepSimulation`), `apps/openmw/mwclass/neutral.cpp` (`insertActorRendering` also adds the body). Nothing in `components/detournavigator` (a parallel agent works there on branch `skynav`).
Inputs / outputs: SDK `actor_appearance.hpp` 0.3 (`ActorAppearance::heightScale`, a trailing field the core only reads); pack `appearance.cpp` fills it; new `apps/openmw/mwphysics/neutralactor.{hpp,cpp}`; tests `apps/openmw_tests/mwphysics/testneutralactor.cpp` and five `SkyrimAppearanceHeight` tests in `apps/components_tests/skyrimse/testskyrimseappearance.cpp`.
Constraints: main thread only (every new function; none is a callback on a physics thread); no allocation per frame once all actors are grounded (a grounded actor costs one state test; an actor still falling allocates one small vector of `ConstPtr` for the ignore list per frame, bounded by the actor count, only while something falls); deterministic (no clock, no random number, no unordered iteration); off by default behind `[Game] neutral world` and `[Game] neutral actors`.
Acceptance criteria:
- [ ] Tests first, red against stubs, then green: capsule sizes, shape bounds, the fall rule, the pack's height scale.
- [ ] In game (Bannered Mare): NPCs stand on the floor (rest z traced), the player walking into one is stopped (trace), the collision debug view shows the capsules (frame viewed).
- [ ] With the settings off Morrowind is unchanged: `worldobjects_check` dump, fixed-frame Balmora frame hash, C2 player pose trace.
Out of scope: movement of an actor, AI, navmesh agents (task of `skynav`), mechanics hooks, stats, saves, animation selection, creatures, the player as a neutral actor.

## Design: from an inert reference to a physics actor and a mechanics actor

### The facts the design rests on (read in this checkout, branch `skyactors` from `main`)

1. A neutral object is `LiveCellRef<NeutralBase>` of record type `REC_NEUT`, class `MWClass::Neutral` (`apps/openmw/mwclass/neutral.cpp`). `Class::isActor()` is the default `false` (`mwworld/class.hpp`, `virtual bool isActor() const { return false; }`). `Scene::addObject` (`mwworld/scene.cpp`, first `addObject`) calls `ptr.getClass().insertObjectRendering` always, `MechanicsManager::add` only when `useAnim()`, and `insertObject` (physics) only `if (!model.empty())`; `Neutral::getModel` is empty for `ObjectKind::Actor` (`isLiveThing`), so an actor never reached `insertObject` and had no body.
2. `MWPhysics::PhysicsSystem::addActor` builds an `MWPhysics::Actor` and reads `ptr.getClass().getCreatureStats(ptr)` (`physicssystem.cpp`, `addActor`: the water-walk effect check; `actor.cpp` constructor: `getFallHeight()`); `prepareSimulation` reads `getCreatureStats`, `isMobile` and magic effects for every `Actor`. Neutral classes refuse `getCreatureStats` on purpose (task 0131), so the Morrowind actor body cannot be used without faking CreatureStats (decision 0017 option A/C, rejected).
3. `MWPhysics::Object` (`object.cpp`) is a `PtrHolder` with any `PhysicsShapeInstance` and a layer: it needs only a `Ptr`. `Object::commitPositionChange` (called from `PhysicsSystem::updatePtrHolders` inside `stepSimulation`) moves the Jolt body to the Ptr's position. `PhysicsSystem::addBoxObject` is the precedent (a box for a neutral object without a mesh).
4. `Layers::ACTOR` bodies are treated as `MWPhysics::Actor` by `movementsolver.cpp` (the layer test at the top), `projectile.cpp` and `actorconvexcallback.hpp`; a capsule on that layer without an `Actor` behind it would be misread. So the first slice puts the capsule on `Layers::WORLD` (the player's `MovementSolver` collides with it as with furniture).
5. Every navigator site skips a physics object whose shape has `mVisualCollisionType != None`: `scene.cpp` second `addObject`, `scene.cpp` (`removeObject`, `updateObjectScale` paths), `worldimp.cpp` `updateNavigatorObject`, `apps/navmeshtool/worldspacedata.cpp`. An actor's body is marked `Camera` so it never becomes map geometry.
6. `World::doPhysics` (`worldimp.cpp`) is the main-thread place where `PhysicsSystem::stepSimulation` runs; `RenderManager::moveObject` moves the drawn object's transform (`rendermanager.cpp`). `World::moveObject` is NOT used for the settling because its non-player branch calls `mStore.find(refId)` for paging, which a neutral id does not support.
7. `MechanicsManager::add` sends a `Ptr` to `Actors::addActor` only when `getClass().isActor()` (`mwmechanics/mechanicsmanagerimp.cpp` near line 205); `Actors`, `CharacterController`, `AiSequence`, `CreatureStats` and `ContainerStore` are all Morrowind-shaped (decision 0017 section 2.1).
8. The pack already decodes what an actor body needs: `RaceRecord::mHeight[2]` (RACE DATA at 16 and 20) and `ActorNpc::mHeight` (NAM6) in `game_packs/skyrimse/include/game_skyrimse/actors.hpp`; `makeActorAppearance` resolves the traits holder through the template chain.

### What MWClass::Neutral must answer (and what it keeps refusing)

- `isActor()` stays **false** (decision 0017 D3, "revisit after M-S3"). A neutral actor is a *life-bearing neutral object*; the engine reaches its life through new, game-neutral services, never through `CreatureStats`.
- `getCreatureStats`, `getNpcStats`, `getInventoryStore`, `getContainerStore`, `getMovementSettings` keep refusing (task 0131 pinned this). Their neutral equivalents are not `Class` virtuals: movement is a request the pack hands the core (stage S2), actor values stay in the pack.
- What changes in the class: `insertActorRendering` (draw) now also creates the body, and a future `Class::hasLife(ptr)` (default false, true for an actor kind when `[Game] neutral actors`) would let generic code (Lua object API, tooltips, activation prompts) ask "is this a thing with a life" without `isActor()`'s 129 call sites (decision 0017 section 6). Stage S1 does not add it; the first generic consumer (activation of a talker, stage S7) does.

### What the pack supplies

Plain values by value, through interfaces found with `dynamic_cast` on the registered `WorldCellSource` (the 0121 pattern, not new members of the frozen `WorldObject`):
- S1 (built): `ActorAppearance::heightScale` = RACE height for the sex times the NPC's NAM6, clamped to 0.25 to 4.
- S2: a movement description (walk, run and sneak speed; turn rate) from RACE/NPC_ and the actor-value formulas (plan 4.13).
- S3: actor values at their base (health, magicka, stamina from NPC_ DNAM and RACE), kept in the pack's actor table (never in `CreatureStats`).
- S5: the package list (PKID, DPLT) and schedule data for the pack's AI.

### What stays refused or out

Combat, magic, perks, inventory (decision 0017 open question 3: Skyrim inventories live in the pack, never in `ContainerStore`), Morrowind AI (`AiSequence`), `MWRender::Npc`, saves of actor state (N6), creatures and the player as a neutral actor (decision 0017 open questions 4 and 7).

### Ordered stages

| Stage | What | Acceptance |
| --- | --- | --- |
| **S1 (this task)** | The capsule body, `heightScale`, standing on the floor under gravity | the criteria above |
| **S2 movement request** | A core `NeutralActorMover`: per frame the pack (through the hooks contract, S3) sets a desired velocity and heading per actor; the core integrates it against Jolt with a swept capsule (`castShape`, step up `kStepUp`, slide, floor snap). The body becomes a kinematic body on a layer of its own that the player's solver treats as an actor (a decision: widen `isActorLayer` in `movementsolver.cpp`, or keep WORLD and move the static body, which Jolt allows); the player pushing an NPC is out of scope | a scripted velocity moves an NPC along the inn floor without leaving it; two runs equal |
| **S3 hooks contract** | `openeden/game/actor_rules.hpp` (SDK 4.1, additive): `onActorAdded/Removed(handle, payload)`, `update(dt)` with a request writer (movement, clip), handles are `WorldId` + session `RefNum`, no pointers; the pack's actor table keyed by `WorldId`; `Class::hasLife` | the pack sees its actors appear and go, and a test pack writes a movement request that S2 executes |
| **S4 locomotion request** | The pack asks "go to P at speed class C"; the core asks the navigator for a path with agent bounds taken from the capsule (`DetourNavigator::AgentBounds` from `NeutralActor::Capsule`; this is the seam to the `skynav` branch: that agent owns `components/detournavigator`, this task only supplies the bounds) and feeds S2 steering; failure and arrival are reported back to the pack | an NPC walks from one inn room to another on a Recast path and arrives |
| **S5 clips** | The pack asks for a clip by name (idle, walk, run); CreativeMotion plays it on the skeleton (task 0132 plays the idle already; the graph and converted clips are plan 4.12) | the NPC's feet match its speed |
| **S6 pack AI** | Package evaluation in the pack (plan 4.19): Sandbox/Wander around a marker, Travel, Sleep, schedules from the game clock; furniture use (sitting NPCs stop settling to the floor: S1 drops them from their furniture, a known limit) | NPCs of the inn wander and sit on their furniture |
| **S7 mechanics-lite** | Actor values from the base data, hostility and activation (talk) through `hasLife`, still no combat or magic | a click on an NPC opens the pack's dialogue stub |

`MechanicsManager`, `Actors` and `CharacterController` are not entered in any stage; if after S7 enough generic engine systems want `isActor()` (Lua `types.Actor`, tooltips), widening it is a separate decision with the 129-site audit in front of it (decision 0017 section 6).

## Design decisions of stage S1

1. **Object, not Actor** (fact 2, 4): an `MWPhysics::Object` on `Layers::WORLD` with a capsule, owned by `PhysicsSystem::mObjects` like every other object; `mNeutralActors` (a vector in insertion order) holds the fall state per actor.
2. **Where the body is created** (fact 1): `Neutral::insertActorRendering`, after the drawing, because that is the one place that has the appearance (the height) and can reach `World::getPhysicsSystem()`; the other choice, changing `Scene::addObject` to call `insertObject` for a model-less object and adding a `Class` virtual, would recompile the whole engine for a rule one class needs. The cost: the body exists only when the actor could be described (`[Game] neutral actors` on and the pack answered), the same condition under which it is drawn.
3. **Shape**: an upright capsule, feet at the origin: `NeutralActor::capsuleFor(heightScale)`, total height `kStandingHeight (128) * scale` clamped to 48..640, radius `kRadius (22)` capped at a quarter of the height. Both numbers are named estimates (Skyrim's character controller is about 1.8 m tall and 0.3 m wide at 69.99 units a metre, RECALLED-VERIFY, not measured against the skeleton). The object's own scale (`XSCL`) multiplies the capsule through `Object`.
4. **Standing on the floor** (`NeutralActor::stepFall`, pure): a ray from `kStepUp` (24) above the feet down 2,048 units against WORLD and HEIGHTMAP, ignoring every neutral actor's body; a floor up to 48 below or 24 above puts the actor on it at once (a placement is nearly always on its floor, and an instant snap avoids a visible drop); a floor farther below is a fall under the engine's gravity (`Constants::GravityConst * UnitsPerMeter`, the constant `movementsolver.cpp` uses), capped at 4,000 units/s; no floor means waiting up to 180 frames (the cell's other objects arrive over several frames), then the actor stays where the placement put it ("Lost"). A grounded actor is never probed again.
5. **Moving the actor** (fact 6): `PhysicsSystem::updateNeutralActors` writes the new z into the `RefData` position and hands the Ptrs back; `World::doPhysics` calls `RenderManager::moveObject` and `PhysicsSystem::updatePosition` for each, before `stepSimulation`, so the step's `commitPositionChange` moves the Jolt body in the same frame.
6. **Navigator**: the shape is marked `VisualCollisionType::Camera` (fact 5), so no navmesh tile is cut around an NPC. A named, documented use of an existing field, not a new flag.
7. **Developer aids** (not settings, as `OPENEDEN_NEUTRAL_DOOR_TRACE`): `OPENEDEN_NEUTRAL_ACTOR_TRACE=1` logs one `NACTBODY` line per actor when it comes to rest (start z, rest z, frames); `OPENEDEN_NEUTRAL_ACTOR_RAISE=<units>` lifts every actor that many units at creation so the fall can be seen.
8. **No new setting**: the capsule rides on `[Game] neutral actors` (decision 0017 asked for exactly one setting for N5), so `tools/testprofile` needs no line.

## Stage log

### Stage log (2026-11-12; same hand wrote tests and code, no fresh reviewer; Windows only; branch `skyactors`, not pushed)

Built in `I:\Projects\OpenEden-work\skyactors\build` (RelWithDebInfo, no editor/launcher). The parent for Morrowind comparisons was built from the SAME tree with the five engine files taken from `main` (`parentdir`, the 5 files then restored), so it has every other recent change (including the `OPENEDEN_FORCE_ANIM_VARIANT` knob).

**Tests first, red.** `testneutralactor.cpp` (23 tests): against a stub 18 failed and 5 passed (they pin "unchanged" behaviour); after the code 21 passed and two shape tests failed for a test fault (Jolt local bounds and `Shape::CastRay` are centre-of-mass relative), fixed in the tests; 23 pass. Pack: 5 `SkyrimAppearanceHeight` tests, 3 failed against a stub that left the scale at 1 (the other two pin the default and the refused case), all pass with the code. Four mutations of `neutralactor.cpp` (settle comparison, terminal speed, navmesh marker, landing test): the first three each fail a named test; the fourth (`z < floor - 5`) is observably equivalent (the actor is snapped to the floor on the next frame) and was not caught. `openmw-tests` as a whole 2,130 pass, 2 skipped (real data, existing).

**In game** (`tools/neutral_session_check/check.sh ... n5body`, new; the five Skyrim masters; quiet window; one game at a time; `OPENMW_DISABLE_CRASH_CATCHER=1` because the cold first run of a build trips the 5 s freeze monitor):
- Standing: six actors, `NACTBODY` rest z 68 to 70.39 from start z 64 to 69.2, each grounded in frame 1 (the placements are nearly on their floor). Frames viewed.
- Falling: `OPENEDEN_NEUTRAL_ACTOR_RAISE=300`: four actors fall 143 to 146 frames and land on the floor (rest z 68 to 69.2); two landed on the upstairs floor (z 347 and 318) because the raised start is inside it.
- Collision: the player walks at each NPC for 300 frames from 200 units away (log `N5B`). Setting on against off: actor 3 moved 166 against 278 and ends facing the NPC 30.8 units from its surface; actor 4 106.7 against 200.5; actor 6 163 against 211 (slides round); actors 1, 2 and 5 are limited by furniture in both runs. So the capsule stops the player where the same walk with the setting off passes through.
- The collision debug view (`n5bodydraw`) shows the capsule round the drawn NPC, feet on the floor (frame `run_draw/user/screenshots/screenshot000.png` viewed).

**Morrowind unchanged** (parent `parentdir`): `worldobjects_check` 110 lines, OK; Balmora fixed-frame (`OPENEDEN_FIXED_FRAME_TIME`, seed 7, frame 600) SHA-256 `1c5d47ad0b5dc32d` for parent and new, `neutral world` off and on; the C2 player pose trace (`creativemotion_check ... cm`, `OPENEDEN_FORCE_ANIM_VARIANT=1`, `OPENEDEN_FIXED_FRAME_TIME`, player lines with `pos=` and `CMFEET` removed): 1,401 lines and the same MD5 for 3 runs of parent and 3 of new. (An earlier reference from another session's folder predated the knob and differed after the hit phase; it is not used.) `check_tests_registered`, `check_layer_rules` (104), `check_game_conventions` pass.

**Not run:** the Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua tests, the whole `components-tests`, sanitizers, a fresh review, a measurement of the real standing height against the skeleton (`kStandingHeight` 128 and `kRadius` 22 are estimates), female and non-human races in a frame, an exterior cell with terrain, frame-time cost with many actors (grounded actors cost one state test).

**Limits found:** the player cannot push an NPC (static body); a teleport or script move does not re-settle an actor; actors placed on furniture drop to the floor; with `[Cells] incremental cell build` an actor can wait up to 180 frames for a floor that is not built (not exercised); `World::moveObject` is unusable for neutral refs (paging `mStore.find`), so the settle moves the render object and body directly.

### Decisions for toddhow

1. Body on `Layers::WORLD` as a static `Object` (first slice) against a kinematic body on a layer the player's solver reads as an actor (needed for S2, when NPCs move): recommended to decide at S2 with the movement design.
2. `ActorAppearance::heightScale` as a trailing field (SDK 0.3, the 0.2 precedent) against a separate interface: taken the field; says so if you want the interface.
3. The body is created in `Neutral::insertActorRendering` (no engine-wide change). If you prefer `Scene::addObject` to call `insertObject` for model-less objects with a `Class` virtual, it costs a full rebuild and touches every class.
4. `kStandingHeight`/`kRadius` are estimates: approve measuring them from the skeleton (a small gated test) or keep them.
5. Task number 0133 was the next free on this branch; another branch may have used it.
