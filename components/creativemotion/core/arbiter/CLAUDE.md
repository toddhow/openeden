# core/arbiter — the character state machine (M5d)

Depends on `core/rig` (for the profile's `arbiter` tuning) and the standard library only: no physics, no behaviors. `core/behaviors` (BalanceDriver) feeds it and acts on its state; the dependency points that way (docs/tasks/0017, Stage M5d interface, change 1). Spec: `docs/tasks/0017`.

## Rules
- `Arbiter::update` applies the rules in the spec's order every step: Fallen stays; Falling becomes Fallen once every body has been slower than `restSpeedMps` for `restTimeS` of updates made while Falling (a faster update restarts the count); Falling on giving up or when pelvis tilt exceeds `fallTiltRad`; Stepping; Standing when balancing, supported and slow; otherwise Leaning. It starts in Standing.
- Falling and Fallen are final in M5d (no get-up).
- `blendFraction()` is 0 on the update that changes the state and grows by `dt / blendTimeS` per later update, capped at 1; it is 1 before any change.
- Pure and allocation-free; deterministic for identical inputs.
