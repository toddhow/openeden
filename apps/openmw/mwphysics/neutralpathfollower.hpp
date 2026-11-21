#ifndef OPENMW_MWPHYSICS_NEUTRALPATHFOLLOWER_H
#define OPENMW_MWPHYSICS_NEUTRALPATHFOLLOWER_H

// Task 0135 (decision 0017, stage S4): a neutral actor's "go to P" request. The follower asks a path finder (the
// navigator, in the game) for waypoints from the actor's feet to the goal and turns them into the velocity request the
// stage S2 mover executes, frame by frame. It knows no Jolt, navigator, Ptr or ESM type: a test gives it a fake path
// finder and positions. It decides three endings: the actor arrived, there is no path, or the actor stopped making
// progress along its path (after asking for new paths a bounded number of times).
//
// Threading: main thread only (PhysicsSystem::updateNeutralActors). Allocation: the path vector is reserved when a
// goal is set and a path is asked for (a path request may allocate if the navigator returns more waypoints than
// the reserve); `step` itself allocates nothing.
//
// Determinism: only the arguments decide anything (no clock, no random number).

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec3.h>

#include <cstdint>
#include <vector>

namespace MWPhysics::NeutralPath
{
    /// Walking and running speeds of a go-to request, game units a second: the speeds the Skyrim SE walk and run clips
    /// cover by their own root motion, so the cycles play at their own rate (task 0148, measured on the installed
    /// `Skyrim - Animations.bsa`: MT_WalkForward 93.448 units in 1.13333 s = 82.45 units/s, MT_RunForward 222.219 units
    /// in 0.633333 s = 350.87 units/s, male and female; `HavokGaitRealData`). Task 0135's first estimates were 120 and
    /// 280. HavokPack::Gait::Params defaults to the same two numbers (a test pins them equal).
    inline constexpr float kWalkSpeed = 82.45f;
    inline constexpr float kRunSpeed = 350.87f;
    /// A waypoint counts as reached when the actor is this close to it on the ground plane.
    inline constexpr float kWaypointRadius = 12.f;
    /// ... and within this in height (the mover's step height, NeutralMover::kStepUp: task 0159).
    inline constexpr float kWaypointHeight = 24.f;
    /// The goal counts as reached when the actor is within this on the ground plane and kArrivalHeight in height, or
    /// when every waypoint has been reached.
    inline constexpr float kArrivalRadius = 24.f;
    inline constexpr float kArrivalHeight = 64.f;
    /// Progress is judged over this many seconds: moving less than kStallProgress of speed * that time is a stall.
    inline constexpr float kStallWindow = 0.75f;
    inline constexpr float kStallProgress = 0.2f;
    /// A stall asks for a new path at most this many times for one goal; the next stall ends the goal as Blocked.
    inline constexpr int kMaxRepaths = 3;
    /// Task 0336: a partial path counts as progress when its end is this much nearer to the goal (on the ground plane)
    /// than the actor is, and at most this many partial paths are walked for one goal. ESTIMATES.
    inline constexpr float kLongPathProgress = 200.f;
    inline constexpr int kMaxPartialRounds = 64;
    /// A navigator that is not ready (no navmesh yet, no polygon at the start) is asked again after this many seconds,
    /// at most this many times in all, then the goal ends as NoPath.
    inline constexpr float kNotReadyInterval = 0.5f;
    inline constexpr int kMaxNotReadyAttempts = 40;
    /// Waypoints the path vector is reserved for.
    inline constexpr std::size_t kReservedWaypoints = 64;

    /// Task 0332: a path the navigator gave leads down from a raised rim (a hearth's edge, a step the mover climbed)
    /// by at most this much; the mover is allowed to step down that far while the next waypoint is lower.
    inline constexpr float kMaxPathDrop = 64.f;
    /// How far down the mover may step while following `path` (the waypoints of the goal, `reached` of them reached)
    /// from `feetZ`: `baseStepDown`, or, when one of the next two waypoints is lower than that, the drop to it plus a
    /// little (at most kMaxPathDrop). Pure.
    [[nodiscard]] float pathStepDown(
        float feetZ, const std::vector<vsg::vec3>& path, std::size_t reached, float baseStepDown);

    /// Task 0336: the steepest ground (the z of its normal) a walker on a navmesh path stands on: recast's voxels pass
    /// lumpy rock faces whose analytic slope is a little steeper than the mover's 0.64 (found: a rock of normal z 0.628
    /// stopped Riverwood guards on a path the navmesh drew over the rock). While a path vouches for the
    /// ground the limit is this or the base, whichever is lower. ESTIMATE (about 70 degrees; rock faces of normal z
    /// 0.63, 0.47 and 0.43 each stopped a walk the navmesh had drawn).
    inline constexpr float kPathMinFloorNormalZ = 0.35f;
    [[nodiscard]] float pathMinFloorNormalZ(float baseMinNormalZ);

    enum class FindStatus : std::uint8_t
    {
        Success, ///< a path that reaches the goal
        Partial, ///< a path that stops short of the goal
        NotReady, ///< the navigator cannot answer yet (navmesh or polygon missing): worth asking again
        NoPath, ///< no path, for good
    };

    /// The navigator as the follower sees it. Appends waypoints (ground points, game units) to `out`, which is empty
    /// on entry; the first waypoint need not be the start.
    class PathFinder
    {
    public:
        virtual ~PathFinder() = default;
        [[nodiscard]] virtual FindStatus find(
            const vsg::vec3& from, const vsg::vec3& to, std::vector<vsg::vec3>& out) const = 0;
    };

    enum class Outcome : std::uint8_t
    {
        None, ///< the goal goes on (or there is none)
        Arrived,
        NoPath,
        Blocked,
    };

    struct StepResult
    {
        bool mSetVelocity = false; ///< false: leave the actor's request as it is (no goal, or an unusable frame time)
        float mVelX = 0.f;
        float mVelY = 0.f;
        Outcome mOutcome = Outcome::None; ///< when not None the goal has ended and the velocity is (0, 0)
    };

    class Follower
    {
    public:
        /// Starts a goal, replacing any earlier one without a result. A non-finite target or speed is refused (false,
        /// no goal).
        bool setGoal(const vsg::vec3& target, float speed);
        /// Task 0336: with long paths a PARTIAL path that gets at least kLongPathProgress nearer to the goal is walked
        /// to its end and the path is asked for again there (the goal lies beyond the navmesh built round the player:
        /// it grows as the walk goes on); without them a partial path is no way to the goal, as before. Set before or
        /// after `setGoal`; it stays until changed.
        void setLongPaths(bool on) { mLongPaths = on; }
        /// Drops the goal without a result.
        void cancel();
        [[nodiscard]] bool active() const { return mState != State::Idle; }
        /// Waypoints of the current path and how many were reached (for traces and tests).
        [[nodiscard]] const std::vector<vsg::vec3>& path() const { return mPath; }
        [[nodiscard]] std::size_t reached() const { return mIndex; }
        [[nodiscard]] int repaths() const { return mRepaths; }

        /// One frame. `feet` is where the actor stands now. Idle -> nothing to set.
        [[nodiscard]] StepResult step(const vsg::vec3& feet, float dt, const PathFinder& finder);

    private:
        enum class State : std::uint8_t
        {
            Idle,
            NeedPath,
            Following,
        };

        StepResult finish(Outcome outcome);
        StepResult follow(const vsg::vec3& feet, float dt);

        State mState = State::Idle;
        vsg::vec3 mTarget;
        float mSpeed = 0.f;
        std::vector<vsg::vec3> mPath;
        std::size_t mIndex = 0;
        float mRetryWait = 0.f;
        int mNotReadyAttempts = 0;
        int mRepaths = 0;
        vsg::vec3 mStallFrom;
        float mStallTime = 0.f;
        bool mLongPaths = false;
        bool mPartial = false; ///< the path being followed stops short of the goal
        int mPartialRounds = 0;
    };

    /// The navmesh agent for an actor of the capsule's radius: its horizontal half extent is the larger of the radius
    /// and the session's neutral pathfinding agent, so a path never runs closer to a wall than the capsule can go (a
    /// narrower actor reuses the session's navmesh, a wider one gets one of its own); the height is the session agent's
    /// (one navmesh per radius, not per individual height).
    [[nodiscard]] vsg::vec3 agentHalfExtents(float capsuleRadius, const vsg::vec3& sessionAgent);

    /// Task 0280: a capsule as wide as the dragon's (NeutralMover::kLargeCreatureRadius and more) goes by the
    /// navmesh of
    /// an agent of this radius, game units. Every agent size has a navmesh of its own, built tile by tile on the worker
    /// threads, and a radius 150 agent needed over 1,500 tile jobs, minutes of work, before its first path was
    /// complete; the cylinder of radius 32 (the horse's and the giant's) is the widest the creature rows already use,
    /// and the mover's sweeps keep the wider body off what that navmesh let it near. ESTIMATE. Narrower capsules keep
    /// their own agent, as before.
    inline constexpr float kMaxNavigationRadius = 32.f;
    /// The radius of the navmesh agent for a capsule of `capsuleRadius`: kMaxNavigationRadius for a large creature,
    /// else the radius itself (also one that is not finite or not positive, which `agentHalfExtents` ignores).
    [[nodiscard]] float navigationRadius(float capsuleRadius);
}

#endif
