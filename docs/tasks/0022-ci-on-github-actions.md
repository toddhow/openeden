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
