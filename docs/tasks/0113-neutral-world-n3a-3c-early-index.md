# Task: The neutral world model, stage N3a-3c (start the Skyrim SE pack's world index before the engine's own content load, so the two overlap)

Status: BUILT (option A, see the stage log at the end). Was: DRAFT. Facts below are cited from the code at the current checkout; anything not read in code is marked RECALLED-VERIFY.

Design doc section: `docs/design/neutral-world-model.md` section 10.6 (stages) and the N3a-3b log in `docs/tasks/0109-neutral-world-model-n3a-3.md` (decision 2: the main thread waits at the first query until the index starts before the `World` constructor); decision 0016.
Goal: with `[Game] neutral world` on and `game=skyrimse`, the pack's world index (`SkyrimWorldView::startLoad`) runs on its worker WHILE the engine loads the session's content, so the first query (the neutral start) no longer waits 1.45 to 1.79 s and time-to-cell drops by roughly the overlap (target below).

## What the code does today (ordering)

1. `Engine::prepareEngine` (`apps/openmw/vsgengine.cpp`, lines ~376 to 430): builds the `ExtensionHost` (discovers and loads extensions and game packs; `mLoaded` is set in `extensionhost.cpp` ~229/257), builds the VFS (`VFS::registerArchives`), calls `mExtensionHost->setEngineVfs(mVFS.get())`, then `mExtensionHost->setSessionContent(args.contentFiles, mFileCollections)` (~428). The content list, the file collections and the VFS are all ready here, long before any world exists.
2. `ExtensionHost::setSessionContent` (`extensionhost.cpp` ~501): returns at once when `mNeutralWorldStore == nullptr` (neutral world off). Otherwise reads one header per plugin (`collectSessionContent`, `Content::readSessionTag`) and hands them to `mHost.worldSources().setSessionContent(...)` and `setContentFilter`. It starts nothing.
3. The engine's own content load: `Boot::threadLoad` (`apps/openmw/boot.hpp`, runs on the loading thread) constructs `MWWorld::World`; its constructor calls `World::loadContentFiles` (`apps/openmw/mwworld/worldimp.cpp` ~267 and ~2831), loading every plugin into `ESMStore` (about 2.6 s on the five Skyrim masters per the N3a-3b log; the ESM4 path stays until N7).
4. Only after that, `Game::Game` (`apps/openmw/game.hpp` ~82) calls `mEngine.mExtensionHost->startGamePack(mWorld->getPhysicsSystem())`. `ExtensionHost::startGamePack` (`extensionhost.cpp` ~538) does `beginRunning()`, attaches the world if given a `PhysicsSystem*`, `mLifecycle.initializeGamePack()` (`components/extension/lifecycle_manager.cpp` ~260), then `installPackRules()`.
5. The pack's `start()` reaches `loadSessionContent` (`game_packs/skyrimse/src/skyrimse_game_pack.cpp` ~100 to 170): builds a `VfsFileSource` over the `VfsService` (string tables), `mWorldView.configure(...)`, asks `WorldSourceService::sessionContent` (`sdk/include/openeden/game/world_source_service.hpp` ~61, v1.1, main thread only) and calls `SkyrimWorldView::startLoad` (`game_packs/skyrimse/src/world_view.cpp` ~207), which spawns the worker. The first query then waits for it (bounded by `Options::mIndexWait`).

So the index could start at step 2 but starts after step 3. It needs the session content list (in `WorldSourceService` after step 2), the engine VFS (`setEngineVfs`), the pack's settings service and a logger. It does not need the `World`, the `ESMStore` or physics (RECALLED-VERIFY: confirm `SkyrimGamePack::start` touches no physics/world service before `loadSessionContent`; `startGamePack` already supports `physics == nullptr`).

## Options

**A (recommended): start the game pack at the end of `prepareEngine`, right after `setSessionContent`, only with `[Game] neutral world` on.** Call `mExtensionHost->startGamePack(nullptr)` there. It already handles a null physics system (`if (physics != nullptr && !mWorld) attachWorldNow(...)`); `ExtensionHost::attachWorld` tolerates a pack that started first (~397). The later `Game::Game` call must still attach the physics world: `startGamePack` returns false on `mRunning`, so the attach in `Game::Game` has to be made explicit (see Inherited code touched). No SDK or ABI change (SDK stays 4.0, `WorldSourceService` stays 1.1), no pack change.
- Threading: the pack start runs on the main thread in `prepareEngine`; the index worker then overlaps the World constructor on the loading thread. Both read the VFS; registration has finished by then (RECALLED-VERIFY: confirm `VFS::Manager` is safe for concurrent readers after registration, task 0050's documented contract).
- `game.hpp` needs the pack started BEFORE the crafting windows and dialogue manager are built; earlier still satisfies that.
- Risks: a pack fault now arises before the Game exists; `takeSessionFault` is polled from `Game::run`, so it is held until then (RECALLED-VERIFY). `beginRunning()` installs the log bridge earlier, so earlier engine log lines are forwarded to extension sinks.

**B: a new additive pack hook, "content list known"** (for example `prepareSession()` called from `setSessionContent`, `start()` unchanged), so indexing starts early but rules install at the old point. Needs an additive SDK interface change and test-double changes: more surface than A for the same gain. Keep only if toddhow does not want the pack started that early.

**C: the pack reads the config's content list itself.** Rejected: duplicates `collectSessionContent` (missing files, header probing, loader filter), bypasses the loader filter, and needs engine configuration types in the pack (layer rules).

## Recommendation

Option A: start the game pack at the end of `prepareEngine`, gated on `Settings::game().mNeutralWorld.get()` and on the host having a game pack, so the index worker overlaps the World constructor; no SDK, ABI or pack change. Expected saving is the overlap of the 1.45 to 1.79 s first-query wait with the ~2.6 s content load (RECALLED-VERIFY: measure; two parallel loads may contend for memory and disk).

## Inherited code touched (current checkout; fill the hash when work starts)

- `apps/openmw/vsgengine.cpp` `Engine::prepareEngine`: one gated call after `setSessionContent`.
- `apps/openmw/game.hpp` `Game::Game` and `apps/openmw/extensionhost.cpp` `ExtensionHost::startGamePack` / `attachWorld`: the physics attach must still happen when the pack started earlier (attach when `physics != nullptr && !mWorld` even if already running).
- No change to `game_packs/skyrimse`, `sdk/`, `components/extension`, CMake or `mwphysics`.

Inputs / outputs: no format or interface change.
Constraints: determinism (the index result identical to N3a-3b: same cell, same object counts); threading as above; no hot-path allocation change; `[Game] neutral world` off runs no new code path (the call sits behind the setting and `hasGamePack()`).

## Acceptance criteria

- [ ] Tests first, red against the current ordering: a host test with a fake game pack and neutral world on, showing `startGamePack(nullptr)` before a world exists starts the pack once; a later `startGamePack(physics)` from `Game` does not start it again and DOES attach the world; with neutral world off nothing starts early.
- [ ] The log shows the pack's "world view built from the session" line (or its start) before `World` finishes loading, with timestamps in the stage log.
- [ ] Measured time-to-cell with `tools/neutral_session_check/check.sh` (five Skyrim SE masters, `WhiterunBanneredMare`): at least 1.0 s better than the N3a-3b figure of 5.0 to 5.1 s (target 4.0 s or less), three runs each side; the `a query waited N ms` line below 0.5 s. A smaller gain is explained in the log; the stage lands only without regression.
- [ ] Same result as before: 694 of 697 objects inserted, player on the floor, 400 frames, exit 0, no dump.
- [ ] With `[Game] neutral world` off, Morrowind is unchanged: `tools/worldobjects_check` dump (110 lines) byte-identical to the parent executable, and a fixed-time Balmora frame (`OPENEDEN_FIXED_FRAME_TIME`, `--random-seed 7`, frame 600) has the same SHA-256 as the parent's, setting off and on.
- [ ] A pack fault during the early start still ends the session once, refuses saving (task 0084 I2 path) and is shown.
- [ ] `CI/check_layer_rules.sh` exceptions stay 104; `openmw-tests` and the extension host tests pass; GCC `-Werror` and clang-format 14 before a push (say so if Docker is still hung).

Out of scope: retiring the engine's own ESM4 load (N7), a different indexing thread model, any change to the index algorithm or `WorldSourceService`, the save format, Eden Studio.

## Open questions for toddhow

1. Is starting the game pack at the end of `prepareEngine` (before the loading screen and the `World`) acceptable, or must pack start stay in `Game::Game` (then option B, an additive SDK change)?
2. Early start only with `[Game] neutral world` on (recommended, so default play is untouched), or for every game pack?


## Stage log (2026-11-11, option A built; same hand wrote tests and code, no fresh reviewer; Windows only; local commit on branch `n3a3c`, not pushed)

Status: BUILT on toddhow's standing approval of the recommendations (option A, only with `[Game] neutral world` on).

RECALLED-VERIFY items checked in code first:
- `SkyrimGamePack::start` touches no physics and no world: it uses the logger, the world source service, the settings service and the VFS service only (`skyrimse_game_pack.cpp`, `loadSessionContent`). True.
- `VFS::Manager` is safe for concurrent readers once `buildIndex()` has run (`components/vfs/manager.hpp`, "Thread-safety contract"); `registerArchives` has finished before `setSessionContent`. True.
- `HostImpl::attachPhysicsWorld` only does `mPhysicsService.emplace(...)`, so attaching after the pack has started is safe. True (nothing else reads the physics service in this pack).
- `takeSessionFault` is polled from `Game::run`, so an early pack fault is held until the Game exists, then shown once. True; a new test pins it.
- `beginRunning()` installs the log bridge earlier; engine lines before the Game are forwarded to extension sinks (harmless, only with the setting on).
None made option A unsafe, so option B was not needed.

Change: `ExtensionHost::startGamePackEarly()` (no-op unless `Config::neutralWorld`) called from `Engine::prepareEngine` right after `setSessionContent`; `ExtensionHost::planGamePackStart` is the pure decision behind `startGamePack` (Start, AttachWorldOnly, Nothing); `Game::Game`'s existing `startGamePack(physics)` now attaches the physics world explicitly when the pack already started (and returns false). The later `attachWorld` stays idempotent. No SDK, pack, CMake or `mwphysics` change.

Tests (written first, red against stubs): 4 of the 8 new tests failed against the stub (`GamePackStartPlan.aFreshHostWithAPackStarts`, `.aPackStartedWithoutPhysicsIsGivenTheWorldLater`, `BuiltInPackTest.theEarlyStartWithNeutralWorldOnStartsThePackOnceAndTheGamesFirstPhaseDoesNotRepeatIt`, `.aPackThatFailsInTheEarlyStartIsTheSessionErrorOnceAndInstallsNothing`); the other four pin absence (setting off, no pack, no physics, after start()) and passed at once. After: `openmw-tests` 1,955 pass. The AttachWorldOnly branch's real call (`attachWorldNow` with a PhysicsSystem) is covered only by the pure plan test and the game run, not a unit test (a PhysicsSystem is not buildable in a unit test).

Measured (`tools/neutral_session_check/check.sh`, five Skyrim SE masters, `WhiterunBanneredMare`, 400 frames, quiet window; time-to-cell = first log line to "Neutral cell inserted"; parent = this branch's base built unchanged):

| run | parent time-to-cell | parent first-query wait | new time-to-cell | new wait |
| --- | --- | --- | --- | --- |
| 1 | 8,617 ms | 1,542 ms | 5,188 ms | none logged |
| 2 | 7,003 ms | 2,042 ms | 5,361 ms | none logged |
| 3 | 7,964 ms | 2,583 ms | 4,941 ms | none logged |

Mean 7.86 s to 5.16 s (-2.7 s). The pack start line now comes before the engine's content load (0.5 s before `Loading content file Skyrim.esm`), and the index finishes during it. The absolute figures are higher than the N3a-3b log's 5.0 to 5.1 s (shared machine, a fresh build), so compare the pairs, not the old figures. Same result: 694 objects inserted, exit 0, no dump, 0 unexpected error lines, in all six runs.

Morrowind unchanged: `tools/worldobjects_check` (110 result lines) byte-identical to the parent executable with `[Game] neutral world` off and on (`OK` both). Fixed-time Balmora frame (`OPENEDEN_FIXED_FRAME_TIME`, `--random-seed 7`, frame 600): SHA-256 prefix `1c5d47ad0b5dc32d` for new and parent, setting off and on. (That config has no `game=` line, so with the setting on there is no game pack to start early; the early path was exercised only by the Skyrim runs and the unit tests.)

Not run: the Linux gate (Docker hung; GCC traps checked by hand: braces on all ifs, no new gtest-in-if, no unused functions), clang-format 14 (not installed here), clang-tidy, the Lua tests, `components-tests`, a fresh review, Morrowind with `game=morrowind` plus the setting on (would start the Morrowind pack early), a save/load in a Skyrim session.
