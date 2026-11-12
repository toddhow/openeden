# Task: Skyrim SE NPCs play the walk and run cycles when they move (M-S3, locomotion clips)

Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.11 and 4.12 (decision D4: our own graph on CreativeMotion plus clips converted from the game's `.hkx`), milestone M-S3. Follow-up of tasks 0135 (S2 to S4: the mover) and 0145 (the idle).
Status: written 2026-11-12 from the coordinator's brief; same hand writes tests and code.

Goal: with `[Game] neutral actor locomotion` on (and `neutral actor idles`, `neutral actors`), a Skyrim SE NPC that moves plays the walk or run cycle of its sex, blended with the idle by its measured ground speed, the cycle rate matched to the ground speed through the clips' own root motion; it stands in the idle when it stops.

Inherited code touched: none in behaviour. `apps/openmw/mwphysics/physicssystem.cpp` `updateNeutralActors` and `walkNeutralActor` (a measured speed is reported), `apps/openmw/mwworld/worldimp.cpp` `doPhysics` (hands it to the renderer), `apps/openmw/mwrender/scene.cpp` `attachNeutralIdle` (the controller is chosen by the setting).

## Data settled on the user's installed files (Skyrim SE, `Skyrim - Animations.bsa`, 8,979 files)

- Clips: `meshes/actors/character/animations/<male|female>/mt_walkforward.hkx` and `mt_runforward.hkx` (both sexes have them, with `mt_walkbackward`, `mt_walkleft`, `mt_walkright`, the four diagonals for walk and run, `mt_sprintforward`). There is NO turn clip with an `mt_` name in the character folders (turns are a rotation in Skyrim's graph); `mt_idle`'s start and stop transitions are behaviour graph states, not files. So the slice is forward walk, forward run and the idle.
- Root motion (`animationdata/default<male|female>.txt` names the generator `MT_WalkForward` = animation 965 and `MT_RunForward` = 944; `boundanims/anims_default<male|female>.txt` holds their root motion): walk 93.448 units in 1.13333 s = **82.45 units/s** (female 93.431, 82.44), run 222.219 units in 0.633333 s = **350.87 units/s**, both straight along +Y. A foot-contact event pair: walk FootLeft 0.2 s, FootRight 0.7667 s; run FootLeft 0 s, FootRight 0.3333 s. (Opt-in test `HavokGaitRealData` pins these against the extracted sidecars.)
- The mover's estimates (task 0135 S4: `NeutralPath::kWalkSpeed` 120, `kRunSpeed` 280 units/s) differ: at 120 the walk cycle plays at 1.46 times its natural rate, at 280 the run at 0.80. **Decision for toddhow (the S4 decision 3):** set walk to about 82 and run to about 350 to make the feet match the ground with no speed-up; the constants are LEFT as they were in this task (the blend uses the mover's speeds as the gait switch points, `Gait::Params`).

## Design

- **CreativeMotion or the 0145 controller?** The 0145 controller. CreativeMotion's runtime (task 0124) converts clips into `opas::anim::Clip` and drives them from `MWAnim::Play`'s group table; a neutral Skyrim actor is a bare `MWAnim::ObjectAnimation` with no `Play`, no `CharacterController`, and its clips are Havok clips read by `components/havokpack`. Going through CM needs the Havok-to-`opas::anim::Clip` conversion and the `AnimSet` of 0124 stage C1, which are not done. The decision lives in one pure function (`HavokPack::Gait::weights`), so a CM graph can replace the controller later without touching the decoder, the binding or the physics feed.
- **Pure maths** (`components/havokpack/locomotion.*`, formats layer, standard library only): `Gait::weights(speed)` gives idle, walk and run weights (they add up to 1) and a cycle rate; `smoothSpeed`, `advancePhase`, `clipTime`, `blend`. One shared phase p advances at `speed / stride` cycles a second (stride = the clips' root motion distance per cycle, mixed between the walk's 93.448 and the run's 222.219 by the gait mix); a clip is sampled at `fract(p + leftFoot) * duration`, which puts the left foot on the ground at p = 0 in both clips so a mix keeps the feet together. The idle fades out between 5 and 30 units/s; the gait mix is linear between the mover's walk and run speeds.
- **Controller** (`apps/openmw/mwrender/skyrimlocomotion.*`): three clip futures (idle, walk, run; the same `loadIdleAsync` workers as 0145, cached per skeleton, sex and clip), polled with a zero wait; a clip not decoded yet (or missing) hands its weight to the next slower gait; the idle behaves as in 0145 when the speed is 0 (the phase is the ref-id hash). Bones are the union of the actor's bones; each clip's tracks map to them by name; per frame the active clips are sampled into preallocated scratch and blended into a preallocated accumulator, then written (`SkyrimIdle::apply`). Root motion is never applied to the body.
- **Speed feed:** `PhysicsSystem::walkNeutralActor` measures the horizontal distance moved over the frame's `dt`; `updateNeutralActors` lists each actor whose measured speed changed (a stopped or blocked actor once with 0) in a reserved vector; `World::doPhysics` hands each to `RenderingInterface::setNeutralActorSpeed` (only with the setting on), `Scene` finds the controller among the object's `keepAlive` and stores the value in a relaxed atomic float. The controller smooths it (time constant 0.12 s) so the blend does not jump.

Constraints: off by default (`[Game] neutral actor locomotion`, in `tools/testprofile/profile.cfg` and the README); with it off the idle controller of 0145 is made as before and the physics feed is not read; no allocation per frame (scratch, accumulator and report list are sized when a clip is resolved or an actor added); deterministic (no clock but the controller's own `time`, no random).

Threads: `Gait::*` pure, any thread. `PhysicsSystem::updateNeutralActors`, `World::doPhysics`, `Scene::setNeutralActorSpeed` and `Controller::setGroundSpeed`: main thread. `Controller::run`: the object's update (an update worker for some objects), one object at a time. The decode workers are 0145's.

Acceptance criteria:
- [ ] `HavokGait*` unit tests (weights, smoothing, phase, clip time, blend) and `SkyrimLocomotion*` controller tests, run red against stubs first.
- [ ] In game (five Skyrim masters, `WhiterunBanneredMare`, `actor test goto` or `actor test walk`): frames a fraction of a second apart show the legs stepping while the NPC moves and the idle pose when it stops; a stated measure of foot sliding against ground speed.
- [ ] Setting off: frames equal today's idle; Morrowind unchanged (`tools/worldobjects_check` dump and the fixed Balmora frame against a parent build).
Out of scope: turn in place, strafe and backward clips, sprint, start and stop transitions, root motion on the body, creatures, changing the mover's speeds, the CreativeMotion graph.

## Stage log

(see below, filled in as the work goes)
