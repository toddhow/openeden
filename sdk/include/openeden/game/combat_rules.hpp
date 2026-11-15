#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <openeden/game/actor_rules.hpp>
#include <openeden/version.hpp>

// The hooks a game pack uses to run COMBAT for its actors (task 0168, milestone M-S5 stage S5-0, decision 0018, research
// note 0056): the pack decides what an actor does in a fight (which attack clip, when it hits, how much damage, when it
// dies); the core owns what needs the engine: playing a named clip and telling the pack when the clip's events happen, a
// shape sweep against the one physics world, and the HUD and ragdoll notices. The core never reads the pack's health or
// stats.
//
// ABI choice (decision 0018): a SEPARATE interface from ActorRules, found by dynamic_cast on the game's registered
// WorldCellSource, so the AI/schedule rules and the combat rules version independently. A pack that does not implement
// it has actors that do not fight. The SDK stays 4.0; nothing here changes ActorRules.
//
// Version policy: DRAFT 0.1 (major 0 means any change may still break). Once a first consumer exists (stage S5-2) an
// additive change (a new trailing virtual with a "not supported" default, a trailing field a consumer only reads) raises
// the minor number.
//
// Threading: main thread only. The core calls combatUpdate() once per frame right after ActorRules::update() (so a
// pack that implements both sees the same frame), and the event and result callbacks just before it, never from inside
// a request. A writer is valid during one combatUpdate() call only; a request written outside it is dropped and counted.
//
// Handles are those of actor_rules.hpp (ActorHandle: placement id and a session number, never a pointer). The PLAYER
// has no ActorHandle: requests and events name a CombatSubject (an actor or the player), and the structs where the
// player can be a victim carry a `player` flag.
//
// Units: game units and radians about Z as the engine stores them (0 faces +Y), like ActorInfo. Times are seconds.

namespace openeden::sdk::game
{
    inline constexpr Version kCombatRulesVersion{ 0, 2 };

    /// Who a combat request or event is about: an actor by handle, or the player (who has no ActorHandle). The player
    /// swings, is swept from and is told its clip events through the same functions as an actor, so S5-3 (NPC against
    /// player) can be written against 0.1.
    struct CombatSubject
    {
        bool player = false;
        ActorHandle actor; ///< valid when !player

        [[nodiscard]] bool valid() const { return player || actor.valid(); }
        static CombatSubject ofPlayer() { return { true, {} }; }
        static CombatSubject of(ActorHandle handle) { return { false, handle }; }
        friend bool operator==(const CombatSubject&, const CombatSubject&) = default;
    };

    /// What a clip request is for. The pack owns the table slot -> clip name (SlotTable in the pack); the core only
    /// carries the slot back in events so the pack knows which request an event belongs to.
    enum class ClipSlot : std::uint8_t
    {
        None = 0,
        Attack = 1,
        PowerAttack = 2,
        AttackLeft = 3,
        BlockStart = 4,
        BlockHit = 5,
        BlockStop = 6,
        Bash = 7,
        Recoil = 8,
        Stagger = 9,
        BowDraw = 10,
        BowRelease = 11,
        Cast = 12,
        Death = 13,
    };

    /// The sidecar events of the attack clips (research note 0056 section 6.2), as the core classifies them from the
    /// event name. The raw name is delivered too; `Other` covers every name the core does not classify. The meanings of
    /// the window events are RECALLED-VERIFY.
    enum class ClipEventKind : std::uint8_t
    {
        Other = 0,
        PreHit = 1, ///< preHitFrame
        WeaponSwing = 2, ///< weaponSwing
        Hit = 3, ///< HitFrame: the moment the weapon should damage
        AttackStop = 4, ///< attackStop
        AttackWindowStart = 5, ///< AttackWinStart
        AttackWindowEnd = 6, ///< AttackWinEnd
        BlockWindowStart = 7, ///< InitiateWinBegin
        BlockWindowEnd = 8, ///< InitiateWinEnd
        BlockHitStop = 9, ///< blockHitStop
        BashRelease = 10, ///< bashRelease
        BowDrawStart = 11, ///< bowDrawStart
        BowDrawn = 12, ///< bowDrawn
        BowRelease = 13, ///< BowRelease, BowReleaseFast
        StaggerStop = 14, ///< staggerStop
        RecoilStop = 15, ///< recoilStop
    };

    /// Longest clip name accepted by a ClipRequest (letters, digits and underscores only), the rule of FurnitureUse.
    inline constexpr std::size_t kMaxCombatClipName = 64;

    /// Most hits one sweep returns; the nearest come first and the rest are dropped (counted by the core).
    inline constexpr std::size_t kMaxSweepHits = 16;

    /// Ask the core to play a clip on an actor once (no loop), on top of its idle, and to deliver its events.
    struct ClipRequest
    {
        ClipSlot slot = ClipSlot::None; ///< must not be None
        /// The clip's file name under the actor's animations folder without extension. Valid during the call only; the
        /// core copies it.
        std::string_view clip;
        bool clipPerSex = false; ///< true when the clip lives in the `male` / `female` sub-folder
        float blendSeconds = 0.1f; ///< cross-fade in, at least 0
        /// A number the pack makes up; the core hands it back in every event and in onClipFinished of this request. A
        /// new request for the same actor replaces the running clip (its onClipFinished says Interrupted).
        std::uint32_t token = 0;
    };

    /// One event of a playing clip, in clip-time order. Delivered by CombatRules::onClipEvent.
    struct ClipEvent
    {
        CombatSubject subject;
        ClipSlot slot = ClipSlot::None;
        std::uint32_t token = 0;
        ClipEventKind kind = ClipEventKind::Other;
        /// The sidecar's event name as written; valid during the call only.
        std::string_view name;
        /// Playback time of the clip when the event fires (the sidecar's own time scaled as the data says, F48 of
        /// docs/research/0054-provenance.md); never decreases within one token.
        float clipTime = 0.f;
    };

    enum class ClipEnd : std::uint8_t
    {
        Completed = 0,
        Interrupted = 1, ///< replaced by another request or stopped
        Failed = 2, ///< the clip could not be read or the actor has no skeleton for it
    };

    /// A forward arc in front of an actor: the shape a melee swing is tested against. The core casts it against the
    /// physics world and returns the nearest hits.
    struct SweepRequest
    {
        std::uint32_t token = 0; ///< handed back in onSweepResult
        double x = 0.0; ///< sweep origin (usually the actor's position), game units
        double y = 0.0;
        double z = 0.0;
        float heading = 0.f; ///< the arc's centre line
        float reach = 0.f; ///< length from the origin, > 0
        float halfAngle = 0.f; ///< half the arc's opening, radians in (0, pi]
        float heightBelow = 0.f; ///< extent below the origin's z, >= 0
        float heightAbove = 0.f; ///< extent above the origin's z, > 0
    };

    enum class SweepTarget : std::uint8_t
    {
        Actor = 0,
        Player = 1,
        World = 2, ///< a static or other body (no actor)
    };

    struct SweepHit
    {
        SweepTarget target = SweepTarget::World;
        ActorHandle actor; ///< valid when target == Actor
        double x = 0.0; ///< where the shape touched
        double y = 0.0;
        double z = 0.0;
        float distance = 0.f; ///< from the sweep's origin
    };

    /// What the pack tells the core about a hit it has resolved, for hit reactions, the HUD and impact effects. The
    /// core changes nothing in the pack's numbers.
    struct DamageReport
    {
        bool attackerIsPlayer = false;
        ActorHandle attacker; ///< valid when !attackerIsPlayer
        bool victimIsPlayer = false;
        ActorHandle victim; ///< valid when !victimIsPlayer
        float amount = 0.f; ///< health lost, >= 0
        float healthAfter = 0.f; ///< the victim's health after it
        float healthMax = 0.f;
        bool blocked = false;
        double x = 0.0; ///< impact position
        double y = 0.0;
        double z = 0.0;
    };

    /// Where a combat subject stands now, for the pack to aim a sweep with (task 0171, DRAFT 0.2). For the player the
    /// engine's own health is reported too (the pack never owns the player's health); `healthMax` is 0 for an actor.
    struct SubjectState
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        float heading = 0.f; ///< radians about Z as the engine stores it (0 faces +Y)
        float health = 0.f; ///< the player's current health (0 for an actor)
        float healthMax = 0.f; ///< the player's maximum health (0 for an actor, and for a player the engine has no health for)
    };

    /// How the pack answers in combatUpdate(). One writer, valid during one call.
    class CombatRequestWriter
    {
    public:
        virtual ~CombatRequestWriter() = default;

        /// Plays a clip on the actor (see ClipRequest). False (counted by the core) for a stale handle, a request whose
        /// slot is None, a name that is empty, longer than kMaxCombatClipName or has characters other than letters,
        /// digits and `_`, a blend that is negative or not finite, or a call outside combatUpdate(). The default refuses.
        virtual bool playClip(CombatSubject subject, const ClipRequest& request)
        {
            (void)subject;
            (void)request;
            return false;
        }

        /// Stops the actor's combat clip; its onClipFinished says Interrupted. A no-op returning true when none plays.
        /// False as for playClip. The default refuses.
        virtual bool stopClip(CombatSubject subject)
        {
            (void)subject;
            return false;
        }

        /// Asks for a sweep from `subject` (an actor must exist; the subject's own body is not hit). The result arrives in a LATER
        /// frame through CombatRules::onSweepResult, once per request. False for a stale handle, a value that is not
        /// finite, a reach, angle or height outside the ranges of SweepRequest, or a call outside combatUpdate(). The
        /// default refuses.
        virtual bool requestSweep(CombatSubject subject, const SweepRequest& request)
        {
            (void)subject;
            (void)request;
            return false;
        }

        /// Reports a resolved hit (see DamageReport). When the victim is the player the core writes `healthAfter` into the
        /// engine's own player health (0.2): the one place where the core changes a number the pack computed, because the
        /// player's health is the engine's. False for a stale non-player handle, a value that is not finite or
        /// an amount below 0. The default refuses.
        virtual bool reportDamage(const DamageReport& report)
        {
            (void)report;
            return false;
        }

        /// Turns an actor on the spot to `heading` (radians about Z as the engine stores it) before it swings (task 0171,
        /// 0.2). False for a stale handle, a heading that is not finite, the player (the player's heading is the
        /// player's), or a call outside combatUpdate(). The default refuses.
        virtual bool faceHeading(CombatSubject subject, float heading)
        {
            (void)subject;
            (void)heading;
            return false;
        }

        /// Where the subject is now (task 0171, 0.2). False for a stale handle, an actor that is not placed in the physics
        /// world, or a call outside combatUpdate(). The default refuses.
        virtual bool subjectState(CombatSubject subject, SubjectState& out)
        {
            (void)subject;
            out = {};
            return false;
        }

        /// Reports that the pack's health for the actor reached 0: the core starts the death (ragdoll, corpse). Once per
        /// actor; a second report is ignored and returns true. False for a stale handle. The default refuses.
        virtual bool reportDeath(ActorHandle actor)
        {
            (void)actor;
            return false;
        }
    };

    /// Implemented by a WorldCellSource whose game has combat.
    class CombatRules
    {
    public:
        virtual ~CombatRules() = default;

        /// A clip event, in clip-time order for one token. Called just before combatUpdate() of the frame.
        virtual void onClipEvent(const ClipEvent& event) = 0;

        /// The sweep of `token` finished: `count` hits (at most kMaxSweepHits, nearest first, possibly 0), valid during
        /// the call only. Called just before combatUpdate(), at most once per request, not for an actor that has gone.
        virtual void onSweepResult(CombatSubject subject, std::uint32_t token, const SweepHit* hits, std::size_t count) = 0;

        /// Once per frame after ActorRules::update(); `seconds` is the frame time as the engine's loop gives it.
        virtual void combatUpdate(float seconds, CombatRequestWriter& writer) = 0;

        /// The player pressed the attack control (core to pack): the pack decides whether and with which slot to answer
        /// with playClip(CombatSubject::ofPlayer(), ...) in the next combatUpdate(). `power` is true for a held attack.
        /// Called just before combatUpdate(). The default ignores it.
        virtual void onPlayerAttack(bool power) { (void)power; }

        /// A clip of `token` ended. Called just before combatUpdate(). The default ignores it.
        virtual void onClipFinished(CombatSubject subject, std::uint32_t token, ClipEnd how)
        {
            (void)subject;
            (void)token;
            (void)how;
        }
    };
}
