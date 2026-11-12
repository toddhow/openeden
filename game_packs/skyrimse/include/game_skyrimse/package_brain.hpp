#ifndef OPENEDEN_GAME_SKYRIMSE_PACKAGE_BRAIN_H
#define OPENEDEN_GAME_SKYRIMSE_PACKAGE_BRAIN_H

// The Skyrim SE pack's scheduler for AI packages (task 0135 stage S5). Pure code over the SDK's actor hooks
// (sdk/include/openeden/game/actor_rules.hpp 0.3): each NPC has an ordered list of packages; about once a second the
// brain picks the FIRST package whose schedule fits the game clock and whose conditions pass, and runs what it makes
// the actor do:
//   Sandbox / Wander: go to the package's location, then wander to random reachable points within its radius and
//                     pause at each for a few seconds;
//   Travel:           go to the location and stay;
//   Sleep, Eat, Stand, Unsupported: stand at/near the location (no furniture use: not built).
// Nothing here knows an engine, renderer or Jolt type. Main thread only. No clock (the game clock comes through the
// writer), no unseeded randomness (a splitmix64 stream per actor, seeded from Options::mSeed and the placement id), no
// iteration over unordered containers. update() allocates nothing once an actor's packages are resolved.
//
// Guesses, each a named constant below or in package.hpp, marked RECALLED-VERIFY or "estimate" where they are not
// settled by the data: the day-of-week numbering, the one hour window of an hour with no duration, the pause lengths,
// the re-evaluation period, the default sandbox radius, walking pace for every package whatever its preferred speed.

#include <game_skyrimse/package.hpp>

#include <openeden/game/actor_rules.hpp>

#include <cstdint>
#include <map>
#include <optional>
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

    /// 0 Sunday .. 6 Saturday for a day counter that starts on the session's start day (RECALLED-VERIFY: a new game
    /// starts on Morndas, weekday 1).
    [[nodiscard]] int weekdayOfDay(std::int32_t day);

    /// Is the schedule in force at `hour` of `day`? An invalid clock (`clockValid` false) passes every schedule.
    /// Hour -1 passes always (a duration then is a scene timer). An hour with duration <= 0 is a one hour window. A
    /// window may cross midnight. The day-of-week filter: -1 any; 0..6 that day; 7 Monday to Friday; 8 Saturday and
    /// Sunday; 9 Monday, Wednesday, Friday; 10 Tuesday, Thursday (RECALLED-VERIFY); other values never pass.
    [[nodiscard]] bool scheduleActive(const PackageSchedule& schedule, float hour, std::int32_t day, bool clockValid);

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
        };

        enum class Phase : std::uint8_t
        {
            Waiting, ///< the records are not ready, or no evaluation yet
            Idle, ///< no package in force
            Walking, ///< a goal is out
            Pausing, ///< standing at a wander point until the timer ends
            Done, ///< arrived at a stand-or-travel anchor (or a sandbox with no room to wander)
            Failed, ///< the last trip failed; retries after kRetrySeconds
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
        };

        PackageBrain(PackageSource& source, Options options = {});

        void onActorAdded(const openeden::sdk::game::ActorInfo& info);
        void onActorRemoved(openeden::sdk::game::ActorHandle handle);
        void onGoalFinished(openeden::sdk::game::ActorHandle handle, openeden::sdk::game::GoalResult result);
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
        };

        void evaluate(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        void start(Actor& actor, const PackageRecord* package, openeden::sdk::game::ActorRequestWriter& writer);
        /// Decides what to do now with the anchor already set: walk to it, or begin to wander.
        void begin(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        [[nodiscard]] bool wanders(const Actor& actor) const;
        void run(Actor& actor, float seconds, openeden::sdk::game::ActorRequestWriter& writer);
        void goTo(Actor& actor, double x, double y, double z, openeden::sdk::game::ActorRequestWriter& writer);
        [[nodiscard]] static float nextUnit(Actor& actor);

        PackageSource& mSource;
        Options mOptions;
        std::map<openeden::sdk::game::WorldId, Actor> mActors;
        Stats mStats;
    };
}

#endif
