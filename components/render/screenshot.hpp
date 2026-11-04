#ifndef VSGOPENMW_RENDER_SCREENSHOT_H
#define VSGOPENMW_RENDER_SCREENSHOT_H

#include <chrono>
#include <filesystem>
#include <functional>
#include <vector>

#include "download.hpp"
#include "framecapture.hpp"

#include <vsg/core/Data.h>

#include <components/vsgutil/operation.hpp>

namespace vsg
{
    class Window;
}
namespace Render
{
    struct Screenshot : public vsgUtil::Operation
    {
        std::filesystem::path writeFilename;
        vsg::ref_ptr<const vsg::Options> writeOptions;
        vsg::ref_ptr<vsg::Data> downloadData;

        vsg::Device* device;
        vsg::ref_ptr<vsg::Image> srcImage;
        VkRect2D srcRect;
        VkFormat srcImageFormat;
        VkImageLayout srcImageLayout;
        // A picture of a window (FrameCapture, the headless copy): operate() makes it opaque (makeOpaque). The world
        // map's picture keeps its alpha.
        bool opaque = false;

        Screenshot(vsg::Device* in_device, vsg::ref_ptr<vsg::Image> in_srcImage, VkImageLayout in_srcImageLayout);
        // The image presented last (imageIndex(1)), at the swapchain's extent. Only for a copy after a wait for the
        // device to be idle (the editor's capture): that image has been handed to the presentation engine, and Vulkan
        // forbids using it before it is acquired again, so this reads it against the specification. The caller makes
        // sure imageIndex(1) names an image (Engine::screenshotOperation).
        Screenshot(vsg::ref_ptr<vsg::Window> window);
        ~Screenshot() override;

        // Sets writeFilename to the first path / "screenshotNNN.<ext>" that is neither on disk nor handed out to
        // another live Screenshot of this process, and holds that name until this Screenshot is destroyed (or named
        // again). The file is written later, in operate() on a worker thread, so the disk alone cannot tell two
        // requests apart: six requests on six consecutive frames used to get one name. Thread-safe.
        void generateFilename(const std::filesystem::path& path, const std::string& ext);
        // A host-visible copy of the source that createDownloadCommands made and that has finished (FrameCapture);
        // operate() reads it and writes the file. The token counts it against the copies waiting to be written, until
        // operate() has read it or this is destroyed.
        void useCopy(vsg::ref_ptr<vsg::Image> copy, ScreenshotCopyBudget::Token token);
        // The same for a scaled copy that ended in a host-visible buffer (createScaledDownloadCommands).
        void useBufferCopy(HostBuffer copy, ScreenshotCopyBudget::Token token);
        // Copies the source image now, on the calling thread, and waits for the copy (a headless engine's image, which
        // is never presented); throws std::runtime_error when the copy fails, which would otherwise give a file of
        // whatever the memory held. Without this or useCopy, operate() copies the source when it runs, as before.
        void copyNow(ScreenshotCopyBudget::Token token);
        void operate() override;

    private:
        void releaseFilename();

        std::filesystem::path mReservedFilename;
        vsg::ref_ptr<vsg::Image> mCopy;
        HostBuffer mBufferCopy;
        std::optional<ScreenshotCopyBudget::Token> mCopyToken;
    };

    // Sets every pixel's alpha to fully opaque in a 4-channel 8-bit picture (vsg::ubvec4Array2D, RGBA or BGRA UNORM);
    // anything else is left as it is. Window captures pass through it (Screenshot::opaque): the swapchain's alpha is
    // whatever the passes left there, and OpenMW 0.51 wrote opaque screenshots (traits->alpha = 0 in its
    // Engine::createWindow, a GL_RGB read for the save thumbnail).
    void makeOpaque(vsg::Data& data);

    // The end of a game screenshot, called once its copy has finished (Engine::requestScreenshot's done): gives it
    // the write options and the first free name in dir, then hands it to queue (the writer thread). A name is taken
    // only here, so a request that was dropped, or whose frame was lost, takes no number.
    void nameAndQueueScreenshot(vsg::ref_ptr<Screenshot> screenshot, const std::filesystem::path& dir,
        const std::string& ext, vsg::ref_ptr<const vsg::Options> writeOptions,
        const std::function<void(vsg::ref_ptr<Screenshot>)>& queue);

    // The game's screenshots handed to the writer thread, kept until they have run (OpenEden, 2026-11-04, third
    // round), so that the engine can wait for them when the game quits: vsg::OperationThreads::stop ends its threads
    // without running what is still queued, and a screenshot asked for just before quitting was lost with no message.
    // Main thread only; the operations run on the writer thread.
    class ScreenshotWriter
    {
    public:
        using Queue = std::function<void(vsg::ref_ptr<Screenshot>)>;
        explicit ScreenshotWriter(Queue queue);

        // Hands screenshot to the queue and keeps it until it has run; forgets those that have.
        void write(vsg::ref_ptr<Screenshot> screenshot);
        // How many handed over have not run yet.
        std::size_t pending();
        // Waits, at most limit in all, until every screenshot handed over has run. Returns those that have not; they
        // are forgotten. The wait comes up for air at least every slice, while a screenshot is still pending, and
        // calls idle then (round four, finding 2): the game's main thread must go on answering its window's messages,
        // or the freeze monitor calls the window hung after 5 s (components/crashcatcher/windowscrashmonitor.cpp,
        // isAppFrozen: IsHungAppWindow) and, under OPENEDEN_QUIET_WINDOW, ends the game. The default slice is well
        // under that.
        static constexpr std::chrono::milliseconds sDefaultWaitSlice{ 500 };
        std::vector<vsg::ref_ptr<Screenshot>> waitForAll(std::chrono::milliseconds limit,
            const std::function<void()>& idle = {}, std::chrono::milliseconds slice = sDefaultWaitSlice);

    private:
        void forgetWritten();

        Queue mQueue;
        std::vector<vsg::ref_ptr<Screenshot>> mPending;
    };
}

#endif
