# Debug environment variables

These variables come from vsgopenmw (see `docs/credits-vsgopenmw.md`) and the renderer still reads them. They are for debugging; none is needed for normal play. Set them before starting the game.

| Variable | Effect |
| --- | --- |
| `VSGOPENMW_VALIDATION` | Enables Vulkan validation output on standard output. The Vulkan validation layer must be installed. |
| `VSGOPENMW_NO_VIEWER_THREADING` | Turns off multi-threaded command graph recording (`vsg::Viewer::setupThreading`); command graphs are recorded on the main thread. |
| `VSGOPENMW_NO_FRAME_QUEUE` | Prevents queued frames in the swapchain by adding a `vkDeviceWaitIdle` between frames. |
| `VSGOPENMW_INDIRECT` | Turns off direct rendering: the GPU still renders, but the output is copied to the window surface in software. For a Vulkan driver without DRI, or an SDL build without Vulkan support. |
| `VSGOPENMW_FAST_BOOT` | Skips time-intensive loading steps. May break some content features. |
| `VSGOPENMW_DECOMPRESS_BC` | `1` forces software conversion of block-compressed textures; `0` forces hardware block-compressed textures even if the hardware does not advertise support. |

Source: vsgopenmw's `vsgopenmw-debug.doc`. Checked against the code on 2026-11-03: each name appears in `apps/` or `components/` (found with `git grep`); what each one does here was not re-tested.

OpenEden test aids (task 0017, for screenshots; not from vsgopenmw):

| Variable | Effect |
| --- | --- |
| `OPENEDEN_OPEN_DEBUG_WINDOW` | Any value: the debug window (F10) opens by itself at running frame 20. |
| `OPENEDEN_DEBUG_WINDOW_TAB` | `extensions`: the debug window starts on its Extensions tab (the extension profiler). |
