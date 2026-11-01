#ifndef VSGOPENMW_RENDER_ENGINE_H
#define VSGOPENMW_RENDER_ENGINE_H

#include <functional>
#include <memory>
#include <vector>

#include <vsg/core/ref_ptr.h>
#include <vsg/vk/vulkan.h>

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

        // Hosted, null while the window has zero size (a collapsed splitter): capturing it would crash.
        vsg::ref_ptr<Screenshot> screenshotOperation();
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

    private:
        vsg::ref_ptr<vsg::WindowTraits> mTraits;
        vsg::ref_ptr<vsg::Viewer> mViewer;
        vsg::ref_ptr<vsg::Window> mWindow;
        bool mHosted = false;
        std::unique_ptr<Headless> mHeadless;
        std::unique_ptr<vsgUtil::DeletionQueue> mDeletionQueue;
        std::unique_ptr<vsgUtil::DebugMessenger> mDebugMessenger;
    };
}

#endif
