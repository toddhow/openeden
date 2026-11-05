# Task: Layer rules checker (the dependency map made executable)
Design doc section: `docs/design/game-packs.md` sections 2 and 10 (stage a); `docs/research/0043-separating-formats-from-game-rules.md` sections 1 and 3 (the measurements this task freezes).
Goal: make "formats do not include rules" a checked rule in CI, with the tree as it is and no files moved.
Inherited code touched, at commit `671c7676e9`: none changed in stage 1. Read: every `#include` in `components/` and `apps/openmw/`. Stage 2 (optional, separate approval) touches `components/CMakeLists.txt` (`add_library(components STATIC ${COMPONENT_FILES})`, line 722, and the `add_component_dir` macro) and `apps/components_tests/CMakeLists.txt`.
Inputs / outputs: new `CI/layer_rules.txt` (the rule file: layers, which directories belong to each, who may include whom, and an **exceptions list**), new `CI/check_layer_rules.sh` (reads `git ls-files`, same style as `CI/check_no_osg.sh`: comments and strings blanked, includes read from the original line), one new step in `.github/workflows/checks.yml` and a job in `.gitlab-ci.yml` beside `check_no_osg.sh`. No change to any C++ or CMake file in stage 1.
Status: draft for toddhow's approval. Lighter loop proposed (CI and scripts only), because stage 1 touches no C++; stage 2 is a build change and needs the full loop.

## Layers (the first rule file; names as in the design doc)
| Layer | Directories | May include |
| --- | --- | --- |
| `base` | `components/misc`, `files`, `debug`, `toutf8`, `serialization`, `settings`, `fallback`, `sqlite3`, `version`, `platform` | `base` only |
| `formats` | `components/esm`, `esm3` (content records), `esm4`, `nif`, `bsa`, `esmloader`, `bgsm`, `vfs` | `base`, `formats` |
| `engine` | `components/` render, resource, lod, terrain, animation, creativemotion, lua, detournavigator and the rest | `base`, `formats` (listed exceptions), `engine` |
| `rules` | `apps/openmw/mwmechanics`, `mwworld`, `mwclass`, `mwdialogue`, `mwscript`, `mwstate`, `components/compiler`, `interpreter` | everything below |
| `ui` | `apps/openmw/mwgui`, `components/widgets`, `myguiplatform` | everything below |
| `tools` | `apps/*` other than `openmw` | everything below |

The rule file says which directory belongs to which layer, in one place, so the later "which directory moves where" decisions edit data and not a script.

## Constraints
- **Exceptions list, not a clean slate.** The tree today breaks the rule (research note 0043 section 1.2): `components/esm` includes `esm3` and `esm4`; `components/resource/foreachphysicsobject.cpp` includes `esm3` and `esmloader`; `components/mwanimation/partbones.hpp` and `light.cpp` include `esm3/loadarmo.hpp` and `loadligh.hpp`; `components/detournavigator/navigatorimpl.cpp` includes `esm3/loadpgrd.hpp`; `components/vfs` includes `components/bsa`; `components/nif` includes `components/resource` once. Each current violation is listed as `file -> header` with a reason. **The list may shrink and never grow**: the script fails on a violation not in the list and on a list entry that no longer occurs (so fixing one removes it).
- **Deterministic, no network, no new tool** beyond `bash`, `git` and `perl` (the same as `check_no_osg.sh`).
- No file name outside the `CI/check_file_names.sh` convention for C++ files (the new files are scripts and text).
- Stage 2 (optional): split the `components` static library into CMake object or static libraries per layer (`components_base`, `components_formats`, `components` linking them), so a format library links without the renderer. Only after toddhow approves, because it changes link order, the tools' link lines and the test targets.

## Acceptance criteria
- [ ] `CI/check_layer_rules.sh` exits 0 on the tree as committed and prints the number of exceptions it knows (a number recorded in the stage log)
- [ ] A test of the checker itself: a temporary file in a scratch copy that adds `#include <components/esm3/loadnpc.hpp>` to a file in `base` makes it exit non-zero with the file, line and rule named; one in `formats` including `mwworld` fails; removing the violation passes. The test is a shell script in `CI/` run by the workflow
- [ ] A listed exception that is fixed and left in the list fails the check ("stale exception")
- [ ] The exceptions list is complete: the count of include lines it covers equals the count found by the independent `tools/include_count.sh` measurement of the research note for the same pairs (stage log)
- [ ] The script passes on Windows (Git Bash) and Linux (CI); runtime under 10 s
- [ ] With the feature off: no source, CMake or setting changes, so the 32 Lua tests and all unit tests are untouched and the built binaries are not rebuilt (stage 1)
- [ ] Stage 2 only: `components-tests`, `openmw-tests`, `eden-studio-tests` and the 32 Lua tests pass; a format-only link (a tool that links only `components_formats`, e.g. `esmtool`) builds; the Linux `-Werror` gate passes; release binary identical in behaviour (the Lua tests; frame time is not at issue)
Out of scope: moving any file; fixing any exception (they are what stages b, c and e remove); a clang-tidy include-what-you-use rule; checking `extensions/` and `sdk/` (they have `CI/check_extension_sdk.sh`).

## Open questions (recommendation first)
1. Stage 2 now or later? Recommend later: the checker gives the protection; the split costs a build change with no behaviour gain until stage c needs a format-only link.
2. Does `vfs` belong to `formats` or `engine`? Recommend `engine`, with the `vfs -> bsa` include as a listed exception that stage c removes (archive registration through the factory list, design doc 5.3).
3. Should the check also forbid `components/` including `apps/`? Recommend yes: zero violations today (measured), so it is free.
