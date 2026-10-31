# The game: `apps/openmw`

Rules and pitfalls for new OpenEden code here. Rendering has its own guide, `mwrender/CLAUDE.md`.

## The plugin host (`pluginhost.{hpp,cpp}`, task 0015)

`OMW::Engine` owns one `PluginHost`, which holds the `HostImpl` and `LifecycleManager` from `components/plugin`.

- **Startup:** `Engine::prepareEngine` creates it before the VFS, which finds and loads the enabled plugins' libraries, and calls `start()` once the state manager exists, which runs register, resolve and start.
- **Where plugins come from:** `plugins/` in the local path (beside the executable on Windows), then `plugins/` in the user data folder. Each plugin is a folder with a `plugin.json`.
- **Enable list:** `plugin=<id>` lines in `openmw.cfg`, plus `--plugin=<id>` for one run. `--no-plugins` loads none. Nothing loads unless listed; an installed plugin that is not listed is only named in the log.
- **Frame phases:**

| Phase | Called from | Runs when |
| --- | --- | --- |
| `Input` | `Engine::frame`, after the input update and the hidden-window check | Every frame the window is visible |
| `PreSimulation` | `Game::run`, before the mechanics update | In a game, unpaused |
| `PostSimulation` | `Game::run`, after the world update | In a game, unpaused |
| `Animation` | `Game::run`, straight after `PostSimulation`; the mechanics update has already advanced animations | In a game, unpaused |
| `PreRender` | `Engine::frame`, before the window manager's frame | Every frame that completes |
| `Ui` | `Engine::frame`, after the window manager's frame | Every frame that completes |

  A hidden window runs no phases. A frame cut short by a game state change runs only the phases it reached. Menus and loading screens have only `Input`, `PreRender` and `Ui`. `Ui` also does the end-of-frame reset of scratch memory, debug draw and the profiler, and runs before `MWRender::RenderManager::onFrame`, so the renderer must take its debug-draw snapshot before `Ui` (task 0017). The simulation phases receive simulation time, the others real frame time.
- **Shutdown:** `~Game` stops plugins before the World goes, and `Engine::go` and `~Engine` call `stop()` too, for any path that did not reach `~Game`; it is safe to repeat. Stop, destroy and unload all happen in that one call. Before unloading, `LifecycleManager` drops everything plugins left in the services, through `HostImpl::releasePluginObjects`: a plugin whose `start` threw never ran `stop`. A new service that keeps plugin objects must join that function.
- **Logging:** plugin lines go to the engine log with their `[pluginId]` prefix. Engine lines reach plugin sinks through `Debug::setExtraLogListener`, so the in-game log viewer keeps `setLogListener`. A plugin's own line reaches its sink once. A line a sink logs while receiving any line is dropped: it would call itself without end, and for an engine line the engine holds its log lock.
- **Zero plugins:** after startup's discovery, which still lists what is installed, every call returns at once and no listener is installed.

## Checking it

`openmw-tests` runs `PluginHost` against real test plugin libraries (`apps/openmw_tests/plugin/`), but no unit test runs the engine loop. To check the phase calls, install the test plugin, run the game with `OPENEDEN_TEST_PLUGIN_LOG=<file>` and `--plugin=<id> --skip-menu --auto-quit 100`, then run `tools/check_plugin_phases.sh <file>`.

## Pitfalls

- **Plain `std::cout` and `std::cerr` lose their first character in `openmw.log`,** and each `<<` starts a new prefix ("uitting peacefully"). Use `Log(Debug::...)` in new code.
- **Running the Lua integration tests by hand:** use only the test's own config directory (`--replace=config --config=<dir>`). Adding the build folder's `openmw.cfg` also loads `morrowind.esm` and the intro movie, and the tests fail.
