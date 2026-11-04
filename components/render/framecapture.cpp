#include "framecapture.hpp"

#include <algorithm>
#include <utility>

#include <vsg/app/CommandGraph.h>
#include <vsg/app/RecordAndSubmitTask.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/Window.h>
#include <vsg/commands/Commands.h>
#include <vsg/core/Exception.h>
#include <vsg/nodes/Switch.h>
#include <vsg/vk/Fence.h>

#include <components/debug/debuglog.hpp>

#include "download.hpp"
#include "screenshot.hpp"

namespace Render
{
    ScreenshotCopyBudget::Token::Token(std::shared_ptr<std::atomic<std::size_t>> inUse)
        : mInUse(std::move(inUse))
    {
    }

    ScreenshotCopyBudget::Token::Token(Token&& other) noexcept
        : mInUse(std::move(other.mInUse))
    {
    }

    ScreenshotCopyBudget::Token& ScreenshotCopyBudget::Token::operator=(Token&& other) noexcept
    {
        if (this != &other)
        {
            release();
            mInUse = std::move(other.mInUse);
        }
        return *this;
    }

    ScreenshotCopyBudget::Token::~Token()
    {
        release();
    }

    void ScreenshotCopyBudget::Token::release()
    {
        if (mInUse)
            mInUse->fetch_sub(1);
        mInUse.reset();
    }

    ScreenshotCopyBudget::ScreenshotCopyBudget(std::size_t cap)
        : mInUse(std::make_shared<std::atomic<std::size_t>>(0))
        , mCap(cap)
    {
    }

    std::optional<ScreenshotCopyBudget::Token> ScreenshotCopyBudget::tryAcquire()
    {
        std::size_t held = mInUse->load();
        while (held < mCap)
        {
            if (mInUse->compare_exchange_weak(held, held + 1))
                return Token(mInUse);
        }
        return std::nullopt;
    }

    std::size_t ScreenshotCopyBudget::inUse() const
    {
        return mInUse->load();
    }

    ArmDecision decideArm(bool imageAcquired, VkExtent2D extent, unsigned framesWaited, unsigned maxWaitFrames)
    {
        if (imageAcquired && extent.width > 0 && extent.height > 0)
            return ArmDecision::Arm;
        return framesWaited < maxWaitFrames ? ArmDecision::Wait : ArmDecision::Drop;
    }

    CopyState copyState(bool fenceInRing, bool fenceSubmitted, VkResult fenceStatus)
    {
        if (!fenceInRing)
            return CopyState::Done;
        if (!fenceSubmitted)
            return CopyState::Lost;
        if (fenceStatus == VK_SUCCESS)
            return CopyState::Done;
        if (fenceStatus == VK_NOT_READY)
            return CopyState::InFlight;
        return CopyState::Lost;
    }

    namespace
    {
        // The task that records graph, whose fences say when the frame's copy is finished.
        vsg::ref_ptr<vsg::RecordAndSubmitTask> taskOf(vsg::Viewer& viewer, const vsg::CommandGraph* graph)
        {
            for (auto& task : viewer.recordAndSubmitTasks)
            {
                for (auto& taskGraph : task->commandGraphs)
                {
                    if (taskGraph.get() == graph)
                        return task;
                }
            }
            return {};
        }
    }

    namespace
    {
        struct Item
        {
            FrameCapture::Done done;
            FrameCapture::Failed failed;
            std::optional<VkExtent2D> size;
            vsg::ref_ptr<Screenshot> screenshot;
            vsg::ref_ptr<vsg::Commands> commands;
        };

        void deliver(Item& item)
        {
            try
            {
                if (item.done)
                    item.done(item.screenshot);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Screenshot not written: " << e.what();
            }
        }

        void fail(Item& item)
        {
            try
            {
                if (item.failed)
                    item.failed();
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Screenshot failure handler: " << e.what();
            }
        }
    }

    struct FrameCapture::State
    {
        CaptureQueue<Item> queue{ FrameCapture::sMaxWaitFrames };
    };

    FrameCapture::FrameCapture(ScreenshotCopyBudget& budget, DownloadSync sync)
        : mBudget(budget)
        , mSync(sync)
        , mSwitch(vsg::Switch::create())
        , mState(std::make_unique<State>())
    {
    }

    FrameCapture::~FrameCapture() = default;

    void FrameCapture::attach(vsg::ref_ptr<vsg::CommandGraph> graph)
    {
        mGraph = std::move(graph);
        mGraph->children.push_back(mSwitch);
    }

    bool FrameCapture::request(Done done, Failed failed, std::optional<VkExtent2D> size)
    {
        return mState->queue.request(Item{ std::move(done), std::move(failed), size, {}, {} });
    }

    void FrameCapture::beforeRecord(vsg::Window& window, VkImageLayout presentLayout)
    {
        if (!mGraph)
            return;
        const vsg::ref_ptr<vsg::Swapchain> swapchain = window.getSwapchain();
        const VkExtent2D extent = swapchain ? swapchain->getExtent() : VkExtent2D{ 0, 0 };
        const std::size_t imageIndex = window.imageIndex(0);
        const bool imageAcquired = swapchain && window.visible() && imageIndex < window.numFrames();
        const unsigned framesWaited = mState->queue.framesWaited();
        const std::optional<ArmDecision> decision = mState->queue.decide(imageAcquired, extent);
        if (!decision || *decision == ArmDecision::Wait)
            return;
        Item item = mState->queue.takeWaiting();
        if (*decision == ArmDecision::Drop)
        {
            Log(Debug::Warning) << "Screenshot skipped: the window had no image to copy for " << framesWaited
                                << " frames (hidden, or of zero size)";
            fail(item);
            return;
        }

        std::optional<ScreenshotCopyBudget::Token> token = mBudget.tryAcquire();
        if (!token)
        {
            Log(Debug::Warning) << "Screenshot skipped: " << mBudget.inUse()
                                << " copies are still on the GPU or waiting to be read (at most " << mBudget.cap()
                                << ")";
            fail(item);
            return;
        }
        try
        {
            vsg::Device* device = window.getDevice();
            const vsg::ref_ptr<vsg::Image> image = window.imageView(imageIndex)->image;
            item.screenshot = vsg::ref_ptr{ new Screenshot(device, image, presentLayout) };
            item.screenshot->srcRect = { { 0, 0 }, extent };
            item.screenshot->srcImageFormat = swapchain->getImageFormat();
            // A picture of the window: the swapchain's alpha is not part of it (makeOpaque).
            item.screenshot->opaque = true;
            auto [commands, copy] = createDownloadCommands(device, image, item.screenshot->srcRect,
                item.screenshot->srcImageFormat, presentLayout, item.size, mSync);
            item.screenshot->useCopy(copy, std::move(*token));
            item.commands = commands;
            mSwitch->children = { vsg::Switch::Child{ vsg::MASK_ALL, commands } };
            mState->queue.armed(std::move(item));
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Screenshot skipped: " << e.what();
            fail(item);
        }
        catch (const vsg::Exception& e)
        {
            Log(Debug::Warning) << "Screenshot skipped: " << e.message << " VkResult " << e.result;
            fail(item);
        }
    }

    void FrameCapture::afterRecord(vsg::Viewer& viewer)
    {
        const vsg::ref_ptr<vsg::RecordAndSubmitTask> task = taskOf(viewer, mGraph.get());
        auto collected = mState->queue.afterRecord([&task](unsigned age) {
            FrameFence result;
            if (!task)
            {
                result.inRing = true; // no task records the graph: nothing will finish
                return result;
            }
            // fence(age) is the fence of the frame recorded age frames ago (RecordAndSubmitTask::advance shifts the
            // ring once a frame), null once the ring has moved past it.
            const vsg::ref_ptr<vsg::Fence> fence = task->fence(age);
            result.inRing = fence != nullptr;
            result.submitted = fence && fence->hasDependencies();
            result.status = result.submitted ? fence->status() : VK_NOT_READY;
            return result;
        });
        // Recorded into this frame's command buffer; the next frame records without it.
        if (collected.armedThisFrame)
            mSwitch->children.clear();
        for (Item& item : collected.done)
            deliver(item);
        for (Item& item : collected.lost)
        {
            Log(Debug::Warning) << "Screenshot lost: its frame did not finish on the GPU";
            fail(item);
        }
    }

    void FrameCapture::finish(std::string_view why)
    {
        auto [done, dropped] = mState->queue.finish();
        mSwitch->children.clear();
        for (Item& item : done)
            deliver(item);
        for (Item& item : dropped)
        {
            Log(Debug::Info) << "Screenshot not taken: " << why;
            fail(item);
        }
    }
}
