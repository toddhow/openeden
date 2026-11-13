#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <openeden/game/world_view.hpp>
#include <openeden/version.hpp>

// What a game pack tells the core about the MECHANICS of its actors, as plain values to read (task 0154, stage S7 of
// task 0135, decision 0017): the numbers an actor has (level, health, magicka, stamina, skills), and how it stands to
// the player and to other actors (the relation its factions give, how aggressive it is). Nothing here acts: no combat,
// no magic, no damage; a consumer (a debug readout, a future combat system, a script binding) asks and decides.
//
// ABI choice: like ActorRules and ActorAppearanceSource, a NEW interface found by dynamic_cast on the game's registered
// WorldCellSource, so the frozen WorldCellSource (world_source.hpp 1.x) is untouched. A pack that does not implement it
// simply has actors without numbers. The SDK stays 4.0.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a "not supported" default, a trailing field a
// consumer only reads) raise the minor number.
//
// Threading: main thread only, like the rest of the neutral world. The pack's answers are values; nothing points into
// the pack.

namespace openeden::sdk::game
{
    inline constexpr Version kActorMechanicsVersion{ 0, 1 };

    /// Skill slots of ActorStats::skills, in the order the game stores them: one-handed, two-handed, archery, block,
    /// smithing, heavy armor, light armor, pickpocket, lockpicking, sneak, alchemy, speech, alteration, conjuration,
    /// destruction, illusion, restoration, enchanting. A game with other skills fills what it has and leaves the rest 0.
    inline constexpr std::size_t kActorSkillCount = 18;

    /// How the members of one group feel about another's: the faction "group combat reaction" of Skyrim's data (the
    /// numbers are the stored ones). A game without factions answers Neutral.
    enum class ActorRelation : std::uint8_t
    {
        Neutral = 0,
        Enemy = 1,
        Ally = 2,
        Friend = 3,
    };

    /// How readily an actor starts a fight (Skyrim's AI "aggression"; the numbers are the stored ones).
    enum class ActorAggression : std::uint8_t
    {
        Unaggressive = 0, ///< never starts a fight
        Aggressive = 1, ///< attacks its enemies on sight
        VeryAggressive = 2, ///< attacks its enemies and the neutral
        Frenzied = 3, ///< attacks anyone
    };

    /// The numbers of one actor at one level. The maxima: current values and regeneration are the consumer's.
    struct ActorStats
    {
        std::uint16_t level = 1; ///< the level these numbers are for (an actor that scales with the player: its level)
        float health = 0.f;
        float magicka = 0.f;
        float stamina = 0.f;
        std::array<std::uint8_t, kActorSkillCount> skills{};
        bool female = false;
        bool essential = false; ///< cannot be killed (is knocked out)
        bool unique = false; ///< a named individual, not one of many
        bool protectedActor = false; ///< cannot be killed by the player, only knocked out
        bool invulnerable = false;
        bool scalesWithPlayer = false; ///< `level` followed the player's level
    };

    /// How an actor stands to the player and whether it would start a fight.
    struct ActorDisposition
    {
        ActorRelation relationToPlayer = ActorRelation::Neutral; ///< from the factions of the actor and the player
        ActorAggression aggression = ActorAggression::Unaggressive;
        /// The pack's rule combining the two: would the actor attack the player when it sees them. A plain answer, no
        /// behaviour.
        bool hostileOnSight = false;
    };

    /// Implemented by a WorldCellSource whose game has actors with numbers.
    class ActorMechanicsSource
    {
    public:
        virtual ~ActorMechanicsSource() = default;

        /// The numbers of the actor base `base`. `variation` is the placement's number (a leveled template picks the same
        /// entry every time for one placement). `playerLevel` is the level an actor that scales with the player scales
        /// to; 0 means "the pack's own stand-in level" (a pack setting, 1 by default). False with `error` set for a base
        /// that is not an actor of the pack.
        virtual bool actorStats(WorldId base, std::uint32_t variation, std::uint16_t playerLevel, ActorStats& out,
            std::string& error)
            = 0;

        /// How the actor stands to the player. False with `error` set for a base that is not an actor of the pack.
        virtual bool actorDisposition(
            WorldId base, std::uint32_t variation, ActorDisposition& out, std::string& error)
            = 0;

        /// How the factions of actor `a` feel about those of actor `b` (symmetric: an enemy relation either way counts).
        /// Neutral for an unknown base.
        virtual ActorRelation relationBetween(
            WorldId baseA, std::uint32_t variationA, WorldId baseB, std::uint32_t variationB)
            = 0;
    };
}
