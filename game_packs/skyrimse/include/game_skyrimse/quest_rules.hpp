#pragma once

// Task 0356: ONE switch for the general rules the quest slices (tasks 0261 to 0350) each switched on under a setting of
// their own. The table of rules, and which settings turned each on, is in docs/tasks/0356-skyrim-quest-rules-switch.md.

#include <array>
#include <string_view>

namespace game_skyrimse::questrules
{
    /// `[extension.skyrimse] quest rules` (default true). Turns on every general rule below; the per-quest settings keep
    /// working (each still turns on its own rules: the new switch is an OR). Hand-written stand-ins (named snap packages,
    /// named trigger subjects, the moored ship, the Helgen start, test aids) are NOT under it.
    inline constexpr std::string_view kKey = "quest rules";

    /// What the switch turns on, for the log line `QUESTRULES on: ...` (one name per rule, no duplicates).
    inline constexpr std::array<std::string_view, 29> kRules{ "box triggers fire for the player and followers",
        "the player enters a box with his body", "quest greetings",
        "blocking branches", "walk-away topics and infos", "fragments run before the next menu", "live GameHour",
        "quest updates reach alias scripts", "the Player alias holds the player", "scene walks end at their targets",
        "live actor places", "failed scene phases are skipped", "begin-fragment and actionless scenes start unloaded",
        "finished scene packages are held", "quests move actors near the player", "alias packages in one priority order",
        "quest-package followers are carried", "dead lookup for scenes and conditions", "game-time updates",
        "hello greetings", "enable children follow their parent", "player events reach alias scripts",
        "created items in inventories", "actors detect the player", "actor cell moves", "package begin and use-magic hooks",
        "dragon souls by the game's script", "essential bleedout", "quest alias packages" };
}
