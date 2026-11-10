# The game: `apps/openmw`

Rules and pitfalls for new OpenEden code here. Rendering has its own guide, `mwrender/CLAUDE.md`.

## The extension host (`extensionhost.{hpp,cpp}`, task 0015)

`OMW::Engine` owns one `ExtensionHost`, which holds the `HostImpl` and `LifecycleManager` from `components/extension`.

- **Startup:** `Engine::prepareEngine` creates it before the VFS, which finds and loads the enabled extensions' libraries. The `Game` state's constructor gives it the world and calls `start()`, which runs register, resolve and start: extensions keep the services they get there, and the world exists only by then. So no phases run on the first loading screen.
- **Two start phases with a game pack (task 0084, stage I1b):** with a game pack chosen by `game=`, `Game::Game` first calls `startGamePack()`, which attaches the world, runs register and resolve for everything, starts the game pack and the extensions it requires, and installs the pack's `GameRules` into the core, all BEFORE the managers and windows are built (the crafting windows begin their sessions in `initUI`). `start()` at the end of the constructor starts the rest. With no game pack `startGamePack()` does nothing and `start()` is the single start it always was. The executable's built-in packs (`Arguments::builtInGamePacks`, today `morrowind`) are used by `game=` when no pack of that id is installed on disk.
- **Where extensions come from:** `extensions/` in the local path (beside the executable on Windows), then `extensions/` in the user data folder. Each extension is a folder with an `extension.json`.
- **Enable list:** `extension=<id>` lines in `openmw.cfg`, plus `--extension=<id>` for one run. `--no-extensions` loads none. Nothing loads unless listed; an installed extension that is not listed is only named in the log. The shipped `openmw.cfg` enables `compat_morrowind`.
- **Game packs (task 0084, decision 0013):** a pack of manifest kind `game` loads only if `game=<id>` (`openmw.cfg`, `--game`) names it; any other enabled game pack is refused with a message, and with no `game=` line none loads. Game packs are searched in `game_packs/` as well as `extensions/`. The pack's id and version go into every save header (`ESM::SavedGame::mGamePackId`, `mGamePackVersion`, written only when set); loading a save of another game or of a newer pack version is refused (`LoadGame::verifyProfile`). A game pack that fails sets `ExtensionHost::sessionError()` and saving is refused (`StateManager::saveGame`). The session ends at the next frame (`ExtensionHost::takeSessionFault` from `Game::run`), and a new game or load is refused with the same message (`StateManager::refuseWhileGamePackFailed`). Its callbacks run before the other kinds' in every phase.
- **Failures** show one message box at the main menu, from `Engine::frame`: the `Game` state does not run while a menu is on top of it.
- **Frame phases:**

| Phase | Called from | Runs when |
| --- | --- | --- |
| `Input` | `Engine::frame`, after the input update and the hidden-window check | Every frame the window is visible |
| `GameSimulation` | `Game::run`, just before `PreSimulation` | In a game, unpaused; game packs only (SDK 4.0) |
| `PreSimulation` | `Game::run`, before the mechanics update | In a game, unpaused |
| `PostSimulation` | `Game::run`, after the world update | In a game, unpaused |
| `Animation` | `Game::run`, straight after `PostSimulation`; the mechanics update has already advanced animations | In a game, unpaused |
| `GameUpdateUi` | `Game::run`, just before the window manager's update | In a game, paused or not; game packs only (SDK 4.0) |
| `PreRender` | `Engine::frame`, before the window manager's frame | Every frame that completes |
| `Ui` | `Engine::frame`, after the window manager's frame | Every frame that completes |

  A hidden window runs no phases. A frame cut short by a game state change runs only the phases it reached. Menus and loading screens have only `Input`, `PreRender` and `Ui`. `Ui` also does the end-of-frame reset of scratch memory, debug draw and the profiler, and runs before `MWRender::RenderManager::onFrame`, so the renderer must take its debug-draw snapshot before `Ui` (task 0017). The simulation phases receive simulation time, the others real frame time.
- **Shutdown:** `~Game` stops extensions before the World goes, and `Engine::go` and `~Engine` call `stop()` too, for any path that did not reach `~Game`; it is safe to repeat. Stop, destroy and unload all happen in that one call. Before unloading, `LifecycleManager` drops everything extensions left in the services, through `HostImpl::releaseExtensionObjects`: an extension whose `start` threw never ran `stop`. A new service that keeps extension objects must join that function.
- **Logging:** extension lines go to the engine log with their `[extensionId]` prefix. Engine lines reach extension sinks through `Debug::setExtraLogListener`, so the in-game log viewer keeps `setLogListener`. An extension's own line reaches its sink once. A line a sink logs while receiving any line is dropped: it would call itself without end, and for an engine line the engine holds its log lock.
- **Data (task 0016):** the VFS gets extension `data/` folders above `data=` and below `data-local`, in load order. They are data folders in full, so an extension can carry content files, though only `content=` lines load them. Settings live in `[extension.<id>]` in `settings.cfg`.
- **Saves:** extensions have no part in them (decision 0005). They only hear `GameLoadedEvent` and `GameSavedEvent`. `SaveGame` and `LoadGame` run on a worker while frames continue, so extension phases keep running during a save.
- **Lua:** marked events go to global scripts as `openeden_<extensionId>_<eventName>`, with a table of the flat payload, in a batch of their own that is not saved (`LuaEvents::addExtensionEvent`). They wait until a game is running, since global scripts need one; publish them on the main thread.
- **World (task 0017):** `OMW::ExtensionWorld`, made only with extensions loaded. Engine code tells it what happens through `MWBase::ExtensionManager`, from any thread: cells load on a worker, contacts come from physics threads. It queues under a lock, and `ExtensionHost::deliverWorldEvents` delivers on the main thread just before `Input`, so extension code never runs elsewhere. It maps objects to entity IDs by the address of their reference, and is the units boundary for event positions.
- **Zero extensions:** after startup's discovery, which still lists what is installed, every call returns at once and no listener is installed.

## Body analysis (task 0069)

`openmw --analyse-bodies [--bake-gi-out DIR]` (`bodyanalyser.*`, over `components/bodynif`) measures the stock body parts of the
loaded data and writes `<DIR or user data>/analysis/bodies.json` and `bodies.txt`, then quits. It is read-only, runs
on the main thread one race and sex per frame, and keeps the plain view distance like the bakes (`sLodBakeRun`). The
report names the user's own records: never commit it. Results: research note 0033.

## Checking it

`openmw-tests` runs `ExtensionHost` against real test extension libraries (`apps/openmw_tests/extension/`), but no unit test runs the engine loop. To check the phase calls, install the test extension, run the game with `OPENEDEN_TEST_EXTENSION_LOG=<file>` and `--extension=<id> --skip-menu --auto-quit 100`, then run `tools/check_extension_phases.sh <file>`. For data, settings, saves and Lua events, run `tools/extension_data_check/check.sh`; for the world, `tools/extension_world_check/check.sh`; for the sample extension, `tools/extension_example_check/check.sh`. Each needs an empty work folder, and no test extension left in the build folder's `extensions/`.

## Pitfalls

- **Plain `std::cout` and `std::cerr` lose their first character in `openmw.log`,** and each `<<` starts a new prefix ("uitting peacefully"). Use `Log(Debug::...)` in new code.
- **Running the Lua integration tests by hand:** use only the test's own config directory (`--replace=config --config=<dir>`). Adding the build folder's `openmw.cfg` also loads `morrowind.esm` and the intro movie, and the tests fail.

- **Test games and the editor open off-screen.** With `OPENEDEN_QUIET_WINDOW` set to anything but empty or `0`, `vsgengine.cpp` creates the window at (-30000, -30000), out of the taskbar and without taking focus; it is not minimised (a minimised window has a 0x0 swapchain and draws nothing), and frames and screenshots are unchanged. Windowed mode only. Eden Studio honours the same variable for every window it opens (`apps/opencs/view/doc/quietwindows.*`, see `apps/opencs/CLAUDE.md`, "Quiet windows"). A crash or a fatal exception shows no message box either: the crash catcher writes `openmw-crash.dmp` as before and the game exits with code 1, and a freeze gets no box: the monitor writes `openmw-freeze.dmp` and ends the game with exit code 0xDEAD, as the box's Abort button would (root `CLAUDE.md`, "A crashed game shows a Fatal Error box"). The rule lives in one place, `Misc::isQuietWindowRequested` (`components/misc/quietwindow.*`). The `tools/` check scripts, the editor's included, set it by default (`OPENEDEN_QUIET_WINDOW=0` shows the window); set it in any new script or manual run that starts the game or the editor. Windows has no supported call to put another program's window on a chosen virtual desktop, so this is the substitute.
