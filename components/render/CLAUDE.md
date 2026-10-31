# components/render

Renderer pieces with no game types: `Render::Engine` (wraps `vsg::Viewer`), render passes and attachments, screenshots, and the small scene helpers shared by the game and the editor. Read the root `CLAUDE.md` first. `components/` never includes `apps/`, so nothing here names `MWWorld`, `MWBase` or a game visibility mask.

## Shared with the editor (task 0014, B2)

- `debuggeometry.*`: lines and triangles submitted as data. The rules (threading, fixed capacity, one geometry per producer) are in its header and in `apps/openmw/mwrender/CLAUDE.md`.
- `simplewater.hpp`: the flat water plane; reads `Water_SurfaceTexture` and `Water_Map_Alpha` from `Fallback::Map`.
- `intersect.hpp`: `nearestIntersection` sorts an intersector's hits and returns the nearest as point, normal, ratio and first `Anim::Transform`. Turning that transform into a game object or an editor instance is the caller's job (`apps/openmw/mwrender/intersect.hpp` in the game).

## Hosting `Render::Engine` on someone else's viewer (task 0014, B3)

- `Engine(viewer, window)`: one Engine per window over one viewer. Everything an Engine does but `compile`, `loop` and `createCompileContext` is about one window (`createRenderGraph`, `createCommandGraph`, `setRenderPass`, `screenshotOperation`), so per-window keeps those as they are; one Engine with several windows would have to add a window argument to each.
- The host drives frames and calls `endFrame()` afterwards. `loop()` is not for it: it blocks.
- `setup()` appends to the viewer's task rather than calling `Viewer::addRecordAndSubmitTaskAndPresentation`, which replaces every task and leaves earlier compile contexts uploading through a dead `TransferTask`.
- `setup()` restarts the viewer's threading, so the last Engine's `threading` argument decides for all of them.
- `endFrame()` is the only thing that advances a hosted deletion queue, and the host calls it only after `recordAndSubmit` has returned. A frame is stamped in `advanceToNextFrame`, and its fence wait comes later, in `RecordAndSubmitTask::start`, so never call it from an event handler, an update operation or a detach. The queue holds `max(numFrames, 3) + 1` frames: the fence ring is three long however few swapchain images there are (`vsgUtil::DeletionQueue::sizeForPostFrameAdvance`).

## Pitfalls

- **A render area larger than the swapchain renders nothing,** with no error: every pixel reads back as 0, alpha included. `createRenderGraph` has no resize handler, so whoever resizes the window sets `renderArea` (`RenderManager::updateExtents`). A window that grows looks fine but is only drawn in its old area; one that shrinks is blank.
