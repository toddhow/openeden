# Launcher (apps/launcher)

Qt 6 front end of OpenEden. Inherited from OpenMW; the pages below are new (tasks 0057, 0058, 0018). Root `CLAUDE.md` rules apply.

## Pages

| Page | Class | Logic lives in |
| --- | --- | --- |
| Data Files, Graphics, Settings, Import | inherited | `components/config`, `components/contentselector` |
| Extensions | `ExtensionsPage` | `components/extension` |
| Mods | `ModsPage` | `components/modorganizer` (`profilestore.*` is the adapter) |
| World LOD | `WorldLodPage` | `components/worldlod` (`lodsettings.*`, `bakestatus.*`) |

## Rules for these pages

- **A page is a thin view.** A rule that can be tested without Qt goes in a library with GoogleTest tests written first (the libraries are in `components_tests`); a page only reads them and shows the result. The Mods page owns no state except a `ModOrganizer::Store`.
- **No scan or file read on the GUI thread.** Use `startBackgroundJob` (`utils/backgroundjob.hpp`): the work gets copies, the result comes back queued. Disable the page while a job that changes the store runs. A page waits for its threads in its destructor.
- **Never write inside a mod, the game data or the global `openmw.cfg`.** The page writes its own JSON, the user `openmw.cfg` (through `applyActiveProfile`: owned keys only, backup, refusal while the game runs, hand-edit detection) and, for the World LOD page, only files named by `WorldLod::listCacheFiles`.
- **"Is the game running" is a process name check** (`ModOrganizer::isProcessRunning("openmw")`), injected as `setGameRunningCheck` so tests do not depend on other programs. It also fires for a game another session started.
- **Only start commands that exist.** The World LOD page runs `openmw --bake-lod-terrain` and `--bake-lod-objects` and nothing else; `WorldLod::bakeArguments` is pinned by a test. Pause and Resume stay disabled until a baker reads a command file.
- **Every user string goes through `tr()` as a literal** (a table of labels is built with `tr()` calls so that `lupdate` finds them). After a change regenerate the launcher files the way CI does, with Qt's `lupdate` (it is in `deps/Qt/6.6.3/msvc2019_64/bin`): `lupdate -locations none apps/launcher -ts files/lang/launcher_*.ts` from the repository root, then check `git diff` shows only `files/lang/launcher_*.ts`. Restore any `wizard_` or `components_` file that only changed line endings.
- **Status is never colour only**: every state has a text; tables and fields have accessible names; everything is reachable by keyboard.

## Looking at the pages without the real launcher

The launcher reads the real `Documents/My Games/OpenMW` folder, so it is never started for a check. Build `openeden-launcher-pagegrab` (CMake option `OPENEDEN_BUILD_LAUNCHER_PAGEGRAB`, `tools/launcher_check/pagegrab.cpp`): it makes generated mods and plugin headers below the work folder you give it (the path must contain `OpenEden-work`), builds a `Files::ConfigurationManager` that points there (and stops before touching anything if it does not), draws the Mods and World LOD pages with Qt's offscreen platform and saves PNGs. It needs:

- `QT_QPA_PLATFORM=offscreen` and `QT_QPA_FONTDIR=C:\Windows\Fonts` (the offscreen platform has no fonts otherwise, and every label is a box);
- a local `openmw.cfg` beside the executable (copy the build's), or the configuration manager ignores `--config` and falls back to the real folder;
- `openmw.exe` of `tools/launcher_check/fakebake.cpp` (built into the same `pagegrab` folder by the same option) to see the bake states.

## Pitfalls

- The `.ui` and many launcher sources have CRLF endings in a Windows checkout; edit with a tool that keeps them.
- `QProcess::finished` is not emitted when the program fails to start; the page listens to `errorOccurred`.
- `Settings::Manager::getString` throws for a key whose default is missing (a build without the default file); the World LOD page catches it and shows the table's default.
- Reloading the settings (`MainDialog::reloadSettings`) rebuilds every page's state from disk, so a page saves before it asks for one (the Mods page does in `applyProfile`) and keeps its last message across it.
- **The game choice (task 0173):** the Game box at the bottom of the main window lists Morrowind (built in, first) and every installed pack of `kind: "game"` (the four folders the game searches, `GameSession::searchDirectories`). Selecting one makes that game's last profile active and applies it, which writes `game=<id>` as the first owned line of `openmw.cfg` (Morrowind writes none, so its config is byte-identical to before); a game with no profile yet asks for its Data folder and builds one from the plugins whose header is that game's (`ModOrganizer::buildFirstGameProfile`, on a worker thread). Profiles carry a `game` field (written only when not empty); the Mods page lists only the active game's profiles; other games' base data are in `base-data-<game>.txt`, the last profile of each game in `last-profiles.txt` (neither is written for a Morrowind-only store). "Launch Eden Studio" starts `eden-studio` as Play starts `openmw`; both get the launcher's own `--config`, `--replace`, `--user-data` and `--resources`, which the launcher now also uses itself.
- The first-profile probe is the plugin header's game guess (`Content::Header::mGame`, `expectedHeaderGame`), not the pack's own content loader: loading a pack in the launcher would mean starting it.
