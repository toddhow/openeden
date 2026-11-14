#ifndef OPENEDEN_GAME_SKYRIMSE_PACKAGE_BRAIN_H
#define OPENEDEN_GAME_SKYRIMSE_PACKAGE_BRAIN_H

// The Skyrim SE pack's scheduler for AI packages (task 0135 stage S5). Pure code over the SDK's actor hooks
// (sdk/include/openeden/game/actor_rules.hpp 0.3): each NPC has an ordered list of packages; about once a second the
// brain picks the FIRST package whose schedule fits the game clock and whose conditions pass, and runs what it makes
// the actor do:
//   Sandbox / Wander: go to the package's location, then wander to random reachable points within its radius and
//                     pause at each for a few seconds;
//   Travel:           go to the location and stay;
//   Sleep:            walk to a free bed near the location and lie on it (stage S6b), until the package ends;
//   Eat:              walk to a free seat near the location and sit on it (stage S6b), until the package ends;
//   Sandbox / Wander with the package's "allow sitting" input on: at a pause, sometimes sit on a free seat near the
//                     location for a while, then get up and go on (stage S6b);
//   Stand, Unsupported: stand at/near the location. Furniture needs a FurnitureSource; without one (or when it finds
//   nothing) Sleep and Eat stand at the location as before.
// Nothing here knows an engine, renderer or Jolt type. Main thread only. No clock (the game clock comes through the
// writer), no unseeded randomness (a splitmix64 stream per actor, seeded from Options::mSeed and the placement id), no
// iteration over unordered containers. update() allocates nothing once an actor's packages are resolved.
//
// Guesses, each a named constant below or in package.hpp, marked RECALLED-VERIFY or "estimate" where they are not
// settled by the data: the day-of-week numbering, the one hour window of an hour with no duration, the pause lengths,
// the re-evaluation period, the default sandbox radius, walking pace for every package whatever its preferred speed.

#include <game_skyrimse/anim_motion.hpp>
#include <game_skyrimse/package.hpp>

#include <openeden/game/actor_rules.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace game_skyrimse
{
    /// How often (seconds of frame time) an actor looks again for the package to run. An estimate.
    inline constexpr float kPackageReevaluateSeconds = 1.f;
    /// A sandbox or wander package without a usable radius wanders this far from its anchor. An estimate.
    inline constexpr float kDefaultSandboxRadius = 512.f;
    /// The most a package radius counts (game units): a hostile or absurd value is cut.
    inline constexpr float kMaxPackageRadius = 2048.f;
    /// A radius below this is "stay put".
    inline constexpr float kMinWanderRadius = 48.f;
    /// How near the anchor counts as being there for a Travel package, and the slack added to a radius.
    inline constexpr float kArriveTolerance = 48.f;
    /// Pauses between wander legs (seconds of frame time), uniformly drawn. Estimates.
    inline constexpr float kPauseMinSeconds = 3.f;
    inline constexpr float kPauseMaxSeconds = 10.f;
    /// After a failed leg (no path or blocked) the actor stands this long before it tries another point.
    inline constexpr float kFailurePauseSeconds = 8.f;
    /// After a failed trip to the anchor it waits this long before asking again.
    inline constexpr float kRetrySeconds = 30.f;

    /// Stage S6f constants. Estimates unless marked.
    /// A patrol marker without an XPRD idles this long (seconds of frame time).
    inline constexpr float kPatrolDefaultIdleSeconds = 2.f;
    /// A follower stays within this many units of its target (and walks to a point kFollowStandOff from it).
    inline constexpr double kFollowDistance = 260.0;
    inline constexpr double kFollowStandOff = 140.0;
    /// How often a follower looks again where its target is.
    inline constexpr float kFollowRepathSeconds = 1.5f;

    /// The speed a package walks at (stage S6f): Run when the package sets its own speed (PKDT flag bit 13,
    /// `kPreferredSpeedFlag`) and the byte is 1 (jog) or 2 (run), else Walk. There is no jog clip (task 0148 has walk and
    /// run), so jog runs; fast walk (3) and walk (0) walk. The byte values' names are RECALLED-VERIFY (xEdit: Walk, Jog,
    /// Run, Fast Walk); that bit 13 gates the byte is settled by the data (see `kPreferredSpeedFlag`).
    [[nodiscard]] openeden::sdk::game::ActorSpeed packageSpeed(const PackageRecord& package);

    /// 0 Sunday .. 6 Saturday for a day counter that starts on the session's start day (RECALLED-VERIFY: a new game
    /// starts on Morndas, weekday 1).
    [[nodiscard]] int weekdayOfDay(std::int32_t day);

    /// Is the schedule in force at `hour` of `day`? An invalid clock (`clockValid` false) passes every schedule.
    /// Hour -1 passes always (a duration then is a scene timer). An hour with duration <= 0 is a one hour window. A
    /// window may cross midnight. The day-of-week filter: -1 any; 0..6 that day; 7 Monday to Friday; 8 Saturday and
    /// Sunday; 9 Monday, Wednesday, Friday; 10 Tuesday, Thursday (RECALLED-VERIFY); other values never pass.
    [[nodiscard]] bool scheduleActive(const PackageSchedule& schedule, float hour, std::int32_t day, bool clockValid);

    /// After a sandbox actor's pause it sits down with this chance (an estimate), for a time drawn from this range of
    /// seconds of frame time (estimates).
    inline constexpr float kSitChance = 0.6f;
    inline constexpr float kSitMinSeconds = 45.f;
    inline constexpr float kSitMaxSeconds = 150.f;
    /// After a failed trip to furniture the actor tries the next seat/bed after this long.
    inline constexpr float kFurnitureRetrySeconds = 4.f;
    /// How far from the package location a seat or bed is looked for when the package gives a smaller radius.
    inline constexpr float kMinFurnitureRadius = 512.f;
    /// The most failed seats an actor remembers (until its package changes).
    inline constexpr std::size_t kMaxBadSlots = 16;

    /// One place an actor can use: a sit or sleep marker of a furniture placement.
    struct FurnitureSlot
    {
        std::uint64_t mKey = 0; ///< unique per marker of a placement; stable
        openeden::sdk::game::FurniturePose mPose = openeden::sdk::game::FurniturePose::Sit;
        double mX = 0, mY = 0, mZ = 0; ///< where the actor's origin goes
        float mHeading = 0.f;
        double mEntryX = 0, mEntryY = 0, mEntryZ = 0; ///< where it walks to first, and steps off to
        std::string mClip;
        bool mClipPerSex = false;
        // Stage S6f: the sit-down and get-up clips and their root motion (null: none, the actor is placed on the seat and
        // put at the entry point as in stage S6b). The motion tables are owned by the source and outlive the brain.
        std::string mEnterClip;
        std::string mExitClip;
        const ClipMotion* mEnterMotion = nullptr;
        const ClipMotion* mExitMotion = nullptr;
        /// The short get-up (a bed's quick exit, about 1 s) used when the player wakes a sleeper to talk. Empty: none.
        std::string mQuickExitClip;
        const ClipMotion* mQuickExitMotion = nullptr;
    };

    /// The furniture the brain may use. The pack's view implements it; tests use a fake.
    class FurnitureSource
    {
    public:
        virtual ~FurnitureSource() = default;
        /// The slots of `pose` within `radius` of (x, y, z) in the same cell as the actor placement `actorReference`,
        /// nearest to (fromX, fromY, fromZ) first. Each slot's entry point is the one nearest to `from`. Replaces `out`.
        virtual void slotsNear(FormId actorReference, double x, double y, double z, double radius,
            openeden::sdk::game::FurniturePose pose, double fromX, double fromY, double fromZ,
            std::vector<FurnitureSlot>& out)
            = 0;
    };

    /// What the brain asks of the world. The pack's view implements it; tests use a fake.
    class PackageSource
    {
    public:
        virtual ~PackageSource() = default;
        /// False while the records are still being read: the brain waits and asks again.
        [[nodiscard]] virtual bool ready() const = 0;
        /// The packages of an NPC base in the order they are tried (own list, then default package list), after the
        /// template rules. False when the base is unknown.
        virtual bool packagesFor(FormId base, std::vector<const PackageRecord*>& out) const = 0;
        [[nodiscard]] virtual PackageBehavior behaviorOf(const PackageRecord& package) const = 0;
        /// Do the package's own conditions pass for this actor now?
        virtual bool conditionsPass(const PackageRecord& package, FormId actorBase, FormId actorReference) = 0;
        /// Where a placed reference stands, if the source knows (only references of cells it has loaded).
        [[nodiscard]] virtual bool locate(FormId reference, double& x, double& y, double& z) const = 0;
        // Stage S6f (defaults: nothing known, so a source that only knows packages runs as before):
        [[nodiscard]] virtual PackageActivity activityOf(const PackageRecord& package) const
        {
            (void)package;
            return PackageActivity::None;
        }
        [[nodiscard]] virtual bool targetOf(const PackageRecord& package, PackageTarget& out) const
        {
            (void)package;
            (void)out;
            return false;
        }
        /// The reference `reference` is linked to with `keyword` (0 = the unkeyworded link).
        [[nodiscard]] virtual bool linkedRef(FormId reference, FormId keyword, FormId& out) const
        {
            (void)reference;
            (void)keyword;
            (void)out;
            return false;
        }
        /// Seconds a patrolling actor idles at this marker; negative when the marker says nothing.
        [[nodiscard]] virtual float patrolIdle(FormId reference) const
        {
            (void)reference;
            return -1.f;
        }
    };

    /// The first package of `packages` in force now, or null.
    [[nodiscard]] const PackageRecord* selectPackage(const std::vector<const PackageRecord*>& packages, float hour,
        std::int32_t day, bool clockValid, PackageSource& source, FormId actorBase, FormId actorReference);

    class PackageBrain
    {
    public:
        struct Options
        {
            std::uint64_t mSeed = 0x5EED5EEDull;
            float mReevaluateSeconds = kPackageReevaluateSeconds;
            /// A sleeper whose walk to the bed fails (no path, blocked: closed doors, stairs the navigator cannot use)
            /// is put on the bed directly, as the game does for an actor whose package begins while its cell is not
            /// loaded. Off by default; the pack turns it on (`[extension.skyrimse] actor sleep snap`).
            bool mSnapSleepers = false;
        };

        struct Stats
        {
            std::uint64_t mActorsAdded = 0;
            std::uint64_t mEvaluations = 0;
            std::uint64_t mSwitches = 0; ///< times an actor's running package changed (including to none)
            std::uint64_t mNoPackage = 0; ///< evaluations that found no package in force
            std::uint64_t mGoalsIssued = 0;
            std::uint64_t mArrived = 0;
            std::uint64_t mNoPath = 0;
            std::uint64_t mBlocked = 0;
            std::uint64_t mWritesRefused = 0; ///< the core refused a request (stale handle)
            std::uint64_t mUnresolvedLocations = 0; ///< a location the brain could not place; the home was used
            std::uint64_t mUnsupported = 0; ///< times an actor started a package of kind Unsupported
            std::uint64_t mFurnitureTrips = 0; ///< times an actor set out for a seat or bed
            std::uint64_t mSeatings = 0; ///< times the core accepted a furniture use
            std::uint64_t mStandUps = 0; ///< times an actor left its furniture
            std::uint64_t mFurnitureFailed = 0; ///< trips that failed (no path, blocked, refused)
            std::uint64_t mNoFurniture = 0; ///< searches that found no free slot
            std::uint64_t mSnapped = 0; ///< sleepers put on their bed after a failed walk
            // Stage S6f
            std::uint64_t mRuns = 0; ///< goals issued at the run speed
            std::uint64_t mPatrolLegs = 0; ///< goals issued to a patrol marker
            std::uint64_t mPatrolUnresolved = 0; ///< a patrol package whose first marker could not be found
            std::uint64_t mFollowLegs = 0; ///< goals issued towards a follow target
            std::uint64_t mFollowUnresolved = 0; ///< a follow package whose target is not an actor of the brain
            std::uint64_t mEscortsSkipped = 0; ///< escort packages started (run as Stand)
            std::uint64_t mConversations = 0; ///< conversations with an actor of the brain
            std::uint64_t mWakeUps = 0; ///< sleepers a conversation stood up
            std::uint64_t mResumes = 0; ///< actors a finished conversation set back on their package
            std::uint64_t mEnters = 0; ///< sit-downs and lie-downs played with a clip and its motion
            std::uint64_t mExits = 0; ///< get-ups played with a clip and its motion
        };

        enum class Phase : std::uint8_t
        {
            Waiting, ///< the records are not ready, or no evaluation yet
            Idle, ///< no package in force
            Walking, ///< a goal is out
            Pausing, ///< standing at a wander point until the timer ends
            Done, ///< arrived at a stand-or-travel anchor (or a sandbox with no room to wander)
            Failed, ///< the last trip failed; retries after kRetrySeconds
            Approaching, ///< walking to a seat or bed (stage S6b)
            Seated, ///< on furniture
            Patrolling, ///< waiting at, or walking between, patrol markers (stage S6f)
            Following, ///< keeping near a target actor (stage S6f)
        };

        /// What the tests and the trace see of one actor.
        struct ActorView
        {
            const PackageRecord* mActive = nullptr;
            PackageBehavior mBehavior = PackageBehavior::Stand;
            Phase mPhase = Phase::Waiting;
            double mX = 0, mY = 0, mZ = 0; ///< the brain's estimate of where the actor stands
            double mAnchorX = 0, mAnchorY = 0, mAnchorZ = 0;
            float mRadius = 0.f;
            std::size_t mPackages = 0;
            bool mSeated = false;
            std::uint64_t mSlotKey = 0; ///< the slot reserved (approaching or seated), else 0
            PackageActivity mActivity = PackageActivity::None;
            openeden::sdk::game::ActorSpeed mSpeed = openeden::sdk::game::ActorSpeed::Walk;
            bool mTalking = false;
            bool mEntering = false; ///< a sit-down or lie-down clip is playing
            bool mExiting = false; ///< a get-up clip is playing
            FormId mPatrolMarker = kNullFormId; ///< the marker the actor walks to or waits at
        };

        PackageBrain(PackageSource& source, Options options = {});

        /// Switches furniture use on (null switches it off). The source must outlive the brain.
        void setFurnitureSource(FurnitureSource* source) { mFurniture = source; }

        void onActorAdded(const openeden::sdk::game::ActorInfo& info);
        void onActorRemoved(openeden::sdk::game::ActorHandle handle);
        void onGoalFinished(openeden::sdk::game::ActorHandle handle, openeden::sdk::game::GoalResult result);
        /// Stage S6f: the player begins or ends a conversation with the actor of this placement (main thread, outside
        /// update(): acted on at the next update). A sleeper wakes and stands; any actor stops where it is while it talks;
        /// a seated one stays seated; afterwards the actor takes up its package again (a sleeper looks for a bed).
        void onConversation(openeden::sdk::game::WorldId reference, bool begins);
        void update(float seconds, openeden::sdk::game::ActorRequestWriter& writer);

        [[nodiscard]] std::size_t size() const { return mActors.size(); }
        [[nodiscard]] const Stats& stats() const { return mStats; }
        [[nodiscard]] std::optional<ActorView> view(openeden::sdk::game::WorldId reference) const;

    private:
        struct Actor
        {
            openeden::sdk::game::ActorHandle mHandle;
            FormId mBase = kNullFormId;
            double mHomeX = 0, mHomeY = 0, mHomeZ = 0;
            float mHeading = 0.f;
            std::vector<const PackageRecord*> mPackages;
            bool mResolved = false;
            bool mEvaluatedOnce = false;
            float mSinceEval = 0.f;
            const PackageRecord* mActive = nullptr;
            PackageBehavior mBehavior = PackageBehavior::Stand;
            Phase mPhase = Phase::Waiting;
            double mX = 0, mY = 0, mZ = 0;
            double mAnchorX = 0, mAnchorY = 0, mAnchorZ = 0;
            float mRadius = 0.f;
            float mTimer = 0.f;
            bool mGoalOut = false;
            bool mToAnchor = false; ///< the goal out leads to the anchor (not a wander leg)
            bool mFaceNext = false; ///< write a heading at the next update (set where no writer is open)
            float mFaceHeading = 0.f;
            double mGoalX = 0, mGoalY = 0, mGoalZ = 0;
            std::uint64_t mRng = 0;
            // Stage S6b
            FurnitureSlot mSlot;
            bool mHasSlot = false;
            bool mToFurniture = false; ///< the goal out leads to mSlot's entry point
            bool mUseNext = false; ///< write the furniture use at the next update
            bool mSeated = false;
            float mSeatTimer = -1.f; ///< seconds left on the furniture; negative: until the package ends
            std::vector<std::uint64_t> mBadSlots;
            // Stage S6f
            PackageActivity mActivity = PackageActivity::None;
            openeden::sdk::game::ActorSpeed mSpeed = openeden::sdk::game::ActorSpeed::Walk;
            FormId mPatrolFirst = kNullFormId;
            FormId mPatrolMarker = kNullFormId;
            bool mToPatrol = false; ///< the goal out leads to mPatrolMarker
            FormId mFollowTarget = kNullFormId; ///< placement id of the actor followed
            bool mToFollow = false;
            float mFollowTimer = 0.f;
            int mTalkRequest = 0; ///< 1 begin, 2 end: acted on at the next update
            bool mTalkEndPending = false; ///< the end came before the begin was acted on (the world is paused during a talk)
            bool mTalking = false;
            bool mWoken = false; ///< a conversation stood the actor up from a bed
            bool mSkipEnter = false; ///< the next furniture use is a snap: no clip, no motion
            bool mEntering = false;
            bool mExiting = false;
            const ClipMotion* mExitMotion = nullptr; ///< the get-up clip's motion being played
            float mClipClock = 0.f; ///< seconds into the enter or exit clip
            double mStartX = 0, mStartY = 0, mStartZ = 0; ///< where the enter clip's motion begins
            float mStartHeading = 0.f;
            double mExitX = 0, mExitY = 0, mExitZ = 0; ///< where the exit clip's motion ends
            float mExitHeading = 0.f;
            const PackageRecord* mDeferred = nullptr; ///< the package to start when the get-up ends
            bool mHasDeferred = false;
            bool mAfterExitPause = false; ///< after the get-up a wanderer pauses (the timer ran out), else is Done
        };

        void evaluate(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        void start(Actor& actor, const PackageRecord* package, openeden::sdk::game::ActorRequestWriter& writer);
        /// Decides what to do now with the anchor already set: walk to it, or begin to wander.
        void begin(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        [[nodiscard]] bool wanders(const Actor& actor) const;
        void run(Actor& actor, float seconds, openeden::sdk::game::ActorRequestWriter& writer);
        void goTo(Actor& actor, double x, double y, double z, openeden::sdk::game::ActorRequestWriter& writer,
            openeden::sdk::game::ActorSpeed speed = openeden::sdk::game::ActorSpeed::Walk);
        [[nodiscard]] static float nextUnit(Actor& actor);
        /// Stage S6b: reserves the nearest free slot of `pose` near the anchor and sets out for it. False (nothing
        /// changed) when there is none.
        bool tryFurniture(Actor& actor, openeden::sdk::game::FurniturePose pose, double radius,
            openeden::sdk::game::ActorRequestWriter& writer);
        void releaseFurniture(Actor& actor);
        /// True when the actor is standing (nothing seated, or no get-up clip); false while the exit clip plays.
        bool standUp(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer, bool quick = false);
        // Stage S6f
        void beginPatrol(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        void patrolNext(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        void beginFollow(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        void runFollow(Actor& actor, float seconds, openeden::sdk::game::ActorRequestWriter& writer);
        void handleTalk(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        void beginUse(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        void runClip(Actor& actor, float seconds, openeden::sdk::game::ActorRequestWriter& writer);
        void finishExit(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        [[nodiscard]] bool sitAllowed(const Actor& actor) const;

        PackageSource& mSource;
        Options mOptions;
        std::map<openeden::sdk::game::WorldId, Actor> mActors;
        FurnitureSource* mFurniture = nullptr;
        std::map<std::uint64_t, openeden::sdk::game::WorldId> mReserved; ///< slot key to the actor holding it
        std::vector<FurnitureSlot> mScratch;
        Stats mStats;
    };
}

#endif
