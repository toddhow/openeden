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
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace game_skyrimse
{
    /// Task 0271: a chosen ForceGreet package asks for its conversation again after this many evaluations (about one
    /// per second each), until the greeting is said and the package's own conditions stop it. An estimate.
    inline constexpr int kGreetAskEvaluations = 5;
    /// Task 0305: an actor of a ForceGreet package farther than this from the player walks up to him, and stops (and
    /// asks to greet) once nearer than the stop distance; ours, below the forced greeting's own range of 3,000 units.
    inline constexpr double kGreetApproachFrom = 2600.0;
    inline constexpr double kGreetApproachStop = 1800.0;
    /// Task 0306 (`dragonborn`): the same walk-up with both distances at this value: the actor approaches from farther
    /// than it and asks to greet only once it stands within it (the cultists of the ambush). An estimate.
    inline constexpr double kGreetAskDistance = 380.0;
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
    /// Task 0277: how often an actor whose Travel target is in another cell looks again.
    inline constexpr float kElsewhereRetrySeconds = 0.5f;

    /// Stage S6f constants. Estimates unless marked.
    /// A patrol marker without an XPRD idles this long (seconds of frame time).
    inline constexpr float kPatrolDefaultIdleSeconds = 2.f;
    /// A follower stays within this many units of its target (and walks to a point kFollowStandOff from it).
    inline constexpr double kFollowDistance = 260.0;
    inline constexpr double kFollowStandOff = 140.0;
    /// How often a follower looks again where its target is.
    inline constexpr float kFollowRepathSeconds = 1.5f;

    /// Task 0196 (followers): a follower of the PLAYER looks again where the player is this often; it keeps within
    /// kFollowDistance (the stage S6f value) of the player and runs when the player is farther than
    /// kFollowerRunDistance. Estimates.
    inline constexpr float kFollowerRepathSeconds = 0.75f;
    /// After a leg towards the player failed (no path, blocked) a follower waits this long before it asks again.
    inline constexpr float kFollowerRetrySeconds = 3.f;
    inline constexpr double kFollowerRunDistance = 700.0;
    /// A goal already out is replaced when the player's spot has moved this far from the goal's.
    inline constexpr double kFollowerGoalSlack = 320.0;
    /// Task 0332: an escort leads the player to its destination; it waits when the player is farther behind than this,
    /// and has arrived within the package's radius of the destination (at least kEscortMinRadius). Estimates.
    inline constexpr double kEscortWaitDistance = 1500.0;
    inline constexpr double kEscortMinRadius = 96.0;
    /// ... and heads for the destination only when it is this near: the navigator knows the cells round the player.
    inline constexpr double kEscortReach = 6000.0;
    /// Where the navigator has no path the escort walks the leg straight, at the walk clip's own speed (task 0148).
    inline constexpr float kEscortDirectSpeed = 82.45f;

    /// The speed a package walks at (stage S6f): Run when the package sets its own speed (PKDT flag bit 13,
    /// `kPreferredSpeedFlag`) and the byte is 1 (jog) or 2 (run), else Walk. There is no jog clip (task 0148 has walk
    /// and run), so jog runs; fast walk (3) and walk (0) walk. The byte values' names are RECALLED-VERIFY (xEdit: Walk,
    /// Jog, Run, Fast Walk); that bit 13 gates the byte is settled by the data (see `kPreferredSpeedFlag`).
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
        // Stage S6f: the sit-down and get-up clips and their root motion (null: none, the actor is placed on the seat
        // and put at the entry point as in stage S6b). The motion tables are owned by the source and outlive the brain.
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
        /// nearest to (fromX, fromY, fromZ) first. Each slot's entry point is the one nearest to `from`. Replaces
        /// `out`.
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
        /// Task 0274: the same list for ONE placement: the packages a running quest's alias gives the placement itself
        /// (an alias filled with a specific reference, such as the Helgen cart horses) come first. The default is the
        /// base's list.
        virtual bool packagesForReference(
            FormId reference, FormId base, std::vector<const PackageRecord*>& out) const
        {
            (void)reference;
            return packagesFor(base, out);
        }
        /// Task 0274: changes whenever the answer of `packagesForReference` may have changed (a quest started or
        /// moved to another stage); the brain then asks again for the actors it knows. The default never changes.
        [[nodiscard]] virtual std::uint64_t packageListRevision() const { return 0; }
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
        /// Task 0271: the topic of a ForceGreet package (the actor says it to the player). Default: none.
        [[nodiscard]] virtual bool forceGreetTopic(const PackageRecord& package, FormId& out) const
        {
            (void)package;
            (void)out;
            return false;
        }
        /// Task 0334: a developer trace: the brain switched this placement to `package` (null: none). Default: nothing.
        [[nodiscard]] virtual bool tracesPackages() const { return false; }
        /// Task 0344: a developer trace: the selection passed over `package` for a reason that is not a condition
        /// (its schedule is not active now). Default: nothing.
        virtual void packagePassedOver(FormId base, const PackageRecord& package, const char* why) const
        {
            (void)base;
            (void)package;
            (void)why;
        }
        virtual void packageChosen(
            FormId reference, FormId base, const PackageRecord* package, const std::string& note) const
        {
            (void)reference;
            (void)base;
            (void)package;
            (void)note;
        }
        /// Task 0277: true when the reference exists but stands in a cell other than the player's (an interior
        /// against anything else), so an actor in the player's cell cannot walk to it before the player goes there.
        /// Default: no.
        [[nodiscard]] virtual bool targetIsElsewhere(FormId reference) const
        {
            (void)reference;
            return false;
        }
        /// Task 0277: a number that changes when the packages `packagesFor` returns may have changed for an actor
        /// already resolved (a quest whose alias gives packages started or moved on). The brain then asks again.
        [[nodiscard]] virtual std::uint32_t packagesRevision() const { return 0; }
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
            std::uint64_t mEscortsArrived = 0; ///< task 0332: escorts that led the player to their destination
            std::uint64_t mConversations = 0; ///< conversations with an actor of the brain
            std::uint64_t mWakeUps = 0; ///< sleepers a conversation stood up
            std::uint64_t mResumes = 0; ///< actors a finished conversation set back on their package
            std::uint64_t mEnters = 0; ///< sit-downs and lie-downs played with a clip and its motion
            std::uint64_t mExits = 0; ///< get-ups played with a clip and its motion
            // Task 0196 (followers)
            std::uint64_t mFollowerLegs = 0; ///< goals issued towards the player
            std::uint64_t mFollowerRuns = 0; ///< of those, at the run speed
            std::uint64_t mFollowerRecruits = 0; ///< setFollowPlayer(true) calls that changed something
            std::uint64_t mFollowerDismissals = 0; ///< setFollowPlayer(false) calls that changed something
            std::uint64_t mFollowerNoPlayer = 0; ///< a repath skipped because the core gave no player position
            std::uint64_t mFollowerWaits = 0; ///< setFollowerWaiting(true) calls that changed something
            // Task 0277
            std::uint64_t mTravelMoves = 0; ///< task 0286: scene Travel walks done by moving the actor into the cell
            std::uint64_t mTravelSnaps = 0; ///< scene Travel walks that failed and ended with the actor placed there
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

        explicit PackageBrain(PackageSource& source);
        PackageBrain(PackageSource& source, Options options);

        /// Switches furniture use on (null switches it off). The source must outlive the brain.
        void setFurnitureSource(FurnitureSource* source) { mFurniture = source; }

        void onActorAdded(const openeden::sdk::game::ActorInfo& info);
        void onActorRemoved(openeden::sdk::game::ActorHandle handle);
        void onGoalFinished(openeden::sdk::game::ActorHandle handle, openeden::sdk::game::GoalResult result);
        /// Stage S6f: the player begins or ends a conversation with the actor of this placement (main thread, outside
        /// update(): acted on at the next update). A sleeper wakes and stands; any actor stops where it is while it
        /// talks; a seated one stays seated; afterwards the actor takes up its package again (a sleeper looks for a
        /// bed).
        void onConversation(openeden::sdk::game::WorldId reference, bool begins);
        /// Task 0264: the combat AI engages the actor: a seated actor gets up (the quick exit clip) at the next update
        /// even when it is a sitter; it is told with the same `onConversation(reference, true)` that follows. No effect
        /// on an actor that is not seated.
        void forceStand(openeden::sdk::game::WorldId reference);
        /// Stage S6g: the player activated the actor and the conversation has not begun (the world still runs). A
        /// sleeper starts getting up with the quick exit clip; the actor then holds still like a talking one. True
        /// when the actor can talk at once (anything but a sleeper), false while it gets up. Idempotent.
        bool prepareTalk(openeden::sdk::game::WorldId reference);
        /// True when the actor can talk: not in a sleeping pose and no get-up clip playing (unknown actor: true).
        [[nodiscard]] bool readyToTalk(openeden::sdk::game::WorldId reference) const;
        void update(float seconds, openeden::sdk::game::ActorRequestWriter& writer);

        /// Task 0196 (followers): makes the actor of this placement follow the player (or stops it). A follower ignores
        /// its packages, keeps within kFollowDistance of the player (the core's ActorRequestWriter::playerPosition) and
        /// is still the follower when the core removes and adds it again (a door carried it to another cell): the
        /// placement id is kept here, not the actor. Dismissing hands it back to its packages. True when the set of
        /// followers changed. An unknown placement is remembered and applied when the actor appears.
        bool setFollowPlayer(openeden::sdk::game::WorldId reference, bool follow);
        /// Task 0274 (the Helgen cart ride): an actor that sits on a cart is moved by the cart rules, not by packages:
        /// the brain leaves it alone (no evaluation, no talk request acted on, no goal) until it is set free. Kept by
        /// placement id like followers, so a door does not end it. True when the set changed.
        bool setVehicleRider(openeden::sdk::game::WorldId reference, bool rides);
        /// Task 0274 (mounted NPCs): an actor that sits on a horse runs its packages as usual, but its goals, stops and
        /// headings are written for the HORSE's handle (the horse is what walks) and a result told for the horse is
        /// handed to the rider. Pass an invalid handle to dismount.
        void setMount(openeden::sdk::game::WorldId rider, openeden::sdk::game::ActorHandle horse);
        /// Task 0274: a package that carries an OnEnd script fragment (`PackageRecord::mEndFunction`) has ended for an
        /// actor: a travel or a patrol with no further marker has arrived. Told once per start of the package.
        using EndHook = std::function<void(openeden::sdk::game::WorldId reference, const PackageRecord& package)>;
        void setPackageEndHook(EndHook hook) { mEndHook = std::move(hook); }
        [[nodiscard]] bool isVehicleRider(openeden::sdk::game::WorldId reference) const
        {
            return mRiderRefs.count(reference) != 0;
        }
        [[nodiscard]] bool followsPlayer(openeden::sdk::game::WorldId reference) const
        {
            return mFollowRefs.count(reference) != 0;
        }
        [[nodiscard]] const std::set<openeden::sdk::game::WorldId>& followerRefs() const { return mFollowRefs; }
        /// Task 0334: placements whose chosen package is a Follow or Escort of the player (Serana's DLC1NPCFollowMedium,
        /// Barbas's DA03BarbasFollowPlayer): they follow by their package, are no brain followers (no saved follower,
        /// the packages still decide), but the player's doors carry them and they take his side like a follower.
        [[nodiscard]] bool followsByPackage(openeden::sdk::game::WorldId reference) const
        {
            return mPackageFollowRefs.count(reference) != 0;
        }
        [[nodiscard]] const std::set<openeden::sdk::game::WorldId>& packageFollowerRefs() const
        {
            return mPackageFollowRefs;
        }
        /// Task 0239 ("Wait here"): a follower that waits stays where it stands (its goal is cancelled and no new one
        /// is issued) until it is told to follow again. Only meaningful for a follower of the player. True when the
        /// state changed. Remembered for a placement the brain does not know yet.
        bool setFollowerWaiting(openeden::sdk::game::WorldId reference, bool waiting);
        [[nodiscard]] bool followerWaiting(openeden::sdk::game::WorldId reference) const
        {
            return mWaitRefs.count(reference) != 0;
        }
        /// Task 0194 (scenes): the package an actor follows instead of the ones its schedule gives; null gives the
        /// schedule back. Takes effect at the next update. False for an actor the brain does not know.
        bool forcePackage(openeden::sdk::game::WorldId reference, const PackageRecord* package);

        /// Task 0277 (companions): an actor whose scene Travel package leads to a reference in another cell waits (it
        /// does not finish at once) and is listed by travellerRefs() so the core carries it through the doors the
        /// player uses, until the target is reachable; a walk that fails ends with the actor placed there. Off by
        /// default; the pack turns it on with `[extension.skyrimse] companions`.
        void setCarryTravellers(bool on) { mCarryTravellers = on; }
        /// Task 0341 (`side quests`): a MUST COMPLETE Travel package that cannot be walked ends with the actor placed at its target
        void setSnapMustComplete(bool on) { mSnapMustComplete = on; }

        /// Task 0286: a scene's Travel package whose target stands in another cell (see targetIsElsewhere) asks this
        /// first. `actor` is the placement, `target` the reference it travels to. When the hook accepts (the world
        /// moves the actor to the target, into its cell, keeping its identity), the package is done and the actor is
        /// not carried through the player's doors. When it refuses, or none is set, the carry of task 0277 applies.
        using TravelMoveHook = std::function<bool(openeden::sdk::game::WorldId actor, FormId target)>;
        void setTravelMoveHook(TravelMoveHook hook) { mTravelMoveHook = std::move(hook); }
        [[nodiscard]] const std::set<openeden::sdk::game::WorldId>& travellerRefs() const { return mTravellerRefs; }

        /// Task 0271: told when an actor's chosen package is a ForceGreet one (`reference` is the placement, `topic`
        /// the DIAL it says): at once, then again every kGreetAskEvaluations evaluations while the package stays
        /// chosen. The hook only records the wish; opening the conversation is the world's business. Main thread.
        using ForceGreetHook = std::function<void(openeden::sdk::game::WorldId reference, FormId base, FormId topic)>;
        void setForceGreetHook(ForceGreetHook hook) { mGreetHook = std::move(hook); }
        /// Task 0306: a ForceGreet package walks the actor to the player and asks only when it is near (off: it asks
        /// at once and stands, as the earlier slices' Brynjolf and Vilkas did).
        void setApproachGreeters(bool on)
        {
            mGreetFrom = on ? kGreetAskDistance : kGreetApproachFrom;
            mGreetStop = on ? kGreetAskDistance : kGreetApproachStop;
        }

        /// Task 0310: a Follow or Escort package whose target is the PLAYER leads the actor along behind him (the
        /// follower rules of task 0196), for as long as that package is the one in force. Off: those packages run as
        /// Stand, as before.
        void setFollowPlayerPackages(bool on) { mFollowPlayerPackages = on; }

        [[nodiscard]] std::size_t size() const { return mActors.size(); }
        [[nodiscard]] const Stats& stats() const { return mStats; }
        [[nodiscard]] std::optional<ActorView> view(openeden::sdk::game::WorldId reference) const;

        /// Task 0302 (draugr ambushes): an actor lying in a sleeping pose on furniture, no clip playing and not in a
        /// conversation, with the place of the bed (the slot's, not the actor's estimate).
        struct Sleeper
        {
            openeden::sdk::game::WorldId mReference = openeden::sdk::game::kNoWorldId;
            double mX = 0, mY = 0, mZ = 0;
        };
        /// Appends every such sleeper, in ascending placement order, to `out` (not cleared).
        void sleepers(std::vector<Sleeper>& out) const;

    private:
        struct Actor
        {
            openeden::sdk::game::ActorHandle mHandle;
            FormId mBase = kNullFormId;
            double mHomeX = 0, mHomeY = 0, mHomeZ = 0;
            float mHeading = 0.f;
            std::vector<const PackageRecord*> mPackages;
            bool mResolved = false;
            std::uint64_t mListRevision = 0; ///< task 0274: the source's revision when the list was read
            bool mEvaluatedOnce = false;
            float mSinceEval = 0.f;
            const PackageRecord* mActive = nullptr;
            const PackageRecord* mForced = nullptr; ///< task 0194: a scene's package, wins over the schedule
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
            bool mForceStand = false; ///< task 0264: the next begin also stands a sitter up
            bool mTalkEndPending
                = false; ///< the end came before the begin was acted on (the world is paused during a talk)
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
            // Task 0196
            bool mFollowPlayer = false; ///< the actor follows the player (set from mFollowRefs)
            bool mFollowEntered = false; ///< enterFollowPlayer ran for this actor
            bool mEndFired = false; ///< task 0274: the package's OnEnd fragment was told
            bool mRider = false; ///< task 0274: sits on a cart (set from mRiderRefs); the brain does nothing with it
            float mFollowPlayerTimer = 0.f;
            // Task 0305: a ForceGreet package whose actor stands too far from the player first walks up to him
            bool mGreetApproach = false;
            bool mGreetApproaching = false; ///< enterFollowPlayer ran for the approach
            bool mPackageFollow = false; ///< task 0310: the package in force leads the actor behind the player
            FormId mEscortRef = kNullFormId; ///< task 0332: the escort's destination reference
            double mEscortRadius = 0.0;
            bool mEscortDirect = false; ///< task 0332: the navigator had no path: the leg is walked straight
            // Task 0277
            FormId mElsewhereRef = kNullFormId; ///< the Travel target that stands in another cell (0: none)
            bool mSnapNext = false; ///< a scene's Travel walk failed: put the actor at the anchor at the next update
            // Task 0271
            int mGreetWait = 0; ///< evaluations until a ForceGreet package asks again
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
        // Task 0196 (followers): these are the only functions of the brain that know about followers (besides the
        // branch at the top of update() and the lines in onActorAdded).
        void enterFollowPlayer(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        void leaveFollowPlayer(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        void runFollowPlayer(Actor& actor, float seconds, openeden::sdk::game::ActorRequestWriter& writer);
        /// Task 0332: an escort whose destination can be reached leads the player there. False: not yet (follow).
        bool runEscort(Actor& actor, double playerX, double playerY, double actorX, double actorY,
            openeden::sdk::game::ActorRequestWriter& writer);
        void beginUse(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        void runClip(Actor& actor, float seconds, openeden::sdk::game::ActorRequestWriter& writer);
        [[nodiscard]] static bool needsWakeToTalk(const Actor& actor);
        void finishExit(Actor& actor, openeden::sdk::game::ActorRequestWriter& writer);
        [[nodiscard]] bool sitAllowed(const Actor& actor) const;

        PackageSource& mSource;
        Options mOptions;
        std::map<openeden::sdk::game::WorldId, Actor> mActors;
        FurnitureSource* mFurniture = nullptr;
        ForceGreetHook mGreetHook;
        bool mFollowPlayerPackages = false; ///< task 0310
        double mGreetFrom = kGreetApproachFrom; ///< tasks 0305 and 0306: the walk-up starts beyond this
        double mGreetStop = kGreetApproachStop; ///< ... and ends (the ask is made) within this
        std::set<openeden::sdk::game::WorldId> mFollowRefs; ///< placements that follow the player (task 0196)
        std::set<openeden::sdk::game::WorldId> mPackageFollowRefs; ///< task 0334: ... by their package
        std::set<openeden::sdk::game::WorldId> mRiderRefs; ///< placements that sit on a cart (task 0274)
        std::map<openeden::sdk::game::WorldId, openeden::sdk::game::ActorHandle> mMounts; ///< rider -> horse (0274)
        [[nodiscard]] openeden::sdk::game::ActorHandle moverOf(const Actor& actor) const;
        EndHook mEndHook;
        void fireEnd(Actor& actor);
        std::set<openeden::sdk::game::WorldId> mWaitRefs; ///< followers told to wait here (task 0239)
        /// task 0277: waiting for the player to reach the target
        std::set<openeden::sdk::game::WorldId> mTravellerRefs;
        /// task 0277: scenes' packages by placement, so a door does not lose them
        std::map<openeden::sdk::game::WorldId, const PackageRecord*> mForcedRefs;
        bool mCarryTravellers = false;
        bool mSnapMustComplete = false; ///< task 0341
        TravelMoveHook mTravelMoveHook; // task 0286
        /// A scene's package is remembered by placement and an unreachable target is looked for.
        [[nodiscard]] bool tracksScenePackages() const
        {
            return mCarryTravellers || static_cast<bool>(mTravelMoveHook);
        }
        std::uint32_t mPackagesRevision = 0; ///< task 0277: what mSource.packagesRevision() said last
        std::map<std::uint64_t, openeden::sdk::game::WorldId> mReserved; ///< slot key to the actor holding it
        std::vector<FurnitureSlot> mScratch;
        Stats mStats;
    };
}

#endif
