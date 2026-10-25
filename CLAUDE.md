# Engineering Guide for Claude Code — OpenEden

Oct 25, 2026 · @toddhow

## Purpose and how to use this guide

This guide tells Claude Code, and any human contributor, how to work on OpenEden: what to build first, where code lives, the rules every change follows, and how each change is verified. What to build and why lives in the design docs (OpenEden Plugin System, Eden Studio plan, and CreativeMotion's Living Design Doc and Implementation Doc for the animation side); this guide covers how.

- **What OpenEden is:** a full fork of OpenMW, started from the Jolt physics branch (cykoder's `jolt-physics`, the work behind OpenMW merge request !3919). The decision is CreativeMotion's `docs/decisions/0004-total-fork-of-openmw.md` (toddhow, 2026-10-18); the name OpenEden was chosen on 2026-10-20 and OpenEden is the only host for CreativeMotion (2026-10-23).
- **Placing it:** this guide lives at the repository root as `CLAUDE.md` and is committed, so Claude Code reads it at the start of every session. Design handoffs live in `docs/design/` and are committed; exported copies of the living design docs stay local and are never committed (the same rule as CreativeMotion).
- **Authority:** the design docs decide what the system does; this guide decides how work is done. When they disagree, or when a design doc disagrees with CreativeMotion's guide, stop and ask the project owner (toddhow) rather than choosing.
- **Owner:** toddhow is the architect and final reviewer, and is learning C++ and engine development through this project, so explain non-obvious changes in plain language in each pull request.
- **Updating:** when a decision changes, update the relevant design doc and this guide in the same change, and add a changelog row.

## What is inherited and what is new

OpenEden starts as roughly a million lines of someone else's working engine. Most mistakes in a fork come from treating inherited code as if it were new.

- **Inherited:** everything from OpenMW (`apps/`, `components/`, `extern/`, `files/`, `CI/`, `scripts/`, `docs/source/`), plus the Jolt branch's physics port (`apps/openmw/mwphysics/`, `components/nifjolt/`, `components/physicshelpers/`, `components/resource/physicsshape*`, `extern/CMakeLists.txt`). It works, it has tests, and Morrowind plays on it. Change it only for a reason a task spec states.
- **New:** the plugin system, the SDK and contracts, the compatibility packs, Eden Studio, and the hosting of CreativeMotion and the other feature plugins. New code follows this guide in full.
- **Default behaviour is Morrowind's:** with every new feature switched off, OpenEden plays Morrowind exactly as the fork point did. Anything that changes gameplay, balance, animation or physics feel ships behind a setting that is off by default, and "unchanged when off" is verified by comparison, not assumed.

## Scope of the first build

**First build: a fork that builds, tests and plays**

- The Jolt branch finished to the standard of OpenMW's own review (Track J below)
- The fork repository set up, building locally and in CI (M7)
- The plugin system's registry and lifecycle, in process (plugin phase P1)

**Next:** the rest of the plugin system (P2 to P6), with CreativeMotion as its first real client (CreativeMotion's M8 onwards), then the Morrowind compatibility pack, then Eden Studio.

**Not yet**

- Eden Studio, beyond what the plugin system's editor hooks need (its order against the plugin phases is an open question)
- Compatibility packs for any game other than Morrowind
- Hot reload of plugins
- A stable C ABI for third-party plugins (plugin phase P6)
- Renderer replacement (Vulkan, PBR); the Eden Studio plan mentions it but no decision or spec exists

## Tech stack and toolchain

Inherited choices stay as OpenMW made them unless a decision record changes them. Items marked "Confirm" are open questions for toddhow.

| Area | Choice | Notes |
| --- | --- | --- |
| Language | C++20 | `CMAKE_CXX_STANDARD 20` in the root `CMakeLists.txt`; matches CreativeMotion |
| Build | CMake (minimum 3.16), Ninja or MSBuild on Windows, Make or Ninja on Linux | Out-of-source builds only; the install prefix must never be inside the build directory (see Pitfalls) |
| Physics | [Jolt Physics](https://github.com/jrouwe/JoltPhysics) v5.5.0, fetched by `extern/CMakeLists.txt`, double precision | Pinned by tag and SHA-512; one Jolt world per session, owned by the core |
| Windows dependencies | vcpkg package set from OpenMW's deps builds (`CI/deps_versions.msvc.sh`, `VCPKG_TAG`), Qt 6.6.3 through aqtinstall | `CI/before_script.msvc.sh` is the reference; local copies go in `deps/` (git-ignored) |
| Linux dependencies | Ubuntu 24.04 packages from `CI/install_debian_deps.sh` | Uses OpenMW's PPAs, including its OpenSceneGraph fork |
| Rendering | OpenSceneGraph (OpenMW's fork on Linux CI, vcpkg's 3.6.5 on Windows) | Inherited; see Pitfalls for the Windows difference |
| UI | MyGUI in game, Qt 6 in the launcher, wizard and editor | Inherited |
| Scripting | Lua (LuaJIT) through OpenMW's Lua host | Plugins register tables under `openeden.<id>` (plugin doc 4.10) |
| Authored data | JSON with a canonical writer that round-trips byte for byte | The project's rule for new formats (CreativeMotion decision 0002, plugin doc 5.2) |
| Tests | GoogleTest (`components-tests`, `openmw-tests`, `openmw-cs-tests`), Lua integration tests (`scripts/integration_tests.py`) | Integration tests need OpenMW's example suite at the revision pinned in `.gitlab-ci.yml` |
| Formatting | clang-format 14 (`.clang-format`, `CI/check_clang_format.sh`), clang-tidy (`.clang-tidy`) | CI uses version 14; newer local versions format some untouched code differently, so only format lines you changed |
| Platforms | Windows (MSVC, x64), Linux (GCC and Clang) | macOS is inherited in CI but not a target until toddhow says so |
| CI | Inherited GitLab CI (`.gitlab-ci.yml`) and GitHub Actions (`.github/workflows/`) | Confirm which one OpenEden keeps (Open questions) |

Units and conventions follow OpenMW in inherited code (Morrowind units, Z up). New code that talks to CreativeMotion uses its conventions (meters, kilograms, seconds, radians, right-handed, Z up), and converts at one named boundary, never ad hoc.

## Repository layout

```text
CLAUDE.md                 this guide
apps/
  openmw/                 the game; mwphysics/ is the Jolt physics system
  opencs/                 the editor; becomes Eden Studio's shell (Eden Studio plan, phase 1)
  launcher/  wizard/      Qt tools; their strings are translated (files/lang/*.ts)
  navmeshtool/  physicsobjecttool/  bsatool/  esmtool/  niftest/  essimporter/  mwiniimporter/
  components_tests/  openmw_tests/  opencs_tests/  benchmarks/
components/               shared engine libraries (resource, nif, nifjolt, physicshelpers, detournavigator, lua, vfs, ...)
extern/                   fetched and vendored third-party code (Jolt, recastnavigation, MyGUI, ...)
files/                    data shipped with the engine: shaders, Lua, translations, settings defaults
docs/
  source/                 OpenMW's user and modding documentation (Sphinx)
  design/                 design handoffs (plugin system, Eden Studio)
  decisions/              decision records, NNNN-title.md (create with the first OpenEden decision)
  tasks/                  task specs, NNNN-title.md
  research/               research notes
CI/  .gitlab-ci.yml  .github/workflows/   build, test and check scripts
scripts/                  integration tests and developer scripts
```

Planned, created by the plugin system's task specs and not before:

```text
sdk/                      the SDK: host API headers, contract headers, CMake package (plugin doc 7.1)
plugins/<id>/             first-party plugins, each built against the SDK only (plugin doc 5.1, 7.2)
```

Each area that gets new OpenEden code gets its own short `CLAUDE.md` describing its rules and pitfalls; create it the first time a task changes that area (for example `apps/openmw/mwphysics/CLAUDE.md`).

## Architecture rules and module boundaries

The plugin design doc's dependency rules (section 3.1) are this project's architecture. Until the SDK exists, new code is written so it can move behind it without a rewrite.

```text
hosts (openeden, Eden Studio, test host) ──► core
plugins ──────────────────────────────────► SDK (host API + contract headers) only
compatibility pack ───────────────────────► SDK; provides openeden.game
core ─────────────────────────────────────► no plugin, ever
```

- **The core is groundwork:** physics on Jolt, rendering, platform, input, audio, VFS and resources, world and entities, frame loop, Lua host, saves. It names no feature and no game. A feature or a game rule never moves into the core to make a plugin's life easier; the host API grows instead.
- **Contracts only:** a plugin never includes another plugin's headers, links its library or shares globals with it. It asks the registry for a versioned contract.
- **Contracts carry no implementation and no third-party types** (no Jolt, OSG, MyGUI or Qt types in a contract signature).
- **One Jolt world:** the core owns it; plugins create bodies and constraints through the physics service and get IDs back, never pointers. Collision layers are registered by name; the core assigns bits.
- **Game rules live in the compatibility pack:** anything specific to Morrowind (records, races, skeleton names, `.nif`/`.kf` rules) moves out of the core into the Morrowind pack step by step, with Morrowind verified to play the same after each step.
- **Fixed, ordered phases:** plugins run in the documented phases (plugin doc 4.3), in load order; plugins never read a clock and get `dt` and the step index from the core.
- **Core never renders for a plugin:** debug drawing is submitted as data (points, lines, labels) and drawn by the core.
- **Inherited boundaries stay:** `components/` never includes `apps/`; `apps/openmw/mwbase/` interfaces are how game subsystems reach each other. These are the precedent the registry generalizes (plugin doc 3.2).

## Coding standards

Write code a learning C++ developer can follow, and that reads like the code around it.

- **Match the surrounding code:** inherited files keep OpenMW's style (naming with `m` prefixes for members, `camelCase` functions, OpenMW's include grouping); see OpenMW's [Policies and Standards](https://wiki.openmw.org/index.php?title=Policies_and_Standards). New SDK and plugin code follows CreativeMotion's standards.
- **No drive-by changes:** do not reformat, rename or "tidy" lines a task does not need to change. Unrelated changes make upstream merges harder and hide the real change in review (OpenMW's `CONTRIBUTING.md` says the same).
- **Ownership:** RAII everywhere; no raw `new` or `delete` in new code; `std::unique_ptr` by default, `std::shared_ptr` or `osg::ref_ptr` only where the surrounding system already uses it, with a comment saying why. Every new class states who owns it.
- **Engine and physics objects:** store handles or IDs (`JPH::BodyID`, entity IDs), never raw pointers, to anything the engine or Jolt can destroy; look them up when used.
- **Threading:** the physics system runs actor simulation on async worker threads (`apps/openmw/mwphysics/mtphysics.cpp`). Code called from a physics step must not move bodies other threads may be querying, take the main thread's locks, or touch game objects without the existing synchronization. Say in the pull request which thread each new callback runs on.
- **Hot paths:** no heap allocation in per-frame or per-physics-step code; preallocate and reuse buffers.
- **Determinism:** no wall-clock time, no unseeded randomness, no iteration over unordered containers where order affects results, in simulation code and in plugin load, phase and event order.
- **Jolt usage:** include `<Jolt/Jolt.h>` before any other Jolt header, in its own include block; use `JPH::RVec3`/`RMat44` for world positions (double precision is on); never construct a Jolt shape whose dimensions can be smaller than its convex radius; prefer `JPH::RayAABox` and similar geometry helpers over building temporary shapes.
- **Errors:** loaders return clear, located errors (file, line, field); no exception crosses the core/plugin boundary in either direction (plugin doc 6.8).
- **Style:** clang-format 14 and clang-tidy clean on changed lines; builds with warnings as errors on Linux (`-Werror`, as CI's `Ubuntu_GCC` job) and with no new warnings on MSVC (`/W4`); `const` by default.
- **Tuning values:** no magic numbers in new code; defaults live in settings, data files or named constants with a comment on their source. Inherited Morrowind values (GMSTs, vanilla formulas) keep their existing source.
- **User-visible strings:** new game UI strings go through OpenMW's YAML localisation; new Qt strings need the translation files regenerated (`CI/check_qt_translations.sh` fails otherwise).

## Upstream merges and the Jolt branch

Merges are where this codebase has already lost work: the Jolt branch's last two merges of OpenMW master silently reverted at least ten upstream fixes (#8303, #8414, #8206, #8847 and more; repaired in commit 5459a65292). Every merge follows these rules.

- **Merging upstream is optional, never casual:** merge OpenMW master only through a task that names the target commit.
- **Resolve conflicts against upstream's intent:** for each conflicted hunk, read the upstream commit that changed it (`git log -p <base>..<upstream> -- <file>`) and keep its behaviour, re-expressed for Jolt where needed. Never resolve a conflict by taking the fork's old version of a file wholesale.
- **Audit after every merge:** list the lines upstream added since the previous merge base that upstream still has but the fork lacks, per changed file, and account for every one in the pull request (kept, ported to Jolt, or dropped on purpose with a reason). A merge without this audit does not merge.
- **Re-run everything:** unit tests, the Lua integration tests and a short in-game check of the areas the merge touched.
- **Never push to OpenMW:** OpenEden does not open merge requests or push branches to `OpenMW/openmw`. Work that would help upstream is offered by toddhow, not by an agent.

## Agent workflow and task spec template

Every change follows the same loop, with tests written independently of the implementation and a fresh reviewer before toddhow.

1. **Spec:** write or confirm a task spec in `docs/tasks/` using the template below; it links the design doc section it implements and names the upstream or inherited code it touches by file and function.
2. **Tests first:** a test-writing subagent writes tests from the spec alone, without seeing any implementation.
3. **Implement:** the implementing agent makes the tests pass. It may not edit or delete tests without toddhow's approval; if a test looks wrong, it stops and says why.
4. **Review:** a fresh reviewing subagent that did not write the code checks the diff against the spec, with extra attention to lifetimes, threading, determinism, allocations in hot paths, and behaviour with the feature switched off.
5. **Verify:** all quality gates pass; physics or gameplay changes also get an in-game check with the steps written in the pull request.
6. **Explain and merge:** the pull request explains the change in plain language for toddhow, who approves the merge.

**Full loop required** for anything under `apps/openmw/mwphysics/`, `components/nifjolt/`, `components/physicshelpers/`, `components/resource/physicsshape*`, the plugin loader, registry, SDK and contracts, save games, and upstream merges. These carry the determinism, lifetime, threading and compatibility risks, so they never use a lighter loop.

**Lighter loop:** available only for tasks outside those areas (for example `CI/`, `scripts/`, documentation, launcher text) and only when toddhow marks the spec as lighter. No separate test-writing subagent; the implementing agent writes focused tests itself where they are cheap; a fresh reviewing subagent runs once per milestone. toddhow still approves each spec before implementation. A task that touches both areas uses the full loop.

Rules for every session:

- One task per branch; small pull requests.
- Ask when the spec is ambiguous instead of guessing.
- Never invent an API, file format detail or engine fact; cite a file and function at the current commit, a Jolt documentation page, or ask.
- Never commit, push, rebase shared branches or delete branches unless toddhow asks in that session.
- Keep each area's `CLAUDE.md` current with rules and pitfalls learned.
- When a change affects CreativeMotion's assumptions (the physics interface, collision layers, frame phases, the event bus), say so in the pull request and update `docs/design/` in the same change.

```markdown
# Task: <short name>
Design doc section: <link>
Goal: <one sentence>
Inherited code touched: <files and functions, at commit <hash>>
Inputs / outputs: <types, files, interfaces touched>
Constraints: <determinism, allocation, threading, off-by-default setting>
Acceptance criteria:
- [ ] <testable statement>
- [ ] With the feature off, <what stays exactly as before and how it is compared>
Out of scope: <what not to change>
```

## Testing and quality gates

A pull request merges only when every gate below passes; gates are added as the code they check comes to exist.

| Gate | What it checks | Runs |
| --- | --- | --- |
| Build | Compiles on Windows (MSVC) and Linux (GCC, Clang); `-Werror` on Linux, no new MSVC warnings | Every pull request |
| Format and lint | clang-format 14 clean (`CI/check_clang_format.sh`), clang-tidy clean on changed files | Every pull request |
| Unit tests | `components-tests`, `openmw-tests`, `openmw-cs-tests` | Every pull request |
| Integration tests | `scripts/integration_tests.py` against the example suite at the pinned revision, all Lua API tests passing | Every pull request touching `apps/openmw`, `components`, `files` or `scripts` |
| Sanitizers | AddressSanitizer and UndefinedBehaviorSanitizer test runs on Linux (inherited `Ubuntu_GCC_tests_asan`, `_ubsan` jobs) | Every pull request touching C++ |
| Translations | Qt `.ts` files match `lupdate` output (`CI/check_qt_translations.sh`) | Every pull request touching launcher, wizard or editor strings |
| Off means unchanged | With every new setting at its default, the affected behaviour matches the fork point (integration tests, a recorded comparison or a stated in-game check, as the spec says) | Every pull request adding a setting |
| Merge audit | Every upstream line dropped by a merge is accounted for (see Upstream merges) | Every upstream merge |
| Plugin boundaries | Plugins include only SDK and contract headers; the core starts with zero plugins | Once the SDK exists |
| Determinism | A scenario run twice gives identical results, also with plugins discovered in a different order | Once the plugin host exists |
| Allocation | Phase callbacks, per-step contract calls and event publishing allocate nothing | Once the plugin host exists |

Golden data (recordings, expected outputs) changes only in a pull request that says why, reviewed by toddhow.

## Milestones with acceptance criteria

Work them in order unless toddhow says otherwise. Each gets a task spec with its own numbers before work starts.

### Track J: finish the Jolt branch

- [x] MR !3919 review threads resolved: actor shape ratio check, `isAreaOccupiedByOtherActor` radius, broadphase layer names (commit 5459a65292)
- [x] Upstream fixes lost in the branch's merges restored (#8303, #8414, #8206, #8847, scene, cell preloader, projectile manager; 5459a65292)
- [x] Actors spawned inside each other separate (placeatpc), matching Bullet's actor-overlap rules (5459a65292)
- [x] Builds on Windows (MSVC) and Linux (GCC with CI's flags); unit tests and all 32 Lua integration tests pass, including `mwscript magic interactions` (2026-10-18, locally)
- [ ] In-game testing of the MR's tester list: flying over the world, backface-collision mods and moving platforms, a short play-through
- [ ] The actor collider shape drawn by `tcb` checked against the configured `actor collision shape type`
- [ ] The launcher's physics threads tooltip no longer mentions Bullet, with the translation files regenerated
- [ ] The integration tests run in OpenEden's CI

### M7 Prepare the fork

- [x] Full fork decided (CreativeMotion decision 0004) and named OpenEden (2026-10-20)
- [ ] OpenEden's own `docs/decisions/` started, recording the fork, its base commit and hosting
- [ ] The fork repository set up under OpenEden's name and hosting (the checkout's remote is still cykoder's GitLab fork)
- [ ] The fork builds in CI on Windows and Linux with all gates above that apply

### P1 Plugin registry and lifecycle, in process

- [ ] SDK skeleton: `Version`, `Registrar`, `ServiceLocator`, `Plugin`, `PluginDescriptor`, the phase list (plugin doc 4.1 to 4.3, 6.2)
- [ ] Registry, dependency resolution and lifecycle with plugins linked statically into a test host
- [ ] Resolution tests from manifests alone: load order, missing providers, version ranges, cycles, optional contracts, duplicate providers, `loadAfter`, deterministic tie-breaks
- [ ] Lifecycle tests: register, resolve, start, stop run in the documented order; a failing plugin disables its dependants and nothing else
- [ ] Empty host: the core runs a world for N steps and shuts down with zero plugins

### P2 Dynamic loading and manifests

- [ ] `openeden.plugin` v1 manifest format, validation with located errors, canonical writer that round-trips byte for byte
- [ ] Discovery, enable list, library loading and the handshake on Windows and Linux; a broken plugin is refused without running its code
- [ ] `openeden_add_plugin()` and the SDK install; CI builds a sample plugin against the installed SDK only, and an include check fails on a core-internal header

### P3 Core services behind the host API

- [ ] Frame phases and fixed-step callbacks wired into the engine loop, in a fixed order
- [ ] Physics service over the shared Jolt world, with collision layers registered by name
- [ ] Entity IDs, lifecycle events and per-entity plugin data
- [ ] Event bus with fixed-size queues; overflow counted and logged
- [ ] VFS mounting of plugin data and file format registration
- [ ] Settings namespaces (off by default) and save chunks; a save with a missing plugin's chunk loads and saves back unchanged

### P4 Scripting, diagnostics and editor hooks

- [ ] Lua tables per plugin (`openeden.<id>`)
- [ ] Per-plugin logging, profiling scopes and debug-draw submission as data
- [ ] Editor extension points for Eden Studio (workspaces, inspectors, gizmos), ignored by the game host

### P5 First real clients

- [ ] CreativeMotion loads as a plugin and uses the core's Jolt world instead of its own (CreativeMotion's M8; its guide owns the CreativeMotion side)
- [ ] Morrowind compatibility pack: the game contract defined, and Morrowind-specific code moved out of the core step by step with Morrowind verified to play the same after each step

### P6 Stable C ABI and community SDK

- [ ] C ABI over the same services with a header-only C++ wrapper
- [ ] SDK compatibility check in CI: a minor version that removes or changes anything fails
- [ ] SDK documentation, a template plugin and sample contracts

### Eden Studio (E1 to E6)

The six phases in `docs/design/eden-studio-plan.md`, section 6: studio shell and viewport, CreativeMotion docking, Reflectron and AdvancedBodies, ComplexClothing, dialogue and face timeline, production hardening. Each gets a task spec; where it starts relative to P1 to P6 is an open question.

### CreativeMotion in game (M8 to M13)

Owned by CreativeMotion's guide (`I:\Projects\CreativeMotion\CLAUDE.md`, "Path to OpenMW"). OpenEden's side is the host API those milestones need; when one of them needs something the SDK lacks, the SDK grows through an OpenEden task spec, not a CreativeMotion shortcut.

## Definition of done

A task is done only when all of these hold:

- Every acceptance criterion in its spec is met and checked by a test or a written, repeatable in-game check
- All quality gates pass
- With every new setting at its default, Morrowind behaves as before, and the pull request says how that was compared
- A fresh reviewing subagent found no unresolved issues
- The area's `CLAUDE.md` and any affected design doc are updated
- The pull request explains what changed and why in plain language
- toddhow approved the merge

## Guardrails

These rules protect the project legally and keep it honest; breaking one blocks a merge.

- **GPL-3.0-or-later.** OpenEden is a fork of a GPLv3 program; every file stays GPL-compatible, and OpenMW's copyright notices, `AUTHORS.md` entries and licence files are kept. First-party plugins are GPL-3.0-or-later (plugin doc 9).
- **Not OpenMW.** OpenEden never presents itself as OpenMW or as endorsed by the OpenMW project, and never pushes to or opens merge requests on OpenMW's repositories.
- **No game assets.** No Bethesda files (`.esm`, `.esp`, `.bsa`, `.nif`, `.kf`, `.hkx`, textures, audio) in the repository; compatibility packs read the user's own installed game data; tests use OpenMW's example suite, generated data or permissively licensed samples with the licence recorded.
- **No proprietary code or SDKs.** Never add the Havok SDK, Creation Kit or game binaries, decompiled game code, or code from mods that are not open-source licensed.
- **Delirium Physics: ideas only.** The CreativeMotion guardrail applies here too: no code, constants, parameter tables, body layout or data formats from Delirium Physics.
- **Licences checked before adding a dependency.** Record each new dependency's licence and pinned version in `docs/dependencies.md` (create it with the first new dependency); it must be GPL-3.0-or-later compatible.
- **Cite engine facts.** Any statement about OpenMW, Jolt, OpenSceneGraph or Morrowind behaviour cites a file and function at a commit, or a documentation page; invented engine facts are treated as bugs.
- **Original engine behaviour is a feature until proven otherwise.** Follow OpenMW's "original engine fixes" policy in `CONTRIBUTING.md`; a change to vanilla mechanics is a new, off-by-default setting, not a fix.

## Pitfalls already met

Learned while finishing the Jolt branch; each cost real time.

- **Windows parallel builds run out of memory.** MSVC with precompiled headers fails with "compiler is out of heap space" at the default parallelism on this machine; build with `-j 8`. In Docker, the same build at `-j $(nproc)` crashed Docker Desktop's engine; use `-j 8` and `--memory 12g`.
- **Never install into the build directory.** OpenEden's Windows install rule copies the build directory's runtime files, so an install prefix inside it copies itself recursively (`install/install/install/...`). Install to a separate folder.
- **Windows runtime files.** A runnable Windows build needs the vcpkg `bin` DLLs, `bin/Release/MyGUIEngine.dll`, the `osgPlugins-3.6.5` folder and Qt deployed with `windeployqt`; without MyGUI the tests exit with `0xC0000135`.
- **Integration tests do not run on Windows.** vcpkg's OpenSceneGraph crashes in `osg::Image`'s copy constructor while loading the example suite's COLLADA player model; Linux CI uses OpenMW's OSG fork. Run them on Linux (CI or the Ubuntu 24.04 container built from `CI/install_debian_deps.sh`).
- **Line endings.** The Windows checkout uses `core.autocrlf=true`, so shell scripts, Python scripts and text test data have CRLF endings there; normalize them before running in Linux, or the `vfs` Lua test fails on `"1\r"`.
- **Build and run deps conflict on Ubuntu.** Installing CI's build groups together with `openmw-integration-tests` fails with held packages; install the build groups and add only xvfb, python3 and git-lfs.
- **Moved checkouts.** CMake caches record absolute source paths; after moving or renaming the checkout, reconfigure (or delete the build directory) before building.
- **Morrowind config import.** A launcher setup that skipped the `Morrowind.ini` import has no `fallback-archive=` lines and shows a magenta menu; `openmw-iniimporter` fixes it but resets the content list, so restore the user's plugins afterwards.

## To do

Work toddhow wants done later, not yet a task spec. Each item becomes a spec (or a research note) before work starts.

- [x] Commit `docs/design/` (the plugin system and Eden Studio handoffs; done 2026-10-25)
- [ ] Start `docs/decisions/` with the fork record: base commit (Jolt branch at 5459a65292 or later), why, and what "upstream merges are optional" means in practice
- [ ] Decide whether CreativeMotion's M8 lands as a plugin (needs P1 to P3) or first as a component inside the engine, since CreativeMotion's guide still says "component"

## Open questions

Claude Code should raise these with toddhow when a task depends on one, rather than deciding alone.

- [ ] Hosting and CI: CreativeMotion decision 0003 moved hosting to GitHub, but this checkout's remote is cykoder's GitLab fork and both GitLab CI and GitHub Actions are present; which does OpenEden keep?
- [ ] Order of Eden Studio against the plugin phases (the studio needs P4's editor hooks)
- [ ] Plugin doc open questions 1 to 6: when to add the C ABI, where contracts live, user-overridable load order, hot reload, one game per session, third-party licence policy
- [ ] Executable and data names: when `openmw`, `openmw-cs` and `openmw.cfg` become OpenEden names, and how existing OpenMW configs are migrated
- [ ] Whether macOS stays a supported platform
- [ ] Renderer direction: the Eden Studio plan mentions Vulkan and PBR; no decision exists

## References

- [OpenMW merge request !3919: Jolt physics implementation](https://gitlab.com/OpenMW/openmw/-/merge_requests/3919)
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics)
- [OpenMW Policies and Standards](https://wiki.openmw.org/index.php?title=Policies_and_Standards) and `CONTRIBUTING.md`
- [GNU GPL FAQ: plug-ins](https://www.gnu.org/licenses/gpl-faq.html#GPLPlugins)
- `docs/design/openeden-plugin-system.md`, `docs/design/eden-studio-plan.md`
- CreativeMotion: `I:\Projects\CreativeMotion\CLAUDE.md`, `docs/decisions/0004-total-fork-of-openmw.md`, `docs/design/advancedbodies-handoff.md`

## Changelog

| Date | Change |
| --- | --- |
| 2026-10-25 | Guide created, modelled on CreativeMotion's: inherited versus new code, scope, stack, layout, plugin architecture rules, coding standards, upstream merge rules, agent workflow, gates, Track J, M7, plugin phases P1 to P6, Eden Studio and CreativeMotion pointers, guardrails, pitfalls from the Jolt branch work, to do and open questions. Draft for toddhow's review |
