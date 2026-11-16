#pragma once

#include <array>

#include <openeden/version.hpp>

// The player's own image space (task 0257): a game's spells and effects can change how the picture looks while they
// run (Skyrim's Night Eye tints the view blue, desaturates it and brightens it). The pack knows which effect is
// active and how it fades; the core owns the picture. The pack describes the change as plain numbers through this
// optional interface, found by dynamic_cast on the game's registered WorldCellSource (as PlayerIdentitySource is),
// and the core POLLS it once a frame. The call never blocks and allocates nothing.
//
// The numbers are a multiply and an add per value, the form the game's image space modifiers have: the value the
// picture would have (the weather's, or neutral) becomes `value * mult + add`. A tint is a colour and an amount in 0
// to 1 (0 = no tint). `active` false means "no change at all", and the core then draws exactly what it drew without
// this interface.
//
// Version policy: DRAFT 0.1. Additive changes (a trailing field the core only reads, a trailing virtual with a
// default) raise the minor number. Threading: main thread only.

namespace openeden::sdk::game
{
    inline constexpr Version kPlayerImageSpaceVersion{ 0, 1 };

    struct PlayerImageSpaceState
    {
        bool active = false;
        float saturationMult = 1.f;
        float saturationAdd = 0.f;
        float brightnessMult = 1.f;
        float brightnessAdd = 0.f;
        float contrastMult = 1.f;
        float contrastAdd = 0.f;
        float tintAmount = 0.f;
        std::array<float, 3> tint{ 1.f, 1.f, 1.f };
    };

    class PlayerImageSpaceSource
    {
    public:
        virtual ~PlayerImageSpaceSource() = default;

        /// Cheap and non-blocking. False when the pack has no image space for the player (the setting is off).
        [[nodiscard]] virtual bool playerImageSpace(PlayerImageSpaceState& out) = 0;
    };
}
