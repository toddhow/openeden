# Task 0013: Track V stage V5 — VSG math everywhere

**Status:** approved by toddhow (2026-10-29) with the answers below. Depends on task 0012 (V4).
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

- [ ] The convention test suite exists, passes, and documents every OSG/VSG difference found.
- [ ] No source file in `apps/openmw`, `components` (except CreativeMotion's vendored copy, which never used OSG) or `apps/components_tests` includes an `osg/` header or names an `osg::` math type (checked by a CI grep, like the guide's include checks).
- [ ] `extern/osg` and `components/vsgadapters/osg` are deleted.
- [ ] The Lua math API behaves identically: existing integration tests pass, and new Lua unit tests cover vector, transform and box operations including `*` as dot product.
- [ ] Scripted gameplay checks give identical results before and after: a fixed-seed movement and combat scenario (positions, rotations and Jolt body states after N steps), AI pathing results, and `tools/render_compare.sh` images identical to the end of task 0012.
- [ ] All gates pass: unit, allocation and integration tests, clang-format.

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
