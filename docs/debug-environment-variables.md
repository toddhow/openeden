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
| `OPENEDEN_NEUTRAL_DOOR_TRACE` | `1` logs one `NDOOR` line per activation of a neutral (game pack) door: placement, outcome, destination cell, arrival, whether the cell was built or reused, the neutral base record count and the resident neutral cells (task 0115). |
| `OPENEDEN_NEUTRAL_ACTOR_TRACE` | `1` logs one `NACTBODY` line when a neutral actor's physics body comes to rest (task 0133): the actor's reference, `grounded` or `lost`, the start and rest height and the frames it took. |
| `OPENEDEN_NEUTRAL_ACTOR_RAISE` | `<units>` lifts every neutral actor's feet that many units when its body is created (task 0133), so the fall under gravity can be seen with `OPENEDEN_NEUTRAL_ACTOR_TRACE`. |
| `OPENEDEN_NEUTRAL_UNLOCK_DOORS` | `1` lets every locked door of a neutral cell open, for testing; without it a locked door is refused with a message (task 0115). Not a setting. |
| `OPENEDEN_NEUTRAL_FORCE_WEATHER` | `<weather editor id>` (for example `SkyrimOvercastSnow`) makes a game pack's weather manager start every region in that weather at once, with no transition; set it before the game starts. Only with `[Game] neutral weather` on and a pack that describes weather; an unknown name logs a warning. The stand-in for `ChangeWeather`, which cannot reach a neutral region yet. Not a setting (task 0127, stage W-S2). |
| `OPENEDEN_NEUTRAL_KEYS` | `skyrimse:<hex id>,...` gives the player those key forms (KEYM base ids) in a neutral session, read at each neutral new game: the stand-in for a key in the inventory until stage N5. With the key a locked door or container opens (and stays open for the session); without it the lock refuses. `OPENEDEN_NEUTRAL_DOOR_TRACE` also logs `NLOCK` (a lock decision) and `NLOCKED` (each locked door and container as its cell is inserted). Not a setting (task 0128). |
| `OPENEDEN_LOD_BAKE_WORLD` | An editor id (default `Tamriel`): the world `openmw --bake-lod-terrain` bakes in a session with a game pack and `[Game] neutral world` (task 0133). The pack goes to `<user data>/openeden/lod/<game>/<world>/terrain.edenpak`; use `WhiterunWorld` for Whiterun. Not a setting. |
| `OPENEDEN_FORCE_ANIM_VARIANT` | `<n>` (1 to 99) makes the random animation variant choices take variant n: the numbered hit, attack, death and swimattack groups (`CharacterController::chooseRandomGroup`, clamped to the highest existing number) and the wander idle number (`AiWander::getRandomIdle`, only if the package allows that idle). So two builds can be traced past the hit phase exactly. Unset, 0 or invalid: the random generator as before. Not a setting; developer only (task 0124, stage C2, `components/misc/forcedvariant.hpp`). |
