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

- **A render area larger than the framebuffer renders nothing,** with no error without the validation layer (VUID-VkRenderPassBeginInfo-pNext-02852): the game then stays blank, not only that frame. The swapchain is rebuilt at the next frame's image acquisition, after the window event that asked for the new size, so never set `renderArea` from the event or from `extent2D()`, which already holds the new size. Set it from `Engine::swapchainExtent()` each frame (`RenderManager::syncExtents`). Graphs with a window or framebuffer need `windowResizeHandler = nullptr`, or VSG scales the area again by new/old.

## GPU timing, the device report and Slang shaders (task 0023)

- **`gputiming.*`:** `[Rendering] gpu timing` (off by default). `Engine::enableGpuTiming()` makes a `GpuTimer` only when asked, so with the setting off there is no query pool and `loop()` records and reads nothing. Whoever builds a command graph wraps a pass with `GpuTimer::beginNode(pass)` and `endNode(pass)` (`timeChild` in `mwrender/rendermanager.cpp`). Rules:
  - The begin node resets the pass's two queries, and a reset is not allowed inside a render pass instance, so a pass is timed from a command graph's children or inside `SuspendRenderPass`, never from inside a render graph.
  - Passes are registered before the first frame (the frame stats file lists them in its header when `loop()` starts); the table holds 16.
  - Results are read three frames after recording, on the main thread, without waiting (`VK_QUERY_RESULT_WITH_AVAILABILITY_BIT`); a pass the GPU has not finished is skipped. The query ring has four frame slots so the slot being read is never the slot being recorded.
  - The frame stats file gains `gpu_frame` (the frame the GPU times belong to) and one column per pass only when timing is on; off, its header and lines are exactly as before.
- **`devicereport.*`:** `[Rendering] device report` writes the report to the log and `device-report.json`. The probe makes its own instance at the loader's version, because the questions need Vulkan 1.1 calls. The game's own instance is already at the loader's version (VSG's `WindowTraits` asks the loader), not Vulkan 1.0. Every entry is `present...`, `absent` or `unknown (...)`; a missing item never throws. `DeviceReport::add` has a `const char*` overload on purpose: without it a string literal picks the `bool` one.
- **Slang:** `[Rendering] slang shaders` (off by default) makes `Pipeline::Graphics::createShaderStage` swap the GLSL module for `<path>/main.<stage>.spv` when it exists. The GLSL is still read first, for its stage; it stays the fallback. The `.spv` files come from `openeden_add_slang_shader()` (`files/shaders/CMakeLists.txt`), which runs the `slangc` that `extern/CMakeLists.txt` downloads. Use `SV_VulkanVertexID`, not `SV_VertexID` (the latter needs the draw-parameters feature). `apps/components_tests/vsg/testslangshaders.cpp` compares a Slang stage's declarations with its GLSL's; extend it for each ported shader.
