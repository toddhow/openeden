# Task: NPC body parts keep their skin and face controllers after an equipment change

Design doc section: none (bug in inherited renderer code, found by task 0069 stage e's equipment scene)
Goal: a body part, head or hair that survives `Npc::updateParts` keeps running its controllers, so the stock chest
follows the skeleton (no poke-through) and the face keeps talking and blinking after any equipment change.
Inherited code touched: `apps/openmw/mwrender/npc.cpp` (`Npc::updateParts`, `Npc::clearControllers`,
`Npc::insertBoundedPart`, `Npc::removeIndividualPart`), `apps/openmw/mwrender/npc.hpp`, at `327cd75125`.
Inputs / outputs: no new setting, no file format. A header-only helper `MWRender::PartControllers`
(`apps/openmw/mwrender/partcontrollers.hpp`) remembers each part's controllers.
Constraints: main thread only (as `updateParts`); no per-frame work or allocation; the merged body (task 0069)
re-attaches its own controllers on every `updateParts` and must keep doing so; deterministic (no unordered iteration).

## The bug

`Npc::updateParts` (called on every equipment change through `updateEquipment`) starts with `clearControllers()`,
which empties `mStaticControllers` (the body parts' skin controllers, run in `MWAnim::Wielding::update`,
`components/mwanimation/wielding.cpp`), `mFace` (the head's), `mWeaponControllers` and `autoPlay`. Every equipment
slot is removed and added again, so equipment re-links its controllers in `insertBoundedPart`. But a stock body
part, the head and the hair are added with priority 1 and kept when nothing displaces them
(`addOrReplaceIndividualPart` returns at `priority <= mPartPriorities[type]`): they stay in the scene with no
controller. A skinned part then keeps the last skin matrices it was given while the skeleton animates: the chest of a
walking actor stays in the pose of the moment the equipment changed and breaks through pants and loincloths; the
face stops blinking and talking.

**Where it comes from:** vsgopenmw. Its `npc.cpp` at the import commit `2830e7e2b4` has the same `clearControllers`
at the start of `updateParts` and the same priority-1 parts. OpenMW 0.51 (`npcanimation.cpp`, `NpcAnimation::
updateParts`) has no such clear: each part's controllers live with the part (`PartHolder`), so a surviving part keeps
them. This task restores OpenMW's behaviour.

## Fix

Remember, per part type, the controllers `insertBoundedPart` linked and the list each went to; forget them when the
part is removed (`removeIndividualPart`, the only place a part's path goes). At the end of `updateParts` (and before
its head-only return) link again the parts that are still there and were not inserted during this call.

## Acceptance criteria

- [x] Unit tests (written first, red against a stub) of `PartControllers`: a part inserted during the update is not
  linked twice; a surviving part is linked again into the same list it had; a removed part is never linked again (its
  controllers point into a node that is being freed); the head's entries go to the face list; an autoplay entry keeps
  its kind; the order is by part type, then insertion.
- [x] The equipment scene of `tools/mergedbody_check` (`SCENE=equipment`, stock variant): frames 009, 010, 045 and 046
  show no chest through the pants or loincloth; the chest follows the animation (viewed).
- [x] Face: after an equipment change the head still blinks and the mouth moves when talking (viewed or traced).
- [x] The merged variant of the same scene is unchanged (viewed; merged body count unchanged).
- [x] This changes the default game (the merged body off): it is a bug fix (a part frozen in the wrong pose is not a
  vanilla mechanic), recorded for toddhow.

Out of scope: the merged body's own controllers (already re-attached), first person, creatures.

## Stage log

**2026-11-04, the fix** (`9782c831f3` helper and tests, `29fc0d05a1` the fix in `Npc`).

- **Cause, from the code:** `Npc::updateParts` calls `clearControllers()` first; `insertBoundedPart` is the only place
  a part links controllers; `addOrReplaceIndividualPart` keeps a part whose priority is not exceeded, so the stock body
  parts, the head and the hair (priority 1) are kept with no controller after every equipment change. The merged body
  already worked around it (`attachMergedBody` re-attaches on every call); the holstered weapon and shield are removed
  and attached again by `updateSheathing`, so they were not affected.
- **Lifetime invariant:** `mParts[type]` is written only in `insertBoundedPart` and emptied only through
  `removeIndividualPart` (`removePath`), which `addOrReplaceIndividualPart` calls before inserting; `forget(type)` sits
  in `removeIndividualPart`, so no recorded controller outlives its part's node.
- **Upstream:** vsgopenmw `2830e7e2b4` `apps/openmw/mwrender/npc.cpp` has the same `clearControllers()` at the start of
  `updateParts`. OpenMW 0.51 (`apps/openmw/mwrender/npcanimation.cpp`, `NpcAnimation::updateParts`) clears nothing
  global: a part's controllers live with the part (`PartHolder`), so a kept part keeps them.
- **Tests first:** the ten `PartControllersTest` cases were written from this spec and run against a stub whose
  functions did nothing: 6 failed, 4 passed vacuously (the cases that expect nothing to be linked). With the
  implementation all 10 pass; `openmw-tests` 878 of 878 on Windows.
- **Frames** (`tools/mergedbody_check`, `SCENE=equipment`, 1280x720 with frame counters, the default settings
  otherwise, the user's Morrowind data; work folder `I:\Projects\OpenEden-work\stockskin`, not committed): a build of
  HEAD's `npc.cpp` (before) against the fix (after), same exe otherwise, both variants.
  - Stock, before: frame 009 (pants, front) and 010 (pants, wrist) show the chest through the pants in jagged pieces;
    045 (naked again, front) and 046 (wrist) show it through the loincloth. After: none of it; the chest lines up with
    the pants or loincloth in all four. A third run on the committed build shows the same. (The commit message of
    `29fc0d05a1` also says the hands left the forearms; the frames do not show that clearly.)
  - Face: a work copy of the scene adds, after the 200 cycles, one change the head survives (naked to shirt and pants),
    then 68 close-ups of the face 0.2 s apart, with `core.sound.say` of a voice line from the 6th second (40 shots
    with `isSayActive` true in both runs). Before: the eyes never close and the mouth never moves. After: the eyes
    close in two consecutive shots in the first 6 s and the mouth opens in many of the talking shots.
  - Merged: before and after look the same in frames 009, 010, 045 and 046; 32 actors draw the merged body in both
    (and in stage e's run).
- **Checks:** clang-format 14 clean on the four files; GCC with CI's flags plus `-Wshadow -Wunused -Wdangling-else
  -Wrange-loop-construct` (`-fsyntax-only`, in the Linux image) clean on `npc.cpp` and the test; MSVC no new warning.
- **Not verified:** NPCs other than the player in motion (the row's NPCs are dressed by a script, so they went through
  the same path, but no frame was compared); first person and the head-only view; item lights from the inventory,
  which `clearControllers` also dropped and nothing linked again (they froze: fixed in the review follow-up below);
  creatures (`WieldingCreature::removeParts` emptied its lists the same way, its own model's auto-play controllers
  and lights included: fixed below); the full Linux gate and the Lua integration tests. (This entry named the spell
  cast glow as dropped too; it never reaches an NPC, see below.) No fresh reviewer has read the diff yet (this work
  ran as a forked worker without subagents).
  The face phase lives only in the work copy `I:\Projects\OpenEden-work\stockskin\tool\mod\scripts\mbequip_player.lua`,
  not in the committed `tools/mergedbody_check`. The base config was `lodtest/config/openmw.cfg` with an empty
  `settings.cfg` (stage e's own runs used the same), so foot planting, inertial locomotion and the other options were
  at their defaults.

**2026-11-04, review follow-up (task 0074b)** (`7d55a76c0a` helper and tests, `b7868ee629` trace, `9c03fbd094` merged
body log ids, `8415dd30f5` the NPC fix, `879ec486c4` the creature fix). A fresh review of the commits above found no
blocker and six gaps of the same family. (Corrected in 0074d: the review text `I:\Projects\OpenEden-work\review0074.md`
was not empty; it was read while still being written. It holds nine findings; 1 to 6 are these, 7 to 9 were handled
only in 0074d, below.) Method: tests first, run red; no separate test writer or reviewer
(this ran as a subagent without subagents).

- **Finding 1, a kept part's auto-play clock restarted:** `clearControllers` destroyed every `AutoPlay::Group`; the
  relink re-added the same controller objects through `AutoPlay::add` into a new group (timer 0, or a fresh
  `std::rand()` phase of up to 3,600 s), with their `DeltaTime::last` stale (`deltatime.hpp` clamps only a negative
  delta). Now each part's auto-play controllers run in the part's own clock (`PartControllers::addAutoPlay`, `update`,
  emptied by `beginPart` and `forget`); `Npc::update` runs the clocks after `Wielding::update`, where `autoPlay` ran.
  A kept part keeps its clock; a part inserted again gets a fresh one, as every inserted part did before. Why this and
  not the smaller-looking alternatives: removing only a removed part's entries from the shared `autoPlay` would leave
  group 0 running from the actor's construction, and a new part's group-0 controllers would join it (a drawn torch's
  `LightController`, added by `MWAnim::decorate` with the default phase group, would see that whole time as its first
  `DeltaTime`, and its colour would swing for dozens of frames); a timer snapshot cannot follow the non-zero groups,
  whose numbers the old relink recomputed from `maxPhaseGroup()`; starting `DeltaTime` at 0 would end the random phase
  of every `Anim::Roll` in a random group (`nif.cpp`, `handleAnimFlags`).
- **Finding 2, inventory lights froze:** `Npc::clearControllers` no longer empties `autoPlay`; it calls
  `MWAnim::Wielding::clearPartControllers` (the static and weapon lists) and empties the face. `autoPlay` then holds
  only the inventory lights. The holstered weapon and shield, which also linked into it and are rebuilt every update,
  have clocks of their own (`mScabbardClock`, `mHolsteredShieldClock`, emptied by `removeHolstered`); the quiver links
  nothing.
- **Finding 3, creatures:** `WieldingCreature::removeParts` calls `clearPartControllers` and forgets its three parts'
  clocks (`PartControllers<3>`), never `autoPlay = {}`; the creature's own model's auto-play controllers
  (`MWAnim::createCreature`, `addNode`, `ObjectAnimation::addController`) and its inventory lights keep running.
- **Finding 4, the spell cast glow:** `addSpellCastGlow` is called for non-actor casters (`spellcasting.cpp`,
  `!mCaster.getClass().isActor()`), for Lock and Open targets that `canLock` (only `Door` and `Container` override it,
  `mwclass/door.hpp`, `container.hpp`) and for doors (`mwclass/door.cpp`), so an Npc never holds its `Timeout`. The
  claim is gone from this log, the render guide and the changelog row.
- **Finding 5, raw pointers:** `PartControllers` holds a `vsg::ref_ptr` to every recorded controller and target until
  `beginPart` or `forget` for the part (`mHeld`), so a part whose node went another way runs its controllers on a
  detached node instead of freed memory. Chosen over an assert because the game's RelWithDebInfo build has `/DNDEBUG`
  (`CMAKE_CXX_FLAGS_RELWITHDEBINFO` in `build-jolt/CMakeCache.txt`). The entries stay raw pointers: the existing tests
  compare them with raw pointers, and `vsg::ref_ptr<vsg::Object> == vsg::Object*` is ambiguous for MSVC (C2666).
- **Finding 6, a throw skipped the relink:** `Npc::updateParts` runs its node work (`updatePartNodes`) through
  `PartControllers::runUpdate`, which runs the relink (`relinkKeptControllers`: the parts, then the merged body's copy)
  on every way out and passes a throw on. The merged body's relink moved from `attachMergedBody` to that step, once per
  update. **Reachability (wrong as first written, corrected in 0074d):** a missing file, or one no reader accepts,
  does not throw: the node options end with `Resource::Fallback` (`createNodeOptions` in `resourcesystem.cpp`:
  `marker_error.nif`, else an empty node) and `nif::read` catches `std::exception`; a weapon whose model was missing and
  one whose model was a PNG both drew the fallback. But the file is opened outside the readers' catch
  (`vsgAdapters::vfs::read`, `VFS::Manager::get`), so an Eden Archive entry that fails its check, or an I/O error,
  does throw, and the game crashed afterwards (0074d). The guard is not merely defensive.
- **Found on the way:** with `[Bodies] merged body` on, an NPC made from a Lua record (a generated id) was not drawn:
  two log lines in `npc.cpp` used `RefId::getRefIdString`, which throws for a generated id, during the Npc's
  construction ("failed to render 'Generated:0x13'"). `9c03fbd094` uses `toString`.
- **Tests first:** 11 cases (`testpartclocks.cpp` 9, `testactorautoplay.cpp` 2), run against stubs (the new helper
  functions doing nothing, `clearPartControllers` emptying `autoPlay` as `clearControllers` did, `removeParts` as it
  was): 10 failed, the early-return relink case passed vacuously (log
  `I:\Projects\OpenEden-work\0074b\red-run-stubs.log`). Then all pass; `openmw-tests` 923 of 923 on Windows.
  GCC with CI's flags plus `-Wshadow -Wunused -Wdangling-else -Wrange-loop-construct` (`-fsyntax-only`, Linux image)
  clean on the changed units at every one of the five commits; clang-format 14 on the changed lines; MSVC no new
  warning.
- **In game** (work folder `I:\Projects\OpenEden-work\0074b`, not committed): a work copy of `tools/mergedbody_check`
  with `SCENE=clocks` (`tool/mod/scripts/mbclocks_*.lua`) and `oe_clockparts.esp` (written by `tool/make_clock_esp.py`:
  two hair body parts whose models are the vanilla `l\light_com_torch_01.nif` and `r\bm_forcefield.nif`; no `.esp`
  goes in the repository). Two Lua-made Bretons with those hairs and an inventory light (`blade light`, the first
  non-carriable flickering light by id) change their shirt eight times, a dremora (its model has particles) its weapon
  eight times; `OPENEDEN_AUTOPLAY_TRACE="oe clock"`; summaries by `tool/clock_summary.py`. Exes:
  `build-jolt/openmw-0074b-before.exe` (`b7868ee629` plus the test-only declarations) and `openmw-0074b-after.exe`.
  - Before: the torch hair's groups were in the object's `autoPlay` and jumped at every change (largest step 3,528.6 s
    off the frame's dt; for example 3,560.73 to 32.09); the light left the groups at the first change in play (none of
    the 4,209 later frames had it). The dremora's own group (phase 1, two controllers) and its light were gone after
    its first change.
  - After: the light is in all 4,395 frames after the first change, in 4,394 different colours; the hair clocks (part
    1) and the dremora's own group run on, no timer stepping off the dt by more than 0.000131 s (corrected in 0074d from
    "0.0001": float rounding at timers near 3,560 s, where one float step is 0.000244 s). Merged variant: the same,
    and 26 actors draw the merged body (24 before: the two Lua-made NPCs were not drawn).
  - Frames viewed: the clocks scene before and after (004, 006: the scene as scripted; daylight hides the light's
    effect, so the trace is the proof), `SCENE=equipment` after, stock 009, 010, 045 and merged 000, 009, 045: no chest
    through the pants or loincloth, 32 actors draw the merged body.
- **Not verified:** the Linux gate and the Lua integration tests; first person; the holstered clocks in game (`[Game]
  weapon sheathing` and `shield sheathing` are off by default, not run); a kept part with auto-play in real data
  (vanilla body parts, hair and heads have none: a byte scan of `meshes\b` found controllers only in heads, the face's
  morpher; armour and clothing in `meshes\a` and `meshes\c` have no auto-play node); the throw in game; the cost of
  running 29 mostly empty clocks per NPC per frame (not measured; nothing allocates).
- **For toddhow:** both fixes change the default game (a frozen light or a stopped creature effect is not a vanilla
  mechanic). Open after 0074b, fixed in 0074c below (the review of 0074b, finding 3, corrected this bullet's size and
  trigger): an inventory light added later (`UpdateItemLights::itemAdded`) to an actor that already had one joined
  `autoPlay`'s group 0, whose timer T runs from the actor's first inventory light (a removed light leaves the group
  running), and its new `LightController` took T as its first step: a strobe of up to 0.375 T times the light's colour
  (0.1875 T for the slow kinds), negative on alternate frames, about 25 frames long for T = 600 s. An actor with no
  inventory light makes a fresh group 0 at 0. Before 0074b the same happened with T counted from the last equipment
  change.

**2026-11-04, 0074c, light first delta** (`ad9d62e22b`). Finding 3 of the fresh review of 0074b
(`I:\Projects\OpenEden-work\0074b\REVIEW-0074b.md`); its other findings are not part of this entry. Method: tests
first, run red; no separate test writer or reviewer (this ran as a subagent without subagents).

- **Cause:** `LightController::apply` (`components/mwanimation/light.cpp`) takes its step from `Anim::DeltaTime`
  (`components/animation/deltatime.hpp`), whose `last` starts at 0, and `Anim::AutoUpdate::update`
  (`components/animation/update.hpp`) runs it with its group's timer, so its first step was the timer of the group it
  joined. The rule (OpenMW's): ticks = step * 15 * 0.25 + ticks * 0.75, and the brightness (starting at 0.675) moves by
  ticks * speed (0.1 for Flicker and Pulse, 0.05 for the slow kinds) towards a phase in [0.25, 1]. A first step T moves
  it by 0.375 T (0.1875 T) at once, then the moves shrink by 0.75 a frame and change sign while the brightness is far
  outside [0.25, 1]. OpenMW 0.51's `SceneUtil::LightController::operator()` sets `mStartTime` at its first run, so its
  first step is 0 (`git show 442eec6e2f:components/sceneutil/lightcontroller.cpp`).
- **Fix:** a first-run flag in `LightController` sets `mDt.last = time` before the first `get`. `Anim::DeltaTime` is
  unchanged: `Anim::Roll::apply` (`roll.cpp`, `speed * mDt.get(time)`) in a random phase group takes the group's random
  timer (`AutoPlay::getOrCreateGroup`, up to 3,600 s) as its first step, which is its random starting angle.
- **Which lights change, and how much.** `LightController` sets only `hints.autoPlay`; its phase group is the default 0,
  and every caller puts it in a group 0: `MWAnim::create` and `createObject` (`create.hpp`) and `addItemLight`
  (`itemlight.cpp`) add it to `autoPlay.getOrCreateGroup()` directly, `MWAnim::decorate` (`decorate.cpp`, light passed
  only by `Npc::insertBoundedPart`) to the part's own clock through `PartControllers::addAutoPlay` (`AutoPlay::add`,
  phase group 0). No light is ever in a random group, so **placed lights did not strobe at cell load.**
  - Placed world lights (`create.hpp`), carried torches (the part's clock, fresh at every insert since 0074b) and an
    actor's first inventory light (`addItemLightsAndListener`, `character.cpp`, when the CharacterController is made;
    for an NPC that makes group 0, which holds only inventory lights) join a group made with them at timer 0. Their old
    first step was the first frame's dt, at most 0.2 s times the simulation time scale (`Game::run` clamps dt,
    `game.hpp`; `World::update` passes it to `RenderManager::update` and `Scene::update`): a first-frame brightness
    change of at most 0.075 (0.0375 for the slow kinds), 0.006 at 60 fps, fading over the next frames. Now 0: such a
    light flickers as before, one frame later (the old first ticks add up to the ticks of one frame).
  - An inventory light added later (`UpdateItemLights::itemAdded`: a script's `AddItem`, Lua `moveInto`, any
    non-carriable light given to an actor that already has one, or after `removeItemLight`, which leaves group 0
    running): its first step was group 0's age. That is the strobe, now gone. For T = 600 s at 60 fps (a simulation of
    the old rule, `I:\Projects\OpenEden-work\0074c\sim\strobe.py`): first brightness 0.675 -/+ 225, about 25 frames
    outside the normal range, up to 10 of them negative; for T = 1 s up to 3 frames slightly outside it (brightness at
    most 1.05); at 0.2 s none.
- **Tests first** (`apps/components_tests/mwanimation/testlightcontroller.cpp`, 4 cases over Flicker, FlickerSlow,
  Pulse, PulseSlow and a Negative Flicker light; the controller made by `MWAnim::addLight` from a hand-built
  `ESM::Light` and run through `Anim::AutoPlay`, or `Anim::Update` at given times): the first run in a group 600 s old
  leaves the brightness at 0.675; 300 frames at 60 fps stay in [0.25 - 15 dt speed, 1 + 15 dt speed] with the base
  colour's sign; the same seed gives the same colours in a fresh group and one 512 s old (1/64 s frames, exact in
  float); and every run follows a reference of OpenMW's rule from time 0. Red run against the old controller
  (`I:\Projects\OpenEden-work\0074c\red-run.log`): the first three failed (first brightness -224.33 = 0.675 - 0.375 *
  600.0167, -111.83 for the slow kinds, -191.33 at 512 s); the fourth passed, as intended: from time 0 the old first
  step was 0 too, so it shows that the reference is the rule the controller had. After the red run one comment in the
  test moved to its own line (clang-format 14); no code or assertion changed. After the fix all 4 pass;
  `components-tests` 5,472 and `openmw-tests` 927 pass on Windows.
- **In game** (work folder `I:\Projects\OpenEden-work\0074c`, not committed): a work copy of the 0074b scene tool with
  `SCENE=lightlater` (`tool/mod/scripts/mblight_*.lua`, no ESP): two Lua-made Bretons and a dremora carry inventory
  light A (`blade light`, Flicker, colour (1, 0.549, 0.157)) from the start; 45 s later each is given light B
  (`green light`, Flicker, colour (0.012, 0.722, 0.525), radius 1000); 23:00, `[Video] framerate limit = 30`,
  `OPENEDEN_AUTOPLAY_TRACE="oe light"`, summaries by `tool/light_summary.py`. Exes `build-jolt/openmw-0074c-before.exe`
  and `-after.exe`; between their builds only `light.cpp` was compiled (`build-green.log`, `build-after.log`).
  - Before: B joined group 0 at t = 45.07 s and its brightness was 17.57 (0.675 + 0.375 * 45.07), then 4.89, -4.64,
    2.52, -2.86, 1.18, -1.86, 0.43, 2.17, ...: 10 frames outside the 30 fps range [0.2, 1.05], 4 of them negative light,
    back in range from the 13th frame; the same in all three actors. Light A on its first frame: 0.6626 and 0.6874,
    0.675 -/+ 0.375 * 0.0332 (its first step was the first frame's dt).
  - After: B at 0.675 on its first frame, all 179 traced frames in range (largest 0.86 to 0.96); A at 0.675 on its first
    frame. The dremora's own model's controllers were in a random group (`g1`, 1,622.9 s on its first frame), its group 0
    was made by light A.
  - Frames viewed (`runs/<exe>/stock/frames`, before 001 to 004, after 002 and 003): before, the shot 2 s before the
    gift is the plain night scene and the first shot after it (3 frames after the event) shows the whole view red and
    the sky white (the red tint fits B's negative green and blue; why the sky, which point lights do not light, went
    white was not investigated); the next two show the green light, the second more strongly; after, the first two
    shots show the green light as the later before shots do.
  - Placed lights at cell load: no frames compared. The change is at most one frame's step (0.075 of the colour at the
    0.2 s clamp), which a frame of a randomly flickering light cannot show either way; the code above is the evidence.
- **Threads and cost:** `apply` runs where its group runs: `vsgUtil::UpdateThreads` workers for objects and NPCs
  (`Scene::Cell::Update::run`), the main thread for the player. The flag is the controller's own; no allocation, no
  extra random draw. `TMutable::cloneIfRequired` copies the flag, but `addLight` makes a new controller per call and the
  clones (`clonePlaceholdersIfRequired`, `cloneIfRequired`) happen before it, so no controller that has run is cloned
  (from reading).
- **Checks:** clang-format 14 on the changed lines; GCC 13.3 with CI's flags (`-Werror`) plus `-Wshadow -Wunused
  -Wdangling-else -Wrange-loop-construct`, `-fsyntax-only`, clean on `light.cpp` and the test (the shared Linux build's
  compile commands, its volume mounted read-only); MSVC no new warning (the C4244 in `light.cpp` is the old
  `intensity = mRadius` line).
- **Found on the way, not changed:** `removeItemLight` (`itemlight.cpp`) never erases the item from its map (OpenMW
  0.51's `ActorAnimation::removeHiddenItemLight` does), so the removed light's node stays referenced and the same item
  is never added again; `WieldingCreature::addPart` passes no light to `MWAnim::decorate`, so a creature carrying a torch
  has no torch light (OpenMW 0.51's `CreatureWeaponAnimation::updatePart` calls `addExtraLight` for a light,
  `creatureanimation.cpp`); `Anim::CullParticles` (`cullparticles.hpp`) also steps by an `Anim::DeltaTime`, not assessed.
- **Not verified:** the Linux gate and the Lua integration tests; a script's `AddItem` in game (the scene uses Lua
  `moveInto`, which reaches the same listener, `InventoryStore`'s `itemAdded` call); the player; frames of placed lights;
  a fresh reviewer.
- **For toddhow:** this changes the default game: an inventory light added to an actor that already has one no longer
  strobes, and every flickering or pulsing light's first frame keeps its starting brightness (invisible: one frame of
  flicker).

**2026-11-04, second review follow-up (task 0074d)** (`37eee7addb` inventory lights, `11a63c3326` findings 1, 2, 4, 5
and 7, `cafbc65b71` a creature's carried light). A fresh review of 0074b
(`I:\Projects\OpenEden-work\0074b\REVIEW-0074b.md`) found no blocker in its two behaviour fixes and seven items; its
finding 3 (a light's first step) was fixed in 0074c above. Method: full loop. A separate test-writing agent wrote the tests from a specification
(`I:\Projects\OpenEden-work\0074d\test-spec.md`) without the implementation, against stubs; the red runs are recorded;
then the implementation; then every one of the reviewer's mutations built and run. No fresh reviewer has read 0074d.

- **Finding 1, a throw is reachable and left uncompiled nodes drawn:** `vsgAdapters::vfs::read` opens the file through
  `VFS::Manager::get` outside `nif::read`'s catch, so an Eden Archive entry that fails its check
  (`EdenArchiveFile::open`), one over `maxOpenSize`, or an I/O error of a BSA or loose file, reaches
  `Npc::insertBoundedPart` and `WieldingCreature::addPart`. The Lua delayed action (`LuaManager::DelayedAction::apply`)
  or mwscript catches it and the game runs on (**corrected in 0074e, finding 2: true for Lua, not for mwscript, the
  windows or the mechanics callers**), but `updateEquipment` had skipped `compile()`. Now
  `MWRender::runEquipmentUpdate` (`equipmentupdate.hpp`) compiles also when the update throws and passes the exception
  on; `Npc::updateEquipment`, `Npc::rebuild` and `WieldingCreature::updateEquipment` use it. A unit test reads through
  `vsgAdapters::vfs` and `Resource::Fallback` over a VFS whose `open()` throws: the error reaches `readNode`'s caller,
  while a missing file gets the fallback node.
- **Finding 2, and the first review's finding 7:** a part inserted and then displaced in the same update (a mod record:
  a left glove that lists a right hand, worn with a right armour gauntlet) left its controllers in the live lists,
  running on its node in the deletion queue and then on freed memory; `mHeld` did not help (forget clears it).
  `Npc::detachPart` now takes the part's recorded static, face and weapon controllers out of the live lists
  (`unlinkController`) before it forgets the part. `updateNpcBase` and a failed `compile()` start with
  `removeAllParts`, which empties the lists first, so a throw from the skeleton or animation reads that follow leaves
  nothing pointing into a removed node (**corrected in 0074e, finding 1: true of the part lists only; the freed Play
  was still in use by the CharacterController and the bone pointers**).
- **The first review's findings 8 and 9 (never handled before):** 8, the tests covered the helper only: see finding 4.
  9, the docs: "restores OpenMW's behaviour" is now true for auto-play and inventory lights too (0074b, 0074d); the fix
  also relinks first-person arm parts (kept parts there too), not verified in game; its "Weapon branch unreachable" no
  longer holds: a throw before the weapon slot's `removeGroup` keeps the old weapon, which the relink then links again.
- **Finding 4, tests of the game's use, not the helper's:** `Npc` gets a constructor for tests (`TestSetup`: no
  record, skeleton or part), `runPartUpdate` (the shape of `updateParts` around a callable; `updateParts` uses it),
  `removeAllParts` and `attachMergedCopy` (the second half of `attachMergedBody`); `WieldingCreature` gets `linkPart`
  (`addPart`'s node work apart from the inventory). Each mutation, applied and built under the build lock with the
  source restored before the lock was let go (`I:\Projects\OpenEden-work\0074d\mut`), makes tests fail:

  | Mutation | Failing tests |
  | --- | --- |
  | (a) `Npc::clearControllers` also empties `autoPlay` | `an_inventory_light_keeps_flickering_through_clear_controllers_and_a_part_update` |
  | (b) no `mPartControllers.update` in `Npc::update` | `a_kept_part_runs_its_auto_play_clock_on_every_update`, `a_removed_part_never_runs_its_auto_play_controllers_again`, `after_removing_every_part_no_part_or_holstered_controller_runs` |
  | (b) no holstered clock updates in `Npc::update` | `after_removing_every_part...`, `a_holstered_weapon_runs_its_clock_until_it_is_removed`, `a_holstered_shield_runs_its_clock_until_it_is_removed` |
  | (b) no `mPartControllers.update` in `WieldingCreature::update` | `a_linked_weapon_runs_its_auto_play_clock_on_every_update`, `removed_parts_never_run_again_and_the_creature_s_own_model_keeps_running`, `a_carried_flickering_light_gives_light_and_flickers` |
  | (c) `insertBoundedPart`'s auto-play into `autoPlay` | `a_removed_part_never_runs_its_auto_play_controllers_again`, `after_removing_every_part...` |
  | (c) `linkPart`'s auto-play into `autoPlay` | `removed_parts_never_run_again_and_the_creature_s_own_model_keeps_running` |
  | (d) `relinkMergedBody` in `attachMergedCopy`'s keep path too | `a_kept_merged_copy_is_linked_once_per_part_update` |
  | (e) `removeHolstered` without `clock.clear()` | `after_removing_every_part...`, both holstered tests |

  The first run of the creature mutation failed to link (`openmw-tests.exe` was open elsewhere) and was run again.
- **Finding 5, threads:** `partcontrollers.hpp` now says it: the clocks (`update`) run on the actor's update thread,
  an `UpdateThreads` worker for an NPC or creature (`Scene::update`, one operation per cell, `[Cells] animation
  threads`), the main thread for the player (`RenderManager`) and the inventory preview (dt 0); everything else on the
  main thread in the part update, never while that actor's update runs (`UpdateThreads::run` waits). The 0074b log's
  "main thread only" was wrong for `update`. The ownership note now says the ref_ptrs cover the entries and clocks, not
  the live lists.
- **Finding 6, numbers:** 0.0001 s corrected to 0.000131 s above (and in the changelog row).
- **Finding 7, trace cost:** no id is built for an actor when `OPENEDEN_AUTOPLAY_TRACE` is unset (`autoPlayTraceOn`).
- **Item A, inventory lights could not come back:** `removeItemLight` removed the light whatever was left of the item
  and kept the item in its map, so `addItemLight` (which returns early for a known item) never added it again: removing
  one light of a stack of two took the light away for good. As in OpenMW 0.51 (`ActorAnimation::itemRemoved`,
  `removeHiddenItemLight`, 442eec6e2f), the light now goes only when the item's count is 0, and the item is forgotten
  with it; the node leaves through the compile context's `detach`. (**Corrected in 0074e, finding 8:** a re-add after
  the count reached 0 is a new stack and a new `Ptr` and already worked; the change is that removing part of a stack
  keeps the light and the map entry and node no longer leak.)
- **Item B, a creature's carried light:** `WieldingCreature::addPart` passed no light to `MWAnim::decorate`; OpenMW
  0.51's `CreatureWeaponAnimation::updatePart` attaches a light record with its light (`addExtraLight`). `addPart` now
  passes it (`linkPart`), as `Npc::insertBoundedPart` does for an NPC's torch.
- **Item C, `Anim::CullParticles` (assessed, not changed):** its `apply` uses the step only to count down while the
  emitter is inactive (`mCountdown -= dt`) and as "time passed" while it is active (`dispatch` on when `dt > 0`);
  `link` starts the countdown at 0 and shows the children. A first step of a running group's whole timer therefore
  changes nothing visible: inactive, the countdown is already 0 and the particles stay hidden as with any step; active,
  the countdown is reset and the dispatch is on, as with any positive step. It has no strobe like the light's.
- **Tests:** 20 (`testnpcparts.cpp` 13, `testequipmentupdate.cpp` 5, `testitemlights.cpp` 2), written by the
  separate agent (the message of `11a63c3326` says 12 of `testnpcparts.cpp`'s are in it: it is 11, the other 2 came
  with `cafbc65b71`). Red against the stubs (`I:\Projects\OpenEden-work\0074d\red-run.log`): 9 failed for the reason they
  name (the displaced part's static and face controllers still listed and running; the lists not emptied by
  `removeAllParts`; no compile after a throw, for a `std::exception` and for another type; no light on a creature's
  carried light, flickering or steady; the item light gone after removing one of two and not back after the add), 11
  passed (the mutation guards and the reachability test). After the implementation all pass; `openmw-tests` 947 of
  947 on Windows. GCC with CI's flags plus `-Wshadow -Wunused -Wdangling-else -Wrange-loop-construct` (`-fsyntax-only`,
  Linux image) clean on the changed units at each of the three code commits; clang-format 14 on the changed lines.
- **In game** (work folder `I:\Projects\OpenEden-work\0074d`, not committed): the 0074b scene tool extended
  (`SCENE=clocks`): `oe_clockparts.esp` gains a left glove that lists a right hand (`oe_clock_glove_rhand`, body part
  `c_glove_common1`); `pack/oe_bad.edenpak` holds two vanilla meshes (an iron dagger and an iron shield) with one byte
  flipped in each, so both fail their chunk check (`edenvalidate`: `E_HASH`), and is listed as a `data=` line; the
  dremora gets `light_com_torch_01_256` (the first carriable flickering light by id) at 22:00; `SKIP_STEPS` leaves out
  the glove, the NPC's throw or the creature's throw; a crash kills only the run's own exe. Exes
  `build-jolt/openmw-0074d-before.exe` (`79c024d6bf`) and `openmw-0074d-after.exe` (the three commits).
  - Glove and gauntlet in one change (`OPENEDEN_PARTSLOTS_TRACE`: the glove's right hand attached, then detached by
    the gauntlet in the same update): before, the static list held 10 controllers (the displaced glove's 3 among them)
    and the game crashed 0.37 s later (a crash dump); after, 7, and the scene ran to its end.
  - Shirt and the bad weapon in one change (the shirt attached, then the weapon's read throws): before, the game
    crashed 0.15 s after the throw; after, the delayed action logs the error and the scene runs to its end; frame 003
    shows the shirt drawn.
  - The dremora's weapon with the bad shield: no crash before or after (its weapon's model was compiled by an earlier
    change, so its attached copy drew); the unit tests cover the creature's catch.
  - Inventory lights (NPC B, a stack of two): before, the light left at "remove one of two" and was not back after "add
    one"; after, it stays through both, goes at "remove both" and is back after "add one again".
  - The dremora's torch: before, its part clock held the flame's two particle groups and no light, and frame 001 shows
    the actors dim and blue; after, the clock also holds the light's controller and frame 001 shows the dremora and the
    NPCs beside it lit warm.
  - `SCENE=equipment` with the after exe, stock and merged: frames 009, 045 (stock) and 009, 046 (merged) viewed, no
    chest through the pants or loincloth, 32 actors draw the merged body, both exit 0.
- **Not verified:** the Linux gate (full build and tests) and the Lua integration tests; first person; the holstered
  clocks in game (sheathing off by default); the creature's compile after a throw in game (no crash either way).
- **For toddhow (decisions):**
  1. **A throw still leaves the actor half-built while the broken item stays equipped.** After the throw the update
     stops before `addBodyParts`, and the delayed action stops too, so the broken weapon stays worn; every later change
     throws at the same place. The after run's frame 005 (10.6 s, after "off again", whose first change threw again)
     shows NPC A without chest, arms and neck. OpenMW 0.51 catches per part (`NpcAnimation::addOrReplaceIndividualPart`
     logs "Error adding NPC part" and returns; `CreatureWeaponAnimation::updatePart` logs "Can not add creature part"),
     so its update completes without the broken part. Recommended: the same catch in `Npc::attachPart` and
     `WieldingCreature::addPart`. It changes what a script sees (no exception) and is outside this task's brief.
  2. Items A and B change the default game, as recorded in their commits and the changelog.

**2026-11-04, fresh review of 0074d (task 0074e)** (`ffff7348e4` finding 1, `552288bb32` finding 7, `b252b644c4`
finding 3, `e8af356972` finding 9, `e16260dda3` finding 5, then the docs). The review is
`I:\Projects\OpenEden-work\0074d\REVIEW-0074d.md`: items A and B of 0074d match OpenMW 0.51, and
`unlinkController`/`removeAllParts` are correct, but the throw-path claims went further than the code. Method: full loop.
A separate test-writing agent wrote the tests from `I:\Projects\OpenEden-work\0074e\test-spec.md` (written from the
review) without seeing the implementation, against a tree of stub seams that keep the old behaviour; red run:
`I:\Projects\OpenEden-work\0074e\red` (`summary.txt`: 18 of the 29 tests failed, for the reason they name; the 11 that
passed are the guards, the helper check and the cases the old code already met). The stub tree was then replaced by the
real seams, one finding per commit, each built and its tests run (`openmw-tests` 978 of 978 at the end; the
implementing agent added two cases to `testcompiledupdates.cpp`, below). The red run is of the whole stub tree, not
repeated per commit.

- **Finding 1, use after free when a rebuild throws (pre-existing; 0074d's "safe" claim was wrong):** `updateNpcBase`
  replaced `animation` (destroying the old `MWAnim::Play`), then read the custom-bone files and the animations. A read
  that throws (`vsgUtil::readNode` and `vsg::read_cast` go through `vsgAdapters::vfs`) left
  `CharacterController::mAnimation` (refreshed only by `forceStateUpdate`), `Actor::mHead/mSpine/mRoot`, the Player's
  neck and the pose and IK bone pointers pointing into the freed Play; when a script or Lua catches, the game runs on
  into them. Now `makeBasePlan` decides the names (head, hair, skeleton, custom-bone files, animations), `applyBasePlan`
  reads the skeleton, every custom-bone file and the animations into locals, and only then runs `removeAllParts`, sets
  the models and swaps the Play; `runRebuild` (the rebuild's seam: apply, parts, compile, refresh) compiles and gives the
  `CharacterController` the new Play (`refresh` is `MechanicsManager::forceStateUpdate`) on every way out of the part
  update. A read that throws leaves the actor exactly as it was (corrected in 0074f, finding 2: its animation, bones and
  everything pointing into them; the type, view mode and part context were set before and changed). Tests: `NpcRebuildTest` (7), a reader that throws at
  the skeleton, the first and the second custom-bone file, the first and the last animation file: the Play is the same
  object with the same bones, `getHead()`, `mHeadModel`, a part inserted before and its controller are unchanged, and
  neither `parts` nor `refresh` ran. **Not proven in game (corrected in 0074f, finding 6: the reason first given here was wrong, and the werewolf
  rebuild was provoked there):** the text said every file a rebuild reads was read by the same actor's build before; but a
  werewolf's custom-bone files are first read at its rebuild. What the 0074e runs showed (`runs\f1-*`): the player's
  rebuild by the first/third person toggle works as before (no error, same frames, build before and after);
  `Player->SetWerewolf 1` from the start script changed nothing visible because `SetWerewolf` is not an mwscript command
  (it is `BecomeWerewolf`), so the werewolf rebuild was not exercised. `Npc::rebuild` itself (the
  one call into `runRebuild`) has no test: it needs a record.
- **Finding 7, a per-part catch, as OpenMW 0.51 (changes the default game):** `Npc::insertBoundedPart` catches a
  `const std::exception&` from `vsgUtil::readNode`, logs "Error adding NPC part: ...", returns false (nothing had changed
  yet), and `attachPart` then skips the sound entry; `WieldingCreature::addItemPart` (`addPart`'s work for one item, the
  seam) catches around `mContext.readNode` ("Can not add creature part: ..."). Reference: OpenMW 0.51
  `NpcAnimation::addOrReplaceIndividualPart` (`442eec6e2f:apps/openmw/mwrender/npcanimation.cpp`: slot and priority set
  before the `try`, `catch (std::exception& e)` logs and `return false` before the sound) and
  `CreatureWeaponAnimation::updatePart` (`creatureanimation.cpp`). The slot stays taken and the body part under it stays
  hidden, as in 0.51; the update completes (the actor gets its chest, arms and the merged body's regions).
  `runEquipmentUpdate` stays as the outer guard for the throw sites left (the ammunition, the holstered items and the
  quiver; a value that is not a `std::exception`). Tests: `PartReadErrorsTest` (3) and `CreaturePartReadErrorsTest`
  (1). **In game** (`runs\f7-before`, `runs\f7-after`: the 0074d clocks scene with its bad Eden Archive,
  `SKIP_STEPS=glove`, executables `openmw-0074e-before.exe`, the tree before this task, and `openmw-0074e-after.exe`):
  before, "Error in DelayedAction SetEquipmentAction: Failed to read 'meshes/oe_bad/weapon.nif'" and frame 003 (shirt and
  the bad weapon) shows NPC A with the shirt but no arms and no lower legs; after, "Error adding NPC part: Failed to read
  ..." and frame 003 shows the whole body; the creature's bad shield logs "Can not add creature part: ..." and the
  dremora keeps drawing. Both runs exit 0 with no dump.
  **Callers, corrected (review finding 2; the 0074d stage log, the guide and the changelog said "the delayed action or
  mwscript catches it and the game runs on", true for few callers):** Lua (`LuaManager::DelayedAction::apply`) logs;
  mwscript (`ScriptManager::run`) logs and puts the target in `mInactive`, so its script stays off for the session; the
  inventory, barter and container windows lose the rest of the frame's input (`Engine::frame`'s
  `InputManager::update` catch, `vsgengine.cpp`); a mechanics-driven change (torch auto-equip at dusk, `aicombataction`
  ActionEquip, autoEquip from spell effects and race changes, werewolf and vampire rebuilds) has no catch before
  `Game::run`'s (`game.hpp`: "Emergency Save", returns false) and `Engine::frame` pops the Game state: **the session
  ends**. A cell-load constructor is caught per object ("failed to render", `mwworld/scene.cpp`). None of this is a
  regression. With finding 7 a bad part file reaches none of them; what still does (ammunition, holstered items, the
  quiver, a rebuild's reads) is as listed. What changes for scripts, default game: mwscript no longer disables the
  target's script, Lua's `SetEquipmentAction` no longer logs an error (the renderer does), a mechanics-driven change no
  longer ends the session.
- **Finding 3, compile on an update worker (pre-existing, from V4's `a7c43f8346`; nothing changes by default: `[Game]
  weapon sheathing` is off):** `Npc::update` ran `updateQuiver(); compile();` for every actor but the player on a
  `vsgUtil::UpdateThreads` worker (`Scene::update` dispatches one operation per cell). The traversal is serialised
  (`CompileManager`), but `CompileContext::compile` then calls `vsg::updateViewer`, which writes the command graph, the
  view's bins and the database pager without a lock, so a worker and another worker or the cell preloader could race;
  the quiver edit touched the graph on the worker, and a failed compile ran `removeAllParts` (hence
  `PartControllers::forget`) there. Now `Npc::update` only leaves `mQuiverDirty` (set by `show()`), and
  `Npc::updatePendingQuiver` (`runPendingQuiver`: the change, then the compile also when the change throws; a
  `std::exception` is logged, not passed on, since it runs in the render update where an exception ends the session)
  runs on the main thread: `MWRender::Scene::update` after `mUpdateThreads->run` (which ends in `waitIdle`) for every
  actor (a `dynamic_cast` per actor per frame), `Player::update` for the player (already the main thread,
  `RenderManager::update` calls it). `partcontrollers.hpp` and the guide now say so. Tests: `QuiverUpdateTest` (6).
  **Not proven in game:** a smoke run with sheathing on (`runs\f3-after-sheath`) exits 0 with no dump and no error
  beyond the bad part's; no scene drew a weapon or a bow and checked the quiver, and a race is not reproducible by a
  run, so the argument is the code and the tests.
- **Finding 9, undefined behaviour in `addEnv` (pre-existing):** `WieldingCreature::linkPart` called `addEnv(node,
  glowColor, getTransparency(*this))` without a colour; `addEnv` (`mwrender/env.cpp`) dereferences it whenever the alpha
  is not 1, so an invisible or chameleon weapon-carrying creature changing to an unenchanted item read an empty
  `optional`. Guarded with `if (glowColor)`, as `Npc` does. `CreatureGlowTest` (2): the transparent creature's weapon
  gets no object data without a colour, and the colour and the alpha with one. Not seen in game (undefined behaviour).
- **Finding 5, the tests now reach the call sites:** `Npc::updateEquipment` and `WieldingCreature::updateEquipment` run
  their node work through `runCompiledUpdate` (the real `compile()`, also when the work throws); `Npc::rebuild` through
  `runRebuild`. `CompiledUpdatesTest` (11): a throwing callable (a `std::exception` and another value) through both
  seams; the real `updateEquipment` of a test actor (**these two cases were added by the implementing agent**: a test
  actor has no record, so its part update fails at once on the empty `Ptr`, `MWWorld::Ptr::getClass` throws, and the
  actor is still compiled once); a carried flickering light item gives the creature one flickering light; a weapon
  inserted and removed in one update leaves `mWeaponControllers`. **Mutations**, applied together, built
  (`openmw-tests`) and run, the source restored (`I:\Projects\OpenEden-work\0074e\mut`): `Npc::updateEquipment`
  compile-only-on-success fails `the_real_npc_update_equipment_compiles_when_its_part_update_throws`; the same for the
  creature fails `the_real_creature_update_equipment_compiles_when_its_part_update_throws`; `addItemPart`'s light back to
  nullptr fails `a_carried_flickering_light_item_gives_the_creature_one_flickering_light`; the Weapon case of
  `detachPart` deleted fails `a_weapon_inserted_and_removed_in_one_update_leaves_the_weapon_list`; `runRebuild` without
  the refresh on a throw fails `NpcRebuildTest.when_the_part_update_throws_the_actor_is_compiled_and_refreshed_with_its_new_play`
  (5 failed, 24 others passed). **Not caught:** `Npc::rebuild` bypassing `runRebuild` (it needs a record).
- **Finding 6, the merged body after a throw:** before finding 7 a throw in the equipment pass never reached
  `attachMergedBody`, and the kept copy was linked again (`relinkKeptControllers`) with the previous
  `mMergedHidden`/`mMergedSlots`: poke-through or a hole until an update succeeded. A `std::exception` from a part read
  no longer stops the pass, so the regions are recomputed in that case; for the throw sites that remain (ammunition,
  holstered items, the quiver) the stale coverage still exists until the next update that succeeds.
- **Finding 4, a visible default-game change missed in 0074d:** `MWAnim::decorate` rotates a node it is given a light
  for by -90 degrees about X (`components/mwanimation/decorate.cpp`; OpenMW 0.51 `ActorAnimation::attach(..., isLight)`
  rotates the same, `442eec6e2f:apps/openmw/mwrender/actoranimation.cpp`), and 0074d's `cafbc65b71` passed a creature's
  carried light, so a creature's torch is now drawn turned toward the camera at the hip instead of horizontal with the
  flame at its side (frames `0074d\runs\before\stock\frames\screenshot001.png` and
  `0074d\runs\after-all\stock\frames\screenshot001.png`). It matches NPC torches and 0.51. Recorded now in the changelog.
- **Finding 8, corrections to 0074d's text:** the changelog's "comes back when the item is added again" is wrong for a
  re-add after the count reached 0: that is a new stack and a new `Ptr` (`ContainerStoreIteratorBase::operator++` and
  `begin` skip count-0 entries, `containerstore.cpp`), so it worked before; what changed is that removing part of a stack
  no longer removes the light, and the map entry and node no longer leak. Test L2 (`testitemlights.cpp`) re-adds the same
  `Ptr`, which the container never does (it still guards the erase). `0074d\mut\summary.txt` said "b3: build failed";
  the rerun's `b3.tests.log` shows the 3 failures, now written there.
- **The final equipment check** (the after executable, `tools/mergedbody_check` `SCENE=equipment`, stock and merged,
  `runs\equip`): both exit 0 with no dump, 49 frames each, 32 actors draw the merged body; frames 009 (stock and merged),
  045 (stock) and 046 (merged) viewed: no chest through the pants or loincloth, the same visible body.
- **Not verified:** the Linux gate (full build and GCC `-Werror` on the changed units) and the Lua integration tests;
  a fresh reviewer has not read 0074e; first person beyond the toggle above; the werewolf rebuild; whether any vanilla
  creature carries a light. clang-format 14 was run on the changed lines (clean).
- **For toddhow (decisions):**
  1. **Finding 7, finding 3 (only with sheathing on) and the creature torch change the default game:** a bad part file
     no longer stops an equipment change (mwscript scripts stay on, mechanics changes no longer end the session, the
     actor keeps its other parts; the broken part's slot stays taken and its body part hidden, as 0.51); a creature's
     torch is drawn rotated -90 degrees (finding 4); a pending quiver change is done at the end of the actor's update.
  2. The remaining throw sites (ammunition, holstered items, the quiver, a rebuild's reads) still end a
     mechanics-driven session; 0.51 has no catch there either. Say if a catch there is wanted.

**2026-11-04, fresh review of 0074e (task 0074f)** (`d2b616254b` finding 1, `9fb873b017` finding 2, `6324222ba1`
finding 3, `1501829504` finding 4, `cdd5cb98a1` finding 5, `28d5607dde` comments). The review is
`I:\Projects\OpenEden-work\0074e\REVIEW-0074e.md`. Method: full loop. A separate test-writing agent wrote the tests from
`I:\Projects\OpenEden-work\0074f\test-spec.md` (written from the review) without reading `npc.cpp`, `player.cpp`,
`scene.cpp` or `wieldingcreature.cpp`, against a tree of seams that kept the old behaviour; red run
`I:\Projects\OpenEden-work\0074f\red` (`summary.txt`): 36 tests, 12 failed for the reason they name, 24 passed (the guards,
the finding 5 tests that pin behaviour 0074e already has, and a few cases the old code already met: the direct
`runRebuild` throw cases, the Npc-in-Scene and Player quiver cases). The seams were then put in one finding per commit with
the fix; each commit's tests passed (`openmw-tests` 1024 of 1024 at the end). The red run is of the whole stub tree, not of
each commit.

- **Finding 1, the stale `Wielding::mAttachAmmo`:** `components/mwanimation/wielding.hpp` holds a raw
  `vsg::Transform*` into the weapon part's subgraph, set by `Npc::insertBoundedPart` and `WieldingCreature::linkPart`.
  `Npc::detachPart` now clears it for the weapon slot (every way a weapon leaves goes through it: `PartSlots::remove`,
  `take`, `removeAllParts`), `insertBoundedPart` clears it before the read (a weapon that cannot be read leaves none),
  `WieldingCreature::removeParts` and `addItemPart` clear it. 11 tests (`AttachAmmoTest`, `CreatureAttachAmmoTest`).
  **In game (`runs\ammo-before`, `runs\ammo-after`: an NPC and a dremora with a short bow and arrows, then the bad weapon
  and eight more equipment changes, `tool\mod\scripts\mbammo_*.lua`):** both exit 0, no dump, 9 "Error adding NPC part" and
  9 "Can not add creature part" lines each. The old code's write through the stale pointer is not visible: it is a write
  to a node of a removed subgraph, which crashed nothing on this machine in 8 changes (finding it would need page heap,
  a system setting, which is not changed here). So finding 1 is proven by the unit tests (red against the old code, and
  caught by the two mutations below), not by the scene.
- **Finding 2, the type, view mode and part context after the reads:** `Npc::updateParts` set `mNpcType` before
  `rebuild()`, and `Player::setViewMode` set `mViewMode` and `mPartContext` before it; when the reads then threw, the
  werewolf or vampire kept its human base for good and the player was in the new view with the old `Play` (a repeated
  `setViewMode(m)` returned at once). `BasePlan` now carries them (`mNpcType`, `mViewMode`, `mPartContext`) and
  `applyBasePlan` commits them after the reads, before `removeAllParts`; `Player::setViewMode` calls
  `Npc::rebuild(view, context)`. A plan that sets none of them (the old tests') changes nothing. **Wording corrected:** the
  0074e text "a read that throws leaves the actor exactly as it was" (this log, the header comment, the guide) was true for
  the animation, its bones and everything that points into them; the type, view mode and part context were set before and
  so changed. Side effect: `Npc::setVampire`'s `rebuild()` now commits the type too, so the next `updateParts` no longer
  rebuilds a second time. Seams for the tests (never used by the game): virtual `Npc::makeBasePlan(view, context)`,
  `getNpcType()` and `refreshController()`, `Player`'s `TestSetup` constructor. 10 tests (`PlanCommitTest`,
  `PlanCommitPlayerTest`).
  **In game (finding 6's repro, below):** with the retry, a file that stays unreadable makes EVERY equipment change of that
  actor throw again (`runs\wolfb-0074f-after2`: five "Failed to read" errors in 12 s, one per change); before, one throw
  and the player stayed human silently (`runs\wolfb-0074f-before`: one error). That is what the review asked for, and it is
  a decision for toddhow (below).
- **Finding 3, the inventory doll's quiver:** `Preview::animate` now calls `updatePendingQuiver` after `update(0)`
  (`Preview::rebuild`'s `show(Weapon, true)` asks for a change with sheathing on; the doll is no `Scene` object). The guide
  and `partcontrollers.hpp` name all three callers. Not tested (a `Preview` needs the render context); off by default. **No in-game before and after:** `updateQuiver`
  returns at once for an actor without the bone `Bip01 Ammo` (`npc.cpp`, `Npc::updateQuiver`), and the base skeletons
  of Morrowind (`xbase_anim.nif`, `base_anim.nif`, `base_anim_female.nif`, `base_animkna.nif`, byte search) do not
  contain that name, nor do the three archives hold any `*_sh.nif` sheath model: the doll draws no quiver on this data,
  so a frame could not differ.
- **Finding 4, the main-thread loops are reached by tests:** `MWAnim::Object::updateOnMainThread()` (virtual, empty);
  `Npc` overrides it with `updatePendingQuiver`; `Scene::update` calls it for every actor object after
  `UpdateThreads::run` (replacing the per-actor `dynamic_cast`) and `Player::update` calls it on itself.
  `Scene::addObject` (the end of `insertNPC` and `insertCreature`, now shared) lets a test fill a `Scene`. 5 tests
  (`MainThreadUpdateTest`: counting objects see their `update` on a worker and `updateOnMainThread` on the test thread
  after the last `update`; a real `Npc` with sheathing compiles on the test thread; the player's `update` does the
  change). Mutations (below): removing the `Scene::update` loop fails 2 of them, the player's call 1, the override 3.
- **Finding 5, tests for the failure state:** `PartFailuresTest` (8) and `CreaturePartFailuresTest` (2): `attachPart`'s
  sound skip (a failed read returns before the inventory lookup, which a test actor's empty `Ptr` turns into a throw, so
  `attachPart` with a good read throws and with a bad one does not), the slot staying taken (`mSlots.priority`, `take`
  refused, `addBodyParts` asks for nothing for it), `Body::hiddenMergedSlots` still hiding the region, a `std::logic_error`
  caught like any `std::exception`, and a value that is not a `std::exception` and a `vsg::Exception` passing through
  `insertBoundedPart` and `attachPart` with the slot taken and nothing attached. They pin what 0074e did, so they pass
  before and after; the mutation runs show they catch the changes they are for.
- **Finding 6, the false argument and the repro:** the 0074e text "every file a rebuild reads was read by the same actor's
  build before, so a rebuild-time throw cannot be provoked" is wrong for the files that differ between builds:
  `Npc::animationSources` picks other skeleton and animation files for a werewolf and for first person, and
  `Npc::appendCustomBoneFiles` (with `[Game] weapon sheathing`) lists every `.nif` in `animations/<skeleton name>/`, so
  the werewolf's custom-bone files are first read at the werewolf rebuild. What I found when I tried each:
  - **First person:** its skeleton, animations and custom-bone files are read when the new game starts (a bad
    `meshes/xbase_anim.1st.nif` or `animations/xbase_anim.1st/*.nif` gives "Failed to start new game"), so a toggle reads
    only cached files and cannot throw.
  - **The werewolf's skeleton and animations** (`meshes/wolf/skin.nif`): a bad Eden Archive entry does not make the rebuild
    throw (`npc.readactor` takes 0.003 ms in the traced werewolf rebuild, so the read is probably served from a cache: an
    inference, not shown; why it would be cached when the first-person file is not was not found); only the werewolf's body part reads of the same file throw, and
    those are caught per part since 0074e.
  - **The werewolf's custom-bone file** works: a bad entry `animations/wolf/skin/oe_bad_bones.nif` in an Eden Archive,
    `[Game] weapon sheathing = true`, and the player turned into a werewolf by `Player->BecomeWerewolf` in a start script
    (`SetWerewolf` is no mwscript command, so 0074e's "SetWerewolf changed nothing visible" was a script compile error:
    `tool\make_wolf_esp.py`), with the camera in third person (first person crashes, below). The player then changes
    equipment four times (`tool\mod\scripts\mbwolf_player.lua`, `SCENE=wolf`, packs in `pack\`, runs in `runs\wolfb-*`):

    | Executable | Result |
    | --- | --- |
    | `openmw-0074e-before.exe` (before 0074e's finding 1) | "Execution of script oe_wolfstart failed: Failed to read ...", then a crash dump (exit 124: the harness gave up): consistent with the use after free of 0074e's finding 1 (the throw was caught by mwscript, the game ran on and then wrote a dump); the dump was not decoded (no PDB for this executable) |
    | `openmw-0074e-after.exe` (0074e) | the same error once, exit 0, no dump: consistent with 0074e's finding 1 working (the game ran on after the throw) |
    | `openmw-0074f-before.exe` (this task's stub tree: 0074e plus inert seams) | the same, once; the later equipment changes log nothing: the actor never retries |
    | `openmw-0074f-after2.exe` (the five commits) | the first error, then one "Error in DelayedAction SetEquipmentAction: Failed to read ..." per equipment change, exit 0, no dump: finding 2 retries |
- **Finding 7, the mutation evidence:** the five mutations of the 0074e log are rebuilt as real unified diffs of the
  committed sources (`I:\Projects\OpenEden-work\0074f\mut\patches`, made by `make_patches.py`), plus mutations for finding 7
  (the per-part catch, Npc and creature) and finding 9 of 0074d's review (the `addEnv` guard), for finding 5's sound skip,
  and for every behaviour fix of this task (17 patches in all). `mut\run_all.sh` applies each under the build lock
  (`mutrun.bat`: `git apply`, build `openmw-tests`, run ALL of `openmw-tests`, `git apply -R`), keeps the command line and
  the full logs (`mut\logs\<name>.{apply,build,tests}.log`, `mut\summary.txt`), retries a build that fails (another
  session's compile error, or a game of another session holding a DLL: LNK1168) and checks that the tree is clean
  afterwards. The old run's `timeout: invalid time interval '/t'` was GNU `timeout` found before Windows' `timeout.exe`
  when the batch file was started from Git Bash; the batch files now call `%SystemRoot%\System32\timeout.exe`. All 17
  mutations were caught, each by at least the test that was named (a mutation run lists every failing test):

  | Mutation | Failing tests (besides the named one, where more) |
  | --- | --- |
  | 0074e: `Npc::updateEquipment` compiles only on success | `CompiledUpdatesTest.the_real_npc_update_equipment_compiles_when_its_part_update_throws` |
  | 0074e: the same for `WieldingCreature::updateEquipment` | `...the_real_creature_update_equipment_compiles_when_its_part_update_throws` |
  | 0074e: `addItemPart` passes no light | `...a_carried_flickering_light_item_gives_the_creature_one_flickering_light` |
  | 0074e: `detachPart`'s Weapon case removed | `...a_weapon_inserted_and_removed_in_one_update_leaves_the_weapon_list` |
  | 0074e: `runRebuild` without `refresh` on a throw | `NpcRebuildTest.when_the_part_update_throws_...` and `PlanCommitTest.a_throw_in_the_part_update_...` |
  | finding 7: `insertBoundedPart` rethrows | 8: 3 `AttachAmmoTest`, 4 `PartFailuresTest`, `PartReadErrorsTest.a_part_whose_model_read_throws_is_skipped_and_the_parts_around_it_are_kept` |
  | finding 7: `addItemPart` rethrows | 3: one each of `CreaturePartReadErrorsTest`, `CreaturePartFailuresTest`, `CreatureAttachAmmoTest` |
  | finding 9: `addEnv` without a colour | `CreatureGlowTest.a_weapon_with_no_glow_colour_on_a_transparent_creature_gets_no_object_data` |
  | 0074f finding 5: `attachPart` does not return after a failed read | 4 `PartFailuresTest` |
  | 0074f finding 1: Npc clears nothing / creature clears nothing | 5 `AttachAmmoTest` / 2 `CreatureAttachAmmoTest` |
  | 0074f finding 2: `updateParts` sets the type first / `setViewMode` sets the view first / `applyBasePlan` commits before the reads | 1 / 1 / 5 |
  | 0074f finding 4: no loop in `Scene::update` / no call in `Player::update` / empty `Npc::updateOnMainThread` | 2 / 1 / 3 |

  Not caught, and why: `Preview::animate`'s call (finding 3) has no test; `Npc::rebuild`'s own call into `runRebuild`
  (needs a record).
- **Finding 8, wording:** the 0074e "For toddhow" item 1 listed finding 3 with the changes of the default game; it only
  matters with `[Game] weapon sheathing` (off by default). The quiver change is not a frame later than before: it runs in
  the same `Scene::update`, right after the workers, and `Player::update` runs before it in `RenderManager::update`; the
  only difference is that it now runs after `Wielding::update` and `mFace->update` instead of inside `Npc::update` (done at
  the end of the actor's update). I found no "one frame later" (or "frame later") about the quiver in the repository at `a24b553975`, in any commit
  message of 0074e or in the code comments (`git grep`; the only hit in this log is 0074c's unrelated light flicker), so
  the phrase was probably in a report and not in a file; the statement above is the correct one.
- **Found on the way, NOT fixed here; FIXED in task 0074g (below):** `Actor::assignBones` (`components/mwanimation/actor.hpp`) is not
  virtual, so `Npc::applyBasePlan`'s call never reaches `Player::assignBones` (which sets the first-person neck bone
  `mNeck`). 0074e's review wrote "assignBones virtual so Player::assignBones still runs": it does not. `Player::setViewMode`
  calls `assignBones()` itself, but a rebuild through `RenderManager::rebuildPtr` (the werewolf and vampire changes:
  `MechanicsManager::setWerewolf`, `World::reattachPlayerCamera`) leaves `mNeck` pointing into the freed `Play` when the
  player is in first person, and `Player::manualAnimation` crashes in `MWAnim::Play::isRotationControlled`
  (`player.cpp:50`, `play.cpp:95`, read from the decoded crash dump of `Player->BecomeWerewolf` in first person with good data;
  `0074d`-before and `0074e`-before also crash in this scene; their dumps were not decoded, no PDB, so the same site is
  likely, not shown). Fix proposed: make
  `Actor::assignBones` virtual. A second route, from reading the code and not run: when the player toggles from first
  person back to third and the part update throws after the swap, `setViewMode` exits before its own `assignBones()`,
  and `mNeck` still points into the freed first-person `Play` (also true in 0074e); the same fix closes it. Not done: it
  is inherited code outside the task.
- **Final checks:** `SCENE=equipment`, stock and merged, `openmw-0074f-after3.exe` (HEAD `28d5607dde` plus other
  sessions' uncommitted renderer files; `runs\equip`): both exit 0 with no dump, 49 frames each, 32 actors draw the merged
  body; frames 009 (stock and merged), 045 (stock) and 046 (merged) viewed: no chest through the pants or loincloth, the
  same visible body. clang-format 14 clean on the changed lines (`fmtdiff.sh`).
- **Not verified:** the Linux gate (GCC `-Werror` build and tests) and the Lua integration tests; a fresh reviewer has not
  read 0074f; the Preview fix; first person; finding 1 in game (above).
- **For toddhow (decisions):**
  1. **Finding 2's retry makes a permanently bad rebuild file repeat its error on every equipment change.** For the
     player or a script that is one logged error per change; for an actor changed by the mechanics (torch auto-equip,
     AI combat, spell effects) a throw in the rebuild's reads still ends the game session, as in 0.51 (it has no catch
     either). Before 0074f the first failure left the actor human for good and nothing more happened. Say if a rebuild
     that fails should instead be given up after one try, or caught like a part.
  2. **`Actor::assignBones` is not virtual** (above): a player who becomes a werewolf or vampire in first person crashes.
     Recommended: make it virtual, in its own task.
  3. `Npc::setVampire` now records the new type, so one rebuild fewer; no visible change.

**2026-11-04, the neck bone after a rebuild (task 0074g)** (`9b654c3917`). The crash 0074f found and did not fix: a player
who becomes a werewolf or a vampire in first person crashed. Fixed; the Linux gate, the Lua tests and a fresh review are
not run.

- **Verified in the code, at `20ee7f80c0`:**
  - `MWAnim::Actor::assignBones` (`components/mwanimation/actor.hpp`) was `void assignBones();`, not virtual. It finds
    `mHead`, `mSpine[0..1]` and `mRoot` in `animation->bones` (`actor.cpp`). `MWAnim::Object::assignBones`
    (`object.hpp:64`) is another function, an empty inline, hidden by Actor's; `Npc` and `Wielding` declare none.
  - Callers: `Npc::applyBasePlan` (`npc.cpp`, last line), through `Npc` and so statically `Actor::assignBones`;
    `Player::Player` and `Player::setViewMode`, which call `Player::assignBones`; `MWAnim::create<ObjectType>` and
    `createCreature<Creature>` (`create.hpp`); `CSVRender::Actor::create` (`apps/opencs/view/render/actor.cpp`, an
    `MWAnim::Actor`); two tests.
  - `Player::assignBones` (`player.cpp`) sets `mNeck = animation->bones.search("bip01 neck")` in first person (else
    null) and then calls `Npc::assignBones()` (qualified).
  - Both rebuild routes reach `applyBasePlan` without `Player::assignBones`: `RenderManager::rebuildPtr` ->
    `Npc::rebuild()` -> `runRebuild` (reached from `Npc::setVampire` through `World::reattachPlayerCamera`, and from
    `MechanicsManager::setWerewolf`), and `Player::setViewMode` when the part update throws after the swap (it leaves
    before its own `assignBones()`).
  - **Other pointers into the Play:** `mHead`, `mSpine` and `mRoot` are refreshed by the same function, so they were
    never stale. The pose driver and the IK controller are built again by `CharacterController::forceStateUpdate` ->
    `createPoseControllers` when the actor's animation is a new one (`Npc::refreshController` calls it after every
    rebuild, also when the part update throws: `runRebuild`). `Camera` (`worldTransform("Camera")`), `Preview`
    (`getHead()`), `updateHeadTracking` and the Lua `hasBone` look a bone up on each call. I found no other raw bone pointer
    kept in an actor by reading (`grep` of `bones.search(`, `searchBone(`, `worldTransform(`, `headTransform(`); an
    effect attached to a bone of the old skeleton (`effect.cpp`) was not followed further.
- **Tests first** (`PlayerNeckTest`, `apps/openmw_tests/mwrender/testplancommit.cpp`, written by the implementing agent: no
  separate test-writing subagent, a deviation from the full loop). The `Player` test constructor of 0074f, its virtual
  `makeBasePlan` and `refreshController`, and one seam added with the fix: `Player::mNeck` and `assignBones` are
  protected. Seven tests: `assign_bones_through_the_base_class_finds_the_first_person_neck` and `..._clears_the_neck_in_third_person`
  (the call `applyBasePlan` makes); `a_rebuild_of_a_first_person_player_gives_it_the_neck_of_the_new_animation` (`runRebuild`);
  `rebuild_of_a_first_person_player_gives_it_the_neck_of_the_new_animation` (the public `rebuild()` of `rebuildPtr`);
  `set_view_mode_to_third_person_clears_the_neck_when_the_part_update_throws` and
  `set_view_mode_to_first_person_finds_the_neck_when_the_part_update_throws` (the second route); and the guard
  `a_rebuild_of_a_third_person_player_leaves_no_neck`. Each rebuilding test asserts that the old neck exists, that the new
  Play is another object and that its neck differs from the old one (the new Play is built before the old is released),
  and compares pointers only. **Red run** (`I:\Projects\OpenEden-work\0074g\red-run.log`, seam in, no fix): 6 failed with
  plain assertion failures (null where the neck should be, or the old neck where the new one should be), the guard passed,
  the 10 `PlanCommit*` tests passed. **Green** (`green-run.log`): 17 of 17; whole `openmw-tests` 1031, `eden-studio-tests`
  702 and `components-tests` 5,815 pass on Windows.
- **The fix:** `Actor::assignBones` is `virtual`; `Player::assignBones` is `override`. The three other ways it is called are
  unchanged: `Player::Player` still calls it (a virtual call in `Npc`'s constructor, through `updateNpcBase`, reaches
  `Actor::assignBones` only), `Player::setViewMode` still calls it after its rebuild (now redundant, left), and `Object`'s
  no-op is not virtual (`create<ObjectType>` needs nothing from it). The `Npc`, `Wielding` and `WieldingCreature` classes
  and the editor's `MWAnim::Actor` have no override, so their behaviour is the same. Destruction: nothing in a destructor
  calls it.
- **In game** (`OPENEDEN_QUIET_WINDOW=1`, copies of the config and user folder under `I:\Projects\OpenEden-work\0074g`,
  `tool\check.sh` of 0074f with a unique executable name each for the watchdog, which kills only that name at the first
  `.dmp`; the player stays in first person: `SKIP_STEPS=third`; `runs\`). Executables: `openmw-0074g-before.exe` is the
  shared `openmw.exe` of 14:10, built before any edit of this task (the shared tree, with other sessions' uncommitted
  files, as in 0074f), `openmw-0074g-after.exe` the tree with this fix (and other sessions' files again).

  | Scene | Before | After |
  | --- | --- | --- |
  | Werewolf: `Player->BecomeWerewolf` in `oe_wolfstart.esp` six seconds in (`SCENE=wolf`) | 3 crashes in 4 runs (the run that survived was the first, without the load trace) | 0 in 4 (exit 0, no dump) |
  | Vampire: `types.Actor.spells(self):add('vampire sun damage')` (the ability with the Vampirism effect; there is no `BecomeVampire` command; `SCENE=vampire`, `mbvamp_player.lua`; Vampirism magnitude 1 in the log) | 3 crashes in 3 | 0 in 3 |

  The three werewolf dumps were read with the stack walker (`dumpstack\ds7.exe` against the PDB of that build, copied to
  `pdb-before`): the stack scan of each lists `MWRender::Player::manualAnimation` (`player.cpp:50`) and
  `MWAnim::Play::isRotationControlled` (`play.cpp:95`). The vampire dumps list `Play::isRotationControlled` and
  `MWAnim::Actor::manualAnimation` (`actor.cpp:60`) and no `Player::` frame: **that is a stack scan, not an unwind** (the
  `~` lines), so the site is not pinned to `mNeck` for the vampire; that the fix removes the crash in all three runs is the
  evidence. That a rebuild happened in the "after" runs is inferred from the load trace: 27 `npc.readactor` scopes in a werewolf
  run, the last two at the time of the transformation (one with a 5.6 ms file read), 26 in a vampire run; there is no log line
  for a rebuild. The "before" runs write no trace (they crash). A use after free does not have to crash: the surviving run
  shows it. Frames of the "after" runs show the normal first-person view (the vampire's effect icons appear).
- **Not proven / not done:** the Linux gate (GCC `-Werror`), the Lua integration tests, a fresh reviewer, a third-person
  play after the change, `UndoWerewolf`, the editor's actors in the window (it builds and its tests pass), a second
  look for other stale pointers beyond the reading above, a build of the "before" tree with an address sanitizer.
- **For toddhow:** nothing to decide beyond merging. Two small notes: `Player::mNeck` and `Player::assignBones` became
  protected (a test seam, like 0074f's); and `Player::setViewMode`'s own `assignBones()` is now redundant (kept).
