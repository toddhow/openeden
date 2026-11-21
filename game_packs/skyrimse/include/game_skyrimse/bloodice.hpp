#pragma once

// Task 0333: "Blood on the Ice" (MS11), the Windhelm murders, through the game's own story manager, scenes, dialogue
// and Papyrus (decompiled with houseCARL; section 1 of docs/tasks/0333-skyrim-blood-on-the-ice.md).
//
// The chain: the story manager's CLOC node `MS11WindhelmEntranceCounting` starts the quest `MS11Kicker` whenever the
// player enters Windhelm (from nowhere or from a place with no LocTypeCity keyword); the kicker's fragment counts the
// entries in the global `EnterWindhelmCount` and, at the fourth entry between 19:00 and 7:00 (the global `GameHour`),
// calls `MS11.Start()`. MS11's stage 0 fragment sets the quest up and sets stage 1; its opening scene has no actions,
// only a phase fragment that puts the witnesses and the guard at the crime scene.

#include <cstdint>
#include <optional>
#include <string_view>

namespace game_skyrimse::bloodice
{
    /// `[extension.skyrimse] blood on the ice` (off; needs `papyrus`, `actor packages`, `quest alias packages`,
    /// `actor cell moves`): the game's hour of day reaches the Papyrus global `GameHour`, and a scene with no actions
    /// begins although none of its actors is loaded (the witnesses stand in houses of Windhelm, which are not loaded).
    inline constexpr std::string_view kKey = "blood on the ice";

    inline constexpr std::uint32_t kGameHourGlobal
        = 0x38; ///< GLOB GameHour (Skyrim.esm; VERIFIED by the kicker's reads)
    inline constexpr std::uint32_t kMs11Quest = 0x1F7A3; ///< MS11 "Blood on the Ice"
    inline constexpr std::uint32_t kKickerQuest = 0xBAC24; ///< MS11Kicker

    /// The value the global `GameHour` takes from the clock the core gave the pack: the hour of the day, or nothing
    /// when the core has no clock (the global then keeps the record's value).
    [[nodiscard]] inline std::optional<float> gameHourValue(float hour, bool valid)
    {
        if (!valid || !(hour >= 0.f) || !(hour < 24.f))
            return std::nullopt;
        return hour;
    }
}
