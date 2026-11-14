#ifndef OPENEDEN_GAME_SKYRIMSE_ACTOR_MECHANICS_H
#define OPENEDEN_GAME_SKYRIMSE_ACTOR_MECHANICS_H

// The Skyrim SE pack's actor numbers and relations (task 0154, stage S7 "mechanics-lite" of task 0135): pure functions
// over ActorIndex that answer the SDK's ActorMechanicsSource (sdk/include/openeden/game/actor_mechanics.hpp). Nothing
// here acts: no combat, no magic, no damage.
//
// What the data settles (checked on the five official masters, aggregates in the task 0154 stage log):
//  * NPC_ DNAM bytes 36, 38, 40 (u16 each, SIGNED: a race with no magicka and an offset of -25 stores 65511) hold the
//    TOTAL health, magicka and stamina of the NPC at its level: for all 1,560 auto-calculated fixed-level NPCs they
//    equal race start (RACE DATA 36, 40, 44) + the ACBS offset + (level - 1) times the class gain below, to within 1
//    point (the game seems to hand out whole points), wherever the class has weights;
//  * the per-level gain is health 5 + 10 * hw / (hw + mw + sw), magicka 10 * mw / sum, stamina 10 * sw / sum, with the
//    class weights at CLAS DATA 32, 33, 34 (fits every class in the installed data, e.g. weights 3 0 2 give 11 0 4,
//    1 1 1 give 8.33 3.33 3.33, 2 4 0 give 8.33 6.67 0);
//  * for an NPC that scales with the player (ACBS flag 0x80, "level" = multiplier times 1000) the stored totals are
//  those
//    of its calc minimum level (369 of 375 within 1 point; the six misses all have calc min 0), so other levels add the
//    gain for the difference;
//  * the skill bytes (DNAM 0 to 17) are final values (the sum is never 0), the skill offsets (18 to 35) are non-zero in
//  22
//    of 6,362 NPCs and are not used.
// RECALLED-VERIFY: the meaning of each ACBS flag bit (names from UESP / xEdit), the aggression levels, and the rule
// that turns relation plus aggression into "hostile on sight".

#include "game_skyrimse/actors.hpp"

#include <openeden/game/actor_mechanics.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    /// The player's own faction, "PlayerFaction" (editor id VERIFIED at local form 0xDB1 in Skyrim.esm).
    inline constexpr FormId kPlayerFactionId = 0x00000DB1;
    /// The level an actor that scales with the player scales to when the caller gives none: a new game's player. A pack
    /// setting (`player level`) can change it.
    inline constexpr std::uint16_t kStandInPlayerLevel = 1;
    /// The most a level can be (the stored fields are 16 bit).
    inline constexpr std::uint16_t kMaxActorLevel = 65535;
    /// A "level" field of an actor that scales with the player holds the multiplier times this.
    inline constexpr double kLevelMultiplierScale = 1000.0;

    /// The pack setting (`[extension.skyrimse] player level`, int, default 1) that sets the stand-in player level.
    inline constexpr std::string_view kPlayerLevelKey = "player level";

    struct MechanicsOptions
    {
        std::uint16_t mPlayerLevel = kStandInPlayerLevel; ///< 0 is read as kStandInPlayerLevel
        /// The factions the player is a member of (rank is not used): the stand-in has PlayerFaction only.
        std::vector<FormId> mPlayerFactions{ kPlayerFactionId };
    };

    /// The per-level gains of a class (zero when the class is unknown or has no weights).
    struct LevelGain
    {
        float mHealth = 0.f;
        float mMagicka = 0.f;
        float mStamina = 0.f;
    };
    [[nodiscard]] LevelGain levelGainOf(const ClassRecord* classRecord);

    /// The numbers of NPC_ `npc` (templates followed with `index.resolve`, `variation` picks leveled templates). False
    /// with `error` set for an unknown or unresolvable NPC.
    [[nodiscard]] bool computeActorStats(const ActorIndex& index, FormId npc, std::uint32_t variation,
        std::uint16_t playerLevel, openeden::sdk::game::ActorStats& out, std::string& error);

    /// How the factions of `a` feel about those of `b`: Enemy if any pair is enemies in either direction, else Ally if
    /// any pair is allies or shares a faction, else Friend if any pair is friends, else Neutral.
    [[nodiscard]] openeden::sdk::game::ActorRelation relationOfFactions(
        const ActorIndex& index, const std::vector<FactionMembership>& a, const std::vector<FactionMembership>& b);

    /// The pack's rule: Unaggressive never; Aggressive attacks an enemy; VeryAggressive an enemy or a neutral; Frenzied
    /// anyone.
    [[nodiscard]] bool wouldAttackOnSight(
        openeden::sdk::game::ActorAggression aggression, openeden::sdk::game::ActorRelation relation);

    [[nodiscard]] bool computeActorDisposition(const ActorIndex& index, FormId npc, std::uint32_t variation,
        const MechanicsOptions& options, openeden::sdk::game::ActorDisposition& out, std::string& error);

    /// Factions between two NPC_ bases; Neutral when either is unknown.
    [[nodiscard]] openeden::sdk::game::ActorRelation relationBetweenActors(
        const ActorIndex& index, FormId a, std::uint32_t variationA, FormId b, std::uint32_t variationB);
}

#endif
