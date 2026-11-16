#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_AI_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_AI_H

// The combat AI's pure part (task 0190, stage S5-9; docs/tasks/0190-skyrim-combat-s5-9-ai.md sections 3 and 4): what a
// fighting NPC decides, from the AIDT and CSTY numbers and a seeded, clock-free rule. No engine, SDK or time source:
// the caller (SkyrimCombat::runAi in combat_ai_run.cpp) hands in the combat clock and the view and acts on the output.
//
// Which numbers are the data's and which are ours: the AIDT bytes, the CSTY floats and the GMST values come from the
// records; the rule that combines them (the weights, the periods, the flee table) is OURS, tunable, named below. Skyrim
// does not say what a style multiplier does.
//
// Determinism: every random number is splitmix64 over (seed + roll counter); the seed comes from the actor's reference
// and the engagement number, the clock is the combat clock the caller passes.

#include <game_skyrimse/combat_style.hpp>

#include <cstdint>

namespace game_skyrimse
{
    /// What the AI reads of an NPC: AIDT (VERIFIED size and ranges, names RECALLED-VERIFY), its combat style and
    /// whether the relation to the player makes it hostile on sight (actor_mechanics.hpp).
    struct AiProfile
    {
        std::uint8_t mAggression = 0; ///< AIDT +0: 0 unaggressive, 1 aggressive, 2 very aggressive, 3 frenzied
        std::uint8_t mConfidence = 2; ///< AIDT +1: 0 cowardly .. 4 foolhardy
        bool mHostileOnSight = false; ///< wouldAttackOnSight(aggression, relation to the player)
        bool mKnown = false; ///< filled from the records (false: the defaults of an actor with no data)
        std::uint8_t mAssistance = 0; ///< AIDT +5 (task 0197): 0 helps nobody, 1 allies, 2 friends and allies
        std::uint8_t mRelationToPlayer = 0; ///< task 0197: the sdk ActorRelation of its factions to the player's
        bool mGuard = false; ///< task 0197: in the faction Factions::kGuardFactionEditorId
        CombatStyleRecord mStyle;
    };

    // ---- ours ---------------------------------------------------------------------------------------------------

    /// Seconds between two decisions of an actor in a duel (a swing of the target forces one at once).
    inline constexpr float kAiDecisionPeriod = 0.5f;
    /// An unengaged hostile checks for the player this often (the first check is staggered by its reference).
    inline constexpr float kAiSightPeriod = 0.5f;
    /// The distance (and the height difference) within which a hostile NPC notices the player. No walls are known to
    /// the pack, so a hostile in the next room can engage.
    inline constexpr float kAiSightDistance = 1800.f;
    inline constexpr float kAiSightHeight = 200.f;
    /// A goal is re-issued at most this often and only when its point moved this far.
    inline constexpr float kAiRetargetPeriod = 0.4f;
    inline constexpr float kAiRetargetDistance = 48.f;
    /// Beyond this distance the NPC runs, else it walks.
    inline constexpr float kAiRunDistance = 500.f;
    /// How far from the target an approaching melee fighter aims to stop, as a fraction of its reach.
    inline constexpr float kAiStandOffFraction = 0.6f;
    inline constexpr float kAiMinStandOff = 40.f;
    inline constexpr float kAiBlockHoldMin = 0.8f;
    inline constexpr float kAiBlockHoldMax = 1.6f;
    inline constexpr float kAiCircleHoldMin = 1.0f;
    inline constexpr float kAiCircleHoldMax = 2.0f;
    inline constexpr float kAiFallbackHold = 0.6f;
    inline constexpr float kAiFallbackStep = 160.f;
    /// The length of one circling goal along the circle, and its smallest radius.
    inline constexpr float kAiCircleStep = 140.f;
    inline constexpr float kAiCircleMinRadius = 90.f;
    /// A fleeing NPC aims this far away from the target; it tries the directions of kAiFleeAttempts in turn.
    inline constexpr float kAiFleeStep = 900.f;
    inline constexpr int kAiFleeAttempts = 8;
    /// A retreat that began below this health fraction ends when the health is back to it:
    /// iAICombatRestoreHealthPercentage of Skyrim.esm is 60 (VERIFIED value; its use here is RECALLED-VERIFY). After it
    /// the NPC flees again only when its health is more than kAiRecoverMargin below what it recovered to (and at or
    /// below its flee fraction).
    inline constexpr float kAiRestoreHealthFraction = 0.6f;
    inline constexpr float kAiRecoverMargin = 0.1f;
    /// A flee that has run out of ways holds still while the target is farther than this and fights when it comes near.
    inline constexpr float kAiCorneredDistance = 500.f;
    /// An engaged NPC whose goals fail (no path, blocked) this many times in a row, outside a flee, gives up (the
    /// package brain takes over) and does not engage again by itself for this many seconds (a hit by the player
    /// still does).
    inline constexpr int kAiMaxGoalFails = 6;
    inline constexpr float kAiGiveUpCooldown = 15.f;
    /// A ranged fighter closes in until it is within this fraction of its range, backs off inside the minimum.
    inline constexpr float kAiRangedFraction = 0.8f;
    inline constexpr float kAiRangedMinDistance = 250.f;
    /// A caster casts only farther away than its melee reach plus this.
    inline constexpr float kAiCastMinDistance = 200.f;
    inline constexpr float kAiCastMaxChance = 0.5f;
    inline constexpr float kAiCastBaseChance = 0.15f;
    /// The weights of a duel decision.
    inline constexpr float kAiWaitWeight = 0.15f;
    inline constexpr float kAiMinAttackWeight = 0.05f;
    inline constexpr float kAiLowAttackStamina = 0.15f; ///< below this stamina fraction an attack weighs 0.2 times
    inline constexpr float kAiNoBlockStamina = 0.1f; ///< below this a block weighs 0
    inline constexpr float kAiTiredStamina = 0.3f; ///< below this a fallback weighs double
    inline constexpr float kAiPowerChance = 0.25f; ///< with a stamina fraction above kAiPowerStamina
    inline constexpr float kAiPowerChanceTired = 0.05f;
    inline constexpr float kAiPowerStamina = 0.5f;
    inline constexpr float kAiBashChanceBlocked = 0.5f; ///< times the style's bash multiplier, at most 0.9
    inline constexpr float kAiBashChance = 0.1f; ///< times the style's bash multiplier, at most 0.5

    /// The health fraction at or below which an engaged NPC of this confidence flees (0 cowardly flees at once, 4
    /// foolhardy never): 1.0, 0.5, 0.25, 0.1, 0 (never). An unknown confidence counts as 2.
    [[nodiscard]] float fleeHealthFraction(std::uint8_t confidence);

    // ---- the decision --------------------------------------------------------------------------------------------

    enum class AiMode : std::uint8_t
    {
        Calm, ///< not engaged
        Advance, ///< closing in
        Duel, ///< in reach, deciding
        Block, ///< guard up
        Circle,
        Fallback,
        Retreat, ///< fleeing
    };

    enum class AiMove : std::uint8_t
    {
        None, ///< stand (and face the target)
        Approach,
        Circle,
        Away, ///< the fallback step
        Flee,
    };

    enum class AiAction : std::uint8_t
    {
        None,
        Attack,
        PowerAttack,
        Bash,
        Cast,
        RaiseGuard,
        LowerGuard,
    };

    /// One NPC's state. Plain data, cheap to copy; reset by aiEngage.
    struct AiActorState
    {
        AiMode mMode = AiMode::Calm;
        std::uint64_t mSeed = 0;
        std::uint32_t mRolls = 0; ///< random numbers drawn so far
        std::uint32_t mDecisions = 0;
        float mNextDecision = 0.f;
        float mUntil = 0.f; ///< the end of the current Block, Circle or Fallback
        int mCircleDir = 1; ///< +1 or -1: which way round the target
        bool mGuardUp = false;
        bool mCornered = false; ///< the flee ran out of ways: it fights
        bool mPrevTargetSwinging = false;
        int mFleeTry = 0; ///< index of the flee direction in use
        bool mFleeStuck = false; ///< every flee direction failed: it holds still until the target is near (cornered)
        float mFleeStartHealth = 1.f; ///< the health fraction the retreat began at
        float mRecoveredAt = 2.f; ///< the health fraction a retreat ended at (2: none yet)
    };

    /// What the caller knows this frame. Distances are in the ground plane, game units.
    struct AiView
    {
        float mNow = 0.f; ///< the combat clock
        float mDistance = 0.f; ///< to the target's centre
        float mReach = 0.f; ///< the distance at which a melee blow lands (reach plus a body radius, less a margin)
        float mRange = 0.f; ///< > 0: a ranged weapon of this range (a bow)
        float mHealthFraction = 1.f;
        float mStaminaFraction = 1.f;
        float mFleeDistance = 3000.f; ///< fFleeDistanceInterior: a flee is over this far away
        bool mTargetAlive = true;
        bool mTargetSwinging = false;
        bool mTargetBlocking = false;
        bool mBusy = false; ///< swinging, staggered, drawing or casting: no new action now
        bool mCanCast = false; ///< an aimed spell it can pay for
        bool mCanBlock = true; ///< has something to block with (always true now)
    };

    struct AiOutput
    {
        AiMove mMove = AiMove::None;
        AiAction mAction = AiAction::None;
        float mGuardHold = 0.f; ///< seconds, with RaiseGuard
        bool mLost = false; ///< the engagement is over (target gone, or the flee succeeded)
        bool mModeChanged = false;
        bool mBecameCornered = false; ///< a stuck flee met the target: it fights from now on
        bool mRecovered = false; ///< the retreat ended because the health came back
    };

    /// Starts an engagement: the mode becomes Advance, the first decision is due now.
    void aiEngage(AiActorState& state, std::uint64_t seed, float now);

    /// A random number in [0, 1) from the state's stream.
    [[nodiscard]] float aiRoll(AiActorState& state);

    /// One step. Pure: the state and the output are everything it changes.
    [[nodiscard]] AiOutput aiThink(AiActorState& state, const AiView& view, const AiProfile& profile);

    /// A seed for an engagement: the actor's reference and how many engagements it has had.
    [[nodiscard]] std::uint64_t aiSeed(std::uint64_t reference, std::uint32_t engagement);

    // ---- geometry (ground plane) ---------------------------------------------------------------------------------

    struct AiPoint
    {
        double x = 0.0;
        double y = 0.0;
    };

    /// The point at `standOff` from `target` on the line to `self` (self itself when it is already nearer or the two
    /// coincide).
    [[nodiscard]] AiPoint aiApproachPoint(AiPoint self, AiPoint target, double standOff);
    /// The point `arc` units further round `target` from `self` at `radius` (direction +1 counter-clockwise).
    [[nodiscard]] AiPoint aiCirclePoint(AiPoint self, AiPoint target, double radius, int direction, double arc);
    /// A point `distance` away from `self`, directly away from `target` turned by `attempt`'s offset (0, +45, -45, +90,
    /// -90, +135, -135, 180 degrees).
    [[nodiscard]] AiPoint aiAwayPoint(AiPoint self, AiPoint target, double distance, int attempt);
}

#endif
