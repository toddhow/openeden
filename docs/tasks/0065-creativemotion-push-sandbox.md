# Task: Eden Studio, the CreativeMotion push sandbox (groundwork and plan)
Design doc section: `docs/design/eden-studio-plan.md` section 5.1 item 3 ("Push Sandbox Panel": ask the core to apply an impulse at a bone and watch it in the shared simulation); `docs/decisions/0007-eden-studio-editor-design.md`; builds on `docs/tasks/0035-creativemotion-m10-physics-adapter.md` stage c (`StudioWorld::cmWorld()`), `docs/tasks/0030-eden-studio-stage-e-jolt-world.md` (Play, Pause, Step, the debug draw) and `docs/tasks/0032-*` (the CreativeMotion editor module).
Status: DRAFT, not started. Written 2026-11-02 after task 0035 stage c, which left the editor's world able to hold CreativeMotion bodies. Nothing here is approved; the open questions are toddhow's.
Goal: in Eden Studio the user picks a ragdoll body (a "bone"), sets an impulse (direction, size, point), presses Push, and watches the ragdoll react in the editor's one simulation, with Play, Pause and Step working as for every other tool.
Inherited code touched: none planned. Editor code only (`apps/opencs/`), plus CreativeMotion's editor module (`creativemotion_editor`, its own task specs for its code). `components/creativemotion/` is never edited in place.

## Why this was not done as a small step inside task 0035
Applying an impulse is one call (`opas::physics::PhysicsWorld::applyImpulse(body, impulseNs, worldPointM)`, implemented by `CMPhysics::HostedJoltWorld::applyImpulse`), and `StudioWorld::cmWorld()` now hands that interface out. But a push needs things that do not exist yet, and each is a design choice, not a one-line change:
1. A ragdoll in the editor: `opas::motor::buildRagdoll` needs a rig and a profile (the game embeds copies in `apps/openmw/mwphysics/ragdollrigdata.cpp`); the editor's module already loads rigs for the inspector (task 0032), but nothing builds a ragdoll from the loaded rig in the world.
2. Which body is "the bone": a map from rig bone to `BodyId` (the builder's `Ragdoll` result has it), and a way to select it (picking or a list; the module API has no picking yet, see `apps/opencs/CLAUDE.md`, "Not yet").
3. The impulse gizmo (overlay lines exist; dragging handles does not).
4. Undo: a push changes the simulation, not project data. It must not go on the document's undo stack; "reset" (rebuild the scene) is the undo.
5. Determinism: a push happens between steps (while Paused, or queued for the next step's start), never from a step listener (`HostedJoltWorld` refuses structural calls in a step, and `applyImpulse` is not allowed there either).

## Design (proposed)
- Stage A (model, `apps/opencs/model/physics/`, no Qt widgets, tested in `eden-studio-tests`): `CSMPhysics::PushScene` owns a ragdoll built through `StudioWorld::cmWorld()` from a rig and profile, remembers bone name to `BodyId`, offers `push(boneName, impulseNs, worldPointM)` (queued, applied just before the next step, once) and `reset()`; destroyed or reset before `StudioWorld::clear()` matters not (clear makes its handles stale).
- Stage B (the module's panel): a rig and bone list, an impulse form (`makePropertyForm`), Push and Reset buttons, an overlay arrow for the last impulse. All through the editor module API; gaps found are fixed by extending the API (decision 0007).
- Stage C (checks): a ragdoll dropped, pushed while Paused, stepped; positions logged and identical across two runs; a frame captured and looked at.

## Constraints
Main thread only. No allocation in the per-step path of `PushScene` (a fixed queue of pending pushes). Lengths in meters at the interface, as everywhere in the adapter. Off unless the user opens the panel: with the panel closed the editor world is exactly as before (checked by the existing `StudioWorld` tests).

## Acceptance criteria (to be refined when approved)
- [ ] A rig loaded in the editor becomes a ragdoll in `StudioWorld` through `cmWorld()` and falls onto the editor's static box (unit test, `eden-studio-tests`).
- [ ] `push` on a named bone changes that body's velocity by `impulse / mass` (within a stated tolerance) after the next step, and not before (unit test).
- [ ] Two runs of the same scenario, 240 steps, give bit-identical body positions (unit test).
- [ ] `reset()` and `StudioWorld::clear()` leave the Jolt body count as before the scene was built (unit test).
- [ ] In the editor, Play, Pause, Step and a Push on a ragdoll's pelvis show the ragdoll moving; a frame is captured and looked at.
Out of scope: balance, stumbling and get-up (they need CreativeMotion's M11 and the sub-step decision of task 0035's open question 7), pushing living actors in a cell, the game, saving a scenario.

## Open questions for toddhow
1. Which rig and profile does the first push sandbox use: the ones embedded for the game's debug ragdoll, or whatever the Rig & Motor Limits Inspector has open?
2. Is picking a bone with the mouse required for the first version, or is a bone list enough?
3. Should a push also be possible while Playing (applied at the start of the next step), or only while Paused?
