# 0005 — Plugins do not affect save data

Date: 2026-10-31 · Decided by: toddhow

Numbered 0005 because a draft 0004 on Slang shaders exists on another branch.

## Decision
- **Plugins add features to the engine and do not affect save data.** They are like script extender plugins in that way. A plugin has no way to write to, or read from, a saved game, and the core saves nothing on a plugin's behalf.

## Why
- **Installing or removing a plugin does nothing to saves.** That is what makes plugins safe to add and drop.

## Limits
Two things can still reach a save, each by a player's or a mod's own choice, and neither is the plugin system's doing:
- **Content files.** A plugin's data folder can hold content files such as an `.omwscripts`. A `content=` line for one is recorded in saves like any other content file, so a save made with it asks about the missing file once it is gone. This is a mod that lives in a plugin's folder, not the plugin.
- **Lua scripts.** A script receiving a plugin's forwarded event may keep what it likes in its own saved state. The event itself is never saved: one still waiting in Lua when a save is made is dropped from it (`LuaEvents::addPluginEvent`).

## Consequences
- **Removed:** `SaveService` and `SaveChunkHandler` from the SDK (`sdk/include/openeden/save.hpp`), `components/plugin/save_service`, the `OEPL` record (`ESM::PluginChunk`) and the engine's save and load hooks for it, all built in tasks 0003 and 0016. A save made while `OEPL` existed still loads: the game skips the unknown record with a warning.
- **SDK version:** removing `getSaveService` changes `Host`'s layout with the SDK still at 2.0, because no plugin was built against an earlier 2.0 outside this branch.
- **A plugin that keeps something** uses its settings or its own files, which do not follow a saved game. `GameSavedEvent` and `GameLoadedEvent` remain, so a plugin can react.
- **A feature that must live in the save belongs in the core,** or in a mod's Lua, whose storage is the core's.
- **This narrows plugin doc 4.9** and task 0016's design, and it answers task 0016's open question about telling plugins of a new game: there is nothing to reset for a save, and `GameLoadedEvent` covers the rest.
