#ifndef OPENEDEN_MWRENDER_EXTENTSYNC_H
#define OPENEDEN_MWRENDER_EXTENTSYNC_H

#include <algorithm>

#include <vulkan/vulkan_core.h>

namespace MWRender
{
    /*
     * The size the render areas, the camera viewport and the projection take for the frame about to be recorded
     * (RenderManager::syncExtents).
     *
     * - swapchain: the extent the framebuffers have now.
     * - requested: the size the window asked for (Engine::extent2D), which is the extent the swapchain gets when the next
     *   frame's image acquisition rebuilds it. That rebuild comes after syncExtents, in the same frame.
     * - applied: what was applied last.
     *
     * A render area larger than the framebuffer in use is a validation error and draws nothing (VUID-VkRenderPassBeginInfo-pNext-02852).
     * So each axis is the smaller of the two: a shrink takes effect before the rebuild that makes the framebuffers smaller,
     * and a grow waits until the framebuffers have grown. Where either axis would be zero (a minimised window), nothing
     * changes and `applied` is returned.
     */
    inline VkExtent2D clampArea(VkExtent2D applied, VkExtent2D swapchain, VkExtent2D requested)
    {
        const VkExtent2D area{ std::min(swapchain.width, requested.width), std::min(swapchain.height, requested.height) };
        if (area.width == 0 || area.height == 0)
            return applied;
        return area;
    }
}

#endif
