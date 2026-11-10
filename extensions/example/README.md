# The example extension

A small extension that uses every service extensions have, as a template for your own. It is off by default: add
`extension=example` to `openmw.cfg` to try it. It builds against the SDK only, as every extension should. Start reading at
`src/example_extension.cpp`.

It shows what extensions may do, and by leaving them out, what they may not: it never changes physics and has no Lua API
(decision 0003), and it has no part in saved games (decision 0005). What it keeps goes in its settings. Things that must
do more belong in the core.

## What it does

Each thing it does is logged with the `[example]` prefix, so you can follow it in `openmw.log`.

| Service | What it does | Log line |
| --- | --- | --- |
| Data files | Reads `example/greeting.txt` from its `data/` folder through the game's VFS | `greeting: <the file's text>` |
| Settings | Reads `greeting_count`, an int defaulting to 1, and counts the game loads in `games_seen`, which outlasts the run | `setting greeting_count=<n>` and `setting games_seen=<n>` |
| Lua | Forwards `greeted` to global scripts as `openeden_example_greeted`, with `count` and `text` | `event sent count=<n>` |
| Physics | Once per game, casts a ray and a sphere down from above the player, against world and terrain | `ray hit=<yes/no>` and `shape hit=<yes/no>` |
| Entities | Counts lifecycle events and keeps data on each spawned entity, freed on despawn | `entities spawned=<n> freed=<n>`, on stop |
| Physics and game events | Counts contacts; logs game loads, saves and cell changes | `event game loaded`, `event game saved`, `event cell changed <name>`, and `contacts=<n>` on stop |
| Debug draw | Submits a line above the player, and a box, a sphere, a point, a label and a line about 4 m ahead of him (some drawn over everything), each frame | `debug draw submitted`, once |
| Profiler | Times its per-frame work in a scope named `example.frame` | `profiler scope used`, once |
| Logging | Everything here | |

Its Lua script, `data/scripts/example/global.lua`, is loaded by adding `content=example.omwscripts` to `openmw.cfg`, and
prints each `openeden_example_greeted` event: `EXAMPLE_LUA count=<n> text=<text>`.

Remove that `content=` line when you turn the extension off. The script lives in the extension's data folder, and the game refuses to start when a listed content file is missing.

With `extension debug draw = true` under `[Game]` in `settings.cfg` the game draws these shapes as lines (the label as a small cross); with `extension profiler = true` the debug window (F10), tab Extensions, lists the extension's timings, `example.frame` and each phase.
