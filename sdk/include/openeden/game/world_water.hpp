#pragma once

#include <cstdint>

#include <openeden/game/world_model.hpp>
#include <openeden/version.hpp>

// Exterior cell water as plain values (task 0129, milestone M-S2 of docs/design/skyrim-se-implementation-plan.md, 4.5).
// A game pack tells the core, per exterior grid square of a world, whether the square has a water surface and at what
// height. The pack resolves everything game specific (Skyrim SE: the cell's own height, the "no water" sentinels, the
// world's default water height and the parent world's); the core only draws a surface of the cell's size at the
// height. Nothing about the water's material crosses in 0.1 (colours, noise maps, waterfalls are follow-ups).
//
// ABI choice: a NEW interface found by dynamic_cast on the game's registered WorldCellSource, as ActorAppearanceSource
// is (actor_appearance.hpp): not a new virtual on the pinned WorldCellSource and not a member added to a struct a shared
// library fills. A pack that does not implement it has no water. Plain values only.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a default meaning "not supported", a new
// trailing field a consumer only reads) raise the minor number; anything else raises the major number.
// Units: the game's own length unit, Z up (the same as world_terrain.hpp). Threading: main thread only.

namespace openeden::sdk::game
{
    inline constexpr Version kWorldWaterVersion{ 0, 1 };

    /// The water of one exterior grid square.
    struct CellWater
    {
        /// False: the square has no water surface (the data says none, or the world has no water at all).
        bool present = false;
        /// The surface's height in units; meaningful only when `present`.
        float height = 0.0f;

        friend bool operator==(const CellWater&, const CellWater&) = default;
    };

    class CellWaterSource
    {
    public:
        virtual ~CellWaterSource() = default;

        /// The water of the square (x, y) of `world` (parent worlds are consulted by the source). False when the world is
        /// unknown to the source; `out` is then left empty. Never throws.
        [[nodiscard]] virtual bool cellWater(WorldId world, std::int32_t x, std::int32_t y, CellWater& out) const = 0;
    };
}
