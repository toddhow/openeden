# Task 0018: Track W stage W4 — Real clients, packaging and the launcher

**Status:** draft, awaiting toddhow's approval. Depends on task 0017 (W3).
**Design doc section:** `docs/design/working-plugin-system.md` (W4, decision 5); `docs/design/openeden-plugin-system.md` sections 5 (packaging), 6.1 (discovery and enable list), 6.8 (failure reporting), 7.2 (plugin build), 8 (testing), 9 (licence and trust).
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
3. **Sample plugin** `plugins/example/` (name proposed): a small, documented plugin using every service: a data file through the VFS, a setting, a save chunk, an event forwarded to Lua (and a Lua script in its data that reacts to it), ray and shape casts against the game world, entity lifecycle and physics events, a game event, debug drawing, a profiler scope. It shows what plugins may and may not do under decision 0003 (no physics changes, no Lua API). It is off by default, built in CI, and doubles as the template referenced by `docs/design/plugin-implementation-guide.md` (update that guide to match the working system).
4. **Launcher:** a "Plugins" page (or a section of the data-files page) listing discovered plugins with name, version, licence, description and enabled state; toggling writes `plugin=` lines (task 0015's enable list); plugins whose manifests fail validation are shown with the reason and cannot be enabled; a one-line trust notice. Strings translated (regenerate `.ts` files).
5. **Failure reporting in game:** when a plugin fails to load or is disabled at runtime, the player sees one message box at the main menu naming the plugin and the reason (plugin doc 6.8), besides the log.
6. **Plugin settings in the launcher:** typed settings from manifests (bool, number with range, enum, string; plugin doc 4.8) drawn without plugin code (proposed; can move to a follow-up if large).
7. **End-to-end test** (plugin doc section 8 in the real game): a Lua integration test with the sample plugin enabled that exercises every service through the running game, including a save and reload.

## Constraints

- Zero plugins enabled (the compatibility pack aside, which changes no behaviour): the game is unchanged, as in tasks 0015–0017.
- The launcher keeps every existing function; translation check passes.
- First-party plugin licences are GPL-3.0-or-later and recorded in their manifests.

## Acceptance criteria

- [ ] An installed build contains `plugins/compat_morrowind/` and `plugins/example/` laid out as plugin doc 5.1, on Windows and Linux.
- [ ] The compatibility pack loads in the game by default for Morrowind and its converter is callable through the registry (test); disabling it changes nothing in Morrowind's behaviour.
- [ ] The sample plugin, enabled, passes an end-to-end Lua integration test covering every service, including save and reload.
- [ ] The launcher lists plugins with their details, enables and disables them by writing `plugin=` lines, shows invalid manifests with reasons, and shows the trust notice; `CI/check_qt_translations.sh` passes.
- [ ] A plugin that fails to load produces one in-game message at the main menu and a log entry.
- [ ] `docs/design/plugin-implementation-guide.md` matches the working system and points to the sample plugin.
- [ ] All existing gates pass.

## Out of scope

- Moving Morrowind-specific rules out of the core into the compatibility pack (the game contract consumers): its own track after this one.
- A stable C ABI and community SDK distribution (plugin phase P6).
- Hot reload; Eden Studio's use of plugins.

## Open questions for toddhow

1. **Compatibility pack on by default** for Morrowind installs (proposed), or off until the engine consumes `openeden.game`?
2. **Launcher placement:** a new "Plugins" page (proposed) or a section of the data-files page?
3. **Plugin settings in the launcher:** in this task (proposed) or a follow-up?
