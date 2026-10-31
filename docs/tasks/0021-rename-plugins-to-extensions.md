# Task: rename engine plugins to extensions
Design doc section: `docs/decisions/0006-engine-plugins-are-extensions.md`
Goal: call OpenEden's native code system extensions everywhere, with no change in behaviour.
Inherited code touched: only lines OpenEden added to inherited files (`vsgengine.*`, `game.hpp`, `mwworld/scene.cpp` and others), at commit 6fe097b474.
Inputs / outputs: every name in the decision's table; the launcher's translation files regenerated.
Constraints: no behaviour change; `plugin` meaning an ESP, Qt or OSG plugin stays; `components/creativemotion` untouched; decisions and specs keep their wording.
Acceptance criteria:
- [ ] No use of "plugin" for this system remains outside history documents, checked by listing what is left
- [ ] Unit tests pass with the same counts as before the rename, and the real-game check scripts and Lua tests pass
- [ ] With no extension enabled, the game runs as before (the unit test for zero extensions and the Lua tests)
- [ ] `lupdate` leaves the launcher's translation files unchanged
Out of scope: the new content file format, stable IDs and the save layout, which are ideas not yet decided.

Workflow: full loop by area, except that no new tests are written: existing tests are renamed with the code (toddhow's approval of the rename covers that) and must keep passing unchanged in count.
