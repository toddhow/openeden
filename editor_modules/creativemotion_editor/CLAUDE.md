# creativemotion_editor

CreativeMotion's editor module (task 0032): the Rig & Motor Limits Inspector (plan section 5.1, item 2). A separate library that only Eden Studio loads, from its fixed core list (decision 0007).

- Built against the Editor Module API (`sdk/include/openeden/editor_module`) and CreativeMotion's `opas::rig` only. No Qt, no Studio type, no engine type. `components/creativemotion` is read, never patched.
- Units: meters, radians, Z up, as CreativeMotion; the one conversion is `convert.hpp` (the type only). Studio converts meters to world units in its overlay.
- Property ids: `rig.*`, `joint.<bone>.<field>`, `profile.rig`, `default.<field>`, `joint.<bone>.<motor field>` (see the task). An edit that would make an invalid rig is refused by running `validateRig` on the result.
- A cone's swing limits cannot be edited as single numbers (min = -max); since API 1.2 (task 0091) a cone is edited by dragging its two half angle handles (`creativemotion.cone.plane` at the rim point t = 0, `creativemotion.cone.normal` at t = 90 degrees, shown for the selected cone joint only; `SetConeHalfAngleCommand` sets min = -max, the result must pass `validateRig`).
- Selection (API 1.2): `JointSelectable` (implemented by `RigDocument`) narrows the panel to the selected joint and draws it in the highlight colour with its name as a label; the module follows the host's selection (`{"joint", bone}`), turns a click on `creativemotion.joint.<bone>` into it, and has Next Joint and Previous Joint commands. The gizmo holds the document's rig by non-const reference: a drag changes it through a command only.
- `creativemotion_editor_export.cpp` (the entry point) is only in the shared library: the tests link the other sources and already have another module's entry point.
- Check in the real editor: `OPENEDEN_CS_RIGCHECK` (see `apps/opencs/CLAUDE.md`) with CreativeMotion's `data/rigs/humanoid.rig.json` and `data/profiles/humanoid-default.profile.json`.
