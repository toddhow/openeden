#ifndef OPENEDEN_GAME_SKYRIMSE_SPEECH_H
#define OPENEDEN_GAME_SKYRIMSE_SPEECH_H

// Skyrim SE speech checks and dialogue services (task 0155, milestone M-S4 "speech checks; training/trade hooks
// stubbed", docs/design/skyrim-se-implementation-plan.md). Pure pack code: no engine, UI or Qt type.
//
// SPEECH CHECKS. The data has no "speech challenge" subrecord we could find (INFO DNAM is a form id, ENAM carries the
// response flags, CNAM the favor level: aggregates in the task log). A persuade, intimidate or bribe line is an
// ordinary INFO gated by conditions, and the checks are three condition functions plus the player's numbers:
//  - fn_14 GetActorValue(Speechcraft) >= a global, run on the player (123 uses; the global holds the difficulty),
//    with fn_182 GetEquipped(Amulet of Articulation) or'ed in;
//  - fn_655 GetIntimidateSuccess and fn_654 GetBribeSuccess, run on the speaker (65 and 57 INFOs).
// The engine of the conditions already knew those functions (task 0122 and its follow-ups) and asked the world; the
// world answered 0. This file gives the world something to answer WITH: a stand-in player (no player actor exists in
// a neutral session yet) and the speaker's numbers from actor_mechanics.hpp.
//
// STAND-IN, not Skyrim's own: the player is a plain value (PlayerStandIn): level 1, every skill 15 (a new game's
// Speechcraft), 100 gold, 100 health, magicka and stamina; the pack settings `player level`, `player speech` and
// `player gold` change them. The intimidate and bribe rules below are OURS (named constants, a decision for toddhow):
// Skyrim computes them from game settings and perk entry points the pack does not read yet.
//
// ACTOR VALUE INDEXES (RECALLED-VERIFY, partly confirmed by the data: index 17 is the one the persuade lines compare
// with difficulty globals): 6 One-Handed, 7 Two-Handed, 8 Archery, 9 Block, 10 Smithing, 11 Heavy Armor, 12 Light
// Armor, 13 Pickpocket, 14 Lockpicking, 15 Sneak, 16 Alchemy, 17 Speechcraft, 18 Alteration, 19 Conjuration, 20
// Destruction, 21 Illusion, 22 Restoration, 23 Enchanting, then 24 Health, 25 Magicka, 26 Stamina. The first 18 are
// the SDK's skill slots in order.
//
// SERVICES. A line that opens a barter or training menu, rents a room and the like is run by the INFO's Papyrus
// fragment (VMAD names a "TIF__" script), not by a flag. The pack cannot run Papyrus in a dialogue yet, so it reads the
// fragment script's compiled form for the names of the functions it calls and says what the line does
// (`classifyService`); the engine then logs "service not implemented" and ends the conversation instead of pretending.

#include <openeden/game/actor_mechanics.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    inline constexpr std::string_view kPlayerSpeechKey = "player speech";
    inline constexpr std::string_view kPlayerGoldKey = "player gold";

    inline constexpr std::uint32_t kAvOneHanded = 6;
    inline constexpr std::uint32_t kAvEnchanting = 23;
    inline constexpr std::uint32_t kAvSpeechcraft = 17;
    inline constexpr std::uint32_t kAvHealth = 24;
    inline constexpr std::uint32_t kAvMagicka = 25;
    inline constexpr std::uint32_t kAvStamina = 26;

    /// A new game's skill value (also the pack setting's default).
    inline constexpr std::uint8_t kStandInSkill = 15;
    inline constexpr float kStandInGold = 100.f;
    inline constexpr float kStandInVital = 100.f;

    /// The player of a neutral session, as plain numbers. Nothing here changes while a conversation runs.
    struct PlayerStandIn
    {
        std::uint16_t mLevel = 1;
        std::array<std::uint8_t, openeden::sdk::game::kActorSkillCount> mSkills = [] {
            std::array<std::uint8_t, openeden::sdk::game::kActorSkillCount> s{};
            s.fill(kStandInSkill);
            return s;
        }();
        float mGold = kStandInGold;
        float mHealth = kStandInVital;
        float mMagicka = kStandInVital;
        float mStamina = kStandInVital;
    };

    /// The skill slot (index into ActorStats::skills) of an actor value index, if it is a skill.
    [[nodiscard]] std::optional<std::size_t> skillSlotOfActorValue(std::uint32_t actorValue);

    /// The value of an actor value for the stand-in player; 0 for one the stand-in does not have.
    [[nodiscard]] float actorValueOfPlayer(const PlayerStandIn& player, std::uint32_t actorValue);

    /// The value of an actor value for an actor with these numbers; 0 for one they do not hold.
    [[nodiscard]] float actorValueOfActor(const openeden::sdk::game::ActorStats& stats, std::uint32_t actorValue);

    /// OUR rules for the two speech functions the data gates lines with (stand-ins, see the file comment).
    /// Intimidation works on a speaker no stronger than the player; a bribe needs `kBribeGoldPerLevel` gold for each
    /// level of the speaker (at least one).
    inline constexpr float kBribeGoldPerLevel = 20.f;
    [[nodiscard]] bool intimidateSucceeds(std::uint16_t playerLevel, std::uint16_t speakerLevel);
    [[nodiscard]] bool bribeSucceeds(float playerGold, std::uint16_t speakerLevel);

    /// What a response with a fragment script does, from the names the script's compiled form refers to (lower case).
    /// Empty when the names say nothing the pack recognises. Names are those of the functions the fragment calls, as
    /// they appear in the .pex string table.
    [[nodiscard]] std::string classifyService(const std::vector<std::string>& referencedNames);
}

#endif
