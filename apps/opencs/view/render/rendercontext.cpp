#include "rendercontext.hpp"

#include <algorithm>
#include <cstdlib>

#include <vsg/app/CommandGraph.h>
#include <vsg/app/CompileManager.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/WindowTraits.h>
#include <vsg/state/Sampler.h>
#include <vsg/state/ViewportState.h>
#include <vsg/vk/Device.h>
#include <vsg/vk/PhysicalDevice.h>

#include <vsg/ui/PointerEvent.h>
#include <vsgQt/Viewer.h>
#include <vsgQt/Window.h>

#include <components/render/attachmentformat.hpp>
#include <components/render/defaultsampler.hpp>
#include <components/render/renderpass.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/vsgadapters/nif/nif.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/deletionqueue.hpp>

#include <apps/opencs/model/world/data.hpp>

#include "scenehost.hpp"

namespace
{
    // The same bin numbers as the game's (apps/openmw/mwrender/bin.hpp), which the model readers write into nodes.
    constexpr int sBinDepthSorted = 0;
    constexpr int sBinCompute = 1;

    // The interval of the frame timer: frames are throttled by this and the swapchain.
    constexpr int sFrameIntervalMs = 8;
    // Only the swapchain's size counts here: a view sets its window's real size before use.
    constexpr uint32_t sInitialWindowSize = 640;
    constexpr int sMillisecondsPerSecond = 1000;

    std::unique_ptr<CSVRender::RenderContext> sContext;

    /// vsg::Viewer::removeWindow rebuilds every task, which leaves the compile contexts uploading through a dead
    /// TransferTask (research note 0003, section 5.4). This takes one window out of the tasks it is in and leaves them, so
    /// a view can close while others stay, and vsgQt::Window's own cleanup comes through here too.
    class SharedViewer : public vsgQt::Viewer
    {
    public:
        void removeWindow(vsg::ref_ptr<vsg::Window> window) override
        {
            auto found = std::find(_windows.begin(), _windows.end(), window);
            if (found == _windows.end())
                return;
            deviceWaitIdle();
            _windows.erase(found);
            const auto eraseWindow
                = [&](vsg::Windows& list) { list.erase(std::remove(list.begin(), list.end(), window), list.end()); };
            for (auto& task : recordAndSubmitTasks)
            {
                eraseWindow(task->windows);
                auto& graphs = task->commandGraphs;
                graphs.erase(std::remove_if(graphs.begin(), graphs.end(),
                                 [&](const vsg::ref_ptr<vsg::CommandGraph>& graph) { return graph->window == window; }),
                    graphs.end());
            }
            for (auto& presentation : presentations)
                eraseWindow(presentation->windows);
        }
    };
}

namespace CSVRender
{
    RenderContext::RenderContext()
        : mTraits(vsg::WindowTraits::create())
        , mViewer(new SharedViewer)
        , mFrameIntervalMs(sFrameIntervalMs)
        , mDeletionQueue(std::make_unique<vsgUtil::DeletionQueue>(vsgUtil::DeletionQueue::sizeForPostFrameAdvance(3)))
    {
        // As Engine::createRenderEngine in the game (apps/openmw/vsgengine.cpp).
        // With multisampling, the depth resolve needs these, as in the game.
        if (Settings::video().mAntialiasing)
        {
            mTraits->deviceExtensionNames.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
            mTraits->deviceExtensionNames.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
            mTraits->deviceExtensionNames.push_back(VK_KHR_MAINTENANCE_2_EXTENSION_NAME);
            mTraits->deviceExtensionNames.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
        }
        mTraits->width = sInitialWindowSize;
        mTraits->height = sInitialWindowSize;
        mTraits->queueFlags |= VK_QUEUE_COMPUTE_BIT;
        mTraits->depthFormat = Render::compatibleDepthFormat;
        mTraits->swapchainPreferences.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // Render::Screenshot
        mTraits->depthImageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        mTraits->swapchainPreferences.surfaceFormat.format = Render::compatibleColorFormat;
        if (const int antialiasing = Settings::video().mAntialiasing)
            mTraits->samples = static_cast<VkSampleCountFlagBits>(antialiasing);
        mTraits->deviceFeatures = vsg::DeviceFeatures::create();

        // On demand, as the OSG editor's renderer was: a view asks for a frame in tick().
        mViewer->continuousUpdate = false;
        QObject::connect(&mTimer, &QTimer::timeout, [this] { tick(); });
    }

    RenderContext::~RenderContext()
    {
        stop();
        if (mDevice)
            mViewer->deviceWaitIdle();
    }

    vsgQt::Window* RenderContext::createWindow()
    {
        return new vsgQt::Window(mViewer, mTraits);
    }

    void RenderContext::deviceReady(vsg::ref_ptr<vsg::Device> device, bool supportsAnisotropy)
    {
        if (mDevice)
            return;
        mDevice = device;
        // Later windows must share this device: a second one could not use the same pipelines or images.
        mTraits->device = device;
        auto physicalDevice = device->getPhysicalDevice();
        mSampler = Render::createDefaultSampler(vsg::ref_ptr<vsg::PhysicalDevice>(const_cast<vsg::PhysicalDevice*>(physicalDevice)), supportsAnisotropy);
        mSupportsCompressedImages = physicalDevice->getFeatures().textureCompressionBC;
        // A debugging override shared with the game (apps/openmw/vsgengine.cpp).
        if (const char* overrideString = std::getenv("VSGOPENMW_DECOMPRESS_BC"))
            mSupportsCompressedImages = overrideString == std::string("0");

        mCompileContext = vsg::ref_ptr{ new vsgUtil::CompileContext };
        mCompileContext->device = device;
        mCompileContext->compileManager = vsg::CompileManager::create(*mViewer, vsg::ref_ptr<vsg::ResourceHints>());
        mCompileContext->onCompiled = [this](const vsg::CompileResult& result) { vsg::updateViewer(*mViewer, result); };
        mCompileContext->onDetached = [this](vsg::ref_ptr<vsg::Object> object) { mDeletionQueue->add(object); };
    }

    vsg::ref_ptr<vsg::RenderPass> RenderContext::renderPass(vsg::Device* device, unsigned int samples, int presentLayout)
    {
        // The game's createMainRenderPass (apps/openmw/mwrender/renderpass.hpp). All windows use one, so that a pipeline
        // compiled for one view is valid in all of them.
        if (!mRenderPass)
            mRenderPass = Render::createMultisampledRenderPass(device, Render::compatibleColorFormat,
                Render::compatibleDepthFormat, static_cast<VkSampleCountFlagBits>(samples), false, true,
                static_cast<VkImageLayout>(presentLayout));
        return mRenderPass;
    }

    void RenderContext::addCompileTarget(vsg::Window& window, const vsg::ResourceRequirements& requirements)
    {
        if (mHasCompileTarget)
            return;
        mHasCompileTarget = true;
        mCompileContext->compileManager->add(window, vsg::ref_ptr<vsg::ViewportState>(), requirements);
    }

    std::shared_ptr<Resource::ResourceSystem> RenderContext::resourcesFor(CSMWorld::Data& data)
    {
        const bool created = data.getResourceSystem() == nullptr;
        auto resources
            = data.getOrCreateResourceSystem(mSampler, mSupportsCompressedImages, sBinCompute, sBinDepthSorted);
        // The editor shows editor markers, which the game hides.
        if (created)
            resources->nifReader->showMarkers = true;
        return resources;
    }

    vsg::ref_ptr<vsgQt::Viewer> RenderContext::viewer() const
    {
        return mViewer;
    }

    vsg::ref_ptr<vsgUtil::CompileContext> RenderContext::compileContext() const
    {
        return mCompileContext;
    }

    void RenderContext::setFrameCallback(std::function<void()> onFrame)
    {
        mOnFrame = std::move(onFrame);
    }

    void RenderContext::setFrameRateLimit(int limit)
    {
        mFrameIntervalMs = limit > 0 ? std::max(sMillisecondsPerSecond / limit, 1) : sFrameIntervalMs;
        if (mTimer.isActive())
            mTimer.setInterval(mFrameIntervalMs);
    }

    void RenderContext::stop()
    {
        mTimer.stop();
    }

    std::uint64_t RenderContext::frameCount() const
    {
        const auto* stamp = mViewer->getFrameStamp();
        return stamp ? stamp->frameCount : 0;
    }

    void RenderContext::add(SceneHost& host)
    {
        mHosts.push_back(&host);
        if (!mTimer.isActive())
            mTimer.start(mFrameIntervalMs);
    }

    void RenderContext::remove(SceneHost& host)
    {
        mHosts.erase(std::remove(mHosts.begin(), mHosts.end(), &host), mHosts.end());
    }

    void RenderContext::tick()
    {
        // With no view there is nothing a viewer could draw or present. The tasks stay for the next view.
        if (mHosts.empty())
        {
            if (mOnFrame)
                mOnFrame();
            return;
        }

        bool anyWindow = false;
        for (SceneHost* host : mHosts)
        {
            host->update();
            anyWindow = anyWindow || host->hasWindow();
            if (host->needsFrame())
                mViewer->request();
        }

        if (anyWindow)
            mViewer->render();

        // Only here, after recordAndSubmit has returned: see Render::Engine::endFrame.
        if (const auto* stamp = mViewer->getFrameStamp())
            mDeletionQueue->advanceToFrame(stamp->frameCount);
        for (SceneHost* host : mHosts)
            host->endFrame();

        if (mOnFrame)
            mOnFrame();
    }
}

CSVRender::RenderContext& CSVRender::RenderContext::get()
{
    if (!sContext)
        sContext = std::make_unique<RenderContext>();
    return *sContext;
}

CSVRender::RenderContext::Lifetime::~Lifetime()
{
    sContext.reset();
}
