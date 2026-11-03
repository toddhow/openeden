# 0030 — Repository cleanup after the vsgopenmw import

Date: 2026-11-03 · Asked by toddhow ("there are leftovers from the vsgopenmw repo") · Method: `git ls-files`, `git grep` (never a recursive grep of the tree) and reading the files. Nothing was built or run except the three CI scripts and a CMake configure after the deletions.

## Deleted (done, two commits)

| Path | Why | Evidence |
| --- | --- | --- |
| `vsgopenmw-authors.doc`, `-build.md`, `-code-of-conduct.doc`, `-code-style.md`, `-contributing.doc`, `-debug.doc`, `-features.doc`, `-help-wanted.doc`, `-readme.md`, `-roadmap.doc`, `-spelling-policy.doc` (root, 11 files) | vsgopenmw's own project files; none describes OpenEden. The build guide told readers to install VSG 1.0.9 and OSG headers, which is wrong here; the feature list and roadmap are vsgopenmw's plans. | No source, CMake, CI or user doc references the file names (`git grep`); only `docs/research` and `docs/tasks` name them, as history. |
| `VSG_MERGE_PLAN.md` | vsgopenmw's own plan for merging OpenMW 0.49 to 0.51; OpenEden did that merge differently (research note 0003). | Nothing references it except `docs/research` and `docs/tasks`, as history. |

Kept from them first (commit `79c2ba4bbd`): the author list, licence note and "Charityware" request in `docs/credits-vsgopenmw.md` (the CLAUDE.md guardrail "Code taken from vsgopenmw keeps its origin" and decision 0002 say the authors are credited; `AUTHORS.md` did not name them), and the debug environment variables in `docs/debug-environment-variables.md`: all six `VSGOPENMW_*` variables are still read by the code (`git grep VSGOPENMW_`), so the documentation of them had to stay. The deleted files are in git history.

## Kept on purpose

| Path | Verdict | Reason |
| --- | --- | --- |
| Code comments and file headers naming vsgopenmw (for example `components/misc/vsgmath.*`, `apps/openmw/mwrender/*`, `components/CMakeLists.txt` `#<vsgopenmw-code-style>` markers) | keep | Attribution; the guardrail says transplanted files keep their notices. The CMake markers are vsgopenmw's own annotations of its source lists and harmless. |
| `tools/verify_no_osg_deps.{sh,ps1,py}` | keep | Added by OpenEden (commit `cf3dbd3aa7`), not vsgopenmw's. Not superseded: `CI/check_no_osg.sh` scans source files, these scripts check the BUILT executable's imported libraries. Referenced by research note 0003. |
| `docs/source/reference/modding/custom-models/pipeline-blender-osgnative.rst` | keep | OpenMW's user documentation, already marked "OpenEden has no reader for OSG native models" and linked from the page index and `texture-basics.rst`. Rewriting the user docs is a separate task. |
| `cmake/Find*.cmake`, `LibFindMacros.cmake` and the other modules | keep | Inherited OpenMW; `find_package(X)` loads `FindX.cmake` by name, so a text search for the file name proves nothing. Not vsgopenmw's. |
| `extern/` (Base64, ffmpeg-videoplayer, oics, smhasher, sol3, sol_config, vulkan) | keep | Each is referenced by `extern/CMakeLists.txt` or the root CMake (`vulkan/vulkan-1.def` by line 264 of `extern/CMakeLists.txt`). |
| `.readthedocs.yaml` | keep | Inherited docs hosting config; no decision on the user docs' hosting yet. |
| Untracked folders `build-shadowfix/`, `MSVC2022_64/`, `.cache/`, `.vs/`, `deps/` | not in the repo | `.gitignore` covers them (`build*/`, `/MSVC*`, `.vs`, `/deps`). They only take disk space; deleting them is for toddhow, and `deps/` is needed. |

## Needs toddhow's decision (nothing deleted)

| Path | What it is | Why not deleted |
| --- | --- | --- |
| `.gitlab-ci.yml`, `.gitlab/` (issue and MR templates), `.resubmitted_merge_requests.txt` | Inherited GitLab CI and templates. Line 1 of `.gitlab-ci.yml` is a vsgopenmw policy token. | Decision 0008: they "stay until the GitHub jobs replace them", and `.github/workflows/push.yml` says the example suite revision is the one pinned in `.gitlab-ci.yml`. Delete after task 0022's port is judged complete. |
| `docker/` (`Dockerfile.ubuntu`, `build.sh`, `README.md`) | OpenMW's Ubuntu build image. | Not vsgopenmw's; nothing references it, and the Linux gate uses its own image. It does not mention OSG, but whether it still builds OpenEden was not tested. Delete, or refresh for the Linux build? |
| `manual/opencs/` (LaTeX manual of OpenMW-CS) | Inherited editor manual, 8 `.tex` files and one image. | Not vsgopenmw's; nothing references it. The editor is now Eden Studio, so the manual is outdated, but it is the only editor documentation. |
| `components/sdlutil/gl4esinit.{cpp,h}`, option `OPENMW_GL4ES_MANUAL_INIT` in the root `CMakeLists.txt` (lines 17 to 19) | OpenGL-ES-on-GL initialisation for mobile builds; dead since OpenGL is gone. | Clearly dead, but removing it edits an inherited root `CMakeLists.txt`; the guide says to keep such edits to what a task needs. |

## Not checked

- **Shader files under `files/shaders/`:** includes are resolved at run time by name, so a plain reference search cannot prove a file unused. Needs a run-time include trace.
- **Unused settings** in `files/settings-default.cfg` and the `docs/source` settings pages: not compared against the code.
- **`apps/` and `components/` dead code** (functions or whole files that nothing calls): needs a build with unused-code reporting, not a text search.
- **Stale cache variables** (`OPENMW_USE_SYSTEM_OSG`, `OPENEDEN_VSG_PENDING_FEATURES`, `OPENMW_USE_SYSTEM_VSG`) exist in `build-jolt/CMakeCache.txt` only; no source or CMake file defines them. They vanish in a fresh build folder.
