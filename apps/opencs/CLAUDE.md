# apps/opencs

The editor (OpenMW-CS), being ported from OpenSceneGraph to VulkanSceneGraph in task 0014, stages C1 to C9. Read the root `CLAUDE.md`, `components/render/CLAUDE.md` and the task's stage log first.

## What builds now

- `csmodel`: everything under `model/`, plus three plain Qt widgets it uses. `openmw-cs-tests` links only this, so it needs no view code and no GPU.
- `openmw-cs-lib`: the table views, documents and dialogues, plus `view/render/rendercontext.*`, `scenehost.*` and `capture.*`.
- **Staged list:** `CMakeLists.txt` lists the `view/render` units not compiled yet, in a comment. Each stage moves its units back. `view/world/scenesubview` and `previewsubview` are off until then, and `OPENEDEN_CS_VIEW_RENDER_PENDING` guards the code naming them. Remove the define with the last stage.
- OSG files in `view/render` stay on disk unbuilt until stage D1.

## The 3D design

- **`CSVRender::RenderContext`** is shared: viewer, device, render pass, default sampler, compile context and deletion queue. It drives frames from one `QTimer` on the main thread, in the order host updates, `vsgQt::Viewer::render`, deletion queue and `Engine::endFrame`, then the frame callback.
- **`CSVRender::SceneHost`** is one per view: window, `Render::Engine`, scene graph, camera and view descriptors. It is the editor's counterpart of `MWRender::RenderManager`, which is game-bound and cannot be used here.
- **`CSMWorld::Data`** owns the document's `Resource::ResourceSystem` (pipelines, texture and model caches), created by the first view through `getOrCreateResourceSystem` because it needs a device. It is null before that, so model code must not assume it.
- **The compile context is not an Engine's:** an Engine's compile context must not outlive that Engine, and a view can close while others stay. `RenderContext` builds its own.
- **Settings:** `Settings::Manager::load(cfg, true)` now also loads the game's `defaults.cfg` and `StaticValues`, as the shared renderer reads `Settings::shaders()` and others. The editor sees them at their defaults, or as overridden in `openmw-cs.cfg`; it does not read the game's `settings.cfg`.

## Capture

`openmw-cs --open-cell X --auto-screenshot N --auto-quit M [--camera ex,ey,ez,cx,cy,cz,ux,uy,uz] [--capture-views V]`, plus the usual `--replace=config --config DIR --user-data DIR`. `capture.cpp` runs it before `CS::Editor` exists, so there is no editor window, dialogue or IPC server. The one window takes no focus. It opens the configuration's `content=` files, and writes `screenshots/editor<i>.png` in the user data folder. X is a record ID (`#-2 -9`) or a cell name; a name shared by exterior cells takes the first. Point `--config` at a scratch folder: the settings state belongs to that folder.

## Rules

- All 3D work runs on the main thread. `setup()`'s threading argument is false, and the last Engine's value decides for the viewer.
- Advance deletion queues only after `Viewer::render` returns. See `components/render/CLAUDE.md`.
- `scenehost.cpp` copies the game's bin numbers. They must stay equal to `apps/openmw/mwrender/bin.hpp`, as the model readers write them into nodes.

## Pitfalls

- **Create a window only after the previous SceneHost exists.** `vsgQt::Window`'s constructor copies the traits, which hold the shared device only from the first SceneHost on. A window made earlier gets a second device, and the second `Engine::setup` then hung with no message. `SceneHost` now throws on a mismatch.
- **`vsgQt/Window.h` does not include what it uses:** include `vsg/ui/PointerEvent.h` before it, or MSVC fails on `ButtonMask`.
- **Medians for a default view:** Morrowind interiors can keep a few objects far outside the room, so a bounding box of the references frames empty space.
- **`view/render/mask.hpp`** is still the OSG build's list. Only `Mask_ParticleSystem` is in use on VSG so far; the rest belongs to C3 to C9, which decide how masks map to `vsg::Switch` and view masks.
- **Git Bash runs need Qt's `bin` on `PATH`.** Without it, an executable linking Qt exits with 127 and a misleading message about `api-ms-win-crt-locale`.
