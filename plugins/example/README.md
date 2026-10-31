# The example plugin

A small plugin that uses every service plugins have, as a template for your own. It is off by default: add
`plugin=example` to `openmw.cfg` to try it. It builds against the SDK only, as every plugin should. Start reading at
`src/example_plugin.cpp`.

It shows what plugins may do, and by leaving them out, what they may not (decision 0003): it never changes physics and
has no Lua API. Things that must do those belong in the core.

## What it does

Each thing it does is logged with the `[example]` prefix, so you can follow it in `openmw.log`.

| Service | What it does | Log line |
| --- | --- | --- |
| Data files | Reads `example/greeting.txt` from its `data/` folder through the game's VFS | `greeting: <the file's text>` |
| Settings | Reads `greeting_count`, an int defaulting to 1, and counts the game loads in `games_seen` | `setting greeting_count=<n>` and `setting games_seen=<n>` |
| Saved games | Keeps a counter in a save chunk at version 2; a version 1 chunk is migrated | `saved count=<n>` and `loaded count=<n> version=<v>` |
| Lua | Forwards `greeted` to global scripts as `openeden_example_greeted`, with `count` and `text` | `event sent count=<n>` |
| Physics | Once per game, casts a ray and a sphere down from above the player, against world and terrain | `ray hit=<yes/no>` and `shape hit=<yes/no>` |
| Entities | Counts lifecycle events and keeps data on each spawned entity, freed on despawn | `entities spawned=<n> freed=<n>`, on stop |
| Physics and game events | Counts contacts; logs game loads, saves and cell changes | `event game loaded`, `event game saved`, `event cell changed <name>`, and `contacts=<n>` on stop |
| Debug draw | Submits a line above the player each frame | `debug draw submitted`, once |
| Profiler | Times its per-frame work in a scope named `example.frame` | `profiler scope used`, once |
| Logging | Everything here | |

Its Lua script, `data/scripts/example/global.lua`, is loaded by adding `content=example.omwscripts` to `openmw.cfg`, and
prints each `openeden_example_greeted` event: `EXAMPLE_LUA count=<n> text=<text>`.

Remove that `content=` line when you turn the plugin off. The script lives in the plugin's data folder, and the game refuses to start when a listed content file is missing.

Showing the debug line on screen waits for task 0017's remaining work: it is submitted, not yet drawn.
