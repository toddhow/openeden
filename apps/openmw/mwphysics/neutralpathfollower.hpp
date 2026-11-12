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
    /// Walking and running speeds of a go-to request, game units a second. Named estimates (Skyrim's walk is about
    /// 1.7 m/s, its run about 4 m/s at 69.99 units a metre): RECALLED-VERIFY, to be measured from the animation clips.
    inline constexpr float kWalkSpeed = 120.f;
    inline constexpr float kRunSpeed = 280.f;
    /// A waypoint counts as reached when the actor is this close to it on the ground plane.
    inline constexpr float kWaypointRadius = 12.f;
    /// The goal counts as reached when the actor is within this on the ground plane and kArrivalHeight in height, or
    /// when every waypoint has been reached.
    inline constexpr float kArrivalRadius = 24.f;
    inline constexpr float kArrivalHeight = 64.f;
    /// Progress is judged over this many seconds: moving less than kStallProgress of speed * that time is a stall.
    inline constexpr float kStallWindow = 0.75f;
    inline constexpr float kStallProgress = 0.2f;
    /// A stall asks for a new path at most this many times for one goal; the next stall ends the goal as Blocked.
    inline constexpr int kMaxRepaths = 3;
    /// A navigator that is not ready (no navmesh yet, no polygon at the start) is asked again after this many seconds,
    /// at most this many times in all, then the goal ends as NoPath.
    inline constexpr float kNotReadyInterval = 0.5f;
    inline constexpr int kMaxNotReadyAttempts = 40;
    /// Waypoints the path vector is reserved for.
    inline constexpr std::size_t kReservedWaypoints = 64;

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
        [[nodiscard]] virtual FindStatus find(const vsg::vec3& from, const vsg::vec3& to, std::vector<vsg::vec3>& out) const
            = 0;
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
    };

    /// The navmesh agent for an actor of the capsule's radius: its horizontal half extent is the larger of the radius
    /// and the session's neutral pathfinding agent, so a path never runs closer to a wall than the capsule can go (a
    /// narrower actor reuses the session's navmesh, a wider one gets one of its own); the height is the session agent's
    /// (one navmesh per radius, not per individual height).
    [[nodiscard]] vsg::vec3 agentHalfExtents(float capsuleRadius, const vsg::vec3& sessionAgent);
}

#endif
