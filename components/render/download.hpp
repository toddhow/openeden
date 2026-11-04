#ifndef VSGOPENMW_RENDER_DOWNLOAD_H
#define VSGOPENMW_RENDER_DOWNLOAD_H

#include <optional>

#include <vsg/state/Buffer.h>

#include <vsg/vk/Context.h>

namespace Render
{
    // How the copy is ordered against earlier work on the source image (OpenEden, 2026-11-04).
    enum class DownloadSync
    {
        // As before: from the transfer stage, for a copy in a submission of its own after a wait for the image's
        // writer (the world map, the headless copy, download()).
        Transfer,
        // Recorded in the frame's own submission after the last pass that writes the swapchain image, before it is
        // presented (screenshots: FrameCapture on the game's GUI graph or an editor view's graph): the first scope is
        // every stage with the attachment and transfer writes, the image goes back to srcImageLayout, and the copy is
        // made visible to the host.
        BeforePresent,
        // Recorded in the frame's own submission between passes that write the swapchain image (the save thumbnail,
        // after the scene, tonemap and post-process, before the GUI pass): the first scope is the colour attachment
        // output and transfer stages with their writes, the transition back is ordered before the next pass's colour
        // attachment reads and writes, and the copy is made visible to the host.
        InFrame,
    };

    // The masks createDownloadCommands puts in its two barriers for a sync mode (pure; tested in
    // testframecapture.cpp). The first barrier: firstSrcStage with srcImageWrites on the source; the second: from the
    // transfer stage to lastDstStage, copyReader on the copy and srcImageAfter on the source.
    struct DownloadSyncMasks
    {
        VkPipelineStageFlags firstSrcStage = 0;
        VkAccessFlags srcImageWrites = 0;
        VkPipelineStageFlags lastDstStage = 0;
        VkAccessFlags copyReader = 0;
        VkAccessFlags srcImageAfter = 0;
    };
    DownloadSyncMasks downloadSyncMasks(DownloadSync sync);

    std::pair<vsg::ref_ptr<vsg::Commands>, vsg::ref_ptr<vsg::Image>> createDownloadCommands(vsg::Device* device,
        vsg::ref_ptr<vsg::Image> srcImage, VkRect2D srcRect, VkFormat srcImageFormat, VkImageLayout srcImageLayout,
        std::optional<VkExtent2D> dstExtent = {}, DownloadSync sync = DownloadSync::Transfer);

    // The middle of a source of the given size, cut to the aspect ratio of target (round four, finding 3): OpenMW
    // 0.51's save thumbnail did this before it scaled (apps/openmw/mwrender/screenshotmanager.cpp at 442eec6e2f,
    // ReadImageFromFramebufferCallback::drawImplementation: the padding on each side is the excess rounded down to a
    // whole pixel and halved). A source of the target's aspect is whole; a zero size gives the whole source. Pure.
    VkRect2D cropToAspect(VkExtent2D source, VkExtent2D target);

    // Whether a linear-filtered blit from an optimal-tiling image with the source format's features into an
    // optimal-tiling image with the destination format's can be recorded (Vulkan: vkCmdBlitImage valid usage: the
    // source needs BLIT_SRC, and SAMPLED_IMAGE_FILTER_LINEAR for VK_FILTER_LINEAR; the destination BLIT_DST). Pure.
    bool blitScalingSupported(VkFormatFeatureFlags srcOptimalFeatures, VkFormatFeatureFlags dstOptimalFeatures);

    // A host-visible buffer a scaled copy ends in, tightly packed rows of extent.width pixels of format.
    struct HostBuffer
    {
        vsg::ref_ptr<vsg::Buffer> buffer;
        VkExtent2D extent{};
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    struct ScaledDownload
    {
        vsg::ref_ptr<vsg::Commands> commands;
        HostBuffer target;
    };

    // The save thumbnail's copy (round four, finding 3): srcRect of the source image is blitted with VK_FILTER_LINEAR
    // into an optimal-tiling image of dstExtent, which is copied to a host-visible buffer. Before, the blit went
    // straight into a linear-tiling host image with VK_FILTER_NEAREST, a decimation, and where the device could not
    // blit into linear tiling the copy cut the top left corner of the screen instead of scaling it. Null (with a
    // warning) when the device cannot blit the format that way. The barriers follow the sync mode as
    // createDownloadCommands'.
    std::optional<ScaledDownload> createScaledDownloadCommands(vsg::Device* device, vsg::ref_ptr<vsg::Image> srcImage,
        VkRect2D srcRect, VkFormat srcImageFormat, VkImageLayout srcImageLayout, VkExtent2D dstExtent,
        DownloadSync sync);

    // The pixels of a finished ScaledDownload.
    vsg::ref_ptr<vsg::Data> readDownloadedBuffer(vsg::Device* device, const HostBuffer& target);

    // Submits the commands on the device's graphics queue and waits for them, with no limit: VSG's
    // submitCommandsToQueue frees its command buffer and this its pool when they return, so they must not return while
    // the GPU may still run them (the old 100 s timeout did). So it waits forever if the GPU never finishes and the
    // driver never reports a lost device (Windows reports a hung GPU as lost after about 2 s, its timeout detection);
    // its callers are download() (the world map, on the operation thread) and the headless copy. Returns the submit's
    // or the wait's result; anything but VK_SUCCESS (a lost device) means the copy holds nothing.
    VkResult submit(vsg::Device* device, vsg::ref_ptr<vsg::Commands> commands);

    bool mapAndCopy(vsg::Device* device, vsg::ref_ptr<vsg::Image> dstImage, vsg::ref_ptr<vsg::Data> out_data);

    // Null, with a log line, when the submit fails.
    vsg::ref_ptr<vsg::Data> download(vsg::Device* device, vsg::ref_ptr<vsg::Image> srcImage, VkRect2D srcRect,
        VkFormat srcImageFormat, VkImageLayout srcImageLayout);

    // The second half of download(): the pixels of a host-visible image that createDownloadCommands made and that has
    // been submitted and finished. Reading that memory is the slow part (it is asked for as host visible and coherent,
    // not host cached): at 1280 x 720 a screenshot cost the game's main thread about 16 ms with the whole download
    // there (2026-11-04), so it is read on the writer thread (Screenshot::operate).
    vsg::ref_ptr<vsg::Data> readDownloadedImage(vsg::Device* device, vsg::ref_ptr<vsg::Image> dstImage);
}

#endif
