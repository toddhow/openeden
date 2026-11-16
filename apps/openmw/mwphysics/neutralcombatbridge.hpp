#ifndef OPENMW_MWPHYSICS_NEUTRALCOMBATBRIDGE_H
#define OPENMW_MWPHYSICS_NEUTRALCOMBATBRIDGE_H

// Task 0170 (decision 0018, milestone M-S5 stage S5-2): the engine's side of the combat hooks
// (sdk/include/openeden/game/combat_rules.hpp) for neutral actors: clip requests and clip events.
//
// The bridge turns CombatRequestWriter::playClip into a sink call (the renderer plays the clip once on the actor) and
// keeps a clip clock per playing clip. Once a frame, just before the pack's combatUpdate(), it advances every clock by
// the frame time and delivers the sidecar events that clock passed, in clip-time order (also several in one frame),
// then onClipFinished; interrupts are told at the start of the next frame. Events and the clip's length come from a
// ClipMeta (components/havokpack/clipevents.hpp) that a worker loads: a clip whose meta is not ready yet has no clock,
// and one that does not arrive within kMetaTimeoutSeconds ends as Failed.
//
// Task 0171 (stage S5-3) adds the sweep, the damage notice and the death notice. requestSweep casts the arc AT THE
// MOMENT OF THE REQUEST (inside the pack's combatUpdate, main thread) against the bodies a NeutralCombatWorld lists and
// keeps the result, which is told to the pack at the start of the NEXT update (onSweepResult, before the events and
// before combatUpdate: a result is never delivered from inside a request). reportDamage forwards a hit on the player to
// the world (the engine's own player health) and reportDeath marks the actor dead: later sweeps skip it. The player is
// a subject for sweeps and damage; clips for the player are still refused (no first-person arms).
//
// Threading: main thread only, like NeutralActorBridge, whose handles it uses. Allocation: update() and a request
// allocate nothing (the tables are reserved in the constructor); the meta is a shared_ptr copied from the provider.
// A pack function that throws is caught and counted. Only actors: the player has no handle (refused and counted).

#include "neutralactorbridge.hpp"
#include "neutralprojectile.hpp"
#include "neutralsweep.hpp"

#include <components/havokpack/clipevents.hpp>

#include <openeden/game/combat_rules.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace MWPhysics
{
    /// Where a projectile is drawn: a position and the unit direction of its flight (task 0178).
    struct NeutralProjectilePose
    {
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
        float mDirX = 0.f;
        float mDirY = 1.f;
        float mDirZ = 0.f;
    };

    /// Where the clip goes. `key` is the actor key of the NeutralActorBridge.
    class NeutralClipSink
    {
    public:
        virtual ~NeutralClipSink() = default;
        /// Task 0178: the clip the actor plays now runs at `multiplier` times its speed (default: nothing).
        virtual void setClipSpeed(const void* key, float multiplier)
        {
            (void)key;
            (void)multiplier;
        }
        /// Task 0178: a projectile starts to be drawn (`id` is the core's number), moves, or goes. Defaults: nothing.
        virtual void spawnProjectile(std::uint32_t id, std::string_view model, const NeutralProjectilePose& pose)
        {
            (void)id;
            (void)model;
            (void)pose;
        }
        virtual void moveProjectile(std::uint32_t id, const NeutralProjectilePose& pose)
        {
            (void)id;
            (void)pose;
        }
        virtual void removeProjectile(std::uint32_t id) { (void)id; }
        /// Task 0193 (stage S5-10): a combat sound at an actor (`key`) or, when `key` is null, at the player. `file`
        /// is a
        /// VFS path, `volume` a gain in [0, 1]. Default: nothing.
        virtual void playSound(const void* key, std::string_view file, std::string_view name, float volume)
        {
            (void)key;
            (void)file;
            (void)name;
            (void)volume;
        }
        virtual void playClip(const void* key, std::string_view clip, bool perSex) = 0;
        virtual void stopClip(const void* key) = 0;
        /// Task 0171: turns the actor on the spot (default: nothing).
        virtual void setHeading(const void* key, float heading)
        {
            (void)key;
            (void)heading;
        }
    };

    /// What the bridge needs from the physics world for sweeps, damage and death (task 0171). Main thread only.
    class NeutralCombatWorld
    {
    public:
        virtual ~NeutralCombatWorld() = default;
        /// Where a subject stands (and the player's engine health). False when it is not in the world.
        virtual bool subjectState(openeden::sdk::game::CombatSubject subject, openeden::sdk::game::SubjectState& out)
            = 0;
        /// Appends every body a swing can hit: the placed neutral actors and, when there is one, the player.
        virtual void collectCandidates(std::vector<NeutralSweep::Candidate>& out) = 0;
        /// A hit on the player was resolved: write report.healthAfter into the engine's player health.
        virtual void applyPlayerDamage(const openeden::sdk::game::DamageReport& report) { (void)report; }
        /// Task 0171 stage S5-3b: true when a wall, a closed door or other static geometry lies on the straight line
        /// from (x0, y0, z0) to (x1, y1, z1); actors never block. Default: nothing blocks.
        virtual bool lineBlocked(double x0, double y0, double z0, double x1, double y1, double z1)
        {
            (void)x0;
            (void)y0;
            (void)z0;
            (void)x1;
            (void)y1;
            (void)z1;
            return false;
        }
        /// Task 0171 stage S5-3b: the sidecar meta of a clip, for the player's hidden swing clock; null while it is
        /// still being read (the first call starts the read) or when it cannot be. Default: never.
        virtual std::shared_ptr<const HavokPack::ClipMeta> playerClipMeta(std::string_view clip, bool perSex)
        {
            (void)clip;
            (void)perSex;
            return {};
        }
        /// Task 0178: the first static body the segment from p0 to p1 meets, as the fraction along it in [0, 1]
        /// (a projectile's flight; actors are not bodies for it). Default: nothing is hit.
        virtual bool rayWorld(const NeutralProjectile::Vec& p0, const NeutralProjectile::Vec& p1, double& fraction)
        {
            (void)p0;
            (void)p1;
            fraction = 0.0;
            return false;
        }
        /// The pack reported the actor behind `key` dead (once).
        virtual void actorDied(const void* key, openeden::sdk::game::ActorHandle actor)
        {
            (void)key;
            (void)actor;
        }
    };

    /// How far the renderer is with the clip the actor behind `key` plays now: no meta while it is still being read (or
    /// when it failed), not started while the clip is being decoded.
    using NeutralClipMetaFn = std::function<HavokPack::ClipProgress(const void* key)>;

    /// The timing a pack needs from a clip's meta: the first HitFrame event and the playback length. False without a
    /// meta; `hasHit` stays false when the clip has no HitFrame.
    [[nodiscard]] bool clipTimingOf(const HavokPack::ClipMeta* meta, openeden::sdk::game::ClipTiming& out);

    /// The event kind of a sidecar event name (case-insensitive; Other for an unknown name).
    [[nodiscard]] openeden::sdk::game::ClipEventKind classifyClipEvent(std::string_view name);

    class NeutralCombatBridge
    {
    public:
        struct Stats
        {
            std::uint64_t mClipsStarted = 0;
            std::uint64_t mClipsCompleted = 0;
            std::uint64_t mClipsInterrupted = 0;
            std::uint64_t mClipsFailed = 0;
            std::uint64_t mEventsDelivered = 0;
            std::uint64_t mRefused
                = 0; ///< requests refused: stale or player subject, bad name or blend, slot None, outside update
            std::uint64_t mTableFull = 0;
            std::uint64_t mRulesFailures = 0; ///< a pack function that threw
            std::uint64_t mUpdates = 0;
            std::uint64_t mSweeps = 0; ///< sweeps cast
            std::uint64_t mSweepHits = 0; ///< hits told to the pack
            std::uint64_t mSweepHitsDropped = 0; ///< hits beyond kMaxSweepHits
            std::uint64_t mSweepHitsBlocked = 0; ///< hits dropped because a wall or door lay between (stage S5-3b)
            std::uint64_t mSweepsRefused = 0; ///< bad request, unknown subject, no world, result table full
            std::uint64_t mSweepResultsDropped = 0; ///< results whose attacker had gone
            std::uint64_t mDamageReports = 0;
            std::uint64_t mDamageRefused = 0;
            std::uint64_t mDeaths = 0;
            std::uint64_t mDeadTableFull = 0; ///< deaths not recorded: kMaxDead living actors are dead already
            std::uint64_t mPlayerAttacks = 0; ///< task 0172: attack key presses told to the pack
            std::uint64_t mPlayerAttackRefused = 0; ///< no rules, or the pack's function threw
            std::uint64_t mWriterFailures = 0; ///< a call into the engine (sink, world, meta) that threw: caught here
            std::uint64_t mProjectilesLaunched = 0; ///< task 0178
            std::uint64_t mProjectilesRefused = 0; ///< bad request, stale owner, table full, outside update
            std::uint64_t mProjectileHitsActor = 0;
            std::uint64_t mProjectileHitsPlayer = 0;
            std::uint64_t mProjectileHitsWorld = 0;
            std::uint64_t mProjectilesExpired = 0;
            std::uint64_t mProjectileResultsDropped = 0; ///< the owner had gone
            std::uint64_t mPlayerAttackEdges = 0;
            std::uint64_t mPlayerShoutEdges = 0; ///< task 0205
            std::uint64_t mPlayerShoutRefused = 0;
            std::uint64_t mSoundsPlayed = 0; ///< task 0193: sound requests handed to the sink
            std::uint64_t mSoundsRefused = 0; ///< no sink, stale actor, empty or over-long path, bad volume
            /// Task 0192: nanoseconds spent inside rules->combatUpdate (summed, only with OPENEDEN_NEUTRAL_ACTOR_COST).
            std::uint64_t mRulesNs = 0;
            std::uint64_t mRaysCast = 0; ///< task 0191
            std::uint64_t mRaysRefused = 0; ///< bad request, unknown subject, no world, outside update
            std::uint64_t mVisualShotsLaunched = 0; ///< launched with visualOnly (also counted in mProjectilesLaunched)
            std::uint64_t mVisualShotsEnded = 0; ///< ended without a word to the pack
            std::uint64_t mEffectsShown = 0;
            std::uint64_t mEffectsHidden = 0;
            std::uint64_t mEffectsRefused = 0; ///< bad request, stale subject, table full, outside update
            std::uint64_t mEffectsLost = 0; ///< removed because the subject went
        };

        static constexpr std::size_t kMaxPlaying = 64;
        /// Corpses remembered at once (review fix of task 0171: pruned of actors that are gone, never grows).
        static constexpr std::size_t kMaxDead = 1024;
        /// Updates between two prunes of the dead list (it is also pruned when it is full).
        static constexpr std::size_t kDeadPruneUpdates = 64;
        static constexpr float kMetaTimeoutSeconds = 5.f;
        static constexpr std::size_t kMaxPendingResults = 64;
        /// Task 0178: projectiles in flight at once, and the longest model path accepted.
        static constexpr std::size_t kMaxProjectiles = 64;
        static constexpr std::size_t kMaxProjectileModelPath = 256;
        /// A projectile that has flown this long is expired whatever its range says (it fell out of the world).
        static constexpr float kMaxProjectileSeconds = 30.f;
        static constexpr std::size_t kMaxCandidates = 4096;
        /// Task 0171 stage S5-3b: a blow needs a clear line from the attacker, this far above its feet, to the target's
        /// capsule: mid-body (half the 128 unit standing height). Measured in the Bannered Mare: the bar counter's top
        /// is about 80 above the floor, so a line at 64 meets it and a line at 80 skims over it.
        static constexpr float kWeaponHeight = 64.f;
        /// ... aimed at the capsule this far inside its top and bottom (never at the floor under it).
        static constexpr float kAimMargin = 8.f;

        explicit NeutralCombatBridge(const NeutralActorBridge& actors);
        NeutralCombatBridge(const NeutralCombatBridge&) = delete;
        NeutralCombatBridge& operator=(const NeutralCombatBridge&) = delete;

        /// One frame: finishes told late, clocks, events, finishes, then rules->combatUpdate(seconds, writer). Does
        /// nothing for null rules (running clips are stopped and forgotten without telling anybody). A non-finite or
        /// negative `seconds` is passed as 0.
        void update(openeden::sdk::game::CombatRules* rules, float seconds, NeutralClipSink& sink,
            const NeutralClipMetaFn& meta, NeutralCombatWorld* world = nullptr);

        /// Task 0183: when set, a clip whose meta is not visible at its first poll is waited for ONCE through `waiter`
        /// (which may block the main thread for a bounded time and returns the meta or none): the frame on which every
        /// event of the clip is delivered, and the order of the events of several clips, then no longer depend on how
        /// long a worker took to read the sidecar. The host sets it only for repeatable test runs (fixed frame time);
        /// a game that is played does not stall on a clip, and its events follow the arrival, as before.
        void setMetaWaiter(NeutralClipMetaFn waiter) { mMetaWaiter = std::move(waiter); }

        /// Task 0172 (stage S5-4): the player pressed attack (`power`: held long enough). Calls rules->onPlayerAttack
        /// just before the next update's combatUpdate would; main thread, outside update(). False (and counted) with no
        /// rules or when the pack's function throws (caught).
        bool playerAttack(openeden::sdk::game::CombatRules* rules, bool power);

        /// Task 0178: the player's attack control went down or up (`pressed`): tells rules->onPlayerAttackEdge. False
        /// (and counted) with no rules or when the pack's function throws (caught).
        bool playerAttackEdge(openeden::sdk::game::CombatRules* rules, bool pressed);

        /// Task 0205 (contract 0.6): the player's shout control went down or up (`pressed`): tells
        /// rules->onPlayerShoutEdge. False (and counted in mPlayerShoutRefused) with no rules or when the pack's
        /// function throws (caught).
        bool playerShoutEdge(openeden::sdk::game::CombatRules* rules, bool pressed);

        /// Projectiles in the air, and arrows stuck in the world and still drawn.
        [[nodiscard]] std::size_t projectilesInFlight() const { return mShots.size(); }
        [[nodiscard]] std::size_t projectilesStuck() const { return mStuck.size(); }

        /// True once the pack reported the actor dead.
        [[nodiscard]] bool isDead(openeden::sdk::game::ActorHandle actor) const;

        /// Task 0184: true once the pack reported dead the actor registered under `key` (the physics system's key of a
        /// neutral actor); false for an unknown key. The activation of a corpse asks this.
        [[nodiscard]] bool isDeadByKey(const void* key) const;

        /// Forgets every clip without telling anybody (shutdown).
        void clear();

        [[nodiscard]] std::size_t playing() const { return mPlaying.size(); }
        /// Actors told dead and still remembered.
        [[nodiscard]] std::size_t deadCount() const { return mDead.size(); }
        /// Bytes the tables hold in reserve now: 0 until the first update with rules (a session without combat pays
        /// nothing, review fix of task 0171).
        [[nodiscard]] std::size_t reservedBytes() const;
        [[nodiscard]] const Stats& stats() const { return mStats; }

    private:
        struct Playing
        {
            openeden::sdk::game::CombatSubject mSubject;
            const void* mKey = nullptr;
            openeden::sdk::game::ClipSlot mSlot = openeden::sdk::game::ClipSlot::None;
            std::uint32_t mToken = 0;
            std::shared_ptr<const HavokPack::ClipMeta> mMeta;
            float mPrevious = -1.f; ///< clip clock before this frame's step
            float mCurrent = -1.f;
            float mWaited = 0.f; ///< playback seconds spent waiting for the meta
            float mRate = 1.f; ///< task 0178: the clip's speed multiplier (the draw speed of a bow)
            bool mWaitedFor = false; ///< task 0183: the meta waiter has been asked for this clip
        };
        struct Stuck
        {
            std::uint32_t mId = 0;
            float mSecondsLeft = 0.f;
            std::uint32_t mToken = 0; ///< task 0262: the pack's token, for cancelProjectile
        };
        struct Finished
        {
            openeden::sdk::game::CombatSubject mSubject;
            std::uint32_t mToken;
            openeden::sdk::game::ClipEnd mHow;
        };

        class Writer final : public openeden::sdk::game::CombatRequestWriter
        {
        public:
            explicit Writer(NeutralCombatBridge& bridge)
                : mBridge(bridge)
            {
            }
            bool playClip(
                openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::ClipRequest& request) override;
            bool stopClip(openeden::sdk::game::CombatSubject subject) override;
            bool faceHeading(openeden::sdk::game::CombatSubject subject, float heading) override;
            bool requestSweep(
                openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::SweepRequest& request) override;
            bool reportDamage(const openeden::sdk::game::DamageReport& report) override;
            bool reportDeath(openeden::sdk::game::ActorHandle actor) override;
            bool subjectState(
                openeden::sdk::game::CombatSubject subject, openeden::sdk::game::SubjectState& out) override;
            bool clipTiming(std::string_view clip, bool perSex, openeden::sdk::game::ClipTiming& out) override;
            bool launchProjectile(
                openeden::sdk::game::CombatSubject owner, const openeden::sdk::game::ProjectileRequest& r) override;
            bool setClipSpeed(openeden::sdk::game::CombatSubject subject, float multiplier) override;
            bool playSound(const openeden::sdk::game::SoundRequest& request) override;
            bool clipEventTime(std::string_view clip, bool perSex, openeden::sdk::game::ClipEventKind kind,
                float& out) override;
            bool castRay(openeden::sdk::game::CombatSubject from, const openeden::sdk::game::RayRequest& request,
                openeden::sdk::game::RayHit& out) override;
            bool showEffect(openeden::sdk::game::CombatSubject subject,
                const openeden::sdk::game::EffectRequest& request) override;
            bool hideEffect(std::uint32_t token) override;
            bool cancelProjectile(std::uint32_t token) override; // task 0262
            NeutralClipSink* mSink = nullptr; ///< non-null only while the pack's combatUpdate runs
            NeutralCombatWorld* mWorld = nullptr; ///< likewise (may be null: sweeps are then refused)

        private:
            // Each public call is the matching do... call inside a try block: nothing the engine's side throws (the
            // sink, the world, the renderer's lambdas behind it) reaches the pack (review fix of task 0171). A throw
            // counts in Stats::mWriterFailures, logs once and makes the call return false.
            template <class F>
            bool guarded(F&& call);
            bool doPlayClip(
                openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::ClipRequest& request);
            bool doStopClip(openeden::sdk::game::CombatSubject subject);
            bool doFaceHeading(openeden::sdk::game::CombatSubject subject, float heading);
            bool doRequestSweep(
                openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::SweepRequest& request);
            bool doReportDamage(const openeden::sdk::game::DamageReport& report);
            bool doReportDeath(openeden::sdk::game::ActorHandle actor);
            bool doSubjectState(openeden::sdk::game::CombatSubject subject, openeden::sdk::game::SubjectState& out);
            bool doClipTiming(std::string_view clip, bool perSex, openeden::sdk::game::ClipTiming& out);
            bool doLaunchProjectile(
                openeden::sdk::game::CombatSubject owner, const openeden::sdk::game::ProjectileRequest& r);
            bool doSetClipSpeed(openeden::sdk::game::CombatSubject subject, float multiplier);
            bool doPlaySound(const openeden::sdk::game::SoundRequest& request);
            bool doClipEventTime(std::string_view clip, bool perSex, openeden::sdk::game::ClipEventKind kind,
                float& out);
            bool doCastRay(openeden::sdk::game::CombatSubject from, const openeden::sdk::game::RayRequest& request,
                openeden::sdk::game::RayHit& out);
            bool doShowEffect(openeden::sdk::game::CombatSubject subject,
                const openeden::sdk::game::EffectRequest& request);
            bool doHideEffect(std::uint32_t token);
            bool doCancelProjectile(std::uint32_t token);
            NeutralCombatBridge& mBridge;
        };

        std::size_t indexOf(openeden::sdk::game::CombatSubject subject) const;
        void ensureStorage(); ///< the tables' reserve, made when the first update with rules runs
        void pruneDead(); ///< forgets the corpses of actors that are no longer registered
        void stopSafely(NeutralClipSink& sink, const void* key); ///< sink.stopClip, a throw caught
        void tellFinished(openeden::sdk::game::CombatRules& rules, const Finished& finished);

        struct SweepResult
        {
            openeden::sdk::game::CombatSubject mSubject;
            std::uint32_t mToken = 0;
            std::size_t mCount = 0;
            openeden::sdk::game::SweepHit mHits[openeden::sdk::game::kMaxSweepHits];
        };

        void stepProjectiles(float seconds, NeutralClipSink& sink, NeutralCombatWorld* world);
        void followEffects(NeutralClipSink& sink, NeutralCombatWorld* world); ///< task 0191

        /// Task 0191: a model shown on a subject (a ward's body), kept at its position every frame. Its id is in the
        /// same space as the projectiles' (the sink does not tell them apart).
        struct Effect
        {
            std::uint32_t mId = 0;
            std::uint32_t mToken = 0;
            openeden::sdk::game::CombatSubject mSubject;
            float mHeight = 0.f;
        };

        const NeutralActorBridge& mActors;
        std::vector<NeutralProjectile::Shot> mShots; ///< in flight
        std::vector<Stuck> mStuck; ///< arrows that hit the world and are still drawn
        std::vector<Effect> mEffects; ///< task 0191, at most kMaxShownEffects
        std::vector<openeden::sdk::game::ProjectileResult> mProjectileResults; ///< ended, not yet told
        std::uint32_t mNextProjectileId = 1;
        std::vector<SweepResult> mResults; ///< cast, not yet told
        std::vector<NeutralSweep::Candidate> mCandidates;
        std::vector<openeden::sdk::game::ActorHandle> mDead; ///< sorted by (reference, session), at most kMaxDead
        std::size_t mUpdatesSincePrune = 0;
        bool mWarnedWriter = false;
        bool mWarnedDeadFull = false;
        std::vector<Playing> mPlaying;
        std::vector<Finished> mLate; ///< interrupts, told at the start of the next update
        Stats mStats;
        NeutralClipMetaFn mMetaWaiter; ///< task 0183: empty unless the host runs with a fixed frame time
        Writer mWriter;
    };
}

#endif
