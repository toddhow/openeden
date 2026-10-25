#pragma once

#include <openeden/physics.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace openeden::plugin
{
    /**
     * @brief Manages registration of named collision layers and declared collision pairs.
     * Enforces a maximum of 32 distinct layers corresponding to a 32-bit collision mask.
     */
    class CollisionLayerRegistry
    {
    public:
        static constexpr std::uint32_t kMaxLayers = 32;

        CollisionLayerRegistry() = default;
        ~CollisionLayerRegistry() = default;

        // Movable and copyable
        CollisionLayerRegistry(const CollisionLayerRegistry&) = default;
        CollisionLayerRegistry& operator=(const CollisionLayerRegistry&) = default;
        CollisionLayerRegistry(CollisionLayerRegistry&&) noexcept = default;
        CollisionLayerRegistry& operator=(CollisionLayerRegistry&&) noexcept = default;

        /**
         * @brief Registers a named collision layer, assigning a unique deterministic single bit.
         * Returns 0 if layer count exceeds maximum capacity (32).
         */
        openeden::sdk::CollisionLayer registerLayer(std::string_view name);

        [[nodiscard]] bool hasLayer(std::string_view name) const;

        [[nodiscard]] std::size_t layerCount() const noexcept;

        [[nodiscard]] std::uint32_t getLayerBit(std::string_view name) const;

        [[nodiscard]] std::uint32_t getLayerMask(std::string_view name) const;

        /**
         * @brief Symmetrically enables collision between layerA and layerB.
         */
        void declareCollisionPair(std::string_view layerA, std::string_view layerB);

        [[nodiscard]] bool canCollide(std::string_view layerA, std::string_view layerB) const;

        [[nodiscard]] bool canCollide(std::uint32_t bitA, std::uint32_t bitB) const;

    private:
        std::map<std::string, std::uint32_t, std::less<>> mLayerBits;
        std::map<std::uint32_t, std::uint32_t> mCollisionMasks;
        std::uint32_t mNextBitIndex = 0;
    };
} // namespace openeden::plugin
