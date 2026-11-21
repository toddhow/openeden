#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <game_skyrimse/records.hpp>

// Task 0268 (M-S8, the main quest opening at Helgen). The pack setting `main quest start` makes a new game begin the
// way Skyrim does: on the cart. Nothing of the opening is hand-written; the game's own quest MQ101 ("Unbound"), its
// scene MQ101Scene1 (begins on quest start), aliases and Papyrus fragments run it. Pure constants and helpers, no
// state.

namespace game_skyrimse::mainquest
{
    /// `[extension.skyrimse] main quest start` (default false): a new game starts at the cart and MQ101 is started.
    inline constexpr const char* kMainQuestStartKey = "main quest start";

    /// MQ101 "Unbound" (Skyrim.esm 0x03372B, VERIFIED by the data: name Unbound, start-up stage 0).
    inline constexpr FormId kUnboundQuest = 0x0003372B;

    /// How far from the player (units, in the plane) a script's MoveTo makes an actor or an object again (the loaded
    /// area; ours).
    inline constexpr double kRelocateRange = 7000.0;

    /// Task 0274: the seconds from a cart rider's exit idle to the animation event "ExitCartEnd" that the game's
    /// scripts wait for (OURS: the exit clips are not played; the rider is stood beside the cart at once).
    inline constexpr double kCartExitSeconds = 3.0;

    /// The `--start` text of the cart: the world Tamriel and the place of MQ101PlayerStartMarker (REFR 0x04678E,
    /// position 22499.803, -94862.59, 13905.831, VERIFIED by the data). The quest itself moves the player to that
    /// marker (stage 10); this spot only has to be in the right cell so the exterior around Helgen loads.
    inline constexpr const char* kHelgenStart = "@Tamriel,22499.8,-94862.6,13905.8";

    /// Task 0308: one entry of the developer aid `story test quest = <hex quest>[:<stage>][@<seconds>],...`: start the
    /// quest (if it is not running) and set the stage `mStage` (-1: none), `mAfter` script seconds after the aid's own
    /// delay (0 when no `@` is given: the earlier behaviour).
    struct StoryTestEntry
    {
        FormId mQuest = 0;
        std::int32_t mStage = -1;
        double mAfter = 0.0;
    };

    inline std::vector<StoryTestEntry> parseStoryTestQuests(const std::string& text)
    {
        std::vector<StoryTestEntry> out;
        std::size_t at = 0;
        while (at < text.size())
        {
            const std::size_t comma = text.find(',', at);
            const std::string part = text.substr(at, comma == std::string::npos ? comma : comma - at);
            at = comma == std::string::npos ? text.size() : comma + 1;
            if (part.empty())
                continue;
            StoryTestEntry entry;
            entry.mQuest = static_cast<FormId>(std::strtoul(part.c_str(), nullptr, 16));
            const std::size_t colon = part.find(':');
            const std::size_t atSign = part.find('@');
            if (colon != std::string::npos && (atSign == std::string::npos || colon < atSign))
                entry.mStage = static_cast<std::int32_t>(std::strtol(part.c_str() + colon + 1, nullptr, 10));
            if (atSign != std::string::npos)
            {
                char* end = nullptr;
                const double seconds = std::strtod(part.c_str() + atSign + 1, &end);
                if (end != part.c_str() + atSign + 1 && std::isfinite(seconds) && seconds > 0.0)
                    entry.mAfter = seconds;
            }
            out.push_back(entry);
        }
        return out;
    }
}
