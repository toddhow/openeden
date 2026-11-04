#include "download.hpp"

#include <iostream>
#include <limits>

#include <vsg/commands/BlitImage.h>
#include <vsg/commands/Commands.h>
#include <vsg/commands/CopyImage.h>
#include <vsg/commands/PipelineBarrier.h>
#include <vsg/vk/CommandPool.h>
#include <vsg/vk/SubmitCommands.h>

#include <components/debug/debuglog.hpp>
#include <components/vsgutil/image.hpp>

namespace Render // namespace vsgExamples
{
    DownloadSyncMasks downloadSyncMasks(DownloadSync sync)
    {
        DownloadSyncMasks masks;
        switch (sync)
        {
            case DownloadSync::Transfer:
                masks.firstSrcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                masks.srcImageWrites = VK_ACCESS_MEMORY_READ_BIT;
                masks.lastDstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                masks.copyReader = VK_ACCESS_MEMORY_READ_BIT;
                masks.srcImageAfter = VK_ACCESS_MEMORY_READ_BIT;
                break;
            case DownloadSync::BeforePresent:
                // A barrier's first scope holds every command submitted earlier at the stages it names, so ALL_COMMANDS
                // with the attachment and transfer writes covers the GUI pass's draws and its multisample resolve
                // directly; the pass's final layout transition is ordered before it by submission order. Nothing
                // follows in the frame: back to the present layout before the semaphore that present waits for.
                masks.firstSrcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                masks.srcImageWrites = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                masks.lastDstStage = VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                masks.copyReader = VK_ACCESS_HOST_READ_BIT;
                masks.srcImageAfter = 0;
                break;
            case DownloadSync::InFrame:
                // After the scene, tonemap and post-process (colour attachment and transfer writes) and before the GUI
                // pass, which loads the image at the colour attachment stage: the transition back must be ordered
                // before those reads and writes, since the GUI pass's own dependency from outside covers only colour
                // attachment and late fragment test work.
                masks.firstSrcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
                masks.srcImageWrites = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                masks.lastDstStage = VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                masks.copyReader = VK_ACCESS_HOST_READ_BIT;
                masks.srcImageAfter = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;
        }
        return masks;
    }

    std::pair<vsg::ref_ptr<vsg::Commands>, vsg::ref_ptr<vsg::Image>> createDownloadCommands(vsg::Device* device,
        vsg::ref_ptr<vsg::Image> srcImage, VkRect2D srcRect, VkFormat srcImageFormat, VkImageLayout srcImageLayout,
        std::optional<VkExtent2D> optional_dstExtent, DownloadSync sync)
    {
        // OpenEden: the barriers' masks (downloadSyncMasks). Transfer keeps the inherited ones.
        const DownloadSyncMasks masks = downloadSyncMasks(sync);
        const VkPipelineStageFlags firstSrcStage = masks.firstSrcStage;
        const VkAccessFlags srcImageWrites = masks.srcImageWrites;
        const VkPipelineStageFlags lastDstStage = masks.lastDstStage;
        const VkAccessFlags copyReader = masks.copyReader;
        const VkAccessFlags srcImageAfter = masks.srcImageAfter;

        auto targetImageFormat = srcImageFormat;
        auto dstExtent = srcRect.extent;
        if (optional_dstExtent)
            dstExtent = *optional_dstExtent;

        //
        // 1) Check to see of Blit is supported.
        //
        auto physicalDevice = device->getPhysicalDevice();
        VkFormatProperties srcFormatProperties;
        vkGetPhysicalDeviceFormatProperties(*(physicalDevice), srcImageFormat, &srcFormatProperties);

        VkFormatProperties destFormatProperties;
        vkGetPhysicalDeviceFormatProperties(
            *(physicalDevice), targetImageFormat /*downloadFormat*/, &destFormatProperties);

        bool supportsBlit = ((srcFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0)
            && ((destFormatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0);
        /*
        if (supportsBlit)
        {
            const auto downloadFormat = VK_FORMAT_R8G8B8A8_UNORM;
            // we can automatically convert the image format when blit, so take advantage of it to ensure RGBA
            targetImageFormat = downloadFormat;
        }
        */

        //
        // 2) create image to write to
        //
        auto dstImage = vsgUtil::createImage(targetImageFormat, dstExtent, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        dstImage->tiling = VK_IMAGE_TILING_LINEAR;
        dstImage->compile(device);

        auto deviceMemory = vsg::DeviceMemory::create(device, dstImage->getMemoryRequirements(device->deviceID),
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        dstImage->bind(deviceMemory, 0);

        //
        // 3) create commands
        auto commands = vsg::Commands::create();

        // 3.a) transition dstImage to transfer destination initialLayout
        auto transitiondstImageToDestinationLayoutBarrier
            = vsg::ImageMemoryBarrier::create(0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, dstImage,
                VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

        // 3.b) transition image from current to transfer source initialLayout
        auto transitionsrcImageToTransferSourceLayoutBarrier = vsg::ImageMemoryBarrier::create(srcImageWrites,
            VK_ACCESS_TRANSFER_READ_BIT, srcImageLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED, srcImage, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

        auto cmd_transitionForTransferBarrier
            = vsg::PipelineBarrier::create(firstSrcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                transitiondstImageToDestinationLayoutBarrier, transitionsrcImageToTransferSourceLayoutBarrier);

        commands->addChild(cmd_transitionForTransferBarrier);

        if (supportsBlit)
        {
            // 3.c.1) if blit using VkCmdBlitImage
            VkImageBlit region{};
            region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.srcSubresource.layerCount = 1;
            region.srcOffsets[0]
                = { static_cast<int32_t>(srcRect.offset.x), static_cast<int32_t>(srcRect.offset.y), 0 };
            region.srcOffsets[1] = { static_cast<int32_t>(srcRect.offset.x + srcRect.extent.width),
                static_cast<int32_t>(srcRect.offset.y + srcRect.extent.height), 1 };

            region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.dstSubresource.layerCount = 1;
            region.dstOffsets[0] = { 0, 0, 0 };
            region.dstOffsets[1] = { static_cast<int32_t>(dstExtent.width), static_cast<int32_t>(dstExtent.height), 1 };

            auto blitImage = vsg::BlitImage::create();
            blitImage->srcImage = srcImage;
            blitImage->srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blitImage->dstImage = dstImage;
            blitImage->dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blitImage->regions = { region };
            blitImage->filter = VK_FILTER_NEAREST; //;

            commands->addChild(blitImage);
        }
        else
        {
            // 3.c.2) else use VkCmdCopyImage
            VkImageCopy region{};
            region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.srcSubresource.layerCount = 1;
            region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.dstSubresource.layerCount = 1;
            region.srcOffset = { srcRect.offset.x, srcRect.offset.y, 0 };
            region.extent = { std::min(srcRect.extent.width, dstExtent.width),
                std::min(srcRect.extent.height, dstExtent.height), 1 };

            auto copyImage = vsg::CopyImage::create();
            copyImage->srcImage = srcImage;
            copyImage->srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyImage->dstImage = dstImage;
            copyImage->dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            copyImage->regions = { region };

            commands->addChild(copyImage);
        }

        // 3.d) transition destinate image from transfer destination layout to general layout to enable mapping to image
        // DeviceMemory
        auto transitiondstImageToMemoryReadBarrier = vsg::ImageMemoryBarrier::create(VK_ACCESS_TRANSFER_WRITE_BIT,
            copyReader, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED, dstImage, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

        // 3.e) transition source image back
        auto transitionsrcImageBackBarrier = vsg::ImageMemoryBarrier::create(VK_ACCESS_TRANSFER_READ_BIT, srcImageAfter,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcImageLayout, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            srcImage, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

        auto cmd_transitionFromTransferBarrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TRANSFER_BIT,
            lastDstStage, 0, transitiondstImageToMemoryReadBarrier, transitionsrcImageBackBarrier);

        commands->addChild(cmd_transitionFromTransferBarrier);
        return { commands, dstImage };
    }

    bool mapAndCopy(vsg::Device* device, vsg::ref_ptr<vsg::Image> dstImage, vsg::ref_ptr<vsg::Data> out_data)
    {
        if (out_data->properties.format != dstImage->format)
        {
            // std::cerr << "out_data->properties.format != dstImage->format" << std::endl;
            return false;
        }

        //
        // 4) map image and copy
        //
        VkImageSubresource subResource{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
        VkSubresourceLayout subResourceLayout;
        vkGetImageSubresourceLayout(*device, dstImage->vk(device->deviceID), &subResource, &subResourceLayout);

        auto deviceMemory = dstImage->getDeviceMemory(device->deviceID);

        // Map the buffer memory that will automatically unmap itself on destruction.
        auto w = dstImage->extent.width;
        auto h = dstImage->extent.height;
        auto mappedData = vsg::MappedData<vsg::ubyteArray>::create(deviceMemory, subResourceLayout.offset, 0,
            vsg::Data::Properties{ dstImage->format }, subResourceLayout.rowPitch * h);
        for (uint32_t row = 0; row < h; ++row)
            std::memcpy(out_data->dataPointer(row * w), mappedData->dataPointer(row * subResourceLayout.rowPitch),
                w * out_data->properties.stride);
        return true;
    }

    vsg::ref_ptr<vsg::Data> download(vsg::Device* device, vsg::ref_ptr<vsg::Image> srcImage, VkRect2D srcRect,
        VkFormat srcImageFormat, VkImageLayout srcImageLayout)
    {
        auto [commands, dstImage] = createDownloadCommands(device, srcImage, srcRect, srcImageFormat, srcImageLayout);
        if (const VkResult result = submit(device, commands); result != VK_SUCCESS)
        {
            Log(Debug::Warning) << "Image download failed: the copy returned VkResult " << result;
            return {};
        }
        return readDownloadedImage(device, dstImage);
    }

    vsg::ref_ptr<vsg::Data> readDownloadedImage(vsg::Device* device, vsg::ref_ptr<vsg::Image> dstImage)
    {
        vsg::ref_ptr<vsg::Data> array;
        switch (dstImage->format)
        {
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_UNORM:
            {
                array = vsg::ubvec4Array2D::create(
                    dstImage->extent.width, dstImage->extent.height, vsg::Data::Properties(dstImage->format));
                break;
            }
            case VK_FORMAT_R8G8B8_UNORM:
            case VK_FORMAT_B8G8R8_UNORM:
            {
                array = vsg::ubvec3Array2D::create(
                    dstImage->extent.width, dstImage->extent.height, vsg::Data::Properties(dstImage->format));
                break;
            }
            default:
                std::cerr << "!download(VkFormat=" << dstImage->format << ")" << std::endl;
                return {};
        }

        mapAndCopy(device, dstImage, array);
        return array;
    }

    VkResult submit(vsg::Device* device, vsg::ref_ptr<vsg::Commands> commands)
    {
        auto physicalDevice = device->getPhysicalDevice();
        auto fence = vsg::Fence::create(device);
        auto queueFamilyIndex = physicalDevice->getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
        auto commandPool = vsg::CommandPool::create(device, queueFamilyIndex);
        auto queue = device->getQueue(queueFamilyIndex);

        // OpenEden: no timeout (it was 100 s). On VK_TIMEOUT submitCommandsToQueue freed a command buffer the GPU may
        // still run, and the pool went with this function; a lost device ends the wait with an error instead.
        return vsg::submitCommandsToQueue(commandPool, fence, std::numeric_limits<uint64_t>::max(), queue,
            [&](vsg::CommandBuffer& commandBuffer) { commands->record(commandBuffer); });
    }
}
