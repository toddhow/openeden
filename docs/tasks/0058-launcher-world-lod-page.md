# Task: Launcher "World LOD" page and the baker status file contract
Design doc section: `docs/design/world-lod-and-streaming.md`, sections 4.2 (process, both modes), 4.3 (work units, resume, pause), 4.5 to 4.7 (cache keys, load-order changes, hand-over at restart) and 4.8 ("Where the user sees it": the page, the status file, the `[WorldLod]` settings), stage S8 of section 11; `docs/tasks/0018-plugins-w4-clients-and-packaging.md` (launcher conventions); `docs/tasks/0057-launcher-mod-organizer.md` (the active profile and the cache root).
Goal: give the launcher a "World LOD" page that shows what the background LOD baker is doing (idle, running, done, out of date, failed), lets the player start, pause and stop it and see and clear the cache, by reading a status file the baker writes, so the page works with any baker that follows the contract.
Inherited code touched: not yet confirmed; to be named at the commit that starts each stage. Expected: `apps/launcher/maindialog.cpp` and `.hpp` (`createPages`, `writeSettings`, page actions), `apps/launcher/extensionspage.*` (pattern), `apps/launcher/ui/mainwindow.ui`, `apps/launcher/CMakeLists.txt`, `components/files/configurationmanager.hpp` (`getCachePath`, `getUserConfigPath`), `components/process/processinvoker.cpp` (`startProcess` with arguments, detached or not), `components/config/gamesettings.*` (reading the content, data and archive lines to show a fingerprint), `components/settings/` (the `[WorldLod]` keys, once the baker task adds them).
Inputs / outputs: a new `WorldLodPage` in `apps/launcher/`; a small non-GUI reader for the status file (proposed `components/worldlod/bakestatus.*`, shared with the baker so reader and writer cannot drift); a documented status file and command file format (below, a **proposal**); Qt translation files regenerated; no change to the game or the baker in this task beyond agreeing the contract.

Status: draft, written 2026-11-02. **Full loop** when it starts (it starts and stops a process, deletes cache files and parses a file written by another process). **Blocked**: it depends on the baker (stage S8 of the world-LOD design: the bake process, its work units, its status file) and on the cache stages S1 and S2, none of which exists. Until then this spec fixes the contract and the page so the baker is built to it. Nothing is built; every format below is a proposal.

## What exists today (read from the code at this commit)

- The launcher has the pages listed in task 0057 and no World LOD page; `ExtensionsPage` is the model for a small code-built page (`apps/launcher/extensionspage.cpp`).
- There is no baker, no `[WorldLod]` settings group, no LOD cache and no status file; the design doc is the only source (section 4). The only bake-like command line mode that exists is the GI bake mentioned in the design doc (`--bake-gi`, task 0028), which is a different tool.
- The launcher starts programs with `Process::ProcessInvoker::startProcess(name, arguments, detached)` from the install folder (`components/process/processinvoker.cpp`); it does not currently watch a long-running child or read its output.
- Cache location: `Files::ConfigurationManager::getCachePath()`; task 0057 puts user repacks in `<cache>/openeden/repack/`; the design doc names a `cache folder` setting for the LOD cache (section 4.8) but no default. This spec proposes `<cache>/openeden/lod/`.

## Design, in plain language

The baker is a separate, low-priority process that slowly builds distant-land data and writes it into a cache folder (design doc 4.1, 4.2). It can run while the game runs (results used after the next start) or ahead of time from the launcher. The player needs one calm place to answer: "Is it building? How far? Is what I have still valid for my mods? How much disk does it use? Can I stop it?" That is this page.

The page itself does no baking. It does three things: **read** a status file, **send** a few requests to the baker, and **manage the cache folder**. It never talks to the baker over a socket (design doc 4.8: no sockets).

### The states the page shows

| State | Meaning | Buttons |
| --- | --- | --- |
| Off | the `[WorldLod]` feature is not enabled | Enable (opens the setting; nothing starts) |
| Not built | enabled, no generation exists for this profile | Build now |
| Building | a baker process is alive and writing the status file | Pause, Stop |
| Paused | alive, not working (by the player, or by the baker because the game's frame times are high or the machine is on battery, shown with the reason) | Resume, Stop |
| Up to date | a complete generation matches the current profile's fingerprint | Rebuild, Delete cache |
| Out of date | a generation exists, but the load order changed (design doc 4.6); the tiles whose keys still match are reused | Update now (starts the baker), Delete cache |
| Failed | the baker stopped with an error (shown with its message and the log path) | Build again, Open log |
| Unknown | the status file is missing, unreadable or from a newer format | (Show why; Delete cache only) |

"Alive" is judged from the status file's heartbeat (below) and, when the launcher itself started the baker, from the child process; a stale heartbeat on a file that says "building" means the baker died and the page shows **Failed (the baker stopped without finishing)**, never "building" forever.

### What the page shows

Progress as a bar and units done of total, a coarse-first map of finished levels (text first: "level 3 of 6 complete"), the elapsed time and an estimated remaining time (shown as an estimate, hidden until the baker has done enough units to give one, never a promise), the current activity ("terrain tiles", "object clusters"), the last cell used for ordering, the number of errors with the first few messages, the cache size on disk and the size limit setting, the generation's tool version and fingerprint, and which profile it was built for.

### The status file contract (proposal)

Written by the baker, read by the launcher and the game. One JSON file in the LOD cache folder (`<lod cache>/status.json`), rewritten **atomically** (write `status.json.tmp`, flush, rename) at least every 5 seconds while working and on every state change. Never edited by anyone else. Canonical writer, `format` string and `version`; a reader that sees a higher major version shows state Unknown and does not parse the rest.

```json
{
  "format": "openeden.lod.status",
  "version": 1,
  "toolVersion": "<baker version string>",
  "pid": 12345,
  "processStartTime": "<opaque token from the OS so a recycled pid is not mistaken for the baker>",
  "state": "building",
  "pauseReason": "",
  "heartbeat": 1730000000,
  "profile": "<name or empty>",
  "fingerprint": "<64 hex: the load-order fingerprint this run is building for>",
  "generation": "<64 hex or empty: the manifest the game would use, once complete>",
  "unitsTotal": 13000,
  "unitsDone": 5400,
  "unitsFailed": 0,
  "level": 3,
  "levelsTotal": 6,
  "activity": "terrain",
  "lastCell": { "worldspace": "", "x": 0, "y": 0 },
  "etaSeconds": -1,
  "cacheBytes": 0,
  "errors": [ { "unit": "tile key", "message": "located message" } ],
  "logPath": ""
}
```

Rules: `heartbeat` is seconds from a clock the reader compares with its own with a generous margin (a heartbeat older than 30 seconds while `state` is `building` is treated as dead; both numbers are settings of the page, not of the file); the baker may use wall-clock time **here only** (status is not simulation output, so the determinism rule of the baker's tile bytes is not touched); `errors` holds at most 20 entries (the rest only counted); no path or text in it is trusted by the reader (shown as plain text, never executed or opened without the player's click). The file contains no mod content.

### Requests to the baker (proposal)

The launcher does not signal the process. It writes a tiny **command file** `<lod cache>/command.json` by atomic rename, containing `{ "format": "openeden.lod.command", "version": 1, "id": <increasing integer>, "command": "pause" | "resume" | "stop" }`; the baker checks it between work units and writes the id it handled as `lastCommandId` in the status file. A stop takes effect at the next unit boundary (units are small, design doc 4.3) and leaves only complete files, so a stop is always safe. The hard fallback, after a timeout shown in the page, is "End process" through the OS, which is also safe by the same rule (a killed baker leaves only complete files and a plan, design doc 4.3). Start is different: the launcher starts a new baker process with `startProcess` and the baker's own arguments (named by the baker task), at below-normal priority, detached from the launcher's window, and then follows the status file. Two bakers must not run for one cache: the baker takes a lock file in the cache folder at start (the baker's task decides the mechanism; a lock left by a crash must expire, design doc 4.7 says **verify**); the page shows "another baker is running" if starting finds the lock.

### Cache size and clearing

The page shows the folder size (computed off the GUI thread, refreshed on demand and on state change, not continuously), the `cache size limit` setting and the number of generations. **Delete cache** lists what it will delete, refuses while a baker is alive or a game is running (the game opens the manifest and tiles once at start and the baker never edits existing files, design doc 4.7, so deleting is safe only when nothing is using them), asks once, and removes only files inside the LOD cache folder whose names match the cache's naming (it will not follow a link out of the folder and refuses if the folder path does not end in the expected names). **Delete old generations** keeps the current one. Neither touches any mod, game file or the repack cache of task 0057.

### Relation to profiles and to the mod organizer

The fingerprint of a profile is computed from names, sizes and modified times of its content files, data folders and archives (design doc 4.6 step 1, a cheap fingerprint). The launcher computes it for the **active profile** with the same function the game uses (one shared function in the baker's library, so the page and the engine cannot disagree) and compares it with the status file's `fingerprint`. When they differ the page says **Out of date** and, if the player has enabled "update automatically after I change my mods" (an off-by-default checkbox saved with the launcher settings), offers the update when the player applies a profile in the Mods page. The page never starts a bake by itself otherwise. A repacked mod changes the layout but not the content, so whether a repack changes the fingerprint is decided by the baker task (recommended: the fingerprint is over the logical content, so a faithful repack does not invalidate a bake; **verify** against the cache-key rules of design doc 4.5).

### Settings the page edits (proposed, from design doc 4.8)

A `[WorldLod]` group, all off by default: `enabled`, `background bake`, `bake threads` (default a quarter of the cores, at least one), `cache size limit`, `cache folder`, `quality tier` (`auto` once enabled). The page writes them through the same `Settings::Manager` path the Graphics page uses (`MainDialog::writeSettings` saves `settings.cfg`); the baker task defines the keys and their defaults, and this page shows them without redefining anything. An ahead-of-time bake (design doc 4.2, decision D2) is the same Build now button with a chosen output folder (a second, advanced section: output folder, Start, and the explanation that the result can be kept or shipped; **verify** how the game is pointed at a shipped result, a baker task decision).

## Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | How the launcher learns the state | A status JSON file the baker rewrites atomically (design doc 4.8), with a heartbeat | Parse the baker's log, or a local socket (both rejected by the design doc) |
| D2 | How the launcher controls the baker | A command file by atomic rename, checked between units; OS kill only as the timed fallback | Signals or named pipes (platform-specific) |
| D3 | Who starts the baker | The launcher (detached, below-normal priority) and, for the background mode, the game; both follow the same lock | Launcher only |
| D4 | Where the page lives and what it is called | A "World LOD" page beside Extensions and Mods | A section in Graphics |
| D5 | Where the contract's code lives | One small shared library (`components/worldlod/bakestatus.*`) used by baker, game and launcher | Three separate parsers |
| D6 | Automatic update after a profile change | Off by default; offered, never forced | On by default |
| D7 | Estimated time | Shown only after enough units, labelled as an estimate | Always show |
| D8 | Cache folder default | `<getCachePath()>/openeden/lod/` | Next to the game's user data |
| D9 | Does a faithful repack invalidate a bake | No (fingerprint over logical content) | Yes (simpler, wasteful) |

## Stages

Each stage commits working, with a fresh reviewer; tests first (full loop).

- **a. Contract and reader.** The status and command formats written as a short spec section next to the baker's design (changed only with the baker's author), the shared reader/writer with located errors, the canonical writer, and tests from generated files: every state, a truncated file, a file from a newer version, huge strings, a missing field, a heartbeat edge. No UI.
- **b. The page, read-only.** `WorldLodPage` showing the states from generated status files (a test fixture folder; no baker), the progress, the cache size and the Out of date comparison, with all strings translated and keyboard and screen-reader support. Buttons are present but disabled with the reason.
- **c. Control.** Start (child process), Pause, Resume, Stop through the command file, the timed fallback, the lock check, and the cache deletion with its guards. Tested against a small **fake baker** program in the tests that follows the contract (writes status, obeys commands, can be killed mid-write) so that the page can be proved without the real baker.
- **d. Real baker integration.** After the baker (S8) exists: replace the fake baker in manual checks, write the in-game steps (start from the page, play, stop, restart the game and see the tiles used), measure the page's cost while the baker runs (it must stay light), and record the result.

## Constraints

- Off means unchanged: with the feature disabled and no status file, the launcher and game behave as before; the page is informational.
- The launcher never writes inside a mod folder or the game's data; it writes its own settings, the command file, and deletes only inside the LOD cache folder.
- No network, no telemetry, no sockets. The page reads one file and writes one small file.
- The status reader treats the file as untrusted: size cap (for example 256 KiB, a proposal), field length caps, no path from it is opened without a click, no recursion on file data, no `new`/`delete`.
- Threading: file reads, size scans and process supervision off the GUI thread; results by queued signals.
- A stale or half-written status file never shows a false "done": a state is trusted only with a fresh heartbeat or a terminal state written by the baker.
- The page strings go through `tr()` and the `.ts` files are regenerated (`CI/check_qt_translations.sh`).
- No game or mod assets in tests; the fake baker and its files are generated.

## Acceptance criteria

- [ ] The status and command formats are written down, versioned, parsed by one shared reader with tests including hostile files (truncated, oversized, wrong types, newer version).
- [ ] The page shows each state of the table correctly for generated status files, including a dead baker (stale heartbeat) shown as Failed and a missing file shown as Unknown with the reason.
- [ ] Start, Pause, Resume and Stop work against the fake baker, a stop leaves only complete files, a kill mid-write leaves the previous status file readable, and two bakers cannot run for one cache.
- [ ] Delete cache refuses while a baker is alive or the game is running, deletes only files of the cache's naming inside the folder, and does not follow links.
- [ ] Out of date is shown exactly when the active profile's fingerprint differs from the status file's, using the shared fingerprint function.
- [ ] With the feature off, nothing changes in the launcher's behaviour or files.
- [ ] The page is usable by keyboard, status is not colour only, progress is announced, strings are translated and `lupdate` output matches.
- [ ] Unit tests, the 32 Lua tests, the Linux gate, clang-format 14 and the CI check scripts pass; a fresh reviewer read each stage. (Stage d, with the real baker, stays open until the baker exists.)
Out of scope: the baker itself and its settings definitions, the tile format and cache keys, the in-game status line (design doc 4.8, game side), any socket or network, impostor baking (needs a GPU context and the game closed, decision D6 of the design doc), editing mod lists (task 0057), shipping an ahead-of-time bake to other players, hot-swapping tiles in a running game (not a goal, design doc 4.7).

## Open items (verify)

1. The baker's command line and lock mechanism (the baker task decides; this spec only needs "start with arguments" and "a lock exists").
2. A portable process-start-time token (Windows `GetProcessTimes`, Linux `/proc/<pid>/stat`: **verify** before relying on them) so a recycled pid is not mistaken for the baker.
3. A portable "on battery" and "game running" signal (design doc 4.3 marks battery as **verify**).
4. Whether the fingerprint ignores a faithful repack (D9) and how the engine computes it at start (design doc 4.6 step 1).
5. Size cap and heartbeat timeout values: starting proposals, to be set from the baker's measured behaviour.

## Stage log (2026-11-09; Windows only, local commits, not pushed)

Taken as recommended. Stage a done, b to d NOT built (no Qt page, no fake baker).

- a. `components/worldlod/bakestatus.*`: status and command files (canonical JSON, v1, newer version flagged and not parsed, 256 KiB cap, field caps, at most 20 errors, duplicate keys and floats refused), `judge()` giving the page states (a stale or future heartbeat on building/paused is Failed, complete without a matching fingerprint is OutOfDate, the 30 s timeout is a parameter), `shownEtaSeconds`, atomic write and capped read, LOD cache guards (`isLodCacheFolder`, `listCacheFiles` leaving links and unknown names, `deleteCache` refusing while a baker or the game runs and anything outside the folder). The cache file naming list (json, tmp, lock, edenpak) is a placeholder until the baker task fixes it. The shared fingerprint function and a process-start token are not built (they need the baker).
- Evidence: 30 tests written first (link-red), green; mutation on the heartbeat edge caught.
- Not run: Linux gate, clang-format, sanitizers, fresh review.


## Stage log (2026-11-10, the launcher page; Windows only, local commits, not pushed)

Stages b and c as far as the bake commands that exist allow; stage d (a real baker) stays open. The status file contract of stage a is used, but **no baker writes `status.json` yet**, so the page works from the packs the existing commands write.

- Built: `WorldLodPage` (`apps/launcher/worldlodpage.*`) with the settings table `components/worldlod/lodsettings.*` (22 keys: `enabled`, `terrain tile threshold`, `terrain fade time`, `view distance`, `map view`, `max view distance`, `haze` and its three numbers, `tier`, `far levels`, `far texture max size`, `far memory budget`, `object clusters`, its threshold and fade, `object cluster effects`, `scenery`, `sway`, `sway models`, `sway lean`, `sway wind`; `terrain tile level` is left out because the game accepts only 1). Defaults come from a test that reads `files/settings-default.cfg`; values are cut to the ranges of `components/settings/categories/worldlod.hpp`. A row is greyed, with a status tip, while the switch it needs is off (`enabled`, `map view`, `object clusters`, `object cluster sway`). The page writes only rows the player changed (`Settings::Manager::setString`; `MainDialog::writeSettings` saves `settings.cfg`).
- Status: when `status.json` exists `judge()` decides (Failed for a stale heartbeat, Out of date only with a fingerprint, progress bar from the units); otherwise the page reports Off, Not baked, Partly baked or Baked from `<user data>/openeden/lod/terrain.edenpak` and `objects.edenpak`. It says plainly that without a status file it cannot tell whether a pack is out of date.
- Control: **Bake terrain tiles** runs `openmw --bake-lod-terrain`, **Bake object clusters** runs `openmw --bake-lod-objects` (the only flags the page can produce, pinned by a test), as a child `QProcess` with the output shown; the page writes the launcher settings first. **Stop** kills the child after asking (the contract's "a stop leaves only complete files" is NOT known to hold for these commands). **Pause and Resume are shown disabled** (no command file is read). Delete baked data uses `listCacheFiles` and `deleteCache` (refuses while a bake runs or a process named `openmw` runs; only files of the cache's naming; links never followed). Both bakes are refused while a game process runs.
- Not built: the fingerprint comparison and "update automatically after I change my mods", the lock check ("another baker is running"), the below-normal priority of the child, a status line during a bake (the commands report no progress), the ahead-of-time output folder, the cost measurement of stage d.

Evidence: 17 tests (`WorldLodSettings`, `WorldLodPacks`, `WorldLodBake`, `WorldLodSummary`) written first, red as link failures; the child-process path was run against a stand-in `openmw.exe` (`tools/launcher_check/fakebake.cpp`, exit 0 for the terrain bake and exit 3 for the object bake) and the page showed Building with a moving bar and the output, then Baked or "failed (exit code 3)"; frames of all states were viewed. **Not run: the real `openmw --bake-lod-*` from the page**, Stop, Delete baked data in a window, the real launcher executable, Linux, a fresh review.
