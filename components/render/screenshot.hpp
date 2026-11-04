#ifndef VSGOPENMW_RENDER_SCREENSHOT_H
#define VSGOPENMW_RENDER_SCREENSHOT_H

#include <filesystem>

#include "download.hpp"

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

        Screenshot(vsg::Device* in_device, vsg::ref_ptr<vsg::Image> in_srcImage, VkImageLayout in_srcImageLayout);
        Screenshot(vsg::ref_ptr<vsg::Window> window);
        ~Screenshot() override;

        // Sets writeFilename to the first path / "screenshotNNN.<ext>" that is neither on disk nor handed out to
        // another live Screenshot of this process, and holds that name until this Screenshot is destroyed (or named
        // again). The file is written later, in operate() on a worker thread, so the disk alone cannot tell two
        // requests apart: six requests on six consecutive frames used to get one name. Thread-safe.
        void generateFilename(const std::filesystem::path& path, const std::string& ext);
        // Copies the source image now, on the calling thread (the GPU copy into a host-visible image of its own, and
        // the wait for it); operate() then reads that copy and writes the file. The caller makes sure the source holds
        // the wanted frame and that nothing draws into it during the copy (Engine::captureScreenshot). Without this
        // call operate() copies the source when it runs, as before.
        void copyNow();
        void operate() override;

    private:
        void releaseFilename();

        std::filesystem::path mReservedFilename;
        vsg::ref_ptr<vsg::Image> mCopy;
    };
}

#endif
