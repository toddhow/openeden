#ifndef VSGOPENMW_RENDER_ENGINE_H
#define VSGOPENMW_RENDER_ENGINE_H

#include <functional>
#include <memory>
#include <vector>

#include <vsg/core/ref_ptr.h>
#include <vsg/vk/vulkan.h>

#include "devicereport.hpp"
#include "framecapture.hpp"
#include "framecounters.hpp"
#include "vramsource.hpp"

namespace vsg
{
    class Device;
    class DeviceFeatures;
    class PhysicalDevice;
    class ImageView;
    class CommandGraph;
    class SecondaryCommandGraph;
    class Window;
    class WindowTraits;
    class Data;
    class RenderGraph;
    class RenderPass;
    class Viewer;
}
namespace vsgUtil
{
    class DeletionQueue;
    class CompileContext;
    class DebugMessenger;
}
namespace Render
{
    struct Screenshot;
    class Headless;
    class GpuTimer;

    /*
     * Encapsulates vsg::Viewer.
     */
    class Engine
    {
    public:
        Engine(vsg::ref_ptr<vsg::Window> window);
        using Callback = std::function<void(vsg::ref_ptr<vsg::Data> pixels)>;
        Engine(Callback headlessCallback, vsg::ref_ptr<vsg::WindowTraits> traits);
        /*
         * Hosted: one Engine for one window of a viewer that the caller owns and drives, such as vsgQt::Viewer with its
         * QTimer. Several windows on one device take one Engine each over the same viewer. The window must already be
         * laid out with a non-zero size and ask for compatibleColorFormat (a capture refuses sRGB swapchains);
         * otherwise this throws std::invalid_argument, as setup() does if the surface does not offer that format.
         * loop() throws std::logic_error; after each frame the caller calls endFrame(). createRenderGraph() installs no
         * resize handler, so on a resize the caller sets each graph's renderArea to swapchainExtent() once per frame,
         * as RenderManager::syncExtents does: a render area larger than the swapchain draws nothing at all, and the
         * swapchain is only rebuilt when the next frame acquires its image. Must be destroyed after the compile
         * contexts it creates, whose callbacks refer to it.
         */
        Engine(vsg::ref_ptr<vsg::Viewer> viewer, vsg::ref_ptr<vsg::Window> window);
        ~Engine();

        bool useFrameQueue = true; // If set to false, prevents queued frames in the swapchain by inserting a
                                   // vkDeviceWaitIdle call in between frames.
        // Ignored by a hosted engine, whose viewer's owner presents and decides whether to wait.

        // Hosted, each call adds this window's graphs to the viewer, which keeps those already there; call compile()
        // again after it, then add the window's compile views to the compile manager (see appendToViewer in
        // engine.cpp). Threading applies to the whole viewer, so the last Engine's call to setup() decides it for every
        // window.
        void setup(std::vector<vsg::ref_ptr<vsg::CommandGraph>> graphs, bool threading);
        void compile();

        void updateExtents(const VkExtent2D& extent);
        // The extent the swapchain images have now, which differs from extent2D() between updateExtents and the rebuild
        // that the next frame's image acquisition does. Render areas must follow this one (RenderManager::syncExtents).
        VkExtent2D swapchainExtent();
        VkSampleCountFlagBits framebufferSamples();
        void setRenderPass(vsg::ref_ptr<vsg::RenderPass> renderPass);

        using FrameCallback = std::function<void(double dt, bool& requiresRender, bool& quit)>;
        void loop(FrameCallback cb, float framerateLimit = 0);
        // Hosted: the per-frame work loop() does beyond what the viewer does itself, which is freeing what has been
        // detached. Call it only between frames, after the viewer's recordAndSubmit has returned: the viewer stamps a
        // frame in advanceToNextFrame and waits on the fence only in recordAndSubmit (RecordAndSubmitTask::start), so
        // counting a stamped frame any earlier frees objects a queued frame still uses. Safe whether or not a frame was
        // drawn, and from every Engine of a viewer.
        void endFrame();

        // Headless: the engine's image. With a window: the image presented last, which Vulkan forbids reading before it
        // is acquired again (Screenshot(window)); nothing in OpenEden takes that one any more (the game and the editor
        // copy inside the frame: setScreenshotGraph). Null while the window has zero size (a collapsed splitter) and
        // while imageIndex(1) names no image (before the second frame, and right after a swapchain rebuild).
        vsg::ref_ptr<Screenshot> screenshotOperation();
        // A copy of the window's image recorded at the end of graph in every frame that asks for one (OpenEden,
        // 2026-11-04; FrameCapture). Call before setup(); the engine keeps it and runs it from loop(), or from
        // drawFrameAndCapture for a hosted engine. Null without a window.
        FrameCapture* addFrameCapture(vsg::ref_ptr<vsg::CommandGraph> graph, DownloadSync sync);
        // The screenshots: graph is the last command graph to write the window's image in a frame (the game's GUI
        // graph, an editor view's only graph); the copy is recorded at its end, before present.
        void setScreenshotGraph(vsg::ref_ptr<vsg::CommandGraph> graph);
        // Copies the next frame that loop() records (main thread, from the frame callback) and calls done with it once
        // the copy has finished, on the main thread in a later frame; done names the file and queues the write. Never
        // called for a request that is dropped (the window had no image for FrameCapture::sMaxWaitFrames frames, or
        // ScreenshotCopyBudget's cap of copies was reached), each with a warning. Headless: the image is copied at
        // once. Requests before the copy is made are one picture.
        using ScreenshotDone = std::function<void(vsg::ref_ptr<Screenshot>)>;
        void requestScreenshot(ScreenshotDone done);
        // Hosted (the editor): draws one frame of the viewer, as vsgQt::Viewer::render does, with the screenshot copy
        // recorded in it before present, waits for the device and returns the copied screenshot (null when it could
        // not be made, with the reason in the log). Main thread, outside the viewer's own frame. Needs
        // setScreenshotGraph.
        vsg::ref_ptr<Screenshot> drawFrameAndCapture();
        size_t numFrames() const;
        vsg::ref_ptr<vsg::ImageView> colorImageView(size_t relativeFrameIndex);
        vsg::ref_ptr<vsg::ImageView> depthImageView();

        VkExtent2D extent2D() const;
        vsg::ref_ptr<vsg::PhysicalDevice> getOrCreatePhysicalDevice();
        vsg::ref_ptr<vsg::Device> getOrCreateDevice();
        vsg::ref_ptr<vsg::Device> getDevice();
        vsg::ref_ptr<vsgUtil::CompileContext> createCompileContext();
        vsg::ref_ptr<vsg::CommandGraph> createCommandGraph(int maxDescriptorSet);
        vsg::ref_ptr<vsg::SecondaryCommandGraph> createSecondaryCommandGraph(
            vsg::ref_ptr<vsg::RenderGraph> inherit_framebuffer, int maxDescriptorSet);
        vsg::ref_ptr<vsg::RenderGraph> createRenderGraph();
        const VkPhysicalDeviceFeatures& getEnabledFeatures() const;
        VkImageLayout getPresentLayout() const;

        // Whether frames go to a window's swapchain, as opposed to the headless engine's image: the linear HDR path
        // (task 0025) writes the swapchain through a pass of its own and needs one.
        bool hasWindow() const { return mWindow != nullptr; }

        // GPU timing (task 0023, F0a). Creates the timer on the first call and returns it, or returns null, and says
        // why in the log once, when the device cannot time passes. Call before the command graphs are built; whoever
        // builds them adds the passes to the timer. Never called when [Rendering] gpu timing is off: then there is no
        // query pool and loop() records and reads nothing. When there is a timer, loop() reads its results each frame,
        // adds a column per pass to the OPENEDEN_FRAME_STATS_FILE, and prints the summary when it ends.
        GpuTimer* enableGpuTiming();
        GpuTimer* gpuTimer() const { return mGpuTimer.get(); }

        // What the machine's Vulkan supports (task 0023, F0c): asks the device, so it takes a moment; call it once, at
        // start.
        DeviceReport deviceReport();

        // The device-local memory in MiB (task 0049), from the game's own physical device; 0 if it cannot be asked.
        uint64_t deviceLocalMiB();
        // Task 0066 stage e: whether the physical device is a CPU implementation of Vulkan (llvmpipe, SwiftShader).
        bool deviceIsSoftware();

        // Task 0054 (S0): the device-local memory this process uses in MiB, from VK_EXT_memory_budget, or -1 when the
        // device does not have it.
        int64_t deviceLocalUsageMiB();

        // Task 0080: the driver's budget and this process's usage in bytes, each summed over the device-local heaps
        // from VK_EXT_memory_budget; both -1 when the device does not have the extension. Call on the main thread.
        VideoMemory::Sample deviceLocalMemorySample();

        // Task 0080: hands the device-memory pool entries that hold nothing back to the driver
        // (vsg::MemoryBufferPools::releaseUnusedPools, VSG 1.1.16: it takes the pools' mutex and frees only an entry
        // whose reserved size is 0 and that nothing else references, so a block a live or still-queued image uses is
        // never touched; compile threads reserving memory meanwhile wait on that mutex). Main thread, between frames
        // (from RenderManager::update). Returns the bytes released, -1 when the device has no pools yet.
        int64_t releaseUnusedDeviceMemory();

        // Task 0080 stage d: every command graph of the viewer's record-and-submit tasks (the texture mip swapper scans them).
        std::vector<vsg::ref_ptr<vsg::CommandGraph>> allCommandGraphs() const;

        // Task 0054 (S0): named numbers that loop() adds to the OPENEDEN_FRAME_STATS_FILE as columns. Whoever wants a
        // column registers it before loop() starts, and only when [Rendering] frame counters is on; with none
        // registered the file is exactly as before.
        FrameCounters& frameCounters() { return mFrameCounters; }

    private:
        vsg::ref_ptr<vsg::WindowTraits> mTraits;
        vsg::ref_ptr<vsg::Viewer> mViewer;
        vsg::ref_ptr<vsg::Window> mWindow;
        bool mHosted = false;
        std::unique_ptr<Headless> mHeadless;
        std::unique_ptr<vsgUtil::DeletionQueue> mDeletionQueue;
        std::unique_ptr<vsgUtil::DebugMessenger> mDebugMessenger;
        // At most this many copies exist at once (OpenEden, 2026-11-04): from the frame that records one until its
        // pixels have been read. A screenshot every frame would otherwise hold one window-sized host-visible image per
        // frame until the writer thread catches up.
        static constexpr std::size_t sMaxScreenshotCopies = 8;
        ScreenshotCopyBudget mScreenshotBudget{ sMaxScreenshotCopies };
        // After the viewer and the window, so they go before them (their copies are images on their device), and after
        // the budget they refer to. The first (if any) is the screenshots' (mScreenshotCapture).
        std::vector<std::unique_ptr<FrameCapture>> mFrameCaptures;
        FrameCapture* mScreenshotCapture = nullptr;
        // Declared last, so it is destroyed first; it holds a reference to the device.
        std::unique_ptr<GpuTimer> mGpuTimer;
        FrameCounters mFrameCounters;
        bool mGpuTimingTried = false;
    };
}

#endif
