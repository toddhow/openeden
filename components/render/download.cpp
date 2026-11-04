#include "download.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>

#include <vsg/commands/BlitImage.h>
#include <vsg/commands/Commands.h>
#include <vsg/commands/CopyImage.h>
#include <vsg/commands/CopyImageToBuffer.h>
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
            if (dstExtent.width != srcRect.extent.width || dstExtent.height != srcRect.extent.height)
                Log(Debug::Warning) << "The device cannot blit into a host-visible image of format " << srcImageFormat
                                    << " (VK_FORMAT_FEATURE_BLIT_DST_BIT in linear tiling): the copy is cut to "
                                    << std::min(srcRect.extent.width, dstExtent.width) << " x "
                                    << std::min(srcRect.extent.height, dstExtent.height) << " from the corner of "
                                    << srcRect.extent.width << " x " << srcRect.extent.height
                                    << " instead of being scaled to " << dstExtent.width << " x " << dstExtent.height;
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

    VkRect2D cropToAspect(VkExtent2D source, VkExtent2D target)
    {
        const VkRect2D whole{ { 0, 0 }, source };
        if (source.width == 0 || source.height == 0 || target.width == 0 || target.height == 0)
            return whole;
        // OpenMW 0.51: imageaspect = mWidth / mHeight; leftPadding = max(0, int(screenW - screenH * aspect) / 2);
        // topPadding = max(0, int(screenH - screenW / aspect) / 2).
        const double aspect = static_cast<double>(target.width) / static_cast<double>(target.height);
        const int left = std::max(0, static_cast<int>(static_cast<double>(source.width) - source.height * aspect) / 2);
        const int top = std::max(0, static_cast<int>(static_cast<double>(source.height) - source.width / aspect) / 2);
        const int width = static_cast<int>(source.width) - left * 2;
        const int height = static_cast<int>(source.height) - top * 2;
        if (width <= 0 || height <= 0)
            return whole;
        return VkRect2D{ { left, top }, { static_cast<uint32_t>(width), static_cast<uint32_t>(height) } };
    }

    bool blitScalingSupported(VkFormatFeatureFlags srcOptimalFeatures, VkFormatFeatureFlags dstOptimalFeatures)
    {
        constexpr VkFormatFeatureFlags srcNeeds
            = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        return (srcOptimalFeatures & srcNeeds) == srcNeeds
            && (dstOptimalFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;
    }

    namespace
    {
        // The bytes per pixel of the formats a window's image has, 0 for anything else.
        VkDeviceSize bytesPerPixel(VkFormat format)
        {
            switch (format)
            {
                case VK_FORMAT_R8G8B8A8_UNORM:
                case VK_FORMAT_B8G8R8A8_UNORM:
                    return 4;
                case VK_FORMAT_R8G8B8_UNORM:
                case VK_FORMAT_B8G8R8_UNORM:
                    return 3;
                default:
                    return 0;
            }
        }
    }

    std::optional<ScaledDownload> createScaledDownloadCommands(vsg::Device* device, vsg::ref_ptr<vsg::Image> srcImage,
        VkRect2D srcRect, VkFormat srcImageFormat, VkImageLayout srcImageLayout, VkExtent2D dstExtent,
        DownloadSync sync)
    {
        const DownloadSyncMasks masks = downloadSyncMasks(sync);

        // The picture keeps the source's format (the same bytes and channel order as an unscaled screenshot).
        const VkDeviceSize pixelBytes = bytesPerPixel(srcImageFormat);
        if (pixelBytes == 0 || dstExtent.width == 0 || dstExtent.height == 0)
        {
            Log(Debug::Warning) << "A scaled copy of format " << srcImageFormat << " to " << dstExtent.width << " x "
                                << dstExtent.height << " cannot be made";
            return std::nullopt;
        }
        VkFormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(*(device->getPhysicalDevice()), srcImageFormat, &formatProperties);
        if (!blitScalingSupported(formatProperties.optimalTilingFeatures, formatProperties.optimalTilingFeatures))
        {
            Log(Debug::Warning) << "The device cannot blit format " << srcImageFormat
                                << " with linear filtering between optimal-tiling images";
            return std::nullopt;
        }

        // The scaled picture, in device-local memory: the blit's destination and the copy's source.
        auto scaled = vsgUtil::createImage(
            srcImageFormat, dstExtent, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        scaled->tiling = VK_IMAGE_TILING_OPTIMAL;
        scaled->compile(device);
        auto scaledMemory = vsg::DeviceMemory::create(
            device, scaled->getMemoryRequirements(device->deviceID), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        scaled->bind(scaledMemory, 0);

        // The host-visible buffer the pixels end in.
        const VkDeviceSize bufferSize = pixelBytes * dstExtent.width * dstExtent.height;
        auto buffer = vsg::createBufferAndMemory(device, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!buffer)
        {
            Log(Debug::Warning) << "No host-visible memory for the scaled copy";
            return std::nullopt;
        }

        const VkImageSubresourceRange colour{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        auto commands = vsg::Commands::create();

        // 1) the source to transfer source, the scaled image to transfer destination
        commands->addChild(vsg::PipelineBarrier::create(masks.firstSrcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            vsg::ImageMemoryBarrier::create(0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, scaled, colour),
            vsg::ImageMemoryBarrier::create(masks.srcImageWrites, VK_ACCESS_TRANSFER_READ_BIT, srcImageLayout,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, srcImage,
                colour)));

        // 2) the scaling blit, with linear filtering
        VkImageBlit region{};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.layerCount = 1;
        region.srcOffsets[0] = { srcRect.offset.x, srcRect.offset.y, 0 };
        region.srcOffsets[1] = { srcRect.offset.x + static_cast<int32_t>(srcRect.extent.width),
            srcRect.offset.y + static_cast<int32_t>(srcRect.extent.height), 1 };
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.layerCount = 1;
        region.dstOffsets[0] = { 0, 0, 0 };
        region.dstOffsets[1] = { static_cast<int32_t>(dstExtent.width), static_cast<int32_t>(dstExtent.height), 1 };
        auto blit = vsg::BlitImage::create();
        blit->srcImage = srcImage;
        blit->srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blit->dstImage = scaled;
        blit->dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blit->regions = { region };
        blit->filter = VK_FILTER_LINEAR;
        commands->addChild(blit);

        // 3) the scaled image to transfer source; the source image goes back now, since nothing here reads it again
        // (the destination stage names what follows in the frame, as in the unscaled copy)
        commands->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT | masks.lastDstStage, 0,
            vsg::ImageMemoryBarrier::create(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED, scaled, colour),
            vsg::ImageMemoryBarrier::create(VK_ACCESS_TRANSFER_READ_BIT, masks.srcImageAfter,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcImageLayout, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                srcImage, colour)));

        // 4) the copy to the host buffer, tightly packed
        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = { dstExtent.width, dstExtent.height, 1 };
        auto copy = vsg::CopyImageToBuffer::create();
        copy->srcImage = scaled;
        copy->srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copy->dstBuffer = buffer;
        copy->regions = { copyRegion };
        commands->addChild(copy);

        // 5) the buffer to the host
        commands->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
            vsg::BufferMemoryBarrier::create(VK_ACCESS_TRANSFER_WRITE_BIT, masks.copyReader, VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED, buffer, 0, bufferSize)));

        return ScaledDownload{ commands, HostBuffer{ buffer, dstExtent, srcImageFormat } };
    }

    vsg::ref_ptr<vsg::Data> readDownloadedBuffer(vsg::Device* device, const HostBuffer& target)
    {
        const VkDeviceSize pixelBytes = bytesPerPixel(target.format);
        if (!target.buffer || pixelBytes == 0)
            return {};
        vsg::ref_ptr<vsg::Data> array;
        if (pixelBytes == 4)
            array = vsg::ubvec4Array2D::create(
                target.extent.width, target.extent.height, vsg::Data::Properties(target.format));
        else
            array = vsg::ubvec3Array2D::create(
                target.extent.width, target.extent.height, vsg::Data::Properties(target.format));

        const size_t size = pixelBytes * target.extent.width * target.extent.height;
        auto deviceMemory = target.buffer->getDeviceMemory(device->deviceID);
        auto mapped = vsg::MappedData<vsg::ubyteArray>::create(deviceMemory,
            target.buffer->getMemoryOffset(device->deviceID), 0, vsg::Data::Properties{ target.format }, size);
        std::memcpy(array->dataPointer(), mapped->dataPointer(), size);
        return array;
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
