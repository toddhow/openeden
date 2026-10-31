# apps/opencs

The editor (OpenMW-CS), ported from OpenSceneGraph to VulkanSceneGraph in task 0014, stages C1 to C9. Read the root `CLAUDE.md`, `components/render/CLAUDE.md` and the task's stage log first.

## What builds now

- `csmodel`: everything under `model/`, plus three plain Qt widgets it uses. `openmw-cs-tests` links only this, so it needs no view code and no GPU.
- `openmw-cs-lib`: everything else, with `view/render` whole.

## The 3D design

- **`CSVRender::RenderContext`** is shared, one per process (`get()`, with a `Lifetime` in `main.cpp`): viewer, device, render pass, default sampler, compile context and deletion queue. It drives frames from one `QTimer` on the main thread, in the order host updates, `vsgQt::Viewer::render`, deletion queue and `Engine::endFrame`, then the frame callback. The timer runs always; a frame is drawn only when a view asks.
- **`RenderWidget`** makes its window and `SceneHost` the first time it is shown with a size. Whatever needs them goes in `hostReady()`, and methods that can run earlier check `getHost()`. Events reach the window, and an event filter sends them on to the widget.
- **`CSVRender::SceneHost`** is one per view: window, `Render::Engine`, scene graph, camera and view descriptors. It is the editor's counterpart of `MWRender::RenderManager`, which is game-bound and cannot be used here.
- **`CSMWorld::Data`** owns the document's `Resource::ResourceSystem` (pipelines, texture and model caches), created by the first view through `getOrCreateResourceSystem` because it needs a device. It is null before that, so model code must not assume it.
- **The compile context is not an Engine's:** an Engine's compile context must not outlive that Engine, and a view can close while others stay. `RenderContext` builds its own.
- **Settings:** `Settings::Manager::load(cfg, true)` now also loads the game's `defaults.cfg` and `StaticValues`, as the shared renderer reads `Settings::shaders()` and others. The editor sees them at their defaults, or as overridden in `openmw-cs.cfg`; it does not read the game's `settings.cfg`.

## Capture

`openmw-cs --open-cell X --auto-screenshot N --auto-quit M [--camera ex,ey,ez,cx,cy,cz,ux,uy,uz] [--camera-script "cmd;..."] [--capture-views V]`, plus the usual `--replace=config --config DIR --user-data DIR`. `capture.cpp` runs it before `CS::Editor` exists, so there is no editor window, dialogue or IPC server, but it uses the worldspace widgets themselves. Also `--lighting day|night|bright`, `--fov`, `--neighbours N`, `--capture-resize WxH,...` and `--capture-close-views N`. The one window takes no focus. It opens the configuration's `content=` files, and writes `screenshots/editor<i>.png` in the user data folder. X is a record ID (`#-2 -9`) or a cell name; a name shared by exterior cells takes the first. Point `--config` at a scratch folder: the settings state belongs to that folder.

## Rules

- All 3D work runs on the main thread. `setup()`'s threading argument is false, and the last Engine's value decides for the viewer.
- Advance deletion queues only after `Viewer::render` returns. See `components/render/CLAUDE.md`.
- `scenehost.cpp` copies the game's bin numbers. They must stay equal to `apps/openmw/mwrender/bin.hpp`, as the model readers write them into nodes.

## Pitfalls

- **Create a window only after the previous SceneHost exists.** `vsgQt::Window`'s constructor copies the traits, which hold the shared device only from the first SceneHost on. A window made earlier gets a second device, and the second `Engine::setup` then hung with no message. `SceneHost` now throws on a mismatch.
- **`vsgQt/Window.h` does not include what it uses:** include `vsg/ui/PointerEvent.h` before it, or MSVC fails on `ButtonMask`.
- **Medians for a default view:** Morrowind interiors can keep a few objects far outside the room, so a bounding box of the references frames empty space.
- **Masks:** `mask.hpp` is still the OSG build's list. On VSG a scene element hangs under `SceneHost::maskNode(mask, node)`, a `vsg::Switch`, and `setVisibilityMask` sets the view's traversal mask.
- **`TerrainStorage` serves one cell.** The alteration hooks have no cell argument, so each `Cell` has its own storage and `Terrain::Paging`, and asks for a batch of one cell.
- **`SimpleWater` follows the camera unless told otherwise:** a cell's water passes `followCamera = false`.
- **Debugging a crash here:** the crash catcher did not write a dump for a capture. A small Win32 debug-API runner that writes a minidump at the first access violation, read with a dbghelp stack walker, found each one quickly. `std::cerr` output through `Debug` loses first characters, so trace with `Log`.
- **Git Bash runs need Qt's `bin` on `PATH`.** Without it, an executable linking Qt exits with 127 and a misleading message about `api-ms-win-crt-locale`.
- **Inside a class derived from `TagBase`, a plain `Object` means `vsg::Object`,** the base's injected class name. Write `CSVRender::Object`.
- **`QApplication::closeAllWindows` stops at the first window that refuses.** `CSVDoc::View` refuses all but a document's last, deleting itself later, so an unattended close repeats it.
- **Unattended normal run:** `OPENEDEN_CS_UI_CHECK="c:#-2 -9;5000" openmw-cs path/Morrowind.esm`. Windows do not take focus.
- **On-demand frames:** a view draws when asked (`requestRedraw`) or when exposed and a `FrameListener` says `isAnimated`, which means an object with auto-play controllers. A controller that is not auto-play still runs, with `MWAnim::ObjectAnimation::addController`: skins are such controllers, and dropping them draws a skinned model in pieces.
- **`CompileManager` has no remove.** Add a target once, without a view (`RenderContext::addCompileTarget`), never per view.
- **Camera:** a controller works on the view matrix through `camerapose.hpp`, in eye space, which is the same as OSG's. Do not flip Y there: the flip is in the projection. A capture's `--camera-script` (`capture.cpp`, `runCameraScript`) calls the controllers and logs each pose.
- **Picking:** `picking.hpp`, without a window, and `Picking::setTag` on a node marks what it covers. A pickable node must have no `NullArrayState`, and a pipeline with VERTEX at location 0. A triangle's facing counts, so keep quads counter-clockwise seen from their front, also with no culling. In a `--camera-script`, `wait` before a `ray` lets markers follow the camera.
- **Overlays:** `OverlayLines` for lines that change often, `ColouredOptions` for the rest. A tool's helper turns `mPickable` off, or a click lands on it. Use `vsg::ComputeBounds` with `useNodeBounds = false`, or a node's bounding sphere gives a box far too big.
- **`vsg::dquat`'s product has OSG's order:** `a * b` is a, then b. Do not swap operands when porting.
- **Scripted edits:** `--camera-script` drives the edit modes (`capture.cpp`). A scene widget needs its selectors made, or `getVisibilityMask` crashes on null, so the capture has a hidden toolbar. `--capture-preview ID` captures a `PreviewWidget` of a referenceable.
- **A terrain rebuild goes through `Paging::clearCache`:** the builder keeps batch data by bounds. A changed set of land textures also needs `setStorage` again (`Cell::mUpdateLayers`).
- **Edit modes outlive the view:** they belong to the toolbar. What they hold in the view checks `SceneHost::aliveToken()`.
- **No terrain rays in a loop:** each ray recomputes a chunk's vertices. A brush outline reads the land (`BrushDraw::getTerrainHeight`).
