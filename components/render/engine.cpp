#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <vsg/app/RecordAndSubmitTask.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/app/SecondaryCommandGraph.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/WindowAdapter.h>
#include <vsg/commands/Commands.h>
#include <vsg/core/Exception.h>
#include <vsg/vk/Device.h>
#include <vsg/vk/Fence.h>
#include <vsg/vk/Surface.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/debugmessenger.hpp>
#include <components/vsgutil/deletionqueue.hpp>
#include <components/vsgutil/texturecap.hpp>

#include "attachmentformat.hpp"
#include "devicereport.hpp"
#include "gputiming.hpp"
#include "headless.hpp"
#include "limitframerate.hpp"
#include "screenshot.hpp"

namespace Render
{
    namespace
    {
        /*
         * Adds a hosted window's graphs to the viewer's existing task for its device and queues.
         *
         * Not Viewer::addRecordAndSubmitTaskAndPresentation, because that replaces every task (VSG 1.1.16,
         * Viewer::assignRecordAndSubmitTaskAndPresentation). Compile contexts made earlier keep the old task's
         * TransferTask, and Device::transferTask stays on the first task's (RecordAndSubmitTask's constructor), so
         * nothing they compile would upload any more: the "geometry never uploads" failure of research note 0003,
         * section 5.4. Appending to the task keeps its TransferTask. Compile views of a later window are therefore
         * added with CompileManager::add after setup(), as RenderManager does in the game.
         */
        void appendToViewer(vsg::Viewer& viewer, vsg::Window& window, const vsg::CommandGraphs& graphs)
        {
            if (graphs.empty())
                return;
            const vsg::ref_ptr<vsg::Device> device = window.getOrCreateDevice();
            const auto& first = *graphs.front();
            for (auto& task : viewer.recordAndSubmitTasks)
            {
                if (task->device != device || static_cast<int>(task->queue->queueFamilyIndex()) != first.queueFamily)
                    continue;
                for (auto& presentation : viewer.presentations)
                {
                    auto& windows = presentation->windows;
                    // A host that closed its last view leaves the task with no windows: any presentation will do.
                    if ((!task->windows.empty()
                            && std::find(windows.begin(), windows.end(), task->windows.front()) == windows.end())
                        || static_cast<int>(presentation->queue->queueFamilyIndex()) != first.presentFamily)
                        continue;
                    const vsg::ref_ptr<vsg::Window> ref(&window);
                    if (std::find(windows.begin(), windows.end(), ref) == windows.end())
                        windows.push_back(ref);
                    if (std::find(task->windows.begin(), task->windows.end(), ref) == task->windows.end())
                        task->windows.push_back(ref);
                    // As the viewer orders them: secondary graphs before the primary graphs that execute them.
                    for (const auto& graph : graphs)
                    {
                        const bool secondary = graph->level() != VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                        auto& list = task->commandGraphs;
                        list.insert(secondary ? list.begin() : list.end(), graph);
                    }
                    return;
                }
            }
            viewer.addRecordAndSubmitTaskAndPresentation(graphs);
        }
    }

    Engine::Engine(vsg::ref_ptr<vsg::Window> window)
        : mWindow(window)
    {
        mViewer = vsg::Viewer::create();
        mViewer->addWindow(mWindow);
        mTraits = mWindow->traits();
        mDeletionQueue = std::make_unique<vsgUtil::DeletionQueue>(mTraits->swapchainPreferences.imageCount + 1);
        if (mTraits->debugLayer)
            mDebugMessenger = std::make_unique<vsgUtil::DebugMessenger>(mWindow->getOrCreateInstance());
    }

    Engine::Engine(Callback headlessCallback, vsg::ref_ptr<vsg::WindowTraits> traits)
    {
        mHeadless = std::make_unique<Headless>(headlessCallback, traits);
        mViewer = vsg::Viewer::create();
        mTraits = traits;
        // mDeletionQueue = {};
        mDeletionQueue = std::make_unique<vsgUtil::DeletionQueue>(3);
        if (mTraits->debugLayer)
            mDebugMessenger = std::make_unique<vsgUtil::DebugMessenger>(mHeadless->getOrCreateInstance());
    }

    Engine::Engine(vsg::ref_ptr<vsg::Viewer> viewer, vsg::ref_ptr<vsg::Window> window)
        : mViewer(viewer)
        , mWindow(window)
        , mHosted(true)
    {
        mTraits = mWindow->traits();
        const VkExtent2D extent = mWindow->extent2D();
        if (extent.width == 0 || extent.height == 0)
            throw std::invalid_argument("Render::Engine: the window must be laid out before it is hosted");
        if (mTraits->swapchainPreferences.surfaceFormat.format != compatibleColorFormat)
            throw std::invalid_argument("Render::Engine: the window's swapchain must use compatibleColorFormat");
        mViewer->addWindow(mWindow);
        mDeletionQueue = std::make_unique<vsgUtil::DeletionQueue>(
            vsgUtil::DeletionQueue::sizeForPostFrameAdvance(mTraits->swapchainPreferences.imageCount));
        if (mTraits->debugLayer)
            mDebugMessenger = std::make_unique<vsgUtil::DebugMessenger>(mWindow->getOrCreateInstance());
    }

    Engine::~Engine()
    {
        // The timer's query pool, and the images of screenshot copies in flight, may still be used by frames in flight
        // (loop() waits at its end, a game that aborts or a hosted engine does not).
        if (mGpuTimer || !mFrameCaptures.empty())
            mViewer->deviceWaitIdle();
        // loop() does this on leaving; a hosted engine never runs it, and the deletion queue is about to go.
        if (mHosted)
            mViewer->deviceWaitIdle();
    }

    void Engine::setup(vsg::CommandGraphs graphs, bool threading)
    {
        if (mDeletionQueue && mWindow)
        {
            mWindow->getOrCreateSwapchain();
            // The swapchain can fall back to sRGB when the surface lacks the format asked for
            // (vsg::selectSwapSurfaceFormat).
            if (mHosted && mWindow->surfaceFormat().format != compatibleColorFormat)
                throw std::invalid_argument("Render::Engine: the surface does not support compatibleColorFormat");
            mDeletionQueue->resize(
                mHosted ? vsgUtil::DeletionQueue::sizeForPostFrameAdvance(mWindow->numFrames()) : mWindow->numFrames());
        }
        else if (mHeadless)
        {
            auto commandGraph = createCommandGraph(0);
            commandGraph->children = { mHeadless->commands };
            commandGraph->submitOrder = std::numeric_limits<int>::max();
            graphs.push_back(commandGraph);
        }

        if (mHosted)
        {
            // Viewer::setupThreading captures each task's graphs when it starts its threads, so they restart below.
            mViewer->stopThreading();
            // stopThreading leaves the viewer's status inactive, and only setupThreading sets it back. While inactive,
            // the viewer stops drawing and CompileManager::compile fails with "Nothing assigned yet".
            mViewer->status->set(true);
            appendToViewer(*mViewer, *mWindow, graphs);
            // The first setup goes through Viewer::assignRecordAndSubmitTaskAndPresentation, which keeps only the
            // windows in its graphs, so this window may have been dropped by another Engine's setup.
            mViewer->addWindow(mWindow);
        }
        else
            mViewer->assignRecordAndSubmitTaskAndPresentation(graphs);
        if (threading)
            mViewer->setupThreading();
    }

    void Engine::compile()
    {
        try
        {
            auto start = std::chrono::steady_clock::now();
            mViewer->compile();
            auto end = std::chrono::steady_clock::now();
            auto dt = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
            std::cout << "Engine::compile() = " << dt << "s" << std::endl;
        }
        catch (const vsg::Exception& e)
        {
            auto message = e.message + " VkResult " + std::to_string(e.result);
            std::cerr << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    void Engine::loop(FrameCallback frameCallback, float framerateLimit)
    {
        if (mHosted)
            throw std::logic_error("Render::Engine: a hosted engine's frames are driven by its viewer's owner");

        auto limitFramerate = LimitFramerate::create(framerateLimit);
        auto last = std::chrono::steady_clock::now();

        // OpenEden (task 0072, review of stages b and c): OPENEDEN_FIXED_FRAME_TIME=<seconds> makes every frame advance
        // the game by that time instead of the wall time, so that captures for image comparisons are repeatable
        // (with --random-seed). Test only; unset or not a positive number, the frame time is the wall time as before.
        double fixedFrameTime = 0.0;
        if (const char* fixed = std::getenv("OPENEDEN_FIXED_FRAME_TIME"))
        {
            const double value = std::strtod(fixed, nullptr);
            if (std::isfinite(value) && value > 0.0)
                fixedFrameTime = value;
        }

        // openeden-vsg: per-frame timings for the performance comparison with the OpenSceneGraph build (task 0010),
        // written only when OPENEDEN_FRAME_STATS_FILE names a file: one line per frame with the frame's wall time and
        // the time spent waiting for the GPU or the swapchain (the frame fence in advanceToNextFrame, present,
        // deviceWaitIdle), in milliseconds. The CPU time is the difference.
        std::unique_ptr<std::ofstream> frameStats;
        if (const char* statsPath = std::getenv("OPENEDEN_FRAME_STATS_FILE"))
        {
            frameStats = std::make_unique<std::ofstream>(statsPath);
            *frameStats << "frame wall_ms wait_ms";
            // OpenEden (task 0023): with gpu timing on, each line also tells which frame the GPU times belong to (they
            // are read a few frames after the frame was drawn) and then one column per pass, in ms, -1 where a pass
            // was not recorded or not ready. With it off the header and every line are exactly as before.
            if (mGpuTimer)
            {
                *frameStats << " gpu_frame";
                for (std::string name : mGpuTimer->passNames())
                {
                    std::replace(name.begin(), name.end(), ' ', '_');
                    *frameStats << " gpu_" << name;
                }
            }
            // Task 0054: the counters' columns, none when [Rendering] frame counters is off.
            *frameStats << mFrameCounters.headerText();
            *frameStats << '\n';
        }
        using Clock = std::chrono::steady_clock;
        const auto toMs = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
        auto frameStart = Clock::now();
        Clock::duration frameWait{};
        unsigned long long frameNumber = 0;
        auto timeWait = [&frameWait](auto&& wait) {
            const auto start = Clock::now();
            auto result = wait();
            frameWait += Clock::now() - start;
            return result;
        };

        std::int64_t traceFrameStart = Misc::LoadTrace::enabled() ? Misc::LoadTrace::nowMicroseconds() : 0;
        std::uint64_t traceFrame = 0;
        while (true)
        {
            {
                Misc::LoadTrace::Scope traceAcquire("loop.acquire");
                if (!timeWait([this] { return mViewer->advanceToNextFrame(); }))
                    break;
            }
            auto now = std::chrono::steady_clock::now();
            auto dt = std::chrono::duration_cast<std::chrono::duration<double>>(now - last).count();
            last = now;
            if (fixedFrameTime > 0.0)
                dt = fixedFrameTime;

            static float fpsTimer = 0;
            fpsTimer += dt;
            if (fpsTimer > 5)
            {
                std::cout << "FPS: " << 1.0 / dt << std::endl;
                fpsTimer = 0;
            }

            Misc::LoadTrace::Scope traceEvents("loop.events");
            mViewer->handleEvents();
            traceEvents.end();

            bool requiresRender = true;
            bool quit = false;
            Misc::LoadTrace::Scope traceCallback("loop.callback");
            do
            {
                frameCallback(dt, requiresRender, quit);
                /*
                 * Discards time taken by non-rendering frames to prevent large values of dt.
                 */
                if (!requiresRender)
                    last = std::chrono::steady_clock::now();
            } while (!requiresRender && !quit);

            traceCallback.end();
            if (quit)
                break;

            Misc::LoadTrace::Scope traceUpdate("loop.viewer.update");
            mViewer->update();
            traceUpdate.end();
            // A copy asked for in this frame's callback (a screenshot, the save thumbnail) is made in this frame's
            // submission, before present.
            if (!mFrameCaptures.empty())
            {
                Misc::LoadTrace::Scope traceArm("loop.screenshot.arm");
                for (auto& capture : mFrameCaptures)
                    capture->beforeRecord(*mWindow, getPresentLayout());
            }
            Misc::LoadTrace::Scope traceRecord("loop.record.submit");
            mViewer->recordAndSubmit();
            traceRecord.end();
            if (!mFrameCaptures.empty())
            {
                Misc::LoadTrace::Scope traceCollect("loop.screenshot.collect");
                for (auto& capture : mFrameCaptures)
                    capture->afterRecord(*mViewer);
            }

            Misc::LoadTrace::Scope tracePresent("loop.present");
            if (mHeadless)
            {
                uint64_t waitTimeout = 19999999999;
                mViewer->waitForFences(0, waitTimeout);
                mHeadless->copyOutput();
            }
            else
                timeWait([this] {
                    mViewer->present();
                    return true;
                });

            if (!useFrameQueue)
                timeWait([this] {
                    mViewer->deviceWaitIdle();
                    return true;
                });

            tracePresent.end();
            {
                Misc::LoadTrace::Scope traceDeletion("loop.deletion");
                if (mDeletionQueue)
                    mDeletionQueue->advanceFrame();
            }

            limitFramerate.limit();
            if (Misc::LoadTrace::enabled())
            {
                const std::int64_t traceNow = Misc::LoadTrace::nowMicroseconds();
                Misc::LoadTrace::frameEnd(traceFrame++, traceFrameStart, traceNow);
                traceFrameStart = traceNow;
            }

            // Main thread, after this frame was submitted: read the GPU times of an earlier, finished frame.
            if (mGpuTimer)
                if (const vsg::FrameStamp* stamp = mViewer->getFrameStamp())
                    mGpuTimer->collect(stamp->frameCount);

            if (frameStats)
            {
                const auto frameEnd = Clock::now();
                *frameStats << frameNumber << ' ' << toMs(frameEnd - frameStart) << ' ' << toMs(frameWait);
                if (mGpuTimer)
                {
                    if (mGpuTimer->hasLast())
                        *frameStats << ' ' << mGpuTimer->lastFrame();
                    else
                        *frameStats << " -1";
                    for (std::size_t pass = 0; pass < mGpuTimer->passNames().size(); ++pass)
                        *frameStats << ' ' << mGpuTimer->lastMilliseconds(pass);
                }
                if (!mFrameCounters.empty())
                    *frameStats << mFrameCounters.lineText();
                *frameStats << '\n';
                // The counters (the scene census) can take a while; do not charge it to the next frame.
                frameStart = mFrameCounters.empty() ? frameEnd : Clock::now();
                frameWait = {};
                ++frameNumber;
            }
        }

        // Wait for GPU before attempting to delete any vulkan objects.
        mViewer->deviceWaitIdle();
        // Every copy in flight is finished now.
        for (auto& capture : mFrameCaptures)
            capture->finish();

        if (mGpuTimer)
            Log(Debug::Info) << "GPU timing, median and 95th percentile of the last " << gpuTimingWindow << " frames:\n"
                             << mGpuTimer->summaryText();
    }

    DeviceReport Engine::deviceReport()
    {
        auto physicalDevice = getOrCreatePhysicalDevice();
        vsg::ref_ptr<vsg::Surface> surface;
        uint32_t instanceApi = VK_API_VERSION_1_0;
        if (mWindow)
        {
            surface = mWindow->getOrCreateSurface();
            instanceApi = mWindow->getOrCreateInstance()->apiVersion;
        }
        return collectDeviceReport(physicalDevice, surface, instanceApi);
    }

    uint64_t Engine::deviceLocalMiB()
    {
        auto physicalDevice = getOrCreatePhysicalDevice();
        if (!physicalDevice)
            return 0;
        auto instance = physicalDevice->getInstance().ref_ptr();
        if (!instance)
            return 0;
        PFN_vkGetPhysicalDeviceMemoryProperties getMemoryProperties = nullptr;
        if (!instance->getProcAddr(getMemoryProperties, "vkGetPhysicalDeviceMemoryProperties") || !getMemoryProperties)
            return 0;
        VkPhysicalDeviceMemoryProperties memory{};
        getMemoryProperties(*physicalDevice, &memory);
        return vsgUtil::TextureCap::deviceLocalMiB(memory);
    }

    bool Engine::deviceIsSoftware()
    {
        auto physicalDevice = getOrCreatePhysicalDevice();
        if (!physicalDevice)
            return false;
        return physicalDevice->getProperties().deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    }

    int64_t Engine::deviceLocalUsageMiB()
    {
        auto physicalDevice = getOrCreatePhysicalDevice();
        if (!physicalDevice || !physicalDevice->supportsDeviceExtension("VK_EXT_memory_budget"))
            return -1;
        auto instance = physicalDevice->getInstance().ref_ptr();
        PFN_vkGetPhysicalDeviceMemoryProperties2 getMemoryProperties2 = nullptr;
        if (!instance || !instance->getProcAddr(getMemoryProperties2, "vkGetPhysicalDeviceMemoryProperties2")
            || !getMemoryProperties2)
            return -1;
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
        budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
        VkPhysicalDeviceMemoryProperties2 memory{};
        memory.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        memory.pNext = &budget;
        getMemoryProperties2(*physicalDevice, &memory);
        uint64_t usage = 0;
        for (uint32_t i = 0; i < memory.memoryProperties.memoryHeapCount; ++i)
            if (memory.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                usage += budget.heapUsage[i];
        return static_cast<int64_t>(usage / (1024 * 1024));
    }

    VideoMemory::Sample Engine::deviceLocalMemorySample()
    {
        auto physicalDevice = getOrCreatePhysicalDevice();
        if (!physicalDevice || !physicalDevice->supportsDeviceExtension("VK_EXT_memory_budget"))
            return {};
        auto instance = physicalDevice->getInstance().ref_ptr();
        PFN_vkGetPhysicalDeviceMemoryProperties2 getMemoryProperties2 = nullptr;
        if (!instance || !instance->getProcAddr(getMemoryProperties2, "vkGetPhysicalDeviceMemoryProperties2")
            || !getMemoryProperties2)
            return {};
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
        budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
        VkPhysicalDeviceMemoryProperties2 memory{};
        memory.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        memory.pNext = &budget;
        getMemoryProperties2(*physicalDevice, &memory);
        uint64_t heapBudget = 0;
        uint64_t heapUsage = 0;
        for (uint32_t i = 0; i < memory.memoryProperties.memoryHeapCount; ++i)
            if (memory.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            {
                heapBudget += budget.heapBudget[i];
                heapUsage += budget.heapUsage[i];
            }
        VideoMemory::Sample sample;
        sample.budgetBytes = static_cast<int64_t>(heapBudget);
        sample.usageBytes = static_cast<int64_t>(heapUsage);
        return sample;
    }

    GpuTimer* Engine::enableGpuTiming()
    {
        if (mGpuTimingTried)
            return mGpuTimer.get();
        mGpuTimingTried = true;

        auto physicalDevice = getOrCreatePhysicalDevice();
        auto device = getOrCreateDevice();
        // Timestamps are valid on a queue family when it reports timestampValidBits above zero. The command graphs
        // use the graphics queue family (vsg::CommandGraph::create(window)).
        uint32_t validBits = 0;
        const int family = physicalDevice->getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
        const auto& families = physicalDevice->getQueueFamilyProperties();
        if (family >= 0 && static_cast<std::size_t>(family) < families.size())
            validBits = families[family].timestampValidBits;

        std::string whyNot;
        mGpuTimer = GpuTimer::create(device, validBits, physicalDevice->getProperties().limits.timestampPeriod, whyNot);
        if (!mGpuTimer)
            Log(Debug::Warning) << "GPU timing is on in the settings but is not available: " << whyNot;
        return mGpuTimer.get();
    }

    void Engine::endFrame()
    {
        // The frame stamp appears on the first advanceToNextFrame, and counts the frames of every window of the viewer.
        // Only here is it known that the stamped frame's fence wait has happened; see the header.
        if (const auto* stamp = mViewer->getFrameStamp(); stamp && mDeletionQueue)
            mDeletionQueue->advanceToFrame(stamp->frameCount);
    }

    VkExtent2D Engine::extent2D() const
    {
        if (mWindow)
            return mWindow->extent2D();
        else
            return mHeadless->extent2D();
    }

    vsg::ref_ptr<vsg::PhysicalDevice> Engine::getOrCreatePhysicalDevice()
    {
        if (mWindow)
            return mWindow->getOrCreatePhysicalDevice();
        else
            return mHeadless->getOrCreatePhysicalDevice();
    }

    vsg::ref_ptr<vsg::Device> Engine::getDevice()
    {
        if (mWindow)
            return mWindow->getDevice();
        else
            return mHeadless->getDevice();
    }

    vsg::ref_ptr<vsg::Device> Engine::getOrCreateDevice()
    {
        if (mWindow)
            return mWindow->getOrCreateDevice();
        else
            return mHeadless->getOrCreateDevice();
    }

    vsg::ref_ptr<vsg::CommandGraph> Engine::createCommandGraph(int maxDescriptorSet)
    {
        vsg::ref_ptr<vsg::CommandGraph> graph;
        if (mWindow)
            graph = vsg::CommandGraph::create(mWindow);
        else
            graph = vsg::CommandGraph::create(mHeadless->getOrCreateDevice(), mHeadless->getQueueFamily());
        // int maxSlot = 1 + maxDescriptorSet;
        //  int maxSlot = 4; // At the moment, reducing maxSlot won't help because vsg::updateViewer will update the
        //  maxSlot for all command graphs.
        graph->recordTraversal = new vsg::RecordTraversal;
        graph->maxSlots = vsg::Slots{ 4, 4 };
        return graph;
    }

    vsg::ref_ptr<vsg::SecondaryCommandGraph> Engine::createSecondaryCommandGraph(
        vsg::ref_ptr<vsg::RenderGraph> inherit_framebuffer, int maxDescriptorSet)
    {
        inherit_framebuffer->contents = VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS;
        vsg::ref_ptr<vsg::SecondaryCommandGraph> graph;
        if (mWindow)
            graph = vsg::SecondaryCommandGraph::create(mWindow);
        else
        {
            graph = vsg::SecondaryCommandGraph::create(mHeadless->getOrCreateDevice(), mHeadless->getQueueFamily());
            graph->framebuffer = inherit_framebuffer->framebuffer;
        }
        graph->recordTraversal = new vsg::RecordTraversal;
        graph->maxSlots
            = vsg::Slots{ static_cast<uint32_t>(maxDescriptorSet + 1), static_cast<uint32_t>(maxDescriptorSet + 1) };
        return graph;
    }

    vsg::ref_ptr<vsg::RenderGraph> Engine::createRenderGraph()
    {
        vsg::ref_ptr<vsg::RenderGraph> renderGraph;
        if (mWindow)
            renderGraph = vsg::RenderGraph::create(mWindow);
        else
            renderGraph = mHeadless->createRenderGraph();
        /*if (useDynamicViewport)*/ renderGraph->windowResizeHandler = nullptr;
        return renderGraph;
    }

    vsg::ref_ptr<vsgUtil::CompileContext> Engine::createCompileContext()
    {
        // we don't have any views / renderGraphs assigned to the viewer yet so assigning a ref_ptr<ResourceHints> will
        // not be effective. instead we'll pass resourceRequirements to CompileManager::add(..).
        auto compile = vsg::CompileManager::create(*mViewer, vsg::ref_ptr<vsg::ResourceHints>());
        auto ret = vsg::ref_ptr{ new vsgUtil::CompileContext };
        ret->device = getOrCreateDevice();
        ret->compileManager = compile;
        ret->onCompiled = [this](const vsg::CompileResult& r) { vsg::updateViewer(*mViewer, r); };
        if (mDeletionQueue)
            ret->onDetached = [this](vsg::ref_ptr<vsg::Object> object) { mDeletionQueue->add(object); };
        else
            ret->onDetached = [](vsg::ref_ptr<vsg::Object>) {};
        return ret;
    }

    vsg::ref_ptr<Screenshot> Engine::screenshotOperation()
    {
        if (mHosted && (mWindow->extent2D().width == 0 || mWindow->extent2D().height == 0))
            return {};
        if (mWindow)
        {
            // OpenEden: Screenshot(window) reads imageIndex(1), the image acquired one frame before the current one
            // (vsg::Window::acquireNextImage shifts _indices). Before a second image was acquired, and after the
            // swapchain is rebuilt, it is numFrames() (the initial value Window::buildSwapchain gives _indices), which
            // imageView() would index past the end; and an extent of zero has nothing to copy.
            const VkExtent2D extent = swapchainExtent();
            if (mWindow->imageIndex(1) >= mWindow->numFrames() || extent.width == 0 || extent.height == 0)
                return {};
            return vsg::ref_ptr{ new Screenshot(mWindow) };
        }
        else
            return vsg::ref_ptr{ new Screenshot(
                getOrCreateDevice(), mHeadless->colorImageView->image, mHeadless->presentLayout) };
    }

    FrameCapture* Engine::addFrameCapture(vsg::ref_ptr<vsg::CommandGraph> graph, DownloadSync sync)
    {
        if (!mWindow)
            return nullptr;
        mFrameCaptures.push_back(std::make_unique<FrameCapture>(mScreenshotBudget, sync));
        mFrameCaptures.back()->attach(std::move(graph));
        return mFrameCaptures.back().get();
    }

    void Engine::setScreenshotGraph(vsg::ref_ptr<vsg::CommandGraph> graph)
    {
        mScreenshotCapture = addFrameCapture(std::move(graph), DownloadSync::BeforePresent);
    }

    void Engine::requestScreenshot(ScreenshotDone done)
    {
        if (mScreenshotCapture)
        {
            mScreenshotCapture->request(std::move(done));
            return;
        }
        if (!mHeadless)
        {
            Log(Debug::Warning)
                << "Screenshot skipped: this window has no screenshot graph (Engine::setScreenshotGraph)";
            return;
        }
        // Headless: loop() waits for each frame's fences after submitting it, so the image holds the last frame and
        // nothing draws into it before the next recordAndSubmit; it is never presented.
        auto operation = screenshotOperation();
        std::optional<ScreenshotCopyBudget::Token> token = mScreenshotBudget.tryAcquire();
        if (!token)
        {
            Log(Debug::Warning) << "Screenshot skipped: " << mScreenshotBudget.inUse()
                                << " copies are still on the GPU or waiting to be read (at most "
                                << mScreenshotBudget.cap() << ")";
            return;
        }
        try
        {
            operation->opaque = true;
            operation->copyNow(std::move(*token));
            done(operation);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Screenshot skipped: " << e.what();
        }
        catch (const vsg::Exception& e)
        {
            Log(Debug::Warning) << "Screenshot skipped: " << e.message << " VkResult " << e.result;
        }
    }

    vsg::ref_ptr<Screenshot> Engine::drawFrameAndCapture()
    {
        if (!mHosted || !mScreenshotCapture)
            return {};
        // The callback holds a share of the slot (round four, finding 1): if the frame throws after the copy was armed,
        // nothing may write to a local of this function afterwards. The recovery below also ends every capture, so no
        // armed item outlives the call.
        ResultSlot<vsg::ref_ptr<Screenshot>> copied;
        mScreenshotCapture->request(copied.sink());
        std::string_view endedBecause = "the frame loop ended first";
        try
        {
            // vsgQt::Viewer::render without its request count, and with the copy armed between the image's acquisition
            // and the recording. handleEvents too: advanceToNextFrame drops the events of the frame before.
            if (mViewer->advanceToNextFrame())
            {
                mViewer->handleEvents();
                mViewer->update();
                for (auto& capture : mFrameCaptures)
                    capture->beforeRecord(*mWindow, getPresentLayout());
                mViewer->recordAndSubmit();
                mViewer->present();
            }
            else
            {
                Log(Debug::Warning) << "Screenshot skipped: the viewer had no frame to draw (advanceToNextFrame "
                                       "returned false)";
                endedBecause = "the viewer had no frame to draw";
            }
        }
        catch (const vsg::Exception& e)
        {
            Log(Debug::Warning) << "Screenshot skipped: the frame failed: " << e.message << " VkResult " << e.result;
            endedBecause = "the frame failed";
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Screenshot skipped: the frame failed: " << e.what();
            endedBecause = "the frame failed";
        }
        // No frame is in flight after this, so the copy is finished (or never will be). A failure to wait leaves
        // nothing safe to read: the copies are then dropped, not delivered.
        try
        {
            mViewer->deviceWaitIdle();
            for (auto& capture : mFrameCaptures)
                capture->afterRecord(*mViewer);
        }
        catch (const vsg::Exception& e)
        {
            Log(Debug::Warning) << "Screenshot skipped: waiting for the device failed: " << e.message << " VkResult "
                                << e.result;
            return {};
        }
        for (auto& capture : mFrameCaptures)
            capture->finish(endedBecause);
        return copied.take();
    }

    size_t Engine::numFrames() const
    {
        if (mWindow)
            return mWindow->numFrames();
        else
            return 1;
    }

    vsg::ref_ptr<vsg::ImageView> Engine::colorImageView(size_t relativeFrameIndex)
    {
        if (mWindow)
            return mWindow->imageView(mWindow->imageIndex(relativeFrameIndex));
        else
            return mHeadless->colorImageView;
    }

    vsg::ref_ptr<vsg::ImageView> Engine::depthImageView()
    {
        if (mWindow)
            return mWindow->getOrCreateDepthImageView();
        else
            return mHeadless->depthImageView;
    }

    VkExtent2D Engine::swapchainExtent()
    {
        if (mWindow)
        {
            if (auto swapchain = mWindow->getSwapchain())
                return swapchain->getExtent();
        }
        return extent2D();
    }

    void Engine::updateExtents(const VkExtent2D& extent)
    {
        if (mWindow)
        {
            if (auto adapter = mWindow->cast<vsg::WindowAdapter>())
                adapter->updateExtents(extent.width, extent.height);
        }
        else
        {
            auto old_framebuffer = mHeadless->mRenderGraph->framebuffer;
            mHeadless->updateExtents(extent);

            for (auto& task : mViewer->recordAndSubmitTasks)
            {
                for (auto& cg : task->commandGraphs)
                {
                    if (cg->framebuffer == old_framebuffer)
                        cg->framebuffer = mHeadless->mRenderGraph->framebuffer;
                }
            }
        }
    }

    const VkPhysicalDeviceFeatures& Engine::getEnabledFeatures() const
    {
        return mTraits->deviceFeatures->get();
    }

    VkSampleCountFlagBits Engine::framebufferSamples()
    {
        return supportedSamples(mTraits->samples, getOrCreatePhysicalDevice());
    }

    void Engine::setRenderPass(vsg::ref_ptr<vsg::RenderPass> renderPass)
    {
        if (mWindow)
            mWindow->setRenderPass(renderPass);
        else
            mHeadless->setRenderPass(renderPass);
    }

    VkImageLayout Engine::getPresentLayout() const
    {
        // we can't use VK_IMAGE_LAYOUT_PRESENT_SRC_KHR unless the associated extension has been enabled in vsg::Device
        // creation.
        if (mWindow)
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        else
            return mHeadless->presentLayout;
    }
}
