#ifndef VSGOPENMW_RENDER_DOWNLOAD_H
#define VSGOPENMW_RENDER_DOWNLOAD_H

#include <optional>

#include <vsg/vk/Context.h>

namespace Render
{
    std::pair<vsg::ref_ptr<vsg::Commands>, vsg::ref_ptr<vsg::Image>> createDownloadCommands(vsg::Device* device,
        vsg::ref_ptr<vsg::Image> srcImage, VkRect2D srcRect, VkFormat srcImageFormat, VkImageLayout srcImageLayout,
        std::optional<VkExtent2D> dstExtent = {});

    void submit(vsg::Device* device, vsg::ref_ptr<vsg::Commands> commands);

    bool mapAndCopy(vsg::Device* device, vsg::ref_ptr<vsg::Image> dstImage, vsg::ref_ptr<vsg::Data> out_data);

    vsg::ref_ptr<vsg::Data> download(vsg::Device* device, vsg::ref_ptr<vsg::Image> srcImage, VkRect2D srcRect,
        VkFormat srcImageFormat, VkImageLayout srcImageLayout);

    // The second half of download(): the pixels of a host-visible image that createDownloadCommands made and that has
    // been submitted and finished. Reading that memory is the slow part (it is asked for as host visible and coherent,
    // not host cached): at 1280 x 720 a screenshot cost the game's main thread about 16 ms with the whole download
    // there and 2 to 3 ms with only the copy (2026-11-04), so a caller may read on another thread
    // (Screenshot::copyNow).
    vsg::ref_ptr<vsg::Data> readDownloadedImage(vsg::Device* device, vsg::ref_ptr<vsg::Image> dstImage);
}

#endif
