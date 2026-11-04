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

    FrameCapture::FrameCapture(ScreenshotCopyBudget& budget)
        : mBudget(budget)
        , mSwitch(vsg::Switch::create())
    {
    }

    FrameCapture::~FrameCapture() = default;

    void FrameCapture::attach(vsg::ref_ptr<vsg::CommandGraph> graph)
    {
        mGraph = std::move(graph);
        mGraph->children.push_back(mSwitch);
    }

    void FrameCapture::request(Done done)
    {
        if (mWaiting)
            return; // the same frame's picture
        mWaiting = std::move(done);
        mFramesWaited = 0;
    }

    void FrameCapture::beforeRecord(vsg::Window& window, VkImageLayout presentLayout)
    {
        if (!mWaiting || !mGraph)
            return;
        const vsg::ref_ptr<vsg::Swapchain> swapchain = window.getSwapchain();
        const VkExtent2D extent = swapchain ? swapchain->getExtent() : VkExtent2D{ 0, 0 };
        const std::size_t imageIndex = window.imageIndex(0);
        const bool imageAcquired = swapchain && window.visible() && imageIndex < window.numFrames();
        switch (decideArm(imageAcquired, extent, mFramesWaited, sMaxWaitFrames))
        {
            case ArmDecision::Wait:
                ++mFramesWaited;
                return;
            case ArmDecision::Drop:
                Log(Debug::Warning) << "Screenshot skipped: the window had no image to copy for " << mFramesWaited
                                    << " frames (hidden, or of zero size)";
                mWaiting.reset();
                return;
            case ArmDecision::Arm:
                break;
        }
        Done done = std::move(*mWaiting);
        mWaiting.reset();

        std::optional<ScreenshotCopyBudget::Token> token = mBudget.tryAcquire();
        if (!token)
        {
            Log(Debug::Warning) << "Screenshot skipped: " << mBudget.inUse()
                                << " screenshots are still waiting to be written (at most " << mBudget.cap() << ")";
            return;
        }
        try
        {
            vsg::Device* device = window.getDevice();
            const vsg::ref_ptr<vsg::Image> image = window.imageView(imageIndex)->image;
            auto screenshot = vsg::ref_ptr{ new Screenshot(device, image, presentLayout) };
            screenshot->srcRect = { { 0, 0 }, extent };
            screenshot->srcImageFormat = swapchain->getImageFormat();
            auto [commands, copy] = createDownloadCommands(device, image, screenshot->srcRect,
                screenshot->srcImageFormat, presentLayout, {}, DownloadSync::BeforePresent);
            screenshot->useCopy(copy, std::move(*token));
            mSwitch->children = { vsg::Switch::Child{ vsg::MASK_ALL, commands } };
            mArmed = Copy{ screenshot, commands, std::move(done), 0 };
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

    void FrameCapture::afterRecord(vsg::Viewer& viewer)
    {
        // One frame was recorded and submitted since the last call: every copy in flight is a frame older.
        for (Copy& copy : mInFlight)
            ++copy.age;
        if (mArmed)
        {
            // Recorded into this frame's command buffer; the next frame records without it.
            mSwitch->children.clear();
            mInFlight.push_back(std::move(*mArmed));
            mArmed.reset();
        }
        if (mInFlight.empty())
            return;

        const vsg::ref_ptr<vsg::RecordAndSubmitTask> task = taskOf(viewer, mGraph.get());
        while (!mInFlight.empty())
        {
            Copy& copy = mInFlight.front();
            CopyState state = CopyState::Lost;
            if (task)
            {
                // fence(age) is the fence of the frame recorded age frames ago (RecordAndSubmitTask::advance shifts
                // the ring once a frame), null once the ring has moved past it.
                const vsg::ref_ptr<vsg::Fence> fence = task->fence(copy.age);
                const bool submitted = fence && fence->hasDependencies();
                state = copyState(fence != nullptr, submitted, submitted ? fence->status() : VK_NOT_READY);
            }
            if (state == CopyState::InFlight)
                break; // later copies are in later frames
            if (state == CopyState::Done)
                deliver(copy);
            else
                Log(Debug::Warning) << "Screenshot lost: its frame did not finish on the GPU";
            mInFlight.pop_front();
        }
    }

    void FrameCapture::deliver(Copy& copy)
    {
        try
        {
            copy.done(copy.screenshot);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Screenshot not written: " << e.what();
        }
    }

    void FrameCapture::finish()
    {
        for (Copy& copy : mInFlight)
            deliver(copy);
        mInFlight.clear();
        mArmed.reset();
        mSwitch->children.clear();
        if (mWaiting)
            Log(Debug::Info) << "Screenshot not taken: the frame loop ended first";
        mWaiting.reset();
    }
}
