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
- **New:** the plugin system, the SDK and contracts, the compatibility packs, Eden Studio, CreativeMotion's integration into the engine (decision 0001), and the hosting of the other feature plugins. New code follows this guide in full.
- **Default behaviour is Morrowind's:** with every new feature switched off, OpenEden plays Morrowind exactly as the fork point did. Anything that changes gameplay, balance, animation or physics feel ships behind a setting that is off by default, and "unchanged when off" is verified by comparison, not assumed.

## Scope of the first build

**First build: a fork that builds, tests and plays**

- The Jolt branch finished to the standard of OpenMW's own review (Track J below)
- The fork repository set up, building locally and in CI (M7)
- The plugin system's registry and lifecycle, in process (plugin phase P1)

**Next:** the move to Vulkan on VulkanSceneGraph (Track V, decision 0002) comes before other work, so new features are built on it once. After it: CreativeMotion's in-game milestones from M10 inside the engine (decision 0001), the rest of the plugin system with the Morrowind compatibility pack as its first real client, then Eden Studio.

**Not yet**

- Eden Studio, beyond what the plugin system's editor hooks need (its order against the plugin phases is an open question)
- Compatibility packs for any game other than Morrowind
- Hot reload of plugins
- A stable C ABI for third-party plugins (plugin phase P6)
- An OpenGL fallback, a DirectX backend or a renderer abstraction layer (decision 0002: none)
- New renderer features (PBR, ray tracing) beyond what the Vulkan move brings, until Track V is done

## Tech stack and toolchain

Inherited choices stay as OpenMW made them unless a decision record changes them. Items marked "Confirm" are open questions for toddhow.

| Area | Choice | Notes |
| --- | --- | --- |
| Language | C++20 | `CMAKE_CXX_STANDARD 20` in the root `CMakeLists.txt`; matches CreativeMotion |
| Build | CMake (minimum 3.16), Ninja or MSBuild on Windows, Make or Ninja on Linux | Out-of-source builds only; the install prefix must never be inside the build directory (see Pitfalls) |
| Physics | [Jolt Physics](https://github.com/jrouwe/JoltPhysics) v5.5.0, fetched by `extern/CMakeLists.txt`, double precision | Pinned by tag and SHA-512; one Jolt world per session, owned by the core |
| Windows dependencies | vcpkg package set from OpenMW's deps builds (`CI/deps_versions.msvc.sh`, `VCPKG_TAG`), Qt 6.6.3 through aqtinstall | `CI/before_script.msvc.sh` is the reference; local copies go in `deps/` (git-ignored) |
| Linux dependencies | Ubuntu 24.04 packages from `CI/install_debian_deps.sh` | Uses OpenMW's PPAs, including its OpenSceneGraph fork |
| Rendering | Moving to Vulkan on VulkanSceneGraph (VSG) with vsgXchange (decision 0002, Track V); until Track V lands, OpenSceneGraph (OpenMW's fork on Linux CI, vcpkg's 3.6.5 on Windows) | No OpenGL fallback, no DirectX, no abstraction layer; VSG and vsgXchange versions pinned in the Track V spec |
| Math | Moving from OSG math to VSG math engine-wide (decision 0002); CreativeMotion keeps `opas::math` | Conversions between the two happen at one named boundary per area during the transition |
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
  plugin/                 the plugin loader, registry, lifecycle and core services behind the host API
  creativemotion/         vendored copy of CreativeMotion, compiled into the engine (decision 0001); never edited in place
  inertialmotion/         inertial locomotion state (docs/design/inertial-locomotion.md)
sdk/                      the SDK: host API headers, contract headers (plugin doc 7.1)
plugins/<id>/             first-party plugins, each built against the SDK only (plugin doc 5.1, 7.2); compat_morrowind first
extern/                   fetched and vendored third-party code (Jolt, recastnavigation, MyGUI, ...)
files/                    data shipped with the engine: shaders, Lua, translations, settings defaults
docs/
  source/                 OpenMW's user and modding documentation (Sphinx)
  design/                 design handoffs (plugin system, Eden Studio, inertial locomotion)
  decisions/              decision records, NNNN-title.md
  tasks/                  task specs, NNNN-title.md
  research/               research notes
CI/  .gitlab-ci.yml  .github/workflows/   build, test and check scripts
scripts/                  integration tests and developer scripts
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
- **CreativeMotion is the one feature in the core** (decision 0001, toddhow 2026-10-28): it is compiled into the engine because it needs engine internals every frame and animation is groundwork every host needs. The exception covers CreativeMotion only; every other feature system is a plugin. CreativeMotion's own code stays free of engine types; only the engine-side adapters (`apps/openmw/mwrender/creativemotiondriver.*`, `ActorIkController`) touch both. `components/creativemotion/` is a copy of CreativeMotion's repository: never patch it in place; a needed change becomes a CreativeMotion task spec, and the copy is refreshed with the source commit named in the commit message.
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

**Full loop required** for every Track V stage (the renderer and the math change), anything under `apps/openmw/mwphysics/`, the CreativeMotion adapters and refreshes of `components/creativemotion/`, `components/nifjolt/`, `components/physicshelpers/`, `components/resource/physicsshape*`, the plugin loader, registry, SDK and contracts, save games, and upstream merges. These carry the determinism, lifetime, threading and compatibility risks, so they never use a lighter loop.

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

### Track V: move the renderer to Vulkan on VulkanSceneGraph

Decision 0002; background in `docs/research/0001-vulkan-renderer.md`. The next major track: other features wait for it. The renderer is transplanted from vsgopenmw (OpenMW 0.49 + VSG) and adapted to OpenEden's 0.51 base; every transplanted file keeps its notice and its source commit is named. Stages may be reordered in their specs, but each ends with the game building and its gates passing.

**V0 Groundwork**
- [ ] VSG and vsgXchange fetched at pinned versions (like Jolt), with their licences in `docs/dependencies.md`; builds on Windows (MSVC) and Linux
- [ ] vsgopenmw pinned to a named commit as the transplant source, and its authors credited
- [ ] An inventory mapping vsgopenmw's rendering components (`animation`, `mwanimation`, `render`, `pipeline`, `view`, `vsgutil`, `vsgadapters`, `esm3terrain`, `apps/openmw/mwrender`) to OpenEden's files, marking what is taken, adapted or written fresh
- [ ] A screenshot comparison rig (fixed cell, fixed frame count, automatic screenshot and quit, as vsgopenmw's `render_gold.sh` does) that captures today's OSG renders as the reference

**V1 Math**
- [ ] OSG math replaced by VSG math in game logic (`mwphysics`, `mwmechanics`, `mwworld`, `components/detournavigator` and the rest outside rendering), with unit and integration tests passing and Morrowind unchanged
- [ ] The CreativeMotion and Jolt conversion helpers (`components/misc/convert.hpp`) work with VSG math

**V2 Renderer transplant**
- [ ] The game starts, loads exterior and interior cells and renders them through VSG, compared against the V0 references
- [ ] Movement, combat, AI pathing (navigator enabled) and saving work, which vsgopenmw's `master` does not yet do
- [ ] All 32 Lua integration tests pass on the Vulkan build

**V3 OpenEden's rendering touchpoints**
- [ ] Foot IK, look-at, compound hitbox bone sampling and the CreativeMotion pose driver work on VSG's animation system, each still off by default and unchanged when off
- [ ] The Jolt physics debug draw and the navmesh debug draw render on VSG

**V4 Parity**
- [ ] vsgopenmw's open parity items done: weather particles, water ripples, object paging, normal maps, alpha to coverage, the missing NIF features
- [ ] Every OpenMW renderer setting either works on VSG or is removed with a reason
- [ ] A decision on modders' post-processing shaders (`.omwfx`): translated, replaced by a new format, or dropped

**V5 Tools and editor**
- [ ] Every application builds and runs: the editor (OpenMW-CS, Eden Studio's shell), navmeshtool, the launcher and wizard
- [ ] OpenSceneGraph removed: no OSG headers or libraries in the build, CI green on Windows and Linux

### P1 Plugin registry and lifecycle, in process

- [x] SDK skeleton: `Version`, `Registrar`, `ServiceLocator`, `Plugin`, `PluginDescriptor`, the phase list (plugin doc 4.1 to 4.3, 6.2)
- [x] Registry, dependency resolution and lifecycle with plugins linked statically into a test host
- [x] Resolution tests from manifests alone: load order, missing providers, version ranges, cycles, optional contracts, duplicate providers, `loadAfter`, deterministic tie-breaks
- [x] Lifecycle tests: register, resolve, start, stop run in the documented order; a failing plugin disables its dependants and nothing else
- [x] Empty host: the core runs a world for N steps and shuts down with zero plugins

### P2 Dynamic loading and manifests

- [x] `openeden.plugin` v1 manifest format, validation with located errors, canonical writer that round-trips byte for byte
- [x] Discovery, enable list, library loading and the handshake on Windows and Linux; a broken plugin is refused without running its code
- [x] `openeden_add_plugin()` CMake helper and dynamic library lifecycle integration (destruction before unload invariant)

### P3 Core services behind the host API

- [x] Frame phases and fixed-step callbacks wired into the engine loop, in a fixed order
- [x] Physics service over the shared Jolt world, with collision layers registered by name
- [x] Entity IDs, lifecycle events and per-entity plugin data
- [x] Event bus with fixed-size queues; overflow counted and logged
- [x] VFS mounting of plugin data and file format registration
- [x] Settings namespaces (off by default) and save chunks; a save with a missing plugin's chunk loads and saves back unchanged

### P4 Scripting, diagnostics and editor hooks

- [x] Lua tables per plugin (`openeden.<id>`)
- [x] Per-plugin logging, profiling scopes and debug-draw submission as data
- [x] Editor extension points for Eden Studio (workspaces, inspectors, gizmos), ignored by the game host

### P5 First real clients

- [x] ~~CreativeMotion loads as a plugin and uses the core's Jolt world instead of its own~~ (done in task 0005, then superseded: CreativeMotion is compiled into the engine, decision 0001)
- [x] Morrowind compatibility pack loads as the first real plugin, providing `openeden.game` and a `.kf` to glTF converter contract (`plugins/compat_morrowind/`)
- [ ] Morrowind-specific code moved out of the core into the pack step by step, with Morrowind verified to play the same after each step

### P6 Stable C ABI and community SDK

- [ ] C ABI over the same services with a header-only C++ wrapper
- [ ] SDK compatibility check in CI: a minor version that removes or changes anything fails
- [ ] SDK documentation, a template plugin and sample contracts

### Eden Studio (E1 to E6)

The six phases in `docs/design/eden-studio-plan.md`, section 6: studio shell and viewport, CreativeMotion docking, Reflectron and AdvancedBodies, ComplexClothing, dialogue and face timeline, production hardening. Each gets a task spec; where it starts relative to P1 to P6 is an open question.

### CreativeMotion in game (M8 to M13)

Owned by CreativeMotion's guide (`I:\Projects\CreativeMotion\CLAUDE.md`, "Path to OpenMW"). Since decision 0001, CreativeMotion is compiled into the engine, so OpenEden's side is the engine adapters those milestones need, each through an OpenEden task spec (0006 IK, 0007 animation runtime); changes to CreativeMotion itself go through CreativeMotion's own task specs.

- [ ] In-engine physics adapter for ragdolls (M10): CreativeMotion's `PhysicsWorld` over the core's single Jolt world, replacing the removed plugin's `HostPhysicsWorld`; never a second Jolt world

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
- **Code taken from vsgopenmw keeps its origin.** It is GPL-3.0 like OpenEden; transplanted files keep their copyright notices, the commit message names the vsgopenmw commit they came from, and vsgopenmw's authors are credited (decision 0002).
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
- [ ] Record the fork as an OpenEden decision (`docs/decisions/` started with 0001): base commit (Jolt branch at 5459a65292 or later), why, and what "upstream merges are optional" means in practice
- [x] Decide whether CreativeMotion lands as a plugin or inside the engine: inside the engine, CreativeMotion only (decision 0001, 2026-10-28)

## Open questions

Claude Code should raise these with toddhow when a task depends on one, rather than deciding alone.

- [ ] Hosting and CI: CreativeMotion decision 0003 moved hosting to GitHub, but this checkout's remote is cykoder's GitLab fork and both GitLab CI and GitHub Actions are present; which does OpenEden keep?
- [ ] Order of Eden Studio against the plugin phases (the studio needs P4's editor hooks)
- [ ] Plugin doc open questions 1 to 6: when to add the C ABI, where contracts live, user-overridable load order, hot reload, one game per session, third-party licence policy
- [ ] Executable and data names: when `openmw`, `openmw-cs` and `openmw.cfg` become OpenEden names, and how existing OpenMW configs are migrated
- [ ] Whether macOS stays a supported platform (with Vulkan it would need MoltenVK, decision 0002)
- [x] Renderer direction: Vulkan on VulkanSceneGraph, total move, no abstraction layer (decision 0002, 2026-10-28; background in `docs/research/0001-vulkan-renderer.md`)
- [ ] The `openeden.animation` contract has had no provider since decision 0001: keep it for plugins that want to drive animation, or remove it from the SDK?

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
| 2026-10-28 | Vulkan decided (decision 0002, toddhow): the renderer moves entirely to Vulkan on VulkanSceneGraph, VSG math replaces OSG math engine-wide, no OpenGL fallback, DirectX or abstraction layer, the renderer is transplanted from vsgopenmw starting now, and the move is the next major track. New Track V (V0 groundwork to V5 OSG removed), stack rows for rendering and math, scope and Not yet updated, Track V in the full-loop list, a guardrail for vsgopenmw code, renderer open question closed |
| 2026-10-28 | CreativeMotion compiled into the engine, not loaded as a plugin (decision 0001, toddhow; task 0007 stage R0): CreativeMotion only, because it needs engine internals and is core to OpenEden. Architecture rule for the exception, layout updated (`components/plugin`, `components/creativemotion`, `components/inertialmotion`, `sdk/`, `plugins/`, `docs/decisions/`), P5's first client is now the Morrowind compatibility pack, CreativeMotion section points at the engine adapters and adds the M10 physics adapter, open question on the unprovided `openeden.animation` contract |
| 2026-10-25 | CreativeMotion Milestone M9 (IK for Morrowind, task 0006) completed: extended `AnimationContract` (`openeden.animation` v1.2) with pure kinematic IK methods (`solveTwoBone`, `solveFootPlacement`, `solveLookAt`, `solveTurnSmoothing`); implemented IK algorithms in `CreativeMotionPlugin` delegating `solveTwoBone` to `opas::ik::solveTwoBone` with double-precision clamping; added `actor collision shape type = 3` (Capsule) in `components/detournavigator` and `MWPhysics::Actor` to allow actors to navigate close to terrain/ledges; added `ActorIkController` in `MWRender` with strict off-by-default invariant (`footPlanting`, `lookAt`, `smootherTurns`); authored comprehensive unit and property test suite (`test_ik_contract.cpp`), passing all 9 test suites and full `components-tests` (1543/1543 passing). |
| 2026-10-25 | Phase P5 first client (CreativeMotion plugin integration, task 0005) completed: extended `PhysicsService` and `PhysicsServiceImpl` over OpenEden's shared Jolt `JPH::PhysicsSystem` (double-precision compatible, bodies, cone/pyramid joints, collision groups, motors, impulses, velocities, kinematic targets, motion types, raycasts), defined `AnimationContract` (`openeden.animation` v1.1), implemented `HostPhysicsWorld` (`opas::physics::PhysicsWorld` over `PhysicsService` with `OPAS_BUILD_PHYSICS=OFF`), implemented `CreativeMotionPlugin` (`plugins/creativemotion/`), authored `docs/design/plugin-implementation-guide.md`, and passed 17 Phase 5 tests (116 total plugin tests passing). |
| 2026-10-25 | Phase P4 (Scripting, diagnostics and editor hooks, task 0004) completed: per-plugin Lua tables (`openeden.<id>`), per-plugin logging, profiling scopes, debug-draw data submission, and Eden Studio editor extension points (`EditorService`), wired into `HostImpl` with 18 unit tests. |
| 2026-10-25 | Phase P3 (Core services behind the host API, task 0003) completed: fixed-step and frame phase callbacks, `EntityManager` with generation-checked handles and per-entity plugin data, `EventBus` with bounded queues and overflow counting, named collision layer registry (`CollisionLayerRegistry`), `VfsService` plugin data mounting and format readers, `SettingsService` namespaces (off by default), and `SaveService` resilient chunk serialization, wired into `HostImpl` with 49 unit tests. |
| 2026-10-25 | Phase P2 (Dynamic loading and manifests) completed: manifest format parser with located errors, canonical round-trip writer, DynamicLibrary RAII wrapper (Windows/POSIX), directory discovery, enable list filtering, handshake validation with exception containment, destruction before unload in LifecycleManager, openeden_add_plugin CMake macro, and 34 new unit tests (68 total plugin tests, 1414 components tests passing). |
| 2026-10-25 | Phase P1 (Plugin registry and lifecycle) completed: SDK skeleton headers, manifest models with C++20 keywords handled, deterministic dependency resolution, service registry, reverse load order shutdown and teardown, zero hot-path allocations, exception boundary containment, and 34 unit tests. |
| 2026-10-25 | Guide created, modelled on CreativeMotion's: inherited versus new code, scope, stack, layout, plugin architecture rules, coding standards, upstream merge rules, agent workflow, gates, Track J, M7, plugin phases P1 to P6, Eden Studio and CreativeMotion pointers, guardrails, pitfalls from the Jolt branch work, to do and open questions. Draft for toddhow's review |
