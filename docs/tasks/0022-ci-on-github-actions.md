# Task: CI on GitHub Actions
Design doc section: none; decision `docs/decisions/0008-hosting-on-github.md`. Guide: "Testing and quality gates".
Goal: every gate in the guide's table that can run in CI runs on GitHub Actions for each pull request, as the GitLab jobs do now.
Inherited code touched: `.gitlab-ci.yml` (read, as the reference), `.github/workflows/push.yml`, `release.yml`, `windows.yml`, `CI/*.sh`, `docker/Dockerfile.ubuntu`. No source under `apps/` or `components/`.
Inputs / outputs: new or changed workflow files under `.github/workflows/`; the `CI/` scripts they call; a short `CI/README`-style note in the guide's pitfalls if a job needs a trick.

Status: approved by toddhow, 2026-10-31. A workflow only runs once a branch is pushed; `vulkan` is pushed on the same day. Not marked as the lighter loop, so the loop applies in full where it can: there is no unit-testable code here, so the workflow runs are the tests, and a fresh reviewer reads the result. toddhow can still mark it lighter.

Constraints:
- **Mirror what exists.** Port the GitLab jobs one by one; do not redesign. The jobs and what each checks: `Ubuntu_GCC` (build with `-Werror`), `Ubuntu_GCC_tests` (unit tests, the 32 Lua integration tests against the example suite at the pinned revision), `_tests_asan`, `_tests_ubsan`, `Ubuntu_Clang`, `Clang_Format` (clang-format 14), the clang-tidy jobs, `CI/check_no_osg.sh`, `CI/check_qt_translations.sh`, and the Windows MSVC build and tests.
- **Dependencies:** Linux from `CI/install_debian_deps.sh` (no OpenSceneGraph packages since task 0014); Windows from `CI/before_script.msvc.sh` and vcpkg, as `windows.yml` does. The Linux jobs need Qt 6, a Vulkan loader and a software Vulkan driver (lavapipe) for the editor start-up check, and `xvfb` for the integration tests.
- **Platforms:** Windows (MSVC, x64) and Linux (GCC, Clang). macOS is not a target; its job is left out, not deleted.
- **Cost and time:** use the cache for vcpkg, ccache and fetched dependencies; GitHub's job time limits apply, and the cold-cache time of the real jobs is not known yet (a local Linux container build and test run takes about 35 minutes), so measure it first and split jobs if needed.
- **Secrets:** none. No tokens in files; releases and packaging (`release.yml`) are out of scope.
- **Not OpenMW:** nothing here opens merge requests or pushes to OpenMW's repositories (guide, guardrails).

Acceptance criteria:
- [ ] A pull request on `toddhow/openeden` runs the Linux build with `-Werror` and the three unit test programs, and all pass.
- [ ] The 32 Lua integration tests run on Linux in CI against the example suite at the revision pinned in the workflow, and pass.
- [ ] The address, undefined-behaviour, clang-format 14, clang-tidy, translation and no-OSG gates run, each failing the pull request when its check fails (shown once by a deliberate violation on a scratch branch).
- [ ] The Windows build and the unit tests pass in CI.
- [ ] The extension game checks (`tools/extension_*_check/check.sh`) run on Linux, or the spec says why not.
- [ ] With the new workflows in place, nothing in `apps/` or `components/` has changed, so the game and editor are exactly as before.

Out of scope: macOS, release packaging, Flatpak, deleting the GitLab files (they stay until the GitHub jobs have run green), a self-hosted runner, GPU tests.

Open questions for toddhow:
1. Which branch is the first to push, and what `master` on GitHub becomes (decision 0008, "Still open").
2. Whether a pull request must wait for the slow jobs (sanitizers, clang-tidy) or they run on a schedule and on `vulkan` only.

## Stage log

### 2026-11-10, workflows completed (written and validated, NOT run: nothing is pushed and no workflow is enabled)

Written by one hand with no separate reviewer; the workflow runs themselves are the tests and have not happened yet.

**What runs where**

| Workflow | Trigger | Job | What it gates | Expected time (estimates, none measured on GitHub) |
| --- | --- | --- | --- | --- |
| `checks.yml` | push to `main` and `vulkan`, every pull request, manual | Format-and-lint | cmake format, file names, no OSG, layer rules (+ self-test), unit test files registered (+ self-test), MWBase test doubles (+ self-test), game conventions, LF storage of scripts and test data, test profile coverage, clang-format 14 on the whole tree, extension SDK version | 5 to 10 min |
| `checks.yml` | same | Translations | `CI/check_qt_translations.sh` (lupdate) | 3 to 5 min |
| `push.yml` | same | Ubuntu | GCC 13 build of the whole tree with `-Werror -Werror=implicit-fallthrough`, RelWithDebInfo, ccache; `components-tests`, `edenarchive-tests` (+ tool check), `openmw-tests`, `eden-studio-tests`; the Lua integration tests (`scripts/integration_tests.py`) against the example suite at the revision pinned in the file, software Vulkan (lavapipe) under xvfb; test XML and integration output are uploaded | cold ccache about 60 to 120 min (a local container build and tests took about 35 min on a faster machine), warm 10 to 25 min |
| `push.yml` -> `windows.yml` | same | Windows (MSVC 2022) | full build with Ninja, vcpkg deps and Qt from cache or download, the four test programs, three benchmarks | **unknown, see below** |
| `nightly.yml` | 02:17 UTC every day on the default branch, manual (an optional TSan run) | Sanitizers (asan, ubsan) | Debug builds of the test programs with `CI/before_script.linux.sh` (the same variables as the GitLab jobs `Ubuntu_GCC_tests_asan` and `_ubsan`), all unit tests | cold 1.5 to 3 h, warm 30 to 60 min |
| `nightly.yml` | same | Clang | `Ubuntu_Clang` equivalent: whole tree, Clang, `-Werror` | cold 1 to 2 h |

Every workflow has a concurrency group per pull request or ref that cancels the older run. `release.yml` is unchanged.

**Windows, said plainly.** The full MSVC build (engine, editor, launcher, tools, tests) on a hosted `windows-2022` runner (4 cores, 16 GB RAM, a limited disk) has never been timed. It is kept because the guide's Build gate asks for MSVC, but nobody knows yet that it fits: the first run decides. Cold it will probably take 2 to 4 hours (a guess), the job limit is 6 hours and `windows.yml` sets 340 minutes. What changed to help: link-time optimisation is off for the test build (it stays on for the packaged release, `OPENMW_LTO_BUILD=${{ inputs.package }}`), the build runs `-j 4` because MSVC with precompiled headers ran out of heap space at the default parallelism on the project machine (guide, Pitfalls), and Qt is cached. There is no compiler cache for MSVC and no build-directory cache (the checkout's file times would rebuild everything anyway), so every Windows run is cold. If the first run is too slow or the disk fills, the fallback is to run `windows.yml` only on pushes to `main`/`vulkan` and on a label, not on every pull request. **The Lua integration tests do not run on Windows** (Linux only, as on GitLab).

**Line endings.** The Linux jobs check out LF, so the failure seen in the local Docker gate (CRLF in `scripts/data/integration_tests/test_lua_api/test_vfs_dir/lines.txt` after a Windows checkout was copied into the image, so the `vfs` Lua test read `"1\r"`) cannot happen on the runner. It can happen on the Windows runner (git there defaults to `core.autocrlf=true`) and in the local container flow, so: a new `.gitattributes` pins `*.sh`, `CI/*.txt`, `CI/github.env`, `tools/**/*.awk` and `scripts/data/integration_tests/**` to `eol=lf`; `windows.yml` runs `git config --global core.autocrlf false` before the checkout; and `checks.yml` fails when any of `*.sh`, `CI`, `scripts` or `tools` is stored with CRLF or mixed endings (none is today). The tree is not renormalised. Effect on a Windows developer: files in the pinned paths keep their CRLF working copy until git rewrites them and `git status` stays clean (git normalises these paths when comparing; checked with `git check-attr` and `git status`), a fresh checkout gets LF so Git Bash can run the scripts. Nothing else is affected.

**Reused, not changed:** `.gitlab-ci.yml` and the GitLab files stay until the GitHub jobs have run green (task scope). **Not ported:** clang-tidy (GitLab runs it on the whole tree for hours: proposed for the nightly workflow after the first measurements), the code coverage job, Coverity, the static-deps job, macOS and Android, the `tools/extension_*_check` game checks (they start the real game and need Morrowind data, which CI never has), and TSan except on request.

**Not changed on purpose:** the whole-tree clang-format 14 check was kept instead of a changed-lines check: it is stricter, and the tree passed it when last verified. The "only format lines you changed" advice is for a developer's newer local clang-format.

**Dry runs done** (Docker image `openeden-linux-noosg`, a fresh `git clone` of HEAD so the line endings are the repository's): `actionlint` (`rhysd/actionlint` through Docker, shellcheck notes ignored because they are the inherited style) reports no error in the five workflow files; `check_no_osg`, `check_layer_rules` (104 exceptions) and its self-test, `test_tests_registered`, `check_mwbase_fakes`, `check_game_conventions`, `check_file_names`, `check_extension_sdk`, `tools/testprofile/coverage.sh` (191 settings, OK) and the LF-storage check pass; clang-format-14 was installed in the container and run on the whole tree. **Not run:** any build, any test program, the Lua tests, `check_qt_translations.sh` (no Qt tools in the image), the nightly configure step, the workflows on GitHub, `windows.yml` (only its YAML is validated).

**Failures the dry run found in HEAD (`64a5ae95c1`) that the first push will turn red:**
1. `CI/check_tests_registered.sh`: `apps/components_tests/body/testmergedruntime.cpp` is listed twice in `apps/components_tests/CMakeLists.txt` at HEAD (lines 301 and 486). The working tree of this checkout no longer has the duplicate (another session's uncommitted edit), so it needs committing.
2. `CI/check_clang_format.sh` (clang-format 14): differences at HEAD in `game_packs/skyrimse` (a doc comment of `ModelRecord`) and `sdk/include/openeden/host.hpp` (the `getWorldViewService` comment), possibly more (the output was cut at 30 lines).
3. `CI/test_mwbase_fakes.sh` (fixed in this change): its self-test copied four headers but the committed doubles now include `dialoguemanager` and `soundmanager`, so it failed on any tree; it now copies one header per committed `.inc`.

**Required secrets:** none. `GITHUB_TOKEN` is used only by the existing Windows step that writes CI-ID.txt.

**What toddhow must do on GitHub (nothing was touched):**
1. Settings > General > Default branch: `main` (scheduled workflows run only on the default branch, so the nightly job needs it).
2. Settings > Branches (or Rulesets): protect `master` (no pushes, no force pushes, no deletion); protect `main` (no force pushes, require the checks below).
3. Required status checks for `main` (they appear in the list only after a first run): `Format-and-lint`, `Translations`, `Ubuntu`, and `windows-2022` once its time is known. Do NOT require the nightly `Sanitizers` and `Clang` jobs.
4. Settings > Actions > General: allow the actions used (`actions/checkout`, `actions/cache`, `actions/upload-artifact`, `hendrikmuhs/ccache-action`, `ilammy/msvc-dev-cmd`, `seanmiddleditch/gha-setup-ninja`, `softprops/action-gh-release`); workflow permissions can stay read-only.
5. After the first push look at the Windows job's time and disk use, then decide the Windows fallback above.

**The `vulkan` branch until 2026-11-13.** `checks.yml` and `push.yml` trigger on pushes to `main` and `vulkan` (the guide: each gated push goes to both until `vulkan` is deleted), so every gated push starts two identical sets of runs; the cost is accepted for the days left. After `vulkan` is deleted, remove it from the two `branches:` lists. `nightly.yml` runs on the default branch only. A pull request runs once.

**Still open for toddhow:** question 2 of the spec (should a pull request wait for the slow jobs): this change answers "sanitizers and Clang are nightly and manual, not required" as the working default.
