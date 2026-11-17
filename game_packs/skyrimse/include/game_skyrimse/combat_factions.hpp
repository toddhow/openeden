#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_FACTIONS_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_FACTIONS_H

// Task 0197: who may fight whom. The pure rules of the combat AI's target choice: relation between two fighters from
// their factions, the player's side (the player and followers), guards, assistance, friendly fire and line of sight.
// Standard library and the SDK's relation and ray types only; no clock, no records.
//
// What is the data's and what is ours: faction relations (FACT XNAM), AIDT aggression and assistance and the faction
// membership come from the records; the rule that combines them below is OURS (named, tunable).

#include <openeden/game/actor_mechanics.hpp>
#include <openeden/game/combat_rules.hpp>

#include <cstdint>
#include <string_view>

namespace game_skyrimse::Factions
{
    using openeden::sdk::game::ActorRelation;

    /// The game's own marker of a guard (a faction of Skyrim.esm; there is no flag for it: the street guards of
    /// Whiterun are AIDT aggression 0 and are known only by this and their crime faction).
    inline constexpr std::string_view kGuardFactionEditorId = "IsGuardFaction";

    /// A watcher looks for a target every this many seconds; the snapshot of positions it reads is that old at most.
    inline constexpr float kSnapshotPeriod = 0.5f;
    /// Watchers looked at per frame (round robin).
    inline constexpr std::size_t kScanBudget = 4;
    /// A fighter asks for help within this distance of the attacked one, and a follower fights within this of the
    /// player.
    inline constexpr float kAssistRadius = 1500.f;
    inline constexpr float kFollowerLeash = 1500.f;
    /// The line of sight runs from this far above the feet of the looker to this far above the feet of the target.
    inline constexpr double kEyeHeight = 120.0;
    inline constexpr double kTargetHeight = 90.0;

    /// What the rules read of one fighter.
    struct Party
    {
        bool mPlayer = false;
        bool mFollower = false; ///< follows the player: fights on its side
        std::uint8_t mAggression = 0; ///< AIDT
        std::uint8_t mAssistance = 0; ///< AIDT: 0 helps nobody, 1 allies, 2 friends and allies
        bool mGuard = false; ///< a member of kGuardFactionEditorId
        bool mHostileOnSight = false; ///< would attack the player on sight (its relation to the player and aggression)
        ActorRelation mRelationToPlayer = ActorRelation::Neutral;
    };

    /// The player and its followers.
    [[nodiscard]] bool playerSide(const Party& party);

    /// The relation of `a` to `b`: Ally when both are on the player's side; Enemy when one is on the player's side and
    /// the other's relation to the player is Enemy; else `factionRelation` (the factions' own).
    [[nodiscard]] ActorRelation sideRelation(const Party& a, const Party& b, ActorRelation factionRelation);

    /// Would `a` start a fight with `b` on sight? The player never does (a is an NPC). A follower attacks what is
    /// hostile to the player's side: `bHostileToPlayerSide` (b is engaged against the player's side) or b would attack
    /// the player on sight; and never its own side. Everybody else: the aggression rule of actor_mechanics on `rel`
    /// (the faction relation, from sideRelation).
    [[nodiscard]] bool attacksOnSight(const Party& a, const Party& b, ActorRelation rel, bool bHostileToPlayerSide);

    /// Does an idle `helper` join the fight of `victim` against an attacker? `toVictim` and `toAttacker` are the
    /// helper's relations. A guard protects its allies and friends (a faction in common is an alliance) and the
    /// player's side (unless the helper is an enemy of the player), whoever the attacker is. Anybody else with
    /// assistance 1 helps its allies, with 2 its friends too, and never against an ally or friend of its own.
    [[nodiscard]] bool assists(
        const Party& helper, ActorRelation toVictim, ActorRelation toAttacker, bool victimOnPlayerSide);

    /// Friendly fire: a blow or arrow may not hurt an ally or friend of the attacker unless it is the attacker's
    /// current target.
    [[nodiscard]] bool mayHit(ActorRelation attackerToVictim, bool victimIsAttackersTarget);

    /// The line from a looker to a target (feet positions given; heights added here) as the RayRequest of the
    /// contract. `valid` is false for non-finite input or two points on top of each other.
    struct SightRay
    {
        openeden::sdk::game::RayRequest mRay;
        bool mValid = false;
        double mLength = 0.0;
    };
    [[nodiscard]] SightRay sightRay(double sx, double sy, double sz, double tx, double ty, double tz);

    /// Does what the ray met leave the target in sight? Nothing, an actor and the player do; a static body does not.
    [[nodiscard]] bool sightClear(openeden::sdk::game::RayFate fate);
}

#endif
