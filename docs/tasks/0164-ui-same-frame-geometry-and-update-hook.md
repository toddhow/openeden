# Task: UI geometry drawn in the frame it is compiled, and an engine hook for `ui.update()` (follow-ups of task 0162)

Status: built 2026-11-13 by the same hand that wrote tests and code (no fresh reviewer). Windows only. Branch `uifollow`, not pushed.
Design doc section: `docs/research/0045-ui-system.md` 13.5 to 13.7; follow-ups 1 and 3 of the stage log of `docs/tasks/0162-ui-element-protocol-and-lua-library.md`.
Goal: (1) a text that changes every frame is drawn in every frame; (2) a script that requires `openeden.ui` need not call `ui.update()`.
Inherited code touched (at commit `ad277e5225`): `components/ui_rml/renderinterface.{hpp,cpp}` (`RenderInterface::CompileGeometry`, `compilePending`, `draw`), `components/ui_lua/backendpackage.{hpp,cpp}`, `apps/openmw/mwlua/luamanagerimp.{hpp,cpp}` (`init`, `synchronizedUpdateUnsafe`, `clear`, `reloadAllScriptsImpl`), `files/data/openeden/ui.lua`, `tools/ui_check`.

## 1. Same-frame geometry

### Cause (read in VSG v1.1.16, `extern/fetched/vsg`)
`RenderInterface::compilePending` compiled the geometry and handed its buffers to `CompileContext::onCompiled`, which gives them to the viewer's `TransferTask`. The vertex and index `vsg::Data` had the default static variance, and a `TransferTask` copies static data in its EARLY transfer (`RecordAndSubmitTask::submit`, before the record traversal), which for data assigned during a frame is the next frame. `Geometry::mDrawable` was therefore set one frame later. RmlUi builds new geometry for every text change, so for a value that changes every frame the newest geometry was never old enough and nothing was drawn.

### What was tried first, and why it cannot work
Taking the transfer task off the overlay's own `CompileTraversal` (`context->transferTask = nullptr`) so that VSG copies through a staging buffer in that traversal's command buffer: in VSG 1.1.16 `Context::createBufferAndTransferData` (`#if 1` branch) only reserves device memory and queues the buffer in `bufferInfosToCopy`; `Context::record` copies that only through a transfer task and otherwise warns "Need to implement copying of data" and records nothing. Run in the game: a warning every frame and no panel. Removed.

### The change
The vertex and index data of every compiled geometry are `vsg::DYNAMIC_DATA_TRANSFER_AFTER_RECORD` ("updated during the record traversal"). `TransferTask` puts such data in its LATE list, copies it in `RecordAndSubmitTask::finish` after all command graphs have recorded, and the frame's submission waits for that copy (`lateDataTransferredSemaphore`). The overlay records its draws in the same traversal, so geometry compiled in a frame is drawn in that frame: `mDrawable` is set at compile, `mJustCompiled` is gone. The viewer-compile race (`vsgUtil::viewerCompileMutex`) is not touched: nothing new is compiled, and the compile still runs on the GUI record thread as before. The data stay in the task's list until the geometry is released (`TransferTask` drops an entry whose `BufferInfo` has no other reference) and are copied again only if modified: nothing is.

Allocation: a frame in which nothing changed compiles nothing and allocates nothing (unchanged, `UiAllocation`). A frame in which text changed allocates what RmlUi and VSG allocate for the new geometry (vectors, `vsg::Data`, `VertexIndexDraw`, a `BufferInfo`); that is inherent to RmlUi's per-change geometry and is the same with or without this change (the old code compiled the same geometry, it just drew it a frame late).

## 2. The update hook

`UiLua::UpdateRegistry` (`components/ui_lua/backendpackage.*`): a session's new function `setUpdater(fn)` registers `fn` in the registry of its package; the library registers `ui.update` when it is required (`files/data/openeden/ui.lua`, guarded so a package without the function still loads). `LuaManager::runUiUpdaters` runs every registered updater once per frame in `synchronizedUpdateUnsafe`, right after the menu and player scripts' `onFrame` (so what `onFrame` changed is flushed in the same frame); menu registry first, then the player's. Registration order is the order of `require`. A failing updater is reported with `Log(Debug::Error) "[UI] update hook: ..."` the first time and again only when its message changes; it never stops the others.

- Two registries, one for each script lifetime: the player scripts' (cleared in `LuaManager::clear` and `reloadAllScriptsImpl`) and the menu scripts' (cleared in `reloadAllScriptsImpl`). An entry holds the Lua function, which keeps its session alive, so it must be dropped with the scripts.
- A manual `ui.update()` stays harmless: a second flush in one frame finds nothing dirty, writes nothing and commits nothing (test `AManualUpdateBesideTheHookIsHarmless`).
- Global scripts have no `openeden.uibackend` (unchanged): only player and menu scripts.
- `tools/ui_check/mod` no longer calls `ui.update()`, updates its counter Hud every frame and takes six consecutive screenshots (script frames 600 to 605). `check.sh` takes `MODDIR` to run another mod folder.

## Tests

`apps/components_tests/ui/testuiupdatehook.cpp`, 9 tests, written first against a stub registry (no hook): 5 of 9 failed red (registering, flush without update, order, failing updater reported once, a changed message reported again); the other 4 pin absence or behaviour that a stub also has (a manual update beside the hook is harmless, `clear`, the library loading without a registry, a non-function updater is an error). After the code: 282 of 282 of `Ui*:*Theme*` pass in `components-tests`. The render change has no unit test: it needs a Vulkan device and a viewer (the draw path is proven by the frames below; `RenderInterface` has none in 0162 either).

## What was proven (RTX 4070, 1280 x 720, `OPENEDEN_FIXED_FRAME_TIME`, seed 1, quiet window, `tools/ui_check/check.sh`, two executables of this tree that differ only in `renderinterface.cpp`: `openmw_old.exe` has the file of `ad277e5225`, `openmw_new.exe` this change)

- **Per-frame counter:** script frames 600 to 605, six consecutive `debug.takeScreenshot()` calls, the Hud `frame N` changed every frame. Old: no counter in any of the six frames (the static panel is drawn). New: `frame 600`, `601`, `602`, `603`, `604`, `605`, each in its own frame (cropped sheets viewed: `I:\Projects\OpenEden-work\uifollow\work\runs\ab_old_lua\counter.png`, `ab_new_lua\counter.png`). The panel needed no `ui.update()` call in the script.
- **Off means unchanged:** `[GUI] rmlui` off, frame 900 screenshot: `openmw_old.exe`, `openmw_new.exe` and the `uilib` build (a build of the tree before this task) give the same pixels (0 differ; SHA-256 of the pixels `94b37596ca758c72` for the first two). With the C++ debug panel on, old against new differ in 46 pixels, all inside the 8 x 10 box of the frame rate digits (`bbox (84, 52, 92, 62)`); with nothing in the overlay 0 pixels. The Lua variant differs in the frame counter's box (it is only drawn by the new code) and the frame rate digits.
- **Overlay frame cost on the GUI thread (mean microseconds over about 1,170 frames: drain / update / render):**

| variant | old | new |
| --- | --- | --- |
| rmlui on, nothing in it | 0.52 / 4.97 / 5.80 | 0.55 / 4.63 / 5.67 |
| C++ debug panel | 0.45 / 12.91 / 39.88 | 0.50 / 12.74 / 40.32 |
| Lua panel, counter every frame | 4.83 / 35.37 / 67.71 | 4.58 / 39.76 / 75.02 |

The only row that moves is the one with a value that changes every frame: render +7 microseconds, because the counter's new geometry is now recorded and drawn (the old code skipped it). Median frame wall time over the last 400 frames (ms, p95): nothing 3.79 / 3.83, C++ panel 3.83 / 3.84, Lua panel 3.84 / 3.90 (old / new): inside run noise on a shared machine.
- The hook in the game: the Lua panel draws and updates with no `ui.update()` in the script; no `[UI] update hook` error line.

## Not run
The Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua integration tests, a fresh review, mutation checks of the hook, a menu script that requires the library (the menu registry is built but no menu script uses `openeden.ui`), a reload of all scripts (`reloadAllScriptsImpl`) with the hook, loading a save with a player script that registered an updater, GPU time of the late transfer (the transfer is one small copy a frame, not measured separately), another GPU.

## Decisions for toddhow
1. The updater runs after `onFrame` of all scripts, once a frame, also while the game is paused (`synchronizedUpdate` runs then too). I chose that so a Hud keeps its text while a menu is open; a script that must not update while paused would have to skip its `Value:set` calls.
2. An updater that raises is logged once per distinct message and stays registered; the alternative is dropping it after the first error.
3. `setUpdater` is called once per session by the library; a second call adds a second entry (the library never does). If scripts are expected to call it, make it replace instead.
4. Harness note: `tools/ui_check/check.sh` runs sometimes never reach `--auto-quit` after the `--auto-screenshot` (the running-frame counter stops while frames go on; seen with the old and the new executable, with the overlay off too, not understood); the run must then be ended by hand. It does not depend on this change.
