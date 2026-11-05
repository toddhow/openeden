# 0012: toddhow's answers to the open review questions, 2026-11-04

Status: decided (toddhow, in chat, 2026-11-04). This record keeps the answers where specs, guides and later sessions can
cite them. toddhow answered "yes" to every recommendation in the lists below (my list was written in the chat of
2026-11-04; the recommendations are repeated here so the record stands alone). Nothing here is built yet unless a line
says so: the work is queued in "To build" below, and toddhow slowed the work down for a few days to save usage, so
none of it starts until he says so.

## A. Approvals of test edits and small changes (all approved)

1. Task 0072 stage f's three edits to `SlangPermutations` (the count rule, id 0 pinned to the count, the two
   `expectedStages()` entries) and the seven lines removed from `testshaderdefines.cpp`.
2. `3111e30676`, the unused-variable fix in `testslangruntime.cpp`; and converting the library tests' skips into failures
   when `OPENEDEN_SLANG_LIBRARY_DIR` is defined and the library is missing.
3. The bounds of `LodFarEvictionSim.a_climb_that_slides_the_window_loads_each_cluster_a_bounded_number_of_times`: 3 loads
   per entry became 7, and 70 in total became 110.
4. Point-light shadows (0077): the corrected guard-band angle (45.2206 to 45.2247), the margin measured at the far
   outline across a cube edge, and the spec texts for `visibility` and `selectInto`.
5. AO accumulation (0076): `defaultClampGamma` 1.5 to 1.0 in `testaotemporalmath.cpp`.
6. The merged body (0069): the skin mark moved back to just before each worn part asks for its slot, and the removed
   per-item and per-slot allocations (identical by the part-bookkeeping trace).
7. Fix `7ebbc09e09` in the LOD work ("no tiles without map view").
8. `Player::mNeck` and `Player::assignBones` stay protected as a test seam; the redundant `assignBones()` call in
   `Player::setViewMode` goes.

## B. Design decisions (all recommendations accepted)

### Temporal anti-aliasing and ambient occlusion (0075, 0076)
- `taa clipping` defaults to `minmax` (it avoids the variance box that US10116916B2 covers). Variance clipping stays an
  opt-in or is removed; **`ao temporal clamp` must follow the same choice** (default 0 / min-max style, not Salvi's
  variance box).
- TAA keeps refusing to run with MSAA on.
- `taa history` rises from 0.9 to 0.95 (the constant and its spec-pinned test change together).
- Transparent draws (rain, snow) are drawn after the TAA resolve, before TAA can become a default.
- Contact shadows are a separate, later task (they need a sun-diffuse G-buffer output and edits to two large shaders).

### Point-light shadows (0077)
- A light's own model no longer casts onto its surroundings (a lantern's bars cast spokes; a torch holder would be worse):
  exclude the model that carries the light from the light's own caster pass.
- Map caching (render a light's map once, redraw only when a caster in range moves) is built later (the first light costs
  about 0.5 ms even in a still scene).
- A warning when slots times resolution gets large (8 slots at 2048 texels is about 768 MiB (805 million bytes; "805 MiB" was a unit slip, corrected from the 0077 follow-up)).

### Slang (0072)
- The cold-cache stall: drop the start-up preload (+237 MiB on every run) and compile on a worker while the GLSL draws
  until the Slang version is ready.
- Release the compiler's global session (about 230 MiB) after an idle time.
- The ten vanilla particle effects with "ignore vertex colour" (`torchfire`, `lavasteam`, `furn_mist256`,
  `ex_waterfall_mist_01`, `light_sotha_sparks`, `active_bubbles00`, `act_sotha_steam00`, `w_nerevarblade_flame`,
  `ex_gg_particles_01`, `xkwama forager`) draw as the GLSL and OpenMW 0.51's code do (no per-particle colour).
- Stages i (shared includes as Slang modules) and j (Slang on by default, then GLSL deleted family by family) go ahead,
  each family's GLSL deletion with toddhow's approval at that time.

### World LOD (0060, 0066)
- **Tier budgets: a dynamic system replaces fixed numbers.** The fixed 256 MiB (minimal) and 320 MiB (low) budgets are
  never reached by Morrowind, so the rule was idle. Instead the renderer watches video memory use and, when it gets too
  high, scales back systems **in an order the user chooses in the settings** (for example: texture size cap, then far
  clusters, then point-light shadow slots and resolution, then volumetric quality, then world LOD levels). The user picks
  which things may be reduced and in what order. This is a new task (spec to write: sources of the memory figure,
  `VK_EXT_memory_budget`; thresholds and hysteresis so it does not flicker; each system's "reduce one step" and "restore
  one step" actions; the settings format; a default order; tests with a fake memory source).
- Clusters kept with no ground: accepted for now.
- The ground wait stays in frames (seconds is low priority). The "LOD cluster latency" log line stays (only with
  `[Cells] load trace`).
- Every terrain pack is re-baked once (`bakerVersion` 2); the test profile's `prepare.bat` does it.

### Actors and bodies (0074, 0069)
- A rebuild that fails is retried on the next change but logs each unreadable file once.
- The ammunition, holstered and quiver reads get a per-part catch like the part reads, so a bad file there no longer
  ends a mechanics-driven session.
- The 15 tests in `testmergedregions.cpp` that only restate the rule are removed; the 124,416-outfit test counts as stage
  e's "all combinations".
- Eden Studio's body panel: API 1.1 for filled overlay triangles and a user-data-folder query; module edits get an undo
  stack of their own; CreativeMotion's panel shows only its own documents; merged entries keep listing the two expected
  `BIND` findings.

## C. Not answered yet (stay open)
- Skeleton mapping decisions (note 0036 section 9): BODY-MAP-022 as a warning, a pass-through action for wrapper nodes,
  inverse-bind correction as the main path, genital-region words, caps, the `BSTriShape` renderer path, the Fallout 4
  skeleton to target.
- CreativeMotion stage 1 questions and the go for stages 2 to 4; his look at a `natural` recording.
- GitHub (what to push first and what `master` becomes), the leftover GitLab files, executable and data names, macOS,
  the C ABI, the `openeden.animation` contract, the fork's decision record, the first-run prompt.
- Reading note 0036's table, flying the whole-island view, V2 verdicts, hands-on checks, the Audio2Face test and the legal
  question about generated curves.

## To build (queued; nothing started; usage paused by toddhow)
1. Defaults and tests: `taa clipping = minmax`, `ao temporal clamp` consistent, `taa history = 0.95`; transparents after
   the TAA resolve.
2. Point-light shadows: exclude the light's own model; the slots-times-resolution warning; later, map caching.
3. Slang: worker compile with GLSL meanwhile, preload dropped, idle release of the compiler session; then stages i and j.
4. The dynamic video-memory scale-back system (new spec, then build).
5. Actors: the retry-log-once rule; per-part catches for the ammunition, holstered and quiver reads; removal of the 15
   restating tests.
6. Eden Studio: API 1.1, a module undo stack, per-module document filtering.
7. Contact shadows (own task, later).
