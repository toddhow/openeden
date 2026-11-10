# Task: animated far field (moving effects on far objects, moving distant creatures)
Status: DRAFT 2026-11-09, awaiting toddhow's approval. Docs only so far. Full loop required (touches the LOD cluster layout, `apps/openmw/mwrender/`, `mwmechanics/actors.cpp`).
Design doc section: research note `docs/research/0050-animated-far-field.md` (findings, options, RECALLED-VERIFY list); `docs/tasks/0059-*` (clusters, layout version 1 frozen).
Goal: far objects keep their moving effects (scrolling or flipping textures, alpha pulses, sway) and creatures in the loaded but out-of-processing-range ring keep animating, as opt-in settings that change nothing when off.
Inherited code touched (at the current tree; confirm hashes when work starts): `components/lod/nifflatten.cpp` (`NifFlattener::visit`, `applyProperty`, `addGeometry`), `components/lod/clusterbuild.*` (`ObjectTraits`, `classifyObject`), `components/lod/objectcluster.*` and `clusterpack.*` (layout), `apps/openmw/mwrender/lodclusters.*`, `apps/openmw/mwrender/distantobjects.cpp` (`buildChunk`), `files/shaders/default/main.frag`/`main.vert` (cluster mode `LOD_FADE`), `apps/openmw/mwmechanics/actors.cpp` (`Actors::update`, the `inRange` gate near lines 1696-1710), `apps/openmw/mwmechanics/character.cpp` (`CharacterController`), `components/mwanimation/object.hpp` (`autoPlay`), `components/vsgadapters/nif/nif.cpp` (controller handlers, as the reference for what each controller means).
Inputs / outputs: new settings (all off); layout 2 of `objects.edenpak` (stage b, needs toddhow's approval); a survey report outside the repository.
Constraints:
- Off by default; with every new setting at its default the cluster draws, paging chunks, actors and saves equal the commit before (compared as in the Test plan).
- Determinism: time comes from game or simulation time or a frame counter fed by it, never a wall clock; no unseeded randomness; phases from stable ids.
- Far animation is visual only: no AI, physics, stats, script, sound, text-key or save effect; saves unchanged.
- Derived data (baked packs) lives in the user data folder, is never committed; no game assets in the repository; survey output is aggregate numbers only, names stay local.
- Threads: say in the PR which thread each new update runs on (actor animation runs on update threads; compile on the main thread only).
- Hot paths: no per-frame allocation in the new updates.
- Every engine fact cited by file and function; unverified ones stay RECALLED-VERIFY until a stage settles them.
Acceptance criteria:
- [ ] Every stage below met, each shippable alone.
- [ ] With all new settings off, Morrowind and the world LOD look and run as before (frame comparison, `taa`/`hdr` off for exactness, same cell and camera; census draws equal).
- [ ] Each new setting is in `tools/testprofile/profile.cfg` (or a variant, or `exclude.txt` with a reason) and `tools/testprofile/README.md`; `tools/testprofile/coverage.sh` passes.
Out of scope: far actor proxies beyond the active grid (no actors exist there), impostors with baked animation, raising `CellGridRadius`, skeletal animation inside clusters, particles in far objects, changes to AI or processing range semantics.

## Findings that shape the stages (details in the note)

- Paging chunks (`DistantObjects::buildChunk`) link no controllers: every object there is a frozen rest pose.
- The flattener excludes models whose node or geometry has `mController`, `NiBSAnimationNode`, billboards, particles, skins and non-cutout blends, but never looks at controllers on properties (flip texture, alpha, material colour): those models are baked static into clusters.
- Actors outside `actors processing range` (default 7168) are skipped by `Actors::update` before their animation is advanced; the active grid reaches about 12,000 units, so loaded actors in that ring freeze.

## Stages

Each stage: tests first (a separate writer where the loop calls for it), off by default, frame criteria with frames viewed. Sizes and risk are estimates.

### Stage a: survey (size S, 2 days; risk low; read-only)
A tool (`edensurvey`-style, outside the game loop, reading the user's data through the VFS and the NIF reader) that lists, in aggregate only, for every static, activator and door model: controller kinds on nodes, geometry and properties (UV, flip, alpha, material colour, keyframe, morph, visibility, texture transform), billboard, particle, skin, blend type; and how many cluster pieces (by level) and paging objects would need each kind, per region. Names go to a local report outside the repository, never committed. Also settles: which models are the Ald-ruhn wall panels and the Ghostgate barrier; whether panels are excluded (paging) or baked (cluster); the count of animated vs plain far objects.
Criteria: report byte-identical over two runs; counts reconcile with `--bake-lod-objects`' excluded-by-reason totals (`Animated`, `AlphaBlended`) for the classes it already knows; the property-controller hole of the research note section 3 is confirmed or refuted with a number.
Out: nothing changes in the game.

### Stage a2: flattener sees property controllers (size S, 1 day; risk low-medium)
`NifFlattener` marks `mAnimated` for property-level controllers (`NiFlipController`, `NiAlphaController`, `NiMaterialColorController`, and any other the survey finds), so such models are excluded from clusters instead of baked static. Needs a re-bake and a bumped `bakerVersion` (staleness warning). Behaviour change of the bake only (live paging then draws them, still frozen until stage d). Criteria: unit tests on generated NIFs (red first against the old flattener); a bake before and after differs exactly by the models the survey lists; the cluster count and excluded totals reported.

### Stage b: time-driven UV, alpha and flip in clusters (size M-L, 5 days; risk medium-high: layout 2)
Layout 2: per piece group an effect record (kind, UV scroll velocity, alpha pulse amplitude/frequency/phase, flip strip count and rate); version 1 packs still read as "no effect". Baker maps `NiUVController` and the property controllers to effect records where the controller is linear or periodic, and otherwise leaves the model excluded (live). Shader: a time uniform (game time) and a small parameter block per group in the cluster pipeline only (`LOD_FADE` mode); `[WorldLod] object cluster effects` (off). Needs toddhow's approval of layout 2 before the freeze (decision 0010 rule).
Criteria: payload and reader tests (golden, round trip, version 1 and 2 coexist, strict reader hostile-input cases); two bakes byte-identical; pure CPU twin of the effect maths with tests (wraparound, large times, precision at hours of play: use fractional time); frame: the wall panels move in the distance by the same velocity as the near ones (compare two frames a second apart, difference present with the setting, none without); off: cluster draws byte-identical to the build before (0 pixels); GPU cost measured (estimate under 0.05 ms).

### Stage c: sway for foliage, flags and banners (size M, 4 days; risk medium)
Per piece sway amplitude and phase (from position hash) in the same effect record; vertex displacement in the cluster vertex stage by wind from `Render::WeatherState` when the weather plan (0094) is in, else a fixed default; capped amplitude, anchored base (displacement scaled by height above the piece's base) so cracks do not open. `[WorldLod] object cluster sway` (off). Only models the survey classes as sway-capable (flag/banner meshes by name list in settings data, vanilla has few vertex sway models: RECALLED-VERIFY).
Criteria: pure maths tests; frames at two wind strengths; no silhouette crack in a still frame; off identical.

### Stage d: animated models kept live beyond range with animation LOD (size L, 5 days; risk medium)
`DistantObjects::buildChunk` links the controllers of the models stage a2 excluded (UV, flip, alpha, keyframe) into a per-chunk `Anim::AutoPlay`-style group, updated at a rate that falls with distance (full rate near, 1/4 mid, 1/16 far, by chunk centre distance, steps use accumulated game dt), with a count and distance budget (`[WorldLod] animated object budget`, `animated object distance`); animated objects get their own StateGroups, others stay merged. Settings off.
Criteria: tests for the rate schedule and budget (pure); the paging chunk of a cell with the wall draws moving panels; the census shows draws +N for N animated objects, frame time delta measured and reported (CPU and GPU, three interleaved runs); hot-path allocation test; chunks freed cleanly (no controller outlives its node: lifetime test); off identical.

### Stage e: distant actor animation (size L, 5 days; risk medium-high: mechanics code)
In `Actors::update`, behind `[Game] far animation range` (0 = off, path identical), actors beyond `actors processing range` and within that range get an animation-only update: idle loop, no AI, no movement or physics, text keys ignored, time stepped by accumulated dt at a rate falling with distance, no skin update when culled. Dead, paralysed or scripted-animation (`PlayGroup`) actors keep their current pose rules. Whether far animals may also wander idly (AI) is NOT part of this stage (open question 3).
Criteria: tests with a fake `CharacterController`/seam proving no AI, stat, sound or script call happens in the tier (red first); determinism: a scripted scene run twice, with and without a save/load, yields the same poses and the same game state hash; a creature 9,000 units away animates in frames viewed; CPU cost measured with N actors (estimate 0.1 to 0.5 ms for a few dozen); off: the `inRange` branch compared by trace to be identical.

### Stage f: test profile, tuning, debug views (size S-M, 3 days; risk low)
Settings added to `tools/testprofile/profile.cfg` with `# task 0096:` comments and README lines; `coverage.sh` clean; a debug view colouring pieces by effect kind and animated chunk objects (exclude.txt with reason); budget tuning; docs (`apps/openmw/mwrender/CLAUDE.md`, `components/lod/` notes), the changelog. Linux gate run before any push.

## Test plan (all stages)
- Pure maths and rate schedules unit-tested first (red against stubs).
- Layout 2 reader: golden bytes derived by hand, hostile-input fuzz, version 1 compatibility.
- Frames viewed for every visible change (CLAUDE.md "frame check before render commits"); numeric comparison against the commit before for "off".
- Perf: three interleaved runs, lowest of three, shared-machine caveat recorded.

## Open questions for toddhow
1. Which effects matter most: the translucent wall/fence panels (UV/alpha), the Ghostgate barrier, banners and flags, fire/braziers (particles are not in scope), windmills and waterwheels, sway of trees?
2. Performance budget for stages d and e (milliseconds CPU/GPU, extra draws), and on which hardware tier these settings should be allowed.
3. Should far creatures also run idle AI (wandering) or only loop an idle animation (this plan: animation only, no AI)?
4. Is showing creatures beyond the active grid (a far-actor proxy) wanted at all? It needs a new design.
5. Approval of cluster layout 2 and the migration (version 1 packs keep working with no effects; a re-bake gives effects)?
6. Should the animation range default to the active grid's reach (about 12,000) or a smaller number when enabled?
7. Is a bake-time change (stage a2) acceptable on its own, since it silently moves some models from clusters to the live path?

## Answers from toddhow (2026-11-09)

- **Which effects matter most:** "the most obvious ones" (the effects a player notices first at distance: for example the Ald-ruhn wall panels, banners and flags, fires, water wheels, sway of large foliage, and creatures that stand still). Stage a (the survey) should therefore rank the controller kinds by how many far-cluster pieces and how large on screen they are, so the obvious ones go first.
- **Performance budget:** dynamic, not a fixed number. It goes into the scale-back system (task 0080, `[VideoMemory]`: `Render::VideoMemory` controller, `Scaler`, adapters named in `order`, such as `far objects`, `far terrain` and `texture mips`). Design consequence for this task: stages d and e register their own adapters (for example `far animation` that shortens the animation range and lowers the animation update rate in steps, and `distant actors` that reduces the animation-only tier) so the controller can reduce and restore them in the user-ordered ladder. **Open point for the spec:** the controller today reacts to VIDEO MEMORY pressure; the cost of far animation is mostly CPU time, so the scale-back needs a second pressure source (a smoothed frame-time or main-thread-time signal against a target) or these adapters must be driven by the existing memory signal only; decide in stage d (note 0042 and the 0080 stage log describe the memory source and the hysteresis rules to reuse).
