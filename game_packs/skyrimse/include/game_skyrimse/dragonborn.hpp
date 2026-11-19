#pragma once

// Task 0306: the start of the Dragonborn questline on Solstheim, through the game's own quests, dialogue and Papyrus
// (decompiled with houseCARL; section 1 of docs/tasks/0306-skyrim-dragonborn-start.md).
//
// The chain: the story manager's CLOC node `DLC2CultistAmbushNode` starts the radiant quest `DLC2CultistAmbush` when the
// player enters a town while MQ105 (The Way of the Voice) is at stage 160 or later, DLC2MQ01 is below stage 5 and he is not
// on Solstheim. The quest creates two cultists and the Cultists' Orders at the town's centre marker; the cultists
// force-greet him, the ambush topics set stage 10 of the quest, whose fragment turns them hostile, puts the note on
// Cultist1, starts DLC2MQ01 ("Dragonborn") and sets its stage 5. The note, taken from the body, sets stage 7 of DLC2MQ01 and
// reading it stage 10 ("Travel to Solstheim").
//
// Everything is the game's own data; this header holds only the names the pack needs to find those records through the
// load order (the DLC's records have the load-order index of Dragonborn.esm in their top byte) and the test aid.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::dragonborn
{
    /// `[extension.skyrimse] dragonborn` (off; needs `papyrus`, `actor packages`, `quest alias packages`): a ForceGreet
    /// package walks its actor to the player before it asks for its conversation (the cultists of the ambush come to him).
    inline constexpr std::string_view kKey = "dragonborn";

    /// `[extension.skyrimse] dragonborn test prerequisites` (off): the story walk of the cultist ambush needs MQ105
    /// (The Way of the Voice) at stage 160 or later, which nobody plays through here. With the aid on the pack marks that
    /// stage done in the quest's state (no fragment, no event) once the Papyrus host runs, before the first story event.
    inline constexpr std::string_view kTestPrerequisitesKey = "dragonborn test prerequisites";

    inline constexpr std::string_view kSkyrimFile = "skyrim.esm";
    inline constexpr std::string_view kDragonbornFile = "dragonborn.esm";

    inline constexpr std::uint32_t kWayOfTheVoiceLocal = 0x0242BA; ///< MQ105 in Skyrim.esm
    inline constexpr std::uint32_t kWayOfTheVoiceStage = 160; ///< the node's GetStage >= 160 (VERIFIED on the record)

    inline constexpr std::uint32_t kCultistAmbushLocal = 0x03546D; ///< DLC2CultistAmbush in Dragonborn.esm
    inline constexpr std::uint32_t kDragonbornQuestLocal = 0x017F8E; ///< DLC2MQ01 "Dragonborn"

    /// Task 0312: game hours the ship between Windhelm and Raven Rock takes (OURS: the data has none; FastTravel to a
    /// marker of another world has no distance to measure).
    inline constexpr int kShipHours = 6;

    /// Task 0320: `ShipNorthwindREF002` (Dragonborn.esm), the ship moored at Raven Rock's dock. The ride script
    /// (`DLC2DialogueRRQuestScript.FirstTimeTravel`) disables it for the boat animation, which has no stand-in here (no
    /// boat is drawn), and the player, set down on the landing marker on its deck, falls into the sea. So the pack keeps
    /// it enabled (HAND-WRITTEN: the ship stands for the boat; every other reference of the script's DisableList is
    /// disabled as the game does).
    inline constexpr std::uint32_t kRavenRockShipLocal = 0x016FE2;
    [[nodiscard]] constexpr bool keepsEnabledDuringTheRide(std::uint32_t local)
    {
        return local == kRavenRockShipLocal;
    }


    /// Task 0329: Neloth is moved from his Tel Mithryn interior to the Earth Stone by DLC2MQ01's stage 10 fragment
    /// (`MoveTo` DLC2MQ01NelothMarker), and his ForceGreet package asks "is he in the Earth Stone location" (fn 359).
    /// The package conditions read the cell the record places him in (the interior, which has no such location), so an
    /// actor that a script made stand in the player's exterior takes the location of the grid square he now stands on.
    [[nodiscard]] constexpr bool movedActorStandsOutdoors(
        bool recordedInterior, bool haveLivePlace, bool playerOutdoors)
    {
        return recordedInterior && haveLivePlace && playerOutdoors;
    }
    /// The 4096 unit grid square a coordinate is in (floor, also below zero).
    [[nodiscard]] constexpr std::int32_t gridSquareOf(double coordinate)
    {
        constexpr double kSquare = 4096.0;
        const double q = coordinate / kSquare;
        const auto t = static_cast<std::int32_t>(q);
        return (static_cast<double>(t) > q) ? t - 1 : t;
    }


    /// Task 0329: the aid `dragonborn test stage = 5+7+10,40` sets the stages in that order (each runs its own
    /// fragments: stage 5 starts the Frea scene quest, as the note read in the game does) at the script second after
    /// the comma; one number is what it always was.
    struct TestStages
    {
        std::vector<std::int32_t> mStages;
        double mDelay = 0.0;
    };
    [[nodiscard]] inline TestStages parseTestStages(std::string_view text)
    {
        TestStages out;
        const std::string whole(text);
        const std::size_t comma = whole.find(',');
        if (comma != std::string::npos)
            out.mDelay = std::strtod(whole.c_str() + comma + 1, nullptr);
        const std::string list = whole.substr(0, comma);
        std::size_t at = 0;
        while (at <= list.size())
        {
            const std::size_t plus = list.find('+', at);
            const std::string piece = list.substr(at, plus == std::string::npos ? std::string::npos : plus - at);
            char* end = nullptr;
            const long value = std::strtol(piece.c_str(), &end, 10);
            if (!piece.empty() && end == piece.c_str() + piece.size() && value > 0)
                out.mStages.push_back(static_cast<std::int32_t>(value));
            if (plus == std::string::npos)
                break;
            at = plus + 1;
        }
        return out;
    }

    /// The aid's one stage: `quest` and `stage` of the prerequisite, nothing else is ever faked.
    struct Prerequisite
    {
        std::string_view mFile;
        std::uint32_t mLocal = 0;
        std::uint32_t mStage = 0;
    };

    inline constexpr Prerequisite kPrerequisite{ kSkyrimFile, kWayOfTheVoiceLocal, kWayOfTheVoiceStage };
}
