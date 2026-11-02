# Task: CreativeMotion transitions and style layer in the game
Design doc section: CreativeMotion `docs/design` (style layer) and `OpenMW Animation System - Implementation Doc.md`; `docs/research/0022-creativemotion-in-game-status.md` (what is wired today)
Goal: make CreativeMotion's contribution to the game visible and controllable: transitions chosen by what the actor is doing, and a data-driven style layer (posture, cadence, additive offsets) that toddhow can A/B per actor.

Status at the time of writing (research note 0022): clip playback and group-to-group inertialization are wired and work (stage S0 below is done); nothing else of CM's animation runtime (graph, pose blend, swing-twist, style) is reached from `apps/openmw`.

Inherited code touched: `apps/openmw/mwrender/creativemotiondriver.*` (`CreativeMotionPoseDriver::update`), `apps/openmw/mwmechanics/character.cpp` (`runPoseDriver`, `createPoseControllers`). `components/creativemotion/` is never patched in place: a needed change to CM becomes a CreativeMotion task spec.
Inputs / outputs: style files (`*.style.json`, `opas.style`, read through the VFS), a settings table for the default style and per-actor overrides, a console command, the trace line.
Constraints: everything behind `creativemotion animation` (off stays exactly vanilla); style layer behind its own setting, off by default; no allocation per frame in `update`; the driver touches only its own actor (it runs on animation threads); deterministic (no clock, no unseeded randomness: the style's noise is seeded from the actor's ref number); no game assets in the repository.

## Decisions (with a recommendation)

1. **Where style data lives.** Recommend: `.style.json` files in the VFS under `styles/`, read at first use through the VFS, cached by name. Alternative: one settings file. VFS keeps mods able to ship styles.
2. **Which style an actor gets.** Recommend a documented rule, first match wins: (1) `[CreativeMotion Styles]` per-actor entry `actor.<refId> = <style>`, (2) per-race `race.<raceId> = <style>`, (3) `default style`. Settings, not code.
3. **What state CM is fed.** Recommend: keep inferring from the group (works today), and add an explicit `ActorState` snapshot (moving, running, sneaking, drawn weapon type, hit, turning, speed) to the driver's `update` for the style layer's cadence and posture only. Do not move the group choice into CM (the engine's `CharacterController` keeps choosing groups).
4. **Graph or no graph.** Recommend: not in this task. The style layer is additive on top of the sampled pose; the graph is a separate, larger task.

## Stages

- **S0 Transitions between groups: done** (before this task). Verified by trace in note 0022.
- **S1 Diagnostics: done in the task that wrote note 0022.** `creativemotion animation debug`, the `CMTRACE` line.
- **S2 Style data in the game.** Load a `.style.json` through the VFS into `opas::style` types; a test generates a strongly different test style (slower cadence, forward-leaning posture) in memory; unit tests parse and round-trip it with located errors. No driver change yet.
- **S3 Evaluate the style on the sampled pose.** The driver evaluates the style's layers after sampling and before the inertializer output is written (so a style change also blends), per actor, seeded noise. Tests on the pure evaluation (a style with zero weights returns the input pose exactly).
- **S4 Selection and A/B.** The documented rule above, `creativemotion style layer = false` (off), a console command `cmStyle` (prints the active style and layer weights of the selected actor; `cmStyle off` and `cmStyle on` toggle the layer for that actor) and the trace line showing the style name and weights.
- **S5 Evidence.** Frame sequences and bone traces: a walk with the default style against the strongly different test style (cadence and lean measured from bone traces), idle to walk with each; the setting off equals today's trace (compare with the `CMTRACE` bone values).

## Acceptance criteria
- [ ] With `creativemotion animation = false`, or the style layer off, the `CMTRACE` bone values of the scripted scenario in note 0022 equal those of the build before the task (compared bone by bone).
- [ ] A style file in the VFS is loaded, an actor selects it by the documented rule, and the console command prints its name and layer weights.
- [ ] The trace shows a measurable difference in cadence (walk cycle period) and posture (spine rotation) between the default and the test style.
- [ ] Unit tests need no GPU, window, network or game data.
- [ ] A fresh reviewing agent found no unresolved issue on lifetimes, threading and allocations.

Out of scope: the animation graph / state machine in the game, IK changes, ragdolls, Eden Studio.
