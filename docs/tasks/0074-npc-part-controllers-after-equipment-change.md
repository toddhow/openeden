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
blocker and six gaps of the same family; the extracted review text (`I:\Projects\OpenEden-work\review0074.md`) was
empty, so the findings were taken from the brief. Method: tests first, run red; no separate test writer or reviewer
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
  update. **Reachability:** not through data, as far as checked. `vsgUtil::readNode` throws only when the read gives
  no node, and the node options end with `Resource::Fallback` (`createNodeOptions` in `resourcesystem.cpp`:
  `marker_error.nif`, else an empty node), while `nif::read` catches `std::exception`; in game a weapon whose model was
  missing and one whose model was a PNG both drew the fallback ("Using fallback object") and nothing threw. The guard
  is defensive.
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
    1) and the dremora's own group run on, no timer stepping off the dt by more than 0.0001 s. Merged variant: the same,
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
