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
    /// Where the clip goes. `key` is the actor key of the NeutralActorBridge.
    class NeutralClipSink
    {
    public:
        virtual ~NeutralClipSink() = default;
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
        };

        static constexpr std::size_t kMaxPlaying = 64;
        /// Corpses remembered at once (review fix of task 0171: pruned of actors that are gone, never grows).
        static constexpr std::size_t kMaxDead = 1024;
        /// Updates between two prunes of the dead list (it is also pruned when it is full).
        static constexpr std::size_t kDeadPruneUpdates = 64;
        static constexpr float kMetaTimeoutSeconds = 5.f;
        static constexpr std::size_t kMaxPendingResults = 64;
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

        /// Task 0172 (stage S5-4): the player pressed attack (`power`: held long enough). Calls rules->onPlayerAttack
        /// just before the next update's combatUpdate would; main thread, outside update(). False (and counted) with no
        /// rules or when the pack's function throws (caught).
        bool playerAttack(openeden::sdk::game::CombatRules* rules, bool power);

        /// True once the pack reported the actor dead.
        [[nodiscard]] bool isDead(openeden::sdk::game::ActorHandle actor) const;

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
            float mWaited = 0.f; ///< seconds spent waiting for the meta
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

        const NeutralActorBridge& mActors;
        std::vector<SweepResult> mResults; ///< cast, not yet told
        std::vector<NeutralSweep::Candidate> mCandidates;
        std::vector<openeden::sdk::game::ActorHandle> mDead; ///< sorted by (reference, session), at most kMaxDead
        std::size_t mUpdatesSincePrune = 0;
        bool mWarnedWriter = false;
        bool mWarnedDeadFull = false;
        std::vector<Playing> mPlaying;
        std::vector<Finished> mLate; ///< interrupts, told at the start of the next update
        Stats mStats;
        Writer mWriter;
    };
}

#endif
