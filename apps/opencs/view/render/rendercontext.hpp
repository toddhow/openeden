#ifndef CSV_RENDER_RENDERCONTEXT_H
#define CSV_RENDER_RENDERCONTEXT_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <QTimer>

#include <vsg/core/ref_ptr.h>
#include <vsg/vk/Device.h>

namespace vsg
{
    class RenderPass;
    class Sampler;
    class WindowTraits;
}

namespace vsgQt
{
    class Viewer;
    class Window;
}

namespace vsgUtil
{
    class CompileContext;
    class DeletionQueue;
}

namespace Resource
{
    class ResourceSystem;
}

namespace CSMWorld
{
    class Data;
}

namespace CSVRender
{
    class SceneHost;

    /// What every 3D view of the editor shares: the viewer, the device, the render pass and the compile context. It drives
    /// the frames from one Qt timer on the main thread. The viewer does not run threads, so everything here and in
    /// SceneHost runs on the main thread, and the timer can never fire in the middle of a frame.
    ///
    /// Owned by whoever opens 3D views, and destroyed after all its SceneHosts and after any document whose
    /// ResourceSystem it created has no views left.
    class RenderContext
    {
    public:
        /// The one context of the process, created when first asked for, so a table-only session has no device.
        static RenderContext& get();

        /// Declare one before anything that opens a 3D view: the context goes after every view and document.
        class Lifetime
        {
        public:
            Lifetime() = default;
            ~Lifetime();
            Lifetime(const Lifetime&) = delete;
            Lifetime& operator=(const Lifetime&) = delete;
        };

        RenderContext();
        ~RenderContext();

        RenderContext(const RenderContext&) = delete;
        RenderContext& operator=(const RenderContext&) = delete;

        /// A window for one view, with the traits every view shares. Qt owns it through its container. Set its size and
        /// lay it out before constructing a SceneHost on it: a zero-size window is refused. Create the next window only
        /// after the previous SceneHost exists: vsgQt::Window copies the traits here, and those hold the shared device only
        /// from the first SceneHost on, so an earlier window would get a second device. SceneHost refuses one that has.
        vsgQt::Window* createWindow();

        /// The document's resources, created on the first call for the document. Needs a device, so only a SceneHost
        /// that has its window calls this.
        std::shared_ptr<Resource::ResourceSystem> resourcesFor(CSMWorld::Data& data);

        vsg::ref_ptr<vsgQt::Viewer> viewer() const;
        vsg::ref_ptr<vsgUtil::CompileContext> compileContext() const;

        /// Called by a SceneHost with its own window's Engine device once it has one.
        void deviceReady(vsg::ref_ptr<vsg::Device> device, bool supportsAnisotropy);
        vsg::ref_ptr<vsg::RenderPass> renderPass(vsg::Device* device, unsigned int samples, int presentLayout);
        vsg::ref_ptr<vsg::Device> device() const { return mDevice; }

        /// onFrame runs after each timer tick, whether or not it drew. For unattended captures.
        void setFrameCallback(std::function<void()> onFrame);
        void stop();

        /// The preference Rendering/framerate-limit: 0 draws at the timer's own pace.
        void setFrameRateLimit(int limit);

        /// The number of the frame last submitted, 0 before the first.
        std::uint64_t frameCount() const;

    private:
        friend class SceneHost;

        void add(SceneHost& host);
        void remove(SceneHost& host);
        void tick();

        vsg::ref_ptr<vsg::WindowTraits> mTraits;
        vsg::ref_ptr<vsgQt::Viewer> mViewer;
        int mFrameIntervalMs;
        vsg::ref_ptr<vsg::Device> mDevice;
        vsg::ref_ptr<vsg::RenderPass> mRenderPass;
        vsg::ref_ptr<vsg::Sampler> mSampler;
        bool mSupportsCompressedImages = false;
        vsg::ref_ptr<vsgUtil::CompileContext> mCompileContext;
        // Not an Engine's: a view can close while others stay open, and an Engine's compile context must not outlive it.
        // Advanced only after a frame has been submitted (DeletionQueue::sizeForPostFrameAdvance).
        std::unique_ptr<vsgUtil::DeletionQueue> mDeletionQueue;
        std::vector<SceneHost*> mHosts;
        QTimer mTimer;
        std::function<void()> mOnFrame;
    };
}

#endif
