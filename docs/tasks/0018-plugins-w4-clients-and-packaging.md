# Task 0018: Track W stage W4 — Real clients, packaging and the launcher

**Status:** (Decision 0005 later took save chunks out of the sample plugin, the design's item 3 and its end-to-end test.) Implemented on Windows, except plugin settings in the launcher (see Notes), awaiting toddhow's review. Depends on task 0017 (W3).
**Design doc section:** `docs/design/working-extension-system.md` (W4, decision 5); `docs/design/openeden-extension-system.md` sections 5 (packaging), 6.1 (discovery and enable list), 6.8 (failure reporting), 7.2 (plugin build), 8 (testing), 9 (licence and trust).
**Goal:** plugins are real for players and authors: OpenEden installs with its first-party plugins in place, the launcher lists them and turns them on and off, failures are shown where the player sees them, the Morrowind compatibility pack runs in the game, and a sample plugin proves every service end to end as a template for authors.
**Workflow:** full loop.

---

## Context (facts this spec relies on)

- **Plugin folder layout** (plugin doc 5.1): `plugins/<id>/` with `plugin.json`, the library (`.dll` on Windows, `lib<id>.so` on Linux), `data/`, `LICENSE`.
- **`openeden_add_plugin()`** (`cmake/OpenEdenPlugin.cmake`) builds a plugin library with `OPENEDEN_BUILDING_PLUGIN`; `plugins/CMakeLists.txt` adds `compat_morrowind`.
- **The Morrowind compatibility pack** (`plugins/compat_morrowind/`) provides `openeden.game` (`sdk/include/openeden/contracts/game_contract.hpp`) and a `.kf` converter contract (`kf_converter_contract.hpp`); Track V's task 0011 removes its renderer dependency. Nothing in the engine consumes `openeden.game` yet.
- **The launcher** (`apps/launcher`, Qt 6) writes `openmw.cfg` (data folders, `content=` lines) and has a data-files page; its strings are translated (`files/lang/launcher_*.ts`, checked by `CI/check_qt_translations.sh`).
- **Install:** `cmake --install` lays out the game (see the guide's Pitfalls: never install inside the build directory; Windows needs runtime DLLs deployed).
- **Licence and trust** (plugin doc 9): first-party plugins are GPL-3.0-or-later; the manifest's `licence` field is recorded; native plugins run with the game's rights, so the launcher says to install plugins only from trusted sources.

## Design

1. **Install layout:** `cmake --install` places each first-party plugin in `<install>/plugins/<id>/` with its manifest, library, data and licence; the game's default search path (task 0015) finds them.
2. **Morrowind compatibility pack in the game:** enabled by default for Morrowind installs (proposed: the only plugin on by default, because it carries no behaviour change until the engine consumes `openeden.game`); it loads, resolves and starts in the game; its `.kf` converter is callable through the registry (an integration test converts a synthetic `.kf` through the running game's registry). Consuming `openeden.game` from the engine (moving Morrowind rules out of the core, plugin doc phase 5) is a separate track, not this task.
3. **Sample plugin** `plugins/example/` (name proposed): a small, documented plugin using every service: a data file through the VFS, a setting, a save chunk, an event forwarded to Lua (and a Lua script in its data that reacts to it), ray and shape casts against the game world, entity lifecycle and physics events, a game event, debug drawing, a profiler scope. It shows what plugins may and may not do under decision 0003 (no physics changes, no Lua API). It is off by default, built in CI, and doubles as the template referenced by `docs/design/extension-implementation-guide.md` (update that guide to match the working system).
4. **Launcher:** a "Plugins" page (or a section of the data-files page) listing discovered plugins with name, version, licence, description and enabled state; toggling writes `plugin=` lines (task 0015's enable list); plugins whose manifests fail validation are shown with the reason and cannot be enabled; a one-line trust notice. Strings translated (regenerate `.ts` files).
5. **Failure reporting in game:** when a plugin fails to load or is disabled at runtime, the player sees one message box at the main menu naming the plugin and the reason (plugin doc 6.8), besides the log.
6. **Plugin settings in the launcher:** typed settings from manifests (bool, number with range, enum, string; plugin doc 4.8) drawn without plugin code (proposed; can move to a follow-up if large).
7. **End-to-end test** (plugin doc section 8 in the real game): a Lua integration test with the sample plugin enabled that exercises every service through the running game, including a save and reload.

## Constraints

- Zero plugins enabled (the compatibility pack aside, which changes no behaviour): the game is unchanged, as in tasks 0015–0017.
- The launcher keeps every existing function; translation check passes.
- First-party plugin licences are GPL-3.0-or-later and recorded in their manifests.

## Acceptance criteria

- [ ] An installed build contains `plugins/compat_morrowind/` and `plugins/example/` laid out as plugin doc 5.1, on Windows and Linux. Windows checked; Linux not run, and see the Notes on where the Linux game looks.
- [x] The compatibility pack loads in the game by default for Morrowind and its converter is callable through the registry (test); disabling it changes nothing in Morrowind's behaviour. Compared with the 32 Lua tests with and without it, on the example suite; not played on Morrowind's own data.
- [x] The sample plugin, enabled, passes an end-to-end Lua integration test covering every service, including save and reload (`tools/plugin_example_check/check.sh`, a shell check as CI's runner needs a plugin it cannot yet install; debug draw is submitted, not shown).
- [ ] The launcher lists plugins with their details, enables and disables them by writing `plugin=` lines, shows invalid manifests with reasons, and shows the trust notice; `CI/check_qt_translations.sh` passes. Built and translations regenerated with `lupdate`, but the page was never opened or clicked, so this stays unticked until someone does.
- [x] A plugin that fails to load produces one in-game message at the main menu and a log entry.
- [x] `docs/design/extension-implementation-guide.md` matches the working system and points to the sample plugin.
- [ ] All existing gates pass. Done on Windows: build, `components-tests`, `openmw-tests`, the 32 Lua tests, every real-game check. Not run: Linux `-Werror`, clang-format 14, clang-tidy, sanitizers, `openmw-cs-tests`.

## Out of scope

- Moving Morrowind-specific rules out of the core into the compatibility pack (the game contract consumers): its own track after this one.
- A stable C ABI and community SDK distribution (plugin phase P6).
- Hot reload; Eden Studio's use of plugins.

## Decisions taken while implementing, for toddhow to confirm

1. **Compatibility pack on by default,** as proposed: `plugin=compat_morrowind` in `files/openmw.cfg` and `.local`. That is every install, not only Morrowind ones, because nothing at that point knows which game is being played; the pack changes nothing until the engine uses it.
2. **Launcher placement:** a new Plugins page, as proposed.
3. **Plugin settings in the launcher: a follow-up,** which the design allowed. Manifests have no range or enum fields to draw them from yet.
4. **Turning off the default:** `openmw.cfg` lines add up across files, so the launcher writes `replace=plugin` once the player changes anything. Until then it writes nothing.
5. **Plugin data folders are data folders in full,** also for content files, so that the example can carry its `.omwscripts`. This changes task 0016's decision. A `content=` line for a file in a plugin's folder stops the game starting if that plugin is turned off.
6. **The failure message is a plain message box,** not one that must be clicked, and appears on the main menu; each failure once. A broken folder is reported only if it was enabled.
7. **Plugin library names no longer change with the build type,** since the manifest names them.

## Notes for toddhow

- **The launcher page has never been opened.** It builds; the listing logic is tested in `listInstalledPlugins`. Writing `plugin=` lines through `GameSettings` is untested. Please look at it once.
- **Linux install:** plugins install to `plugins/<id>` under the prefix, and the game looks in its local path and user data folder. On Linux the local path may not be that prefix, so an installed game may not find them there. Untested; decide with the Linux packaging.
- **No `description`** in the launcher's columns: manifests have no such field. It shows name, version, kind, licence and any problem.
- **The compatibility pack links `components`,** as it did before, which `plugins/compat_morrowind/CLAUDE.md` allows: its DLL carries its own copy.
- **A full `cmake --install` needs `BUILD_OPENCS`:** it copies `defaults.bin`, which only that builds. The check here built without it, so the install stopped there, after installing the plugins. No test plugin was in it.
- **The plugin-example check is not in CI,** and neither are the other real-game checks: they need game runs and a Windows or Linux desktop or xvfb. Whichever CI OpenEden keeps should run them.
- **`tools/plugin_example_check` accepts `freed=0`,** since its small scene despawns little; freeing on despawn is checked by `tools/plugin_world_check`.
- **Flaky tests:** two tests in `DetourNavigatorRecastMeshObjectTest` have now failed intermittently in full runs on this branch (`update_for_compound_shape_with_same_transform_and_changed_child_transform_should_return_true`, `update_with_different_transform_should_return_true`). They pass alone. The fixture's cause is unknown; worth a look on a clean `vulkan` build.
