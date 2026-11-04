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

- [ ] Unit tests (written first, red against a stub) of `PartControllers`: a part inserted during the update is not
  linked twice; a surviving part is linked again into the same list it had; a removed part is never linked again (its
  controllers point into a node that is being freed); the head's entries go to the face list; an autoplay entry keeps
  its kind; the order is by part type, then insertion.
- [ ] The equipment scene of `tools/mergedbody_check` (`SCENE=equipment`, stock variant): frames 009, 010, 045 and 046
  show no chest through the pants or loincloth; the chest follows the animation (viewed).
- [ ] Face: after an equipment change the head still blinks and the mouth moves when talking (viewed or traced).
- [ ] The merged variant of the same scene is unchanged (viewed; merged body count unchanged).
- [ ] This changes the default game (the merged body off): it is a bug fix (a part frozen in the wrong pose is not a
  vanilla mechanic), recorded for toddhow.

Out of scope: the merged body's own controllers (already re-attached), first person, creatures.

## Stage log

(filled in as the work goes)
