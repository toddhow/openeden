# Task 0099: The neutral world model, stage N0 (types, the Skyrim adapter, survey and design)

Design doc section: `docs/design/neutral-world-model.md` (decision 0016, ACCEPTED by toddhow on 2026-11-10: option 3)
Goal: record the decision, survey what the engine consumes, design the neutral model and its stages, and build N0: the value types and a Skyrim SE adapter that fills them, with no engine behaviour change.
Inherited code touched: none (new files only; read: `apps/openmw/mwworld/{ptr,livecellref,cellstore,scene,class}.*`, `mwrender/scene.cpp`, `mwphysics/physicssystem.cpp`, at commit `414f50d0c6`).
Inputs / outputs: `sdk/include/openeden/game/world_model.hpp` (new, types only, `kWorldModelVersion` 0.1 DRAFT); `game_packs/skyrimse/{include/game_skyrimse,src}/neutral_world.*` (new: `eulerToQuat`, `toNeutral`, `loadNeutralCell`); tests `apps/components_tests/skyrimse/testskyrimseneutralworld.cpp` and `...real.cpp`.
Constraints: no engine, Jolt, VSG or Qt type in a signature; main thread only; deterministic; layer exceptions stay at 104; every `CI/check_*.sh` passes.
Acceptance criteria:
- [x] Decision 0016 recorded as ACCEPTED.
- [x] The survey cites file and function for each claim (design doc section 1).
- [x] The design doc covers types, identity, ownership and threading, the Morrowind migration, the relation to `world_view.hpp`, saves, determinism, allocation and the off-means-unchanged method, with stages N0 to N7 and open questions.
- [x] Tests written first and run red against stubs (11 of 14 failed; the 3 that passed test absence or the identity), then green.
- [x] The five installed Skyrim SE masters load as neutral cells (every interior cell and an even sample of exterior cells) with 0 failures and the same object count as the contract.
- [x] No engine source file changed (`git diff --stat` lists only new files, CMake lists, `CI/check_extension_sdk.sh` and docs).
Out of scope: any engine consumption (N3), the core store (N1), saves, actors, the Morrowind projection.

## Stage log

### N0 (2026-11-10; same hand wrote tests and code, no fresh reviewer; Windows only; local commits, not pushed)

**Tests.** 14 tests in `testskyrimseneutralworld.cpp` (rotation: the identity, +90 degrees about Z takes +X to -Y, a 216-combination comparison against an independently built `Rz * Ry * Rx` matrix with negated angles and unit-norm check, non-finite angles; the object: every field, collision from model then bounds then none, every `BaseKind`, eleven hostile values refused with a reason and the output untouched, bounds ignored when not wanted; the cell: a scripted `WorldView` for field copying, order, `droppedObjects`, `unresolvedBases` and a failing view; a real `SkyrimWorldView` over a generated plugin equal to the contract's objects mapped one by one, two loads and two fresh views equal, an unknown id) and 1 gated aggregate test (`...real.cpp`, `OPENEDEN_SKYRIMSE_DATA`). Against a stub that answered nothing **11 of 14 failed** (`I:\Projects\OpenEden-work\nwm\rig\red.txt`); the three that passed test the identity and absence (zero angles, non-finite angles, unknown id). With the code all 14 passed on the first full run. One mutation (the angle sign flipped to +1) failed the two rotation tests that depend on it. The tests ran in a private rig (`I:\Projects\OpenEden-work\nwm\rig\cc.bat`, `link.bat`: the pack's sources compiled with `cl` and linked with gtest); **the whole `components-tests` binary was not built.** GCC 13 `-Wall -Wextra -Werror -Wshadow -fsyntax-only` is clean on `neutral_world.cpp` (the two test files could not be checked: the Linux image has no gtest).

**Real data** (aggregates only; the game's five masters, every interior cell and an even sample of exterior cells, as in the task 0086 check): 2,786 cells loaded, 0 failed, 522,288 neutral objects against 522,288 in the contract, 0 dropped, 0 unresolved bases, 0 quaternions off unit length, 0 out of id order, 0 payloads differing from the base id; collision hints: 479,763 from the model, 36,507 box, 6,018 none; kinds (by `BaseKind` number): 1=332,261, 2=28,325, 3=9,239, 4=3,303, 5=16,072, 6=13,052, 7=7,658, 8=12,734, 9=13,429, 10=70,639, 11=6,017, 13=9,559; 0.83 s for the 2,786 cells (the same masters took 0.79 s through the contract alone in task 0086).

**Decisions made here (toddhow may overturn):** the types are in the SDK and DRAFT 0.1; `ObjectKind` is an alias of `BaseKind`; the transform is a double precision quaternion with the Euler convention at one named function; `PackPayload` is a 64-bit number, never a pointer; the adapter works over any `WorldView&`; owner, lock and teleport stay empty for Skyrim until the record decoders exist (N4).

**Not run / not done.** The Linux gate (beyond the one GCC syntax check), clang-tidy, sanitizers, the Lua tests, the whole `components-tests`, a fresh review, X and Y rotation signs in a frame (RECALLED-VERIFY, as in task 0086), any engine run (nothing in the engine changed). Stages N1 to N7 are drafts in the design document and need his approval one by one.
