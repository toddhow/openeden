# Task 0016: Track W stage W2 — Data services on the engine

**Status:** implemented, awaiting toddhow's review. **The save part was removed by decision 0005 (plugins do not affect save data):** the design's item 3, its save criterion and constraint, and the notes on saves below describe what was built and then taken out. Depends on task 0015 (W1).
**Design doc section:** `docs/design/working-extension-system.md` (W2, decision 4); `docs/design/openeden-extension-system.md` sections 4.7 (VFS and file formats), 4.8 (settings), 4.9 (save games), 4.10 (Lua).
**Goal:** a plugin's data folder, settings and save data work through the engine's real subsystems, and the events it marks reach Lua scripts: its files are in the game's virtual file system in load order, its settings live in `settings.cfg`, its save chunks travel inside `.omwsave` files, and mods can react to its forwarded events (decision 0003: plugins have no Lua API of their own).
**Depends also on:** task 0019 (SDK 2.0: event marking for Lua forwarding).
**Workflow:** full loop.

---

## Context (facts this spec relies on)

Cite every engine function again at the commit this task starts from (after Track V's task 0009, startup and saves are vsgopenmw's code).

- **VFS:** `OMW::Engine::prepareEngine` creates `VFS::Manager` and calls `VFS::registerArchives(mVFS.get(), mFileCollections, mArchives, true, …)` (`apps/openmw/engine.cpp`, lines 737–739, OSG engine; vsgopenmw `vsgengine.cpp` lines 298–304). Data directories come from `openmw.cfg` (`data=`), later ones overriding earlier ones.
- **`VfsService`** (`components/plugin/vfs_service.{hpp,cpp}`): mounts per plugin sorted by load order, format readers by extension (including multi-dot extensions) with located errors; today its own table.
- **Settings:** `Settings::Manager::load(cfgMgr)` (`apps/openmw/main.cpp:71`) reads `settings-default.cfg` and the user's `settings.cfg`; `Settings::Manager::saveUser` writes it back (`engine.cpp:1075`). **`SettingsService`** (`settings_service.{hpp,cpp}`) keeps per-plugin namespaces with typed defaults from the manifest (`plugin.json` `settings`: bool, number with range, enum, string; plugin doc 5.2), off by default.
- **Saves:** `MWState::StateManager::saveGame` writes records with `ESMWriter` (`writer.startRecord(ESM::REC_SAVE)`, `apps/openmw/mwstate/statemanagerimp.cpp:309`), and `loadGame` dispatches on record type (`case ESM::REC_…`, line 494 onwards); OpenMW's Lua stores its own data the same way in a dedicated record. **`SaveService`** (`save_service.{hpp,cpp}`, SDK `save.hpp`): versioned chunks per plugin via `SaveChunkHandler` (`currentVersion`, `writeSaveChunk`, `readSaveChunk(version, reader)`), little-endian, unread chunks preserved byte for byte.
- **Lua:** OpenMW's Lua runs in contexts (global, local, player, menu) with sandboxing (`apps/openmw/mwlua/luamanagerimp.cpp`); scripts already exchange events (global events from `core.sendGlobalEvent`, local events on objects), delivered through `MWLua::LuaManager`. After task 0019, plugins have **no** Lua tables (`ScriptingService` is removed, decision 0003); event types they register can be marked as forwarded to Lua, with a flat payload of numbers, strings and booleans.

## Design

1. **VFS:** after `data=` directories and before content is loaded, mount each enabled plugin's `data/` folder (manifest `data` field) into `VFS::Manager` in plugin load order, above the user's data directories and below `data-local` (proposed; open question 1). `VfsService` becomes a facade over `VFS::Manager` for plugins (read, exists, list), keeping its located-error reporting. Format readers registered by plugins are consulted by the resource system for their extensions (start with the reader dispatch `VfsService` already has; a resource-system hook for model and animation formats is listed as a follow-up if the resource system has no extension point at the starting commit).
2. **Settings:** each plugin's settings live in `[plugin.<id>]` in the user's `settings.cfg`; manifest defaults act as the default layer (like `settings-default.cfg`); values the user never changed are not written. `SettingsService` reads and writes through `Settings::Manager`; reading is allocation-free per frame (cache on start and on change). Changing a setting at runtime notifies the plugin (callback) if the engine's settings system supports change notification at the starting commit; otherwise settings are read at start only, and that is documented.
3. **Saves:** a new record type for plugin data (proposed `OEPL`, one record per plugin: plugin id, chunk version, bytes). Written after the engine's own records in `saveGame`; read in `loadGame` and routed to `SaveService` by plugin id. Chunks of plugins not loaded are kept in memory and written back unchanged on the next save. The record is skipped harmlessly by readers that do not know it (check how the loader treats unknown record types at the starting commit and make it skip, not fail).
4. **Plugin events forwarded to Lua:** when a plugin publishes an event type marked for forwarding, the core delivers it to Lua as a global event named `openeden_<pluginId>_<eventName>` (name scheme proposed; open question 3) with the payload as a Lua table, through the same queue OpenMW uses for global events, so it arrives on the Lua thread at the usual point in the frame. No plugin code runs inside Lua; the payload is copied, never referenced. Forwarding is lossless and in publish order; a payload that cannot be converted is dropped with a located warning.

## Inputs / outputs

- Engine adapters in `apps/openmw` (or `components/plugin/engine/`, as the implementer finds cleaner; plugin-facing behaviour stays in `components/plugin`).
- The `OEPL` record (or the agreed name) in `components/esm3` (or the save-format area at the starting commit), with read/write and a format note in `docs/source` or `docs/design`.
- SDK documentation comments updated for threading (which thread each callback runs on).
- Tests: unit tests for each adapter; an integration test with a test plugin that mounts a data file and reads it through the game's VFS, declares a setting and reads the user's value, saves and loads a chunk with the game, and publishes a forwarded event that a Lua integration test's global script receives with its payload.

## Constraints

- Zero plugins: VFS contents, settings files and saves are byte-identical to a build without this task (test: save a game, compare files).
- A save made with a plugin loads in a build without it (chunk preserved and written back) and the plugin's data is intact when the plugin returns (test).
- Plugin data never overrides the user's explicitly configured `data-local` (or whatever toddhow decides).
- No allocation per frame added.

## Acceptance criteria

- [x] A plugin's `data/` files are visible through the game's VFS in load order; a file in two plugins resolves to the later one; a game data file overridden by a plugin is reported once in the log.
- [x] A plugin's settings appear under `[plugin.<id>]` in `settings.cfg` only when changed; defaults come from its manifest; wrong-typed values fall back to the default with a located warning. Out-of-range is not checked: manifests have no range or enum fields (`PluginSetting`, `manifest.hpp`), so that waits for them.
- [x] ~~Removed by decision 0005~~ A plugin's save chunk is written into the `.omwsave` and read back with the right version; a migration callback receives older versions; saving without the plugin preserves its chunk byte for byte (round-trip test); saves without plugins gain no record. Byte identity was not compared with a build without this task: saves hold timestamps, and the save path adds nothing when there are no chunks.
- [x] An event a plugin marks for forwarding reaches Lua global scripts with its payload, in publish order, on the Lua thread; an unconvertible payload is dropped with a warning; unmarked events never reach Lua (tests).
- [ ] All existing gates pass; the 32 Lua integration tests are unchanged. Done on Windows: build, unit tests, the 32 Lua tests, `tools/plugin_data_check/check.sh`. Not run: Linux `-Werror`, clang-format 14, clang-tidy, sanitizers.

## Out of scope

- Physics, entities, events, debug draw, profiler (task 0017); launcher UI for plugin settings (task 0018).

## Decisions taken while implementing, for toddhow to confirm

1. **VFS priority:** plugin data above `data=` folders and below `data-local`, as proposed. So a plugin's file hides a mod in a `data=` folder. Since task 0018 they are also searched for content files, so a plugin can carry an `.omwscripts`.
2. **Save record name:** `OEPL`, as proposed (`components/esm3/pluginchunk.hpp`, one record per plugin: NAME, VERS, DATA).
3. **Forwarded events:** global events named `openeden_<pluginId>_<eventName>`, as proposed.
4. **Threads:** plugins' save handlers are never called on the save or load worker, because frames and phase callbacks continue meanwhile. Chunks are collected on the main thread before the worker starts (`SaveGame::run`) and delivered after loading ends (`LoadGame::postLoad`). So a chunk describes the moment the save was asked for. Forwarded events are likewise sent only just before Lua's synchronized update, because the worker uses Lua while frames continue.
5. **Events with no game:** global scripts exist only in a running game, so events wait until one runs, up to 1024; beyond that they are dropped with one warning. This and a full deferred queue are the two exceptions to "lossless".
6. **Starting a new game or loading** forgets chunks kept for absent plugins, so they do not leak into another playthrough.
7. **`StateManager` and `MWBase::Environment`** gain `MWBase::PluginManager`, the interface the save code reaches plugins through.

## Notes for toddhow

- **Format readers:** plugin readers read files from the game's VFS, but the resource system does not call them for models or animations. That hook is the follow-up the design names.
- **Settings at runtime:** a change is not announced to the plugin; it reads the value when it wants it. `Settings::Manager` has no per-plugin change notification.
- **A setting set back to its default:** if `settings.cfg` held it, the settings writer leaves `# invalid setting: ...` in the file (`components/settings/parser.cpp`).
- **A plugin whose `start` fails keeps its data in the VFS,** because folders are added before plugins start.
- **Overrides report:** a plugin file also in `data-local` is reported as overriding game data, although `data-local` wins.
- **Flaky test seen again:** `DetourNavigatorRecastMeshObjectTest.update_for_compound_shape_with_same_transform_and_changed_child_transform_should_return_true` failed in 1 of 2 full `components-tests` runs on this branch; it passes alone. Cause unknown; not compared with a clean `vulkan` build.
