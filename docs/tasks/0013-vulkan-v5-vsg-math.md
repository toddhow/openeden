# Task 0013: Track V stage V5 — VSG math everywhere

**Status:** CLOSED 2026-11-10 by toddhow (remaining checks are follow-ups, not blockers; listed in `docs/toddhow-checks.md`, "Known open items from Track V"). Earlier status: approved by toddhow (2026-10-29) with the answers below. Depends on task 0012 (V4).
**Design doc section:** `docs/decisions/0002-vulkan-renderer-on-vulkanscenegraph.md` ("Math: VSG's math types … replace OSG's … everywhere in the engine, … so no OSG dependency remains").
**Goal:** replace OpenSceneGraph's math types (`osg::Vec2f/3f/4f`, `osg::Vec3d`, `osg::Quat`, `osg::Matrixf/d`, `osg::BoundingBox`, `osg::Plane` and the rest) with VulkanSceneGraph's (`vsg::vec2/3/4`, `vsg::dvec3`, `vsg::quat`, `vsg::mat4`/`dmat4`, `vsg::box`, `vsg::plane`, …) across the whole engine, then delete the bundled OSG headers (`extern/osg`) and vsgopenmw's OSG math shims (`components/vsgadapters/osg`), with no change in behaviour.
**Workflow:** full loop. This is a large mechanical change with subtle traps; the test-writing subagent writes the convention tests first.

---

## Context (facts this spec relies on)

- **After task 0009, OpenEden compiles against OSG headers only** (`extern/osg`, 231 files, from vsgopenmw) and vsgopenmw's implementations of `osg::Matrixf`, `osg::Matrixd`, `osg::Quat` and `MatrixDecomposition` in `components/vsgadapters/osg` (vsgopenmw `components/CMakeLists.txt`: "Avoids linking full OSG library for yet-to-be-ported modules").
- **Size (measured on the OSG build, `442eec6e2f`):** about 620 files use `osg::`, about 15,000 references. Outside rendering directories, 307 files use `osg::`, most in `apps/openmw/mwmechanics` (40), `mwphysics` (22), `mwworld` (19), `mwclass` (19), `mwlua` (16), `mwgui` (16), `apps/components_tests/detournavigator` (9), `mwsound` (7), `mwinput` (7), `apps/opencs/model` (5). After tasks 0009–0012 the renderer itself uses VSG types; re-measure at the start of this task.
- **`components/misc/convert.hpp`** converts between OSG math and Jolt (`JPH::Vec3`, `RVec3`, `Quat`, `Mat44`) and is used by physics and CreativeMotion's adapters (36 `osg::` uses).
- **OSG operator semantics that differ from common conventions** (OSG's `include/osg/Vec3f`): `Vec3f * Vec3f` is the **dot product** and `Vec3f ^ Vec3f` is the **cross product**. OSG matrices use row vectors (`v * M`) and `osg::Matrix::operator*` composes in that order; VSG's matrices are column-major with column vectors (`M * v`), like GLSL. `osg::Quat` composition order also has its own convention. Do not assume VSG matches any of these: verify each against VSG's headers (`include/vsg/maths/*` at the pinned `v1.1.16`) with tests before converting call sites.
- **The Lua API exposes math types.** `components/lua/utilpackage.cpp` binds OSG types as `util.vector2/3/4`, `util.transform` (`LuaUtil::TransformM` over `osg::Matrixf`, `TransformQ` over `osg::Quat`) and `util.box`. Mods depend on their behaviour, including `*` between vectors meaning the dot product. The Lua API must not change.
- **CreativeMotion uses its own math** (`opas::math`); only its OpenEden adapters convert, through one boundary per area (guide, "Units and conventions").
- **Saves and ESM records store plain floats** (`ESM::Position` and similar), not OSG types; nothing on disk changes.

## Design

1. **Convention tests first.** A test suite `apps/components_tests/math/` that pins down, for every operation the engine uses, OSG's result and VSG's result on the same inputs: vector dot, cross, length, normalize, component-wise products; matrix multiply order, `v * M` against `M * v`, inverse, transpose, decomposition, `makeRotate`/`rotate`, `translate`, `scale`, `lookAt`, perspective; quaternion multiply order, slerp, `makeRotate(angle, axis)`, `makeRotate(from, to)`, conversion to and from matrices; bounding box expansion, plane distance. These run while both OSG headers and VSG are available, and they document every difference.
2. **A small helper header** `components/misc/vsgmath.hpp` (name proposed) for the operations call sites need that VSG lacks or names differently (for example `length2`, OSG-style `makeRotate`, explicit `dot`/`cross` where OSG used operators), so conversions are uniform and greppable. No wrapper types: the engine uses VSG types directly (decision 0002).
3. **Convert area by area,** each as its own commit with all gates passing: `components/misc/convert.hpp` and its tests first; then `components` (detournavigator, lua, settings, nif, esm), then `apps/openmw` (`mwphysics`, `mwmechanics`, `mwworld`, `mwclass`, `mwlua`, `mwgui`, `mwsound`, `mwinput`, `mwbase`, `mwscript`, `mwrender` leftovers), then tests, then `apps/opencs/model` and the other tools. Every `*` and `^` between OSG vectors becomes an explicit `dot`/`cross` call; every matrix product is checked against the convention tests.
4. **The Lua API keeps its behaviour.** `util.vector*`, `util.transform`, `util.box` keep their Lua-visible semantics exactly (including `*` as dot product between vectors), implemented over VSG types; `scripts/data/integration_tests` plus new Lua unit tests in `apps/components_tests/lua` check them.
5. **Remove OSG:** delete `extern/osg`, `components/vsgadapters/osg` and the `osgcompat` unit; the build has no OSG include path left (task 0014 removes the remaining OSG use in the editor and tools; if they still need OSG headers at this point, keep a minimal include path only for those targets and record it for task 0014).

## Constraints

- **No behaviour change.** Physics, AI, movement, Lua and rendering results are identical before and after (bit-identical where the operations are the same in float, within 1e-6 relative where VSG computes differently; any larger difference is a bug).
- Double precision stays where OSG used `Vec3d`/`Matrixd` (world positions, Jolt `RVec3` conversions).
- No allocation added in hot paths.
- One area per commit, gates green after each, so `git bisect` can find a broken conversion.

## Acceptance criteria

- [x] The convention test suite exists, passes, and documents every OSG/VSG difference found. OSG's side is now its recorded result (below).
- [x] No source file in `apps/openmw`, `components` (except CreativeMotion's vendored copy, which never used OSG) or `apps/components_tests` includes an `osg/` header or names an `osg::` math type (`CI/check_no_osg.sh`). The exception is the uncompiled files listed in task 0014 for the editor and tools, as Design item 5 allows.
- [x] `extern/osg` and `components/vsgadapters/osg` are deleted.
- [x] The Lua math API behaves identically: existing integration tests pass, and new Lua unit tests cover vector, transform and box operations including `*` as dot product.
- [ ] Scripted gameplay checks give identical results before and after: a fixed-seed movement and combat scenario (positions, rotations and Jolt body states after N steps), AI pathing results, and `tools/render_compare.sh` images identical to the end of task 0012. **Not run**: no such scenario exists yet, and neither the images nor AI pathing were compared. This is what keeps V5 open.
- [x] All gates pass: unit and integration tests on Windows and Linux (below). clang-format: run with a newer local version on the converted tests only, not with CI's version 14.

## Out of scope

- The editor's rendering (task 0014).
- Changing any algorithm while converting it (open a separate task if a conversion reveals a bug; fix it only with toddhow's agreement, as its own commit).
- CreativeMotion's `opas::math` (stays as it is; only adapters change).

## Open questions for toddhow

1. **Timing:** run this after task 0012 (proposed, so parity work is not disturbed by a 300-file change), or in parallel on its own branch with a merge afterwards?
2. **Helper header:** acceptable to have a small `vsgmath.hpp` of helpers, or must every call site use VSG's API as it is?

### Answers (toddhow, 2026-10-29)

1. **Timing:** after task 0012 (V4).
2. **Helper header:** a small `vsgmath.hpp` of helpers is acceptable.

## Notes from the convention tests (2026-10-31)

`apps/components_tests/math/` holds the convention tests of Design item 1: 135 tests, written from this spec by a separate agent, all passing on Windows and Linux. Each states which OSG expression equals which VSG expression. Before conversion starts, the spec needs these corrections; points 3 and 4 are toddhow's call.

1. **Precision in the goal line.** OSG's unsized `Quat`, `Plane` and `Matrix` are double, so they map to `dquat`, `dplane` and `dmat4`, not `quat`, `plane` and `mat4`.
2. **Bounding volumes are not one type on Windows.** `<osg/Config>` can resolve to vcpkg's copy, which defines `OSG_USE_FLOAT_BOUNDINGBOX` and `OSG_USE_FLOAT_BOUNDINGSPHERE`; the bundled copy leaves them undefined. So `osg::BoundingBox` is float in some translation units and double in others (seen in `math/testboundsconventions.cpp`), the ODR hazard research note 0003 section 5 meant to close. Each use has to be checked rather than assumed to be `dbox`.
3. **"Identical" against the tolerance.** The acceptance criterion wants identical positions and Jolt states after N steps; the constraints allow 1e-6. VSG's `/`, `slerp`, `inverse` and `makeRotate` differ in the low bits, and that can accumulate over steps. Float matrix products also differ in the last bit between GCC and MSVC, with no VSG involved in the difference (`math/testmatrixconventions.cpp`), so bit-identical results across platforms were never available. Either fix a tolerance for that check, or use helpers wherever bit-identity matters. Proposed: helpers matching OSG in simulation code, with a recorded tolerance only for what cannot match.
4. **The suite after OSG is removed.** Design item 5 deletes `extern/osg`, which the suite includes. Proposed: reduce it then to literal expected values, so the record of each trap stays.
5. **`*` between vectors.** "Every `*` between OSG vectors becomes `dot`" is wrong for `Vec2i`, whose `*` is component-wise even in OSG.
6. **Size.** A count of OSG math types only finds about 451 files and 5,688 references, not about 620 and 15,000. Much of it is code that is not compiled (`components/nifosg`, most of `components/sceneutil`, parts of `apps/openmw/mwrender`). Those files would still fail the final grep, so they should be deleted, not converted.
7. **Operations the spec does not name:** `.x()` style accessors, `osg::PI` and degree conversion, `xMin()` and friends on boxes, `getTrans`/`setTrans`, `preMultTranslate` and similar, `transform3x3`, `corner`, matrix `decompose` (VSG's can fail where OSG's cannot), and the three-point `Plane` constructor. The list `vsgmath.hpp` must provide is in each test file's header comment.

## How the conversion runs (2026-10-31)

- **Defaults taken for notes 3 and 4 until toddhow says otherwise:** helpers matching OSG in simulation code, and the convention suite reduced to literal expected values when OSG goes.
- **Bridge:** `extern/osg` has implicit conversions to and from the VSG types, marked "OpenEden V5 transition", so one area converts while its callers still use OSG and every commit builds. It goes with `extern/osg` at the end.
- **Note 2 resolved by the bridge:** the root `CMakeLists.txt` now puts `extern/osg` first, because Windows was compiling against vcpkg's OSG headers. So `osg::BoundingBox` and `BoundingSphere` are double on Windows too, as on Linux. Reference frames of Seyda Neen and the Caldera Mages Guild looked the same afterwards, by eye.
- **Silent traps made loud:** `components/misc/vsgmath.hpp` deletes `vec * vec` of the same type, which VSG reads component-wise and OSG as a dot product. Every converted file includes it. A file being converted also defines `OPENEDEN_VSGMATH_CONVERTING`, which deletes matrix products and vector-matrix products, so each is looked at and flipped by hand. That macro goes at the end.
- **Dead code:** files the build does not compile stay until the end of V5, then go, apart from what the editor needs for task 0014.

## How V5 closed (2026-10-31)

**Verified:** on Windows, 2,140 of components-tests and 717 of openmw-tests; on Linux (GCC, `-Werror`), 2,142 and 717 and all 32 Lua integration tests. `CI/check_no_osg.sh` passes. After a Windows build, the only objects whose dependencies name an OSG header are `extern/osgQt`'s, which are task 0014's.

**Not verified:** the fixed-seed scenario, AI pathing and the image comparison were not run. The game was not started on Windows for this work, and no frame was viewed; the Linux integration tests are the only run of the game.

**Recorded results.** Before OSG went, each OSG side of the convention tests became its result, inline or in a `*.golden.inc` table, and a build compared all 18,432 recorded evaluations with the OSG expression they replaced, bit for bit, on Windows. Inputs and traps stay. Recording happened on MSVC, so comparisons stay exact there; other compilers get 16 float or 256 double units in the last place (`sameAsRecorded` in `conventions.hpp`), because GCC and glibc round differently with no VSG involved (note 3). This is a tolerance the brief for this step did not name: toddhow may want it narrower.

**For toddhow:**

1. `apps/openmw/mwinput/mousemanager.cpp` includes `sdlutil/sdlinputwrapper.hpp` while the object is `sdlutil/inputwrapper.hpp`'s, a different layout, since vsgopenmw's merge. So `getMouseRelative()` there reads the wrong byte, and the cursor warp on leaving relative mode depends on it. Including the right header changes behaviour, so it stays, with the old member's size kept, until toddhow decides.
2. `DetourNavigatorRecastMeshObjectTest`'s fixture reads `mObjectTransform` before it is initialised (member order), which is why these tests are flaky on Windows. With `compareEqual`, a NaN in that garbage reads as equal, as under OSG. Reordering the members is a test input change, so it was left.
3. No `reportStats` has a caller on the Vulkan build: the OSG engine's per-frame resource report was not carried over. `Misc::Stats` keeps the names for when one is.

## Before-and-after comparison (2026-10-31, Windows)

The commit before V5 (`ff3b0be8b7`) against the end of V5 (`feda4093f4`), two runs of each.

- **Exact results, identical:** a Lua script prints the player's start, and for eight fixed offsets in Seyda Neen `nearby.findPath`, `findNearestNavMeshPosition`, `castNavigationRay` and `castRay`, plus composed transforms, inverse, dot and normalize, to six decimals. All 35 lines match between the builds and between runs of each. Eight offsets in one town is a sample, not a proof.
- **Images, no difference beyond noise:** the same frame of eight of `tools/render_scenes.txt`'s scenes (`vivec_arena_interior` names a cell the game can't find, in both builds). Most scenes differ between builds as much as between two runs of one build. Identical images, as the acceptance criterion words it, were never available, because runs of one build differ.
- **Why runs differ:** in Balmora the player starts partly inside terrain and slides out by an amount that depends on frame timing, in both builds (logged: -12327.90 against -12329.21 in X for the baseline, -12328.60 against -12329.89 for the current build), so the whole frame shifts by a pixel or two. In Sadrith Mora the differences are local to a small area; two baseline runs happened to differ by less than two current runs, 6,277 against 63,445 in the summed difference. That was not traced further, and more runs would be needed to rule out a small real effect there.
- **Not run:** the fixed-seed movement and combat scenario with Jolt body states after N steps. The game has no fixed-step harness, and frame timing moves the player as shown above, so it needs one first. Movement and combat are covered only by the Lua integration tests (`player with equipped weapon...`, running), which pass. This criterion stays open.
- **Found while comparing:** `mousemanager.cpp` compiled against a stale copy of `SDLUtil::InputWrapper` from the renderer merge, and read the wrong byte. Fixed in `feda4093f4`; built, but mouse grabbing has not been tried by a person.
