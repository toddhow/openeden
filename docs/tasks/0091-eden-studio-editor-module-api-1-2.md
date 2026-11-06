# Task: Eden Studio Editor Module API 1.2 (close the gaps two modules already hit)
Status: draft (2026-11-06), needs toddhow's approval before work starts. Full loop (SDK headers and the module loader are in the full-loop list of the guide).
Design doc section: `docs/design/eden-studio-plan.md` sections 3.2, 3.3, 3.5, 3.7, 3.10, 3.11 and 8 (Phase 3); decision 0007.
Goal: grow the Editor Module API from 1.1 to 1.2 with the pieces the first two modules (`creativemotion_editor`, `advancedbodies_editor`) had to work around, so later modules (the push sandbox, ComplexClothing, the face timeline) are written against an API that has them.
Inherited code touched: none of OpenMW's world editing. New or changed: `sdk/include/openeden/editor_module/` (new virtual functions at the END of each interface, with do-nothing defaults, as 1.1 did), `apps/opencs/model/module`, `apps/opencs/view/module`, `apps/opencs/view/render` (picking of overlay items), the sample module.
Inputs / outputs: API version 1.2; a module built for 1.0 or 1.1 keeps loading (the version tests change only as 1.1 did: approval needed for the pinned lines); a 1.2 module is refused by an older host with a message naming both versions.
Constraints: determinism (no clock in the API); no allocation per frame in overlay submission beyond the existing `OverlayBuffer` caps; failure isolation unchanged (an exception in a module never reaches Studio); every addition off the hot path of the game (the game never loads editor modules).

## The gaps (from tasks 0032 and 0069 stage f)
1. **Picking and dragging of overlay data** (3.5): joint limits are editable only as numbers; a handle on a cone's rim needs the viewport to pick overlay items and call `Gizmo::drag` from mouse input, with an undoable edit on release.
2. **Labels** (3.5): the debug geometry has no text, so bone names appear as property groups instead of next to joints.
3. **Selection of a bone or entity** (3.7): the rig panel shows every joint; selecting a bone to show only its joint and highlight its gizmo needs a selection kind beyond "asset".
4. **Studio event bus** (3.10): selection changed, asset opened, simulation stepped, parameter changed, so modules stop polling.
5. **Settings pages** (3.11): a module's own preferences in Studio's preferences window.
6. **Background tasks** (3.11): a module can run a task off the UI thread with progress and cancel (the body cache read and the NIF dump block the UI today).
7. **Dialogs and canvases** (3.2, 3.3): not first priority; only if a stage needs them.

## Stages (each small, each with tests written first)
- **a. API shapes and the pinned version tests** (headers only; stubs in the host; the version message names both versions). The edited version tests need approval.
- **b. Selection and the event bus** (3, 4): a selection value the host owns and publishes; modules read it and subscribe; the rig panel uses it to show one joint.
- **c. Picking and dragging** (1): a pure picking function over overlay lines and triangles (ray to segment distance, ray to triangle), tests first; a drag session with an undo step at the end; the rig inspector's cone half-angle handle as the first user.
- **d. Labels** (2): text submitted as data like lines and triangles (position, string, colour), drawn by Studio's viewport as screen-space text; capped; no localisation in modules' strings yet.
- **e. Settings pages and background tasks** (5, 6): a module registers named settings with defaults (stored in `eden-studio.cfg` under its own namespace, like extension settings) and a task runner with progress and cancel on Studio's thread pool.
- **f. The sample module uses every addition**, as the reference and the test; the plan's Phase 3 text is updated.

## Acceptance criteria
- [ ] API version is 1.2; a 1.0 and a 1.1 sample module still load and run (tests build both against the old headers)
- [ ] A module built for 1.2 loaded by a 1.1 host is refused with a message naming both versions (test with `ModuleHost::setProvidedApiVersion`)
- [ ] Picking: a ray at a handle picks it within a pixel tolerance, the nearest wins, ties are decided by the item index (deterministic); a drag ends in one undo step; undo restores the value
- [ ] Selection and events: publishing and subscribing is single threaded on the UI thread, in registration order; an exception in a subscriber is contained and logged once
- [ ] Settings: defaults are written when absent, a value outside its declared range is clamped with one warning, the registered names are shown in the preferences window
- [ ] Background task: progress and cancel reach the task; the result is delivered on the UI thread; closing the document cancels its tasks
- [ ] `tools/edenstudio_check/check.sh`, `tools/bodypanel_check/check.sh` and `tools/modulecheck/check.sh` pass; `eden-studio-tests` and the Linux gate pass
- [ ] With no editor module loaded, Studio behaves exactly as before (the smoke check); the game and `openmw-tests` are untouched
Out of scope: NIF editing (task 0046), the push sandbox itself (task 0065), cell objects in the Jolt world, any change to the game's runtime, a C ABI for editor modules.

## Open questions for toddhow (recommendation first)
1. Order: selection and events first, picking second, labels third, settings and tasks last? Recommend yes: the rig inspector needs the first two, the body panel the tasks.
2. Should labels be drawn by the same Mesh view and cell viewport (recommended: one overlay path) or by a Qt overlay widget (simpler text, but then not part of captured frames)?
3. Where do module settings live: `eden-studio.cfg` under `[module.<id>]` (recommended, matching `[extension.<id>]`) or per-document project files?
4. Do dialogs and canvases (3.2, 3.3) wait until a module asks for them? Recommend yes.
