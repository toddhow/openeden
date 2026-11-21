#pragma once

// Task 0346: Solitude, the execution of Roggvir (SolitudeOpening) and the start of "The Wolf Queen Awakened"
// (MS06Start, MS06), through the game's own story manager, scenes, dialogue and Papyrus (section 1 of
// docs/tasks/0346-skyrim-solitude.md).

#include <cstdint>
#include <string_view>

namespace game_skyrimse::solitude
{
    /// `[extension.skyrimse] solitude` (off; needs `papyrus`, `actor packages`, `quest alias packages`): box triggers
    /// fire for the player, and the PLAYER enters a trigger with his body (feet to `kPlayerBodyHeight` above them), as
    /// the engine's own trigger tests his collision: the gate's box (SolitudeOpening) hangs 36 units above the road.
    inline constexpr std::string_view kKey = "solitude";

    /// OURS (the stand-in player has no collision of its own in the pack): the height of the segment the body tests
    /// with.
    inline constexpr double kPlayerBodyHeight = 128.0;

    inline constexpr std::uint32_t kSolitudeOpeningQuest = 0xB2FD9;
    inline constexpr std::uint32_t kMs06StartQuest = 0x93807;
    inline constexpr std::uint32_t kMs06Quest = 0x26C4D;
}
