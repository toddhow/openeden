# Task: The neutral world model, stage N2 (review, freeze to 1.0, Eden Studio viewer switched) and the N3a plan

Design doc section: `docs/design/neutral-world-model.md` sections 5, 9 and 10 (decision 0016)
Goal: review the draft SDK types against what N0 and N1 learned, freeze `world_model.hpp` and `world_source.hpp` at version 1.0, make Eden Studio's Skyrim cell viewer read the neutral store instead of the pack's `WorldView`, and write the detailed plan of N3a without implementing it.
Inherited code touched: none changed in `apps/openmw` beyond the Morrowind source's new lookups in `apps/openmw/mwworld/neutralprojection.*` (added in N1, unused by the engine). Eden Studio: `apps/opencs/model/mesh/{cellscene,skyrimcellsource}.*`, `apps/opencs/view/render/meshscene.cpp` (the viewer, added in task 0086).
Inputs / outputs: `sdk/include/openeden/game/world_model.hpp` (1.0), `world_source.hpp` (1.0, four lookups at the end of `WorldCellSource`), `components/worldmodel/store.*` (forwarding lookups), `game_packs/skyrimse/.../neutral_world.*` and `apps/openmw/mwworld/neutralprojection.*` (the lookups and units), the viewer files above; tests in `apps/components_tests/worldmodel/{testfrozen,testlookups}.cpp`, `apps/components_tests/skyrimse/testskyrimsecelllookups.cpp`, `apps/openmw_tests/mwworld/testneutralprojectionlookups.cpp`, `apps/opencs_tests/model/mesh/testcellsceneneutral{,real}.cpp`; `CI/check_extension_sdk.sh`.
Constraints: no engine behaviour change; no `Host` accessor and no `kSdkVersion` change; layer exceptions stay at 104; main thread only.
Acceptance criteria:
- [x] Every change the review found necessary is applied and the ones refused have a reason (design doc section 9).
- [x] Both headers say 1.0; CI and layout tests pin them (versions, enumerator values, field order, defaults, the virtual function order, old-style sources still compile).
- [x] The viewer reads the neutral store and shows the same cells: equal object counts per cell on the installed masters, equal scenes to rounding, byte-identical frames.
- [x] The detailed N3a plan with risks, stages and tests is written (section 10); nothing of it is built.
- [ ] A Morrowind cell in the Studio through the neutral interface: not done on purpose, options recorded (section 9).
Out of scope: the engine consuming the neutral model (N3a), the `Host` accessor, light/terrain/water components, saves.

## Stage log

### N2 (2026-11-10; same hand wrote tests and code, no fresh reviewer; Windows only; local commit, not pushed)

**Rebase.** The Skyrim decoding agent's commits (`7fed8677d2`, `122296b57a`: `TeleportComponent::flags`, the pack-internal `ComponentSource`) were taken before the review; the review decided to keep the `ComponentSource` out of the frozen contract (section 9 finding 6).

**Order of work, honestly.** The tests were written before the code (frozen pins, store lookups, the Skyrim and Morrowind lookups, the Studio neutral overload and its real-data check) but were **not run red against stubs**: they were first run after the code existed, and passed. The only red evidence is for `testfrozen.cpp`: compiled against the headers of the parent commit it fails with 12 errors (`I:\Projects\OpenEden-work\nwm2\red\red.txt`: the four new virtual functions "did not override"). The others are not shown to fail against the old behaviour; their sensitivity is by reading. A mutation check was not done.

**Tests.** components-tests: `worldmodel/testfrozen.cpp` 6, `testlookups.cpp` 4, `skyrimse/testskyrimsecelllookups.cpp` 2 (the filter `Skyrim*:Neutral*:WorldModel*:WorldSource*:Content*`: 249 pass; the whole binary was not run: its Slang tests take tens of minutes); openmw-tests `testneutralprojectionlookups.cpp` 3 (whole binary 1,664 pass); eden-studio-tests `testcellsceneneutral.cpp` 5 and the gated `testcellsceneneutralreal.cpp` 1 (whole binary 1,050 pass).

**Real data** (Skyrim SE masters, aggregates): 759 interior and 1,972 exterior cells (every 37th), 522,969 objects, boxes-only and with a stand-in mesh through both paths: 0 mismatches of counts, nodes, indices, vertex counts; worst vertex difference 0.000015 units. Editor frames of `WhiterunBanneredMare` (697 objects, 655 meshes, 15 boxes, 284,695 triangles), `WhiterunDragonsreach` (1,441 objects) and `@Tamriel,6,-2,1` (318 objects): log lines identical and **PNG frames byte-identical** to the executable built from the parent commit (`I:\Projects\OpenEden-work\nwm2\run\before` and `run\after`; run again after clang-format: identical); frames viewed.

**Checks.** `CI/check_extension_sdk.sh` (with the new major-version pin), `check_layer_rules.sh` (104 exceptions), `check_tests_registered.sh`, `check_game_conventions.sh`, `check_mwbase_fakes.sh`, `check_no_osg.sh`: pass. clang-format 14 (docker `openeden-cf14`) applied to new files and to the changed lines only.

**Not run.** The Linux gate (no GCC check of the new files), clang-tidy, sanitizers, the Lua tests, the whole `components-tests`, a fresh review, a mod-list exterior comparison. A Morrowind cell in the Studio (see the design doc for options).

**Decisions made here (toddhow may overturn).** The lookups are virtual functions with "not supported" defaults rather than pure functions (so 0.1-style sources and the N1 test doubles still compile); `unitsPerMetre` 0 means not stated; the Skyrim value is Morrowind's, not measured; `ComponentSource` is not frozen; `ownerIsFaction`, lights, water and terrain are not in 1.0; no `Host` accessor in N2. `tools/skyrimcell_check/check.sh` got an `EXE=` variable for before/after runs.
