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
    045 (naked again, front) and 046 (wrist) show it through the loincloth, with the hand off the forearm. After: none
    of it; the chest, hands and loincloth line up in all four. A third run on the committed build shows the same.
  - Face: a work copy of the scene adds, after the 200 cycles, one change the head survives (naked to shirt and pants),
    then 68 close-ups of the face 0.2 s apart, with `core.sound.say` of a voice line from the 6th second (40 shots
    with `isSayActive` true in both runs). Before: the eyes never close and the mouth never moves. After: the eyes
    close in two consecutive shots in the first 6 s and the mouth opens in many of the talking shots.
  - Merged: before and after look the same in frames 009, 010, 045 and 046; 32 actors draw the merged body in both
    (and in stage e's run).
- **Checks:** clang-format 14 clean on the four files; GCC with CI's flags plus `-Wshadow -Wunused -Wdangling-else
  -Wrange-loop-construct` (`-fsyntax-only`, in the Linux image) clean on `npc.cpp` and the test; MSVC no new warning.
- **Not verified:** NPCs other than the player in motion (the row's NPCs are dressed by a script, so they went through
  the same path, but no frame was compared); first person and the head-only view; item lights from the inventory and
  the spell cast glow, which `clearControllers` also drops and nothing links again (out of scope, not checked); the
  full Linux gate and the Lua integration tests.
