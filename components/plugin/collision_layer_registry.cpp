#include "collision_layer_registry.hpp"

#include <iostream>

namespace openeden::plugin
{
    openeden::sdk::CollisionLayer CollisionLayerRegistry::registerLayer(std::string_view name)
    {
        auto it = mLayerBits.find(name);
        if (it != mLayerBits.end())
        {
            return it->second;
        }

        if (mNextBitIndex >= kMaxLayers)
        {
            std::cerr << "OpenEden CollisionLayerRegistry: ERROR: Exceeded maximum supported layers (" << kMaxLayers
                      << "). Layer '" << name << "' rejected.\n";
            return 0;
        }

        const std::uint32_t bit = 1u << mNextBitIndex++;
        mLayerBits.emplace(std::string(name), bit);
        return bit;
    }

    bool CollisionLayerRegistry::hasLayer(std::string_view name) const
    {
        return mLayerBits.find(name) != mLayerBits.end();
    }

    std::size_t CollisionLayerRegistry::layerCount() const noexcept
    {
        return mLayerBits.size();
    }

    std::uint32_t CollisionLayerRegistry::getLayerBit(std::string_view name) const
    {
        auto it = mLayerBits.find(name);
        if (it != mLayerBits.end())
        {
            return it->second;
        }
        return 0;
    }

    std::uint32_t CollisionLayerRegistry::getLayerMask(std::string_view name) const
    {
        const std::uint32_t bit = getLayerBit(name);
        if (bit == 0)
            return 0;

        auto it = mCollisionMasks.find(bit);
        if (it != mCollisionMasks.end())
        {
            return it->second;
        }
        return 0;
    }

    void CollisionLayerRegistry::declareCollisionPair(std::string_view layerA, std::string_view layerB)
    {
        const std::uint32_t bitA = registerLayer(layerA);
        const std::uint32_t bitB = registerLayer(layerB);

        if (bitA == 0 || bitB == 0)
        {
            return;
        }

        // Symmetric collision mask updates
        mCollisionMasks[bitA] |= bitB;
        mCollisionMasks[bitB] |= bitA;
    }

    bool CollisionLayerRegistry::canCollide(std::string_view layerA, std::string_view layerB) const
    {
        const std::uint32_t bitA = getLayerBit(layerA);
        const std::uint32_t bitB = getLayerBit(layerB);
        if (bitA == 0 || bitB == 0)
            return false;

        return canCollide(bitA, bitB);
    }

    bool CollisionLayerRegistry::canCollide(std::uint32_t bitA, std::uint32_t bitB) const
    {
        if (bitA == 0 || bitB == 0)
            return false;

        auto it = mCollisionMasks.find(bitA);
        if (it != mCollisionMasks.end())
        {
            return (it->second & bitB) != 0;
        }
        return false;
    }
} // namespace openeden::plugin
