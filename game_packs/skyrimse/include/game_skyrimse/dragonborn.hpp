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
#include <string_view>

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

    /// The aid's one stage: `quest` and `stage` of the prerequisite, nothing else is ever faked.
    struct Prerequisite
    {
        std::string_view mFile;
        std::uint32_t mLocal = 0;
        std::uint32_t mStage = 0;
    };

    inline constexpr Prerequisite kPrerequisite{ kSkyrimFile, kWayOfTheVoiceLocal, kWayOfTheVoiceStage };
}
