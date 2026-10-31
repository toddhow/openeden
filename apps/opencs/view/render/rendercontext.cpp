#include "rendercontext.hpp"

#include <algorithm>
#include <cstdlib>

#include <vsg/app/CompileManager.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/WindowTraits.h>
#include <vsg/state/Sampler.h>
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
}

namespace CSVRender
{
    RenderContext::RenderContext()
        : mTraits(vsg::WindowTraits::create())
        , mViewer(vsgQt::Viewer::create())
        , mDeletionQueue(std::make_unique<vsgUtil::DeletionQueue>(vsgUtil::DeletionQueue::sizeForPostFrameAdvance(3)))
    {
        // As Engine::createRenderEngine in the game (apps/openmw/vsgengine.cpp).
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

        mViewer->continuousUpdate = true;
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

    void RenderContext::start(std::function<void()> onFrame)
    {
        mOnFrame = std::move(onFrame);
        mTimer.start(sFrameIntervalMs);
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
    }

    void RenderContext::remove(SceneHost& host)
    {
        mHosts.erase(std::remove(mHosts.begin(), mHosts.end(), &host), mHosts.end());
    }

    void RenderContext::tick()
    {
        for (SceneHost* host : mHosts)
            host->update();

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
