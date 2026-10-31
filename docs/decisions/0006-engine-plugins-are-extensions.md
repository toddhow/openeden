# 0006 — Engine plugins are called extensions

Date: 2026-10-31 · Decided by: toddhow

## Decision
- **OpenEden's native code system is called extensions,** everywhere: code, files, config, the launcher and docs.
- **"Plugin" is left to its Morrowind meaning:** an ESP or ESM file. Inherited code that uses the word that way, and the Qt and OpenSceneGraph plugins, stay as they are.

## Why
- **Players already know "plugin" as a data file,** and OpenEden is considering a new ESP-like format of its own. Two things with one name would confuse everyone. Nothing about that format is decided here.
- **The rename is cheapest now,** before Track W is merged and before anyone outside this project has built one.

## What changed
| Before | After |
| --- | --- |
| `plugins/<id>/`, `plugin.json`, format `openeden.plugin` | `extensions/<id>/`, `extension.json`, format `openeden.extension` |
| `plugin=<id>`, `--plugin`, `--no-plugins` | `extension=<id>`, `--extension`, `--no-extensions` |
| `[plugin.<id>]` in `settings.cfg` | `[extension.<id>]` |
| `components/plugin`, `openeden::plugin`, `openeden::sdk::Plugin` | `components/extension`, `openeden::extension`, `openeden::sdk::Extension` |
| `openeden_plugin_entry`, `openeden_add_plugin` | `openeden_extension_entry`, `openeden_add_extension` |
| `OMW::PluginHost`, `OMW::PluginWorld`, `MWBase::PluginManager` | `OMW::ExtensionHost`, `OMW::ExtensionWorld`, `MWBase::ExtensionManager` |
| Lua events `openeden_<pluginId>_<eventName>` | The same pattern; only test extensions' IDs changed |

Eden Studio's editor libraries were called "editor extensions"; they are now **editor modules**, and the SDK's editor extension points are **editor hooks**. `registerFormatReader`'s parameter is now `fileExtension`, because the word now has two meanings there.

## Consequences
- **History keeps its words:** decisions 0001 to 0005, task specs and changelog rows before this one say "plugin", and their file names are unchanged. Read it as "extension" wherever they mean this system. Links in them to renamed files are updated.
- **No migration:** nothing was released, so old `plugin=` lines and `[plugin.<id>]` settings are not read. Remove them from a local `openmw.cfg` by hand.
- **SDK stays 2.0,** as in decision 0005: no extension was built outside this branch. Every extension must be rebuilt, because the entry point's name changed.
- **Inherited `extensions`:** mwscript also has "extensions" (`apps/openmw/mwscript/extensions.hpp`), in its own namespace. It is unrelated.
