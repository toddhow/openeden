# Linux quality gate in Docker

`run.sh [stage ...]` runs the Linux part of the gates from a Windows (Git Bash) or Linux host with Docker.
Stages: `clone`, `format`, `build`, `test`, `lua` (default: all, in that order).

- `clone`: an LF clone of `main` (`GATE_BRANCH`) into `$GATE_ROOT/src` with `core.autocrlf=false`; records the commit in `out/commit.txt`.
- `format`: clang-format 14 (`openeden-cf14`) over `git ls-files` minus `extern/`, as `CI/check_clang_format.sh` does. That image has no git, so the
  file list is made on the host. It only reports (`out/format_differs.txt`, with +/- line counts); it never reformats.
- `build`: configure and build in `openeden-linux-noosg` as `.github/workflows/push.yml` does (RelWithDebInfo,
  `-Werror -Werror=implicit-fallthrough`, the three test programs, everything else CMake enables), Ninja `-k 0 -j 8`.
  The build folder (`$GATE_ROOT/build`) and ccache persist, so a rerun is incremental. Log: `out/build.log`.
- `test`: `components-tests` without `*Slang*:*Complement*:*Property*` (very slow), `openmw-tests`, `eden-studio-tests`, `edenarchive-tests`. Logs in `out/`.
- `lua`: `scripts/integration_tests.py` against OpenMW's example suite at the revision pinned in `.gitlab-ci.yml`, under xvfb with
  lavapipe. Needs the network (suite, and pip/apt packages the image lacks).

Rules (toddhow): one container at a time, `--memory 12g`, `-j` never above 8, and only `--rm` containers of this script are created.
Environment: `GATE_ROOT` (default `I:/Projects/OpenEden-work/linuxgate`), `GATE_BRANCH`, `GATE_REPO`, `BUILD_IMAGE`, `FORMAT_IMAGE`, `GATE_JOBS`, `GATE_MEM`.
Nothing in the checked repository is modified.
