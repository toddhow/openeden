#include "screenshot.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>

#include <vsg/app/Window.h>
#include <vsg/commands/Commands.h>
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
        srcImageFormat = window->getOrCreateSwapchain()->getImageFormat();
        srcRect = { { 0, 0 }, window->extent2D() };
        // get the colour buffer image of the previous rendered frame as the current frame hasn't been rendered yet. The
        // 1 in window->imageIndex(1) means image from 1 frame ago.
        srcImage = window->imageView(window->imageIndex(1))->image;
        srcImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    void Screenshot::copyNow()
    {
        auto [commands, copy] = createDownloadCommands(device, srcImage, srcRect, srcImageFormat, srcImageLayout);
        submit(device, commands);
        mCopy = copy;
    }

    void Screenshot::operate()
    {
        if (mCopy)
            downloadData = readDownloadedImage(device, mCopy);
        else
            downloadData = download(device, srcImage, srcRect, srcImageFormat, srcImageLayout);
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
}
