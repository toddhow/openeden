#ifndef OPENEDEN_ANIMATION_POINTSHADOWCARRIER_H
#define OPENEDEN_ANIMATION_POINTSHADOWCARRIER_H

#include <array>
#include <atomic>
#include <cstdint>

#include <vsg/core/Mask.h>

namespace Anim
{
    /*
     * A light's own model does not cast onto its surroundings (task 0077, decision 0012): a lantern's bars would draw
     * spokes of shadow, a torch holder would be worse. The model that carries a point light is an Anim::Transform
     * that knows its light (Transform::setPointShadowCarrier); when it is recorded in the shadow pass of the slot that
     * holds that light it draws nothing. Every other view (the main view, the sun's cascades, the other slots) draws
     * it.
     *
     * The shadow pass of slot k has the bit pointShadowSlotMask(k) in its view's mask; the record traversal's mask in
     * the subgraph is the parent's mask AND that mask, so a node can tell which slot it is being recorded for.
     * Views whose mask has every bit (the main view) or none of the slot bits name no slot.
     *
     * The table of which light holds which slot is written by MWRender::PointShadows::update on the main thread, while
     * the record threads are idle, and read by the record threads.
     */
    constexpr int pointShadowSlotCount = 8; // Render::PointShadow::maxSlots (a test compares them)
    constexpr unsigned pointShadowFirstSlotBit = 48; // above every mask of the renderer (the highest is bit 20)

    constexpr vsg::Mask pointShadowSlotMask(int slot)
    {
        return vsg::Mask(1) << (pointShadowFirstSlotBit + static_cast<unsigned>(slot));
    }

    /// The slot a traversal mask is for: the one slot bit it has, or -1 for none or several.
    constexpr int pointShadowSlotOfMask(vsg::Mask mask)
    {
        int found = -1;
        for (int k = 0; k < pointShadowSlotCount; ++k)
        {
            if ((mask & pointShadowSlotMask(k)) != 0)
            {
                if (found >= 0)
                    return -1;
                found = k;
            }
        }
        return found;
    }

    /// Whether the carrier of `lightId` is left out of a traversal with `mask`. `ownerOf(slot)` gives the id of the
    /// light that holds the slot (0 for none). A model without a light (id 0) is never left out.
    template <class OwnerOf>
    bool pointShadowCarrierSkipped(vsg::Mask mask, std::uint64_t lightId, const OwnerOf& ownerOf)
    {
        if (lightId == 0)
            return false;
        const int slot = pointShadowSlotOfMask(mask);
        return slot >= 0 && ownerOf(slot) == lightId;
    }

    namespace Detail
    {
        inline std::array<std::atomic<std::uint64_t>, pointShadowSlotCount>& pointShadowOwners()
        {
            static std::array<std::atomic<std::uint64_t>, pointShadowSlotCount> owners{};
            return owners;
        }
    }

    /// The id (the PointLight node's address, never followed) of the light that holds `slot`, or 0.
    inline std::uint64_t pointShadowOwner(int slot)
    {
        if (slot < 0 || slot >= pointShadowSlotCount)
            return 0;
        return Detail::pointShadowOwners()[static_cast<std::size_t>(slot)].load(std::memory_order_relaxed);
    }

    inline void setPointShadowOwner(int slot, std::uint64_t lightId)
    {
        if (slot < 0 || slot >= pointShadowSlotCount)
            return;
        Detail::pointShadowOwners()[static_cast<std::size_t>(slot)].store(lightId, std::memory_order_relaxed);
    }
}

#endif
