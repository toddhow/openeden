# Task: Layer rules checker (the dependency map made executable)
Design doc section: `docs/design/game-packs.md` sections 2 and 10 (stage a); `docs/research/0043-separating-formats-from-game-rules.md` sections 1 and 3 (the measurements this task freezes).
Goal: make "formats do not include rules" a checked rule in CI, with the tree as it is and no files moved.
Inherited code touched, at commit `671c7676e9`: none changed in stage 1. Read: every `#include` in `components/` and `apps/openmw/`. Stage 2 (optional, separate approval) touches `components/CMakeLists.txt` (`add_library(components STATIC ${COMPONENT_FILES})`, line 722, and the `add_component_dir` macro) and `apps/components_tests/CMakeLists.txt`.
Inputs / outputs: new `CI/layer_rules.txt` (the rule file: layers, which directories belong to each, who may include whom, and an **exceptions list**), new `CI/check_layer_rules.sh` (reads `git ls-files`, same style as `CI/check_no_osg.sh`: comments and strings blanked, includes read from the original line), one new step in `.github/workflows/checks.yml` and a job in `.gitlab-ci.yml` beside `check_no_osg.sh`. No change to any C++ or CMake file in stage 1.
Status: approved by toddhow 2026-11-05 (stage 1 only; stage 2 later; lighter loop: scripts and CI only). **Stage 1 done 2026-11-05, on Windows (Git Bash); Linux and the GitHub workflow not run.**

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
- [x] `CI/check_layer_rules.sh` exits 0 on the tree as committed and prints the number of exceptions it knows (a number recorded in the stage log)
- [x] A test of the checker itself: a temporary file in a scratch copy that adds `#include <components/esm3/loadnpc.hpp>` to a file in `base` makes it exit non-zero with the file, line and rule named; one in `formats` including `mwworld` fails; removing the violation passes. The test is a shell script in `CI/` run by the workflow
- [x] A listed exception that is fixed and left in the list fails the check ("stale exception")
- [x] The exceptions list is complete: the count of include lines it covers equals the count found by the independent `tools/include_count.sh` measurement of the research note for the same pairs (stage log)
- [ ] The script passes on Windows (Git Bash) and Linux (CI); runtime under 10 s
- [x] With the feature off: no source, CMake or setting changes, so the 32 Lua tests and all unit tests are untouched and the built binaries are not rebuilt (stage 1)
- [ ] Stage 2 only: `components-tests`, `openmw-tests`, `eden-studio-tests` and the 32 Lua tests pass; a format-only link (a tool that links only `components_formats`, e.g. `esmtool`) builds; the Linux `-Werror` gate passes; release binary identical in behaviour (the Lua tests; frame time is not at issue)
Out of scope: moving any file; fixing any exception (they are what stages b, c and e remove); a clang-tidy include-what-you-use rule; checking `extensions/` and `sdk/` (they have `CI/check_extension_sdk.sh`).

## Open questions (recommendation first)
1. Stage 2 now or later? Recommend later: the checker gives the protection; the split costs a build change with no behaviour gain until stage c needs a format-only link.
2. Does `vfs` belong to `formats` or `engine`? Recommend `engine`, with the `vfs -> bsa` include as a listed exception that stage c removes (archive registration through the factory list, design doc 5.3).
3. Should the check also forbid `components/` including `apps/`? Recommend yes: zero violations today (measured), so it is free.

## Stage log

### Stage 1 (2026-11-05, lighter loop, scripts and CI only; nothing committed to a C++ or CMake file)
**Built:** `CI/layer_rules.txt` (layers, `dir` lines, `allow` lines, `deny` lines, the exceptions list), `CI/check_layer_rules.sh`, `CI/test_layer_rules.sh`, a step "Layer rules" in `.github/workflows/checks.yml` and two lines in the `.gitlab-ci.yml` checks job (both next to `check_no_osg.sh`). The scripts are LF, mode 100755 in the index as the other `CI/*.sh`.

**Decisions of toddhow applied:** stage 2 later (not started); `vfs` is `engine`, `vfs -> bsa` is listed; `components/` must not include `apps/` (`deny components/ apps/`, zero violations).

**How the rules read (judgement calls, for toddhow):**
- `engine` may include `base` and `engine` only; every engine-to-formats include is a listed exception (the spec table says "formats (listed exceptions)"). There are 70 such lines.
- `esm` is inside the `formats` layer in the spec table, but note 0043 calls `esm -> esm3/esm4` a violation, so two `deny components/esm/ -> components/esm3/` (and `esm4`) lines express it (97 lines).
- The spec table has no layer for the rest of `apps/openmw` (the executable's glue, `mwbase`, `mwrender`, `mwphysics`, `mwsound`, `mwinput`, `mwlua`). I added a layer `host` for it. Measured with `rules`/`ui`/`host` strictly ordered, 780 lines break the order (rules -> host 533, ui -> host 222, rules -> ui 25), because `mwbase` is included by everything and the renderer includes `mwworld`. Listing 780 lines would freeze noise, so in stage 1 `rules`, `ui`, `host` and `tools` may include each other and everything below; the checker enforces the components' layering (`base`, `formats`, `engine`) and the `components -> apps` ban. Tightening the apps layers is a later change and would need its own exceptions list.
- A directory under `components/` or `apps/` that the rule file does not name fails the check. Three non-directories needed `dir` lines: `components/doc.hpp`, `apps/doc.hpp` (doxygen-only files) and `components/nifbullet` (a directory that no longer exists; `components/resource/bulletshapereader.hpp` includes it and nothing includes that header).
- An include is resolved against the repository root, the including directory (quotes only) and the including application (`apps/openmw`, ...); includes of untracked or generated headers under `components/` or `apps/` are classified by their written path.

**Numbers (this tree, Windows, Git Bash):** 206 exceptions covering 206 include lines (one include line per pair; 42 directory pairs). By kind: `esm` -> `esm3` 51, `esm` -> `esm4` 46, engine -> formats 70, base -> engine 16, base -> formats 9, formats -> engine 12, engine -> ui 2. Runtime 2.1 s warm (13.5 s on the very first, cold-cache run after the files were written). The self-test runs in 2.4 s.

**Cross-check of the exceptions list (acceptance criterion 4):** the list's 206 include lines were summed per (source directory, target directory) pair, and for each of the 42 pairs the lines were counted independently with `grep -rnE '^\s*#\s*include\s*[<"](components/<target>/|(\.\./)+<target>/)'` over the source directory (script `I:\Projects\OpenEden-work\layerrules\indep.sh`): **206 by the list, 206 by grep, no pair differs.** The checker also fails on any unlisted violation, so the list cannot be incomplete while the check passes. `tools/include_count.sh` already existed (it counts per top-level target with awk `gensub`, a GNU awk function) and was not used or changed.

**Against research note 0043 section 1.2:**
- Confirmed exactly: `esm -> esm3` 51 and `esm -> esm4` 46 lines; `resource/foreachphysicsobject.cpp` 7 headers (4 `esm3`, 3 `esmloader`); `mwanimation` `partbones.hpp` and `light.cpp`; `bsa -> vfs` 3 lines; `nif/node.cpp -> resource/physicsshape.hpp` once; no `components -> apps` include.
- Found differently: `detournavigator` includes five `esm`/`esm3` headers in five files (the note named only `loadpgrd.hpp`); `vfs -> bsa` is 4 headers in `vfs/bsaarchive.hpp` (the note said one file, which is right, but four lines); `contentselector` has 4 lines (the note said 3).
- Not in the note: base utilities that include higher layers, 25 lines: `misc` -> `esm`/`esm3`/`esm4` (`convert.hpp`, `coordinateconverter.hpp`, `pathgridutils.hpp`, `resourcehelpers.*`), `misc` -> `lua`/`vfs`, `settings` -> `vfs`, `detournavigator`, `sceneutil`, `sdlutil` (setting enum headers), `debug/debugging.cpp` -> `crashcatcher`. Also formats -> engine: `esm/luascripts.*` -> `lua` and `vfs`, `esm4` -> `vfs`, `esmloader/load.cpp` -> `loadinglistener`, `bsa` -> `vfs/pathutil.hpp`; engine -> ui: `fontloader`, `fx/widgets.cpp`; and engine -> NIF: `bodynif`, `lod/nifflatten`, `nifjolt`, `vsgadapters/nif` (the NIF consumers of note 0043 section 1.2, now all listed).

**Self-test (`CI/test_layer_rules.sh`):** builds a scratch git repository with the real layer map and no exceptions (not a copy of the whole tree: faster, and the real exceptions would be stale there). Checks, all passing: a clean tree passes; a base file including `esm3/loadnpc.hpp` fails and the output names `components/misc/bad.hpp:3` and "layer base may not include layer formats"; a formats file including `apps/openmw/mwworld/ptr.hpp` fails with the `deny components/ -> apps/` rule; a relative `"../vfs/pathutil.hpp"` include is resolved and fails; removing the violations passes; an include in a comment or a string does not count; a listed exception passes and is counted; the same exception with its include gone fails as stale; a directory with no `dir` line fails. I did not edit a tracked file of the real tree to try the failure there.

**Not verified:** a Linux run (GNU/Linux Perl and bash are the same tools, and the script uses only portable constructs, but it was not run there); the GitHub workflow and the GitLab job themselves (YAML edited, not executed); a run with CRLF checkouts other than this machine's (`core.autocrlf=true`; the scripts remove a trailing carriage return from rule and source lines, but no CRLF run was done); clang-format (no C++). `CI/check_file_names.sh` passes.
