#include "screenshot.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>

#include <vsg/app/Window.h>
#include <vsg/commands/Commands.h>
#include <vsg/core/Array2D.h>
#include <vsg/io/write.h>

namespace Render
{
    namespace
    {
        // The names handed out by generateFilename to screenshots that still exist, in this process.
        std::mutex sReservedMutex;
        std::set<std::filesystem::path> sReserved;
    }

    Screenshot::Screenshot(
        vsg::Device* in_device, vsg::ref_ptr<vsg::Image> in_srcImage, VkImageLayout in_srcImageLayout)
        : device(in_device)
        , srcImage(in_srcImage)
        , srcRect{ { 0, 0 }, { in_srcImage->extent.width, in_srcImage->extent.height } }
        , srcImageFormat(srcImage->format)
        , srcImageLayout(in_srcImageLayout)
    {
    }

    Screenshot::Screenshot(vsg::ref_ptr<vsg::Window> window)
        : device(window->getOrCreateDevice())
    {
        const vsg::ref_ptr<vsg::Swapchain> swapchain = window->getOrCreateSwapchain();
        srcImageFormat = swapchain->getImageFormat();
        // OpenEden: the swapchain's extent, which the image has. window->extent2D() follows the window's resize events
        // at once, while the images keep their size until the next acquisition rebuilds the swapchain, so a capture in
        // between asked for a region outside the image.
        srcRect = { { 0, 0 }, swapchain->getExtent() };
        // get the colour buffer image of the previous rendered frame as the current frame hasn't been rendered yet. The
        // 1 in window->imageIndex(1) means image from 1 frame ago.
        srcImage = window->imageView(window->imageIndex(1))->image;
        srcImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        opaque = true;
    }

    void Screenshot::useCopy(vsg::ref_ptr<vsg::Image> copy, ScreenshotCopyBudget::Token token)
    {
        mCopy = std::move(copy);
        mCopyToken = std::move(token);
    }

    void Screenshot::copyNow(ScreenshotCopyBudget::Token token)
    {
        auto [commands, copy] = createDownloadCommands(device, srcImage, srcRect, srcImageFormat, srcImageLayout);
        if (const VkResult result = submit(device, commands); result != VK_SUCCESS)
            throw std::runtime_error("the copy returned VkResult " + std::to_string(static_cast<int>(result)));
        useCopy(copy, std::move(token));
    }

    void Screenshot::operate()
    {
        if (mCopy)
        {
            downloadData = readDownloadedImage(device, mCopy);
            // The host-visible copy is no longer needed: free it, and its place among the copies waiting, before the
            // slow write.
            mCopy = nullptr;
            mCopyToken.reset();
        }
        else
            downloadData = download(device, srcImage, srcRect, srcImageFormat, srcImageLayout);
        if (opaque && downloadData)
            makeOpaque(*downloadData);
        if (!writeFilename.empty() && downloadData)
            vsg::write(downloadData, writeFilename.string(), writeOptions);
    }

    Screenshot::~Screenshot()
    {
        releaseFilename();
    }

    void Screenshot::releaseFilename()
    {
        if (mReservedFilename.empty())
            return;
        std::lock_guard<std::mutex> lock(sReservedMutex);
        sReserved.erase(mReservedFilename);
        mReservedFilename.clear();
    }

    void Screenshot::generateFilename(const std::filesystem ::path& path, const std::string& ext)
    {
        // OpenEden: a name is taken if it is on disk or held by another screenshot of this process, whose file is
        // not written yet. Without the second test every request made before the first file was written got the
        // same name, and the files overwrote each other.
        releaseFilename();
        std::lock_guard<std::mutex> lock(sReservedMutex);
        int shotCount = 0;
        do
        {
            std::ostringstream stream;
            stream << std::setw(3) << std::setfill('0') << shotCount++ << "." << ext;
            writeFilename = path / ("screenshot" + stream.str());
        } while (sReserved.count(writeFilename.lexically_normal()) != 0 || std::filesystem::exists(writeFilename));
        mReservedFilename = writeFilename.lexically_normal();
        sReserved.insert(mReservedFilename);
    }

    void nameAndQueueScreenshot(vsg::ref_ptr<Screenshot> screenshot, const std::filesystem::path& dir,
        const std::string& ext, vsg::ref_ptr<const vsg::Options> writeOptions,
        const std::function<void(vsg::ref_ptr<Screenshot>)>& queue)
    {
        screenshot->writeOptions = std::move(writeOptions);
        screenshot->generateFilename(dir, ext);
        queue(std::move(screenshot));
    }

    void makeOpaque(vsg::Data& data)
    {
        const VkFormat format = data.properties.format;
        if (format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_R8G8B8A8_UNORM)
            return;
        auto* picture = data.cast<vsg::ubvec4Array2D>();
        if (!picture)
            return;
        for (vsg::ubvec4& pixel : *picture)
            pixel.a = 255;
    }

    ScreenshotWriter::ScreenshotWriter(Queue queue)
        : mQueue(std::move(queue))
    {
    }

    void ScreenshotWriter::write(vsg::ref_ptr<Screenshot> screenshot)
    {
        forgetWritten();
        mPending.push_back(screenshot);
        mQueue(std::move(screenshot));
    }

    std::size_t ScreenshotWriter::pending()
    {
        forgetWritten();
        return mPending.size();
    }

    std::vector<vsg::ref_ptr<Screenshot>> ScreenshotWriter::waitForAll(std::chrono::milliseconds limit)
    {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        std::vector<vsg::ref_ptr<Screenshot>> unwritten;
        for (auto& screenshot : mPending)
        {
            const auto left = std::max(std::chrono::steady_clock::duration::zero(),
                std::chrono::steady_clock::duration(deadline - std::chrono::steady_clock::now()));
            if (!screenshot->waitFor(left))
                unwritten.push_back(screenshot);
        }
        mPending.clear();
        return unwritten;
    }

    void ScreenshotWriter::forgetWritten()
    {
        mPending.erase(std::remove_if(mPending.begin(), mPending.end(),
                           [](const vsg::ref_ptr<Screenshot>& screenshot) { return screenshot->done.load(); }),
            mPending.end());
    }
}
