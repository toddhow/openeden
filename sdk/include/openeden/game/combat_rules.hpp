#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <openeden/game/actor_rules.hpp>
#include <openeden/version.hpp>

// The hooks a game pack uses to run COMBAT for its actors (task 0168, milestone M-S5 stage S5-0, decision 0018,
// research note 0056): the pack decides what an actor does in a fight (which attack clip, when it hits, how much
// damage, when it dies); the core owns what needs the engine: playing a named clip and telling the pack when the clip's
// events happen, a shape sweep against the one physics world, and the HUD and ragdoll notices. The core never reads the
// pack's health or stats.
//
// ABI choice (decision 0018): a SEPARATE interface from ActorRules, found by dynamic_cast on the game's registered
// WorldCellSource, so the AI/schedule rules and the combat rules version independently. A pack that does not implement
// it has actors that do not fight. The SDK stays 4.0; nothing here changes ActorRules.
//
// Version policy: DRAFT 0.1 (major 0 means any change may still break). Once a first consumer exists (stage S5-2) an
// additive change (a new trailing virtual with a "not supported" default, a trailing field a consumer only reads)
// raises the minor number.
//
// Threading: main thread only. The core calls combatUpdate() once per frame right after ActorRules::update() (so a
// pack that implements both sees the same frame), and the event and result callbacks just before it, never from inside
// a request. A writer is valid during one combatUpdate() call only; a request written outside it is dropped and
// counted.
//
// Handles are those of actor_rules.hpp (ActorHandle: placement id and a session number, never a pointer). The PLAYER
// has no ActorHandle: requests and events name a CombatSubject (an actor or the player), and the structs where the
// player can be a victim carry a `player` flag.
//
// Units: game units and radians about Z as the engine stores them (0 faces +Y), like ActorInfo. Times are seconds.

namespace openeden::sdk::game
{
    inline constexpr Version kCombatRulesVersion{ 0, 5 };

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
        SpellFire = 16, ///< MRh_SpellFire_Event, MLh_SpellFire_Event: the moment a cast clip lets the spell go (0.4)
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
        float healthMax
            = 0.f; ///< the player's maximum health (0 for an actor, and for a player the engine has no health for)
        float pitch = 0.f; ///< 0.3: the look direction's elevation, radians, positive up; 0 for an actor
    };

    /// A projectile the pack asks the core to launch (task 0178, stage S5-6, DRAFT 0.3). The core owns the flight: it
    /// steps the shot every frame (a ballistic arc of speed and gravity against the one physics world: static geometry
    /// by ray, actors and the player by their capsules), draws the model, and tells the pack where it ended. The pack
    /// owns every number about it (which PROJ, which damage) and resolves the hit when told.
    struct ProjectileRequest
    {
        std::uint32_t token = 0; ///< handed back in ProjectileResult
        double x = 0.0; ///< launch position, game units
        double y = 0.0;
        double z = 0.0;
        float heading = 0.f; ///< radians about Z as the engine stores it (0 faces +Y)
        float pitch = 0.f; ///< elevation of the launch direction, radians in (-pi/2, pi/2), positive up
        float speed = 0.f; ///< units a second, > 0
        float gravity = 0.f; ///< units a second squared, pulling down, >= 0
        float range = 0.f; ///< the shot ends after this much path, > 0
        float stickDepth = 0.f; ///< a shot that hits the world is drawn this far into it, >= 0
        float stickSeconds = 0.f; ///< ... and stays that long before it is removed, >= 0
        /// The model's VFS path (for example `meshes/weapons/iron/ironarrowprojectile.nif`); valid during the call
        /// only,
        /// the core copies it. Empty: the shot is not drawn.
        std::string_view model;
        /// 0.5 (task 0191): a shot that is only drawn. The core flies it like any other (it ends on the first actor or
        /// static body, or when its range is used up), but it hurts nothing and is never told to the pack
        /// (onProjectileResult is not called for it), and it does not stick in the world. A pack streams these along a
        /// beam it resolved with castRay so the spray of Flames is seen.
        bool visualOnly = false;
    };

    enum class ProjectileFate : std::uint8_t
    {
        Expired = 0, ///< the range was used up (or it fell out of the world)
        HitActor = 1,
        HitPlayer = 2,
        HitWorld = 3,
    };

    /// How a projectile ended; delivered by CombatRules::onProjectileResult once, at the start of the NEXT update.
    struct ProjectileResult
    {
        CombatSubject owner;
        std::uint32_t token = 0;
        ProjectileFate fate = ProjectileFate::Expired;
        ActorHandle actor; ///< valid when fate == HitActor
        double x = 0.0; ///< where it ended
        double y = 0.0;
        double z = 0.0;
        float flightSeconds = 0.f;
        float distance = 0.f; ///< path length
        float speed = 0.f; ///< at the end, units a second
    };

    /// Task 0193 (stage S5-10): a combat sound to play at a subject (an actor or the player). `file` is the VFS path of
    /// the sound file in the engine's normalised form ("sound/fx/wpn/..."), `name` the sound descriptor's editor id
    /// (for
    /// the log), `volume` a linear gain from 0 (silent) to 1. The core plays it through the sound manager from the
    /// subject; the call is refused (false) outside combatUpdate, for an unknown subject or an empty or over-long file.
    /// Additive: the writer's default refuses, so a host that plays no sound is unchanged.
    struct SoundRequest
    {
        CombatSubject subject;
        std::string_view file;
        std::string_view name;
        float volume = 1.f;
    };

    /// The longest sound file path the core accepts (the VFS paths of the data are under 130 characters).
    inline constexpr std::size_t kMaxCombatSoundPath = 192;

    /// The timing of a clip as its sidecar gives it, in playback seconds (task 0171 stage S5-3b, DRAFT 0.2): when the
    /// first HitFrame event fires and how long the clip runs. The pack uses it to run a swing that has no clip to carry
    /// events (the player's: no first-person arms).
    struct ClipTiming
    {
        bool hasHit = false; ///< the clip has a HitFrame event
        float hitTime = 0.f;
        float length = 0.f;
    };

    /// A straight line from a point of the world (task 0191, 0.5): the line a held aimed spell, a touch spell or a
    /// beam follows, answered at once by CombatRequestWriter::castRay.
    struct RayRequest
    {
        double x = 0.0; ///< start, game units
        double y = 0.0;
        double z = 0.0;
        float heading = 0.f; ///< radians about Z as the engine stores it (0 faces +Y)
        float pitch = 0.f; ///< elevation, radians in (-pi/2, pi/2), positive up
        float range = 0.f; ///< length of the line, > 0
    };

    enum class RayFate : std::uint8_t
    {
        Nothing = 0, ///< the line met nothing within its range
        Actor = 1,
        Player = 2,
        World = 3, ///< a static body (geometry, terrain, a closed door)
    };

    struct RayHit
    {
        RayFate fate = RayFate::Nothing;
        ActorHandle actor; ///< valid when fate == Actor
        double x = 0.0; ///< where it ended: the hit point, or the far end of the line when it met nothing
        double y = 0.0;
        double z = 0.0;
        float distance = 0.f; ///< from the start to (x, y, z)
    };

    /// A model the pack asks the core to show on a subject (task 0191, 0.5), for example the body of a ward. The core
    /// keeps it at the subject's position (plus `heightOffset` up), facing the subject's heading, every frame, until
    /// the pack hides it or the subject is gone. The model is drawn as an effect, not simulated.
    struct EffectRequest
    {
        std::uint32_t token = 0; ///< the pack's number, used to hide it; showing a token again replaces it
        float heightOffset = 0.f; ///< units above the subject's feet position, finite
        /// The model's VFS path (for example `meshes/magic/wardbodyfx.nif`); valid during the call only, the core
        /// copies it. Must not be empty.
        std::string_view model;
    };

    /// Most effects shown at once (task 0191).
    inline constexpr std::size_t kMaxShownEffects = 16;

    /// How the pack answers in combatUpdate(). One writer, valid during one call.
    class CombatRequestWriter
    {
    public:
        virtual ~CombatRequestWriter() = default;

        /// Plays a clip on the actor (see ClipRequest). False (counted by the core) for a stale handle, a request whose
        /// slot is None, a name that is empty, longer than kMaxCombatClipName or has characters other than letters,
        /// digits and `_`, a blend that is negative or not finite, or a call outside combatUpdate(). The default
        /// refuses.
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

        /// Asks for a sweep from `subject` (an actor must exist; the subject's own body is not hit). The result arrives
        /// in a LATER frame through CombatRules::onSweepResult, once per request. False for a stale handle, a value
        /// that is not finite, a reach, angle or height outside the ranges of SweepRequest, or a call outside
        /// combatUpdate(). The default refuses.
        virtual bool requestSweep(CombatSubject subject, const SweepRequest& request)
        {
            (void)subject;
            (void)request;
            return false;
        }

        /// Reports a resolved hit (see DamageReport). When the victim is the player the core writes `healthAfter` into
        /// the engine's own player health (0.2): the one place where the core changes a number the pack computed,
        /// because the player's health is the engine's. False for a stale non-player handle, a value that is not finite
        /// or an amount below 0. The default refuses.
        virtual bool reportDamage(const DamageReport& report)
        {
            (void)report;
            return false;
        }

        /// Turns an actor on the spot to `heading` (radians about Z as the engine stores it) before it swings (task
        /// 0171, 0.2). False for a stale handle, a heading that is not finite, the player (the player's heading is the
        /// player's), or a call outside combatUpdate(). The default refuses.
        virtual bool faceHeading(CombatSubject subject, float heading)
        {
            (void)subject;
            (void)heading;
            return false;
        }

        /// The timing of the named clip, read from its sidecar (task 0171 stage S5-3b, 0.2). False while the clip is
        /// still being read (the first call starts the read) or when it cannot be read, and outside combatUpdate(); the
        /// pack asks again in a later frame. The default refuses.
        virtual bool clipTiming(std::string_view clip, bool perSex, ClipTiming& out)
        {
            (void)clip;
            (void)perSex;
            out = {};
            return false;
        }

        /// Where the subject is now (task 0171, 0.2). False for a stale handle, an actor that is not placed in the
        /// physics world, or a call outside combatUpdate(). The default refuses.
        virtual bool subjectState(CombatSubject subject, SubjectState& out)
        {
            (void)subject;
            out = {};
            return false;
        }

        /// Launches a projectile (see ProjectileRequest), 0.3. `owner` is never hit by its own shot. False for a stale
        /// handle, a value that is not finite or outside the ranges of ProjectileRequest, a model path that is longer
        /// than 256 bytes, has `..` or a drive letter, too many shots in flight (64), or a call outside combatUpdate().
        /// The default refuses.
        virtual bool launchProjectile(CombatSubject owner, const ProjectileRequest& request)
        {
            (void)owner;
            (void)request;
            return false;
        }

        /// Plays the clip the same update's playClip started at `multiplier` times its speed, 0.3 (the draw speed of a
        /// bow): the clip's events fire that much earlier. False for no such clip, a multiplier outside [0.1, 10] or
        /// not finite, or a call outside combatUpdate(). The default refuses.
        virtual bool setClipSpeed(CombatSubject subject, float multiplier)
        {
            (void)subject;
            (void)multiplier;
            return false;
        }

        /// The playback time of the first event of `kind` in the named clip's sidecar, 0.3. False while the clip is
        /// still being read (the first call starts the read), when it has no such event, and outside combatUpdate().
        /// The default refuses.
        virtual bool clipEventTime(std::string_view clip, bool perSex, ClipEventKind kind, float& out)
        {
            (void)clip;
            (void)perSex;
            (void)kind;
            out = 0.f;
            return false;
        }

        /// Reports that the pack's health for the actor reached 0: the core starts the death (ragdoll, corpse). Once
        /// per actor; a second report is ignored and returns true. False for a stale handle. The default refuses.
        virtual bool reportDeath(ActorHandle actor)
        {
            (void)actor;
            return false;
        }

        /// Casts a straight line against the world's static bodies, the placed actors and the player, 0.5. `from`'s
        /// own body and dead actors are never hit. The answer is given at once in `out` (the nearest thing along the
        /// line, or fate Nothing). False for a stale handle, a value that is not finite or outside the ranges of
        /// RayRequest, and a call outside combatUpdate(). The default refuses.
        virtual bool castRay(CombatSubject from, const RayRequest& request, RayHit& out)
        {
            (void)from;
            (void)request;
            out = {};
            return false;
        }

        /// Shows a model on a subject (see EffectRequest), 0.5. False for a stale handle, a request that is not valid
        /// (an empty model, a model path longer than 256 bytes or with `..` or a drive letter, a height that is not
        /// finite), kMaxShownEffects shown already, and a call outside combatUpdate(). The default refuses.
        virtual bool showEffect(CombatSubject subject, const EffectRequest& request)
        {
            (void)subject;
            (void)request;
            return false;
        }

        /// Stops showing the effect of `token`, 0.5. True when one was shown (it goes away at once), false when none
        /// was, and outside combatUpdate(). The default refuses.
        virtual bool hideEffect(std::uint32_t token)
        {
            (void)token;
            return false;
        }

        /// Task 0193 (stage S5-10): plays a sound at a subject (see SoundRequest). Default: refused.
        virtual bool playSound(const SoundRequest& request)
        {
            (void)request;
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
        virtual void onSweepResult(CombatSubject subject, std::uint32_t token, const SweepHit* hits, std::size_t count)
            = 0;

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

        /// A projectile of this pack ended (0.3). Called just before combatUpdate(), once per launch, not for an owner
        /// that has gone. The default ignores it.
        virtual void onProjectileResult(const ProjectileResult& result) { (void)result; }

        /// The player's attack control went down (`pressed`) or up (0.3), once per change; `onPlayerAttack` (a tap or
        /// a hold) is still sent as before. A pack with a bow draws on the press and shoots on the release. Called
        /// just before combatUpdate(). The default ignores it.
        virtual void onPlayerAttackEdge(bool pressed) { (void)pressed; }
    };
}
