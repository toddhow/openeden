#ifndef VSGOPENMW_RENDER_DOWNLOAD_H
#define VSGOPENMW_RENDER_DOWNLOAD_H

#include <optional>

#include <vsg/vk/Context.h>

namespace Render
{
    // How the copy is ordered against earlier work on the source image (OpenEden, 2026-11-04).
    enum class DownloadSync
    {
        // As before: from the transfer stage, for a copy in a submission of its own after a wait for the image's
        // writer (the editor's capture, the world map, the save-game thumbnail).
        Transfer,
        // Recorded in the frame's own submission, after the last render pass that writes the swapchain image and
        // before it is presented (FrameCapture): waits for every earlier stage and attachment write, returns the
        // image to srcImageLayout, and makes the copy visible to the host.
        BeforePresent,
    };

    std::pair<vsg::ref_ptr<vsg::Commands>, vsg::ref_ptr<vsg::Image>> createDownloadCommands(vsg::Device* device,
        vsg::ref_ptr<vsg::Image> srcImage, VkRect2D srcRect, VkFormat srcImageFormat, VkImageLayout srcImageLayout,
        std::optional<VkExtent2D> dstExtent = {}, DownloadSync sync = DownloadSync::Transfer);

    // Submits the commands on the device's graphics queue and waits for them, with no timeout: VSG's
    // submitCommandsToQueue frees its command buffer and this its pool when they return, so they must not return while
    // the GPU may still run them (a timeout did). Returns the submit's or the wait's result; anything but VK_SUCCESS
    // (a lost device) means the copy holds nothing.
    VkResult submit(vsg::Device* device, vsg::ref_ptr<vsg::Commands> commands);

    bool mapAndCopy(vsg::Device* device, vsg::ref_ptr<vsg::Image> dstImage, vsg::ref_ptr<vsg::Data> out_data);

    // Null, with a log line, when the submit fails.
    vsg::ref_ptr<vsg::Data> download(vsg::Device* device, vsg::ref_ptr<vsg::Image> srcImage, VkRect2D srcRect,
        VkFormat srcImageFormat, VkImageLayout srcImageLayout);

    // The second half of download(): the pixels of a host-visible image that createDownloadCommands made and that has
    // been submitted and finished. Reading that memory is the slow part (it is asked for as host visible and coherent,
    // not host cached): at 1280 x 720 a screenshot cost the game's main thread about 16 ms with the whole download
    // there and 2 to 3 ms with only the copy (2026-11-04), so it is read on the writer thread (Screenshot::operate).
    vsg::ref_ptr<vsg::Data> readDownloadedImage(vsg::Device* device, vsg::ref_ptr<vsg::Image> dstImage);
}

#endif
