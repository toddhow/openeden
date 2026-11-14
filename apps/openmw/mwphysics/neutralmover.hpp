#ifndef OPENMW_MWPHYSICS_NEUTRALMOVER_H
#define OPENMW_MWPHYSICS_NEUTRALMOVER_H

// Task 0135 (decision 0017, stage S2): the movement of a neutral actor's capsule body. A request (a desired velocity on
// the ground) is integrated against the world with swept capsules: step up, move, slide along what blocks, snap down to
// the floor. The stepping (`step`) is pure and talks to the world only through the `Sweeper` interface, so the tests
// give it a world of boxes; `JoltSweeper` is the game's implementation over a JPH::PhysicsSystem.
//
// Threading: every function here runs on the main thread (PhysicsSystem::updateNeutralActors, called from
// World::doPhysics before stepSimulation). None is a callback on a physics thread. `step` and `JoltSweeper::sweep`
// allocate nothing.

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec3.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace JPH
{
    class PhysicsSystem;
}

namespace MWPhysics::NeutralMover
{
    /// A walker climbs up to this much in one move (the same distance NeutralActor uses for the floor probe).
    inline constexpr float kStepUp = 24.f;
    /// The floor is followed down this far in one move; a drop beyond it stops the move (no walking off an edge).
    inline constexpr float kStepDown = 24.f;
    /// Distance kept between the capsule and what stopped it, so the next sweep does not start in contact.
    inline constexpr float kSkin = 0.25f;
    /// Slide passes per move: the first stops at a wall, the second slides along it, the third handles a corner.
    inline constexpr int kMaxSlidePasses = 3;
    /// A surface whose normal points up less than this (about 50 degrees from level) is not floor.
    inline constexpr float kMinFloorNormalZ = 0.64f;
    /// A request faster than this (units a second) is cut to it; Skyrim's sprint is about 700.
    inline constexpr float kMaxSpeed = 1500.f;
    /// The centre of the capsule must have floor within this much above to kStepDown below the feet; the normal of
    /// that floor, not the one the capsule's rim touched (a step's edge), decides whether the ground is walkable.
    inline constexpr float kProbeLift = 4.f;
    /// A steep normal under the capsule's centre only refuses the pass when that ground is lower than the resting capsule
    /// by more than this: a bevel or a seam in a floor mesh is steep for a few units and is not a slope (task 0135
    /// stage S4: a floor seam in the Bannered Mare stopped every NPC). A capsule of radius 22 resting on the rim of a
    /// 50 degree slope has the centre ground about 12 units lower, on a 38 degree slope about 6.
    inline constexpr float kBevelSlack = 3.f;
    /// Task 0157: the ground under the centre is the highest surface found by five rays: at the centre and this many
    /// units to each side (stair and floor meshes have thin gaps between planks that a single ray falls through).
    inline constexpr float kProbeSpread = 4.f;
    /// ... and its normal is the most level one among the surfaces hit within this height of the highest.
    inline constexpr float kProbeNormalBand = 4.f;
    /// Task 0157: stairs. A capsule that climbs rests its rim on the next step's nosing, and the ground under its centre
    /// is the lower tread, which can be more than kStepDown below the resting feet. When the pass raised the feet by
    /// more than kRimMinClimb, the ground under the centre may be this much deeper still (a rim that carries the
    /// capsule up is support). A capsule that is level or going down keeps the plain rule (a ledge is a ledge).
    inline constexpr float kRimReach = 24.f;
    inline constexpr float kRimMinClimb = 0.1f;
    /// Task 0159: a trench is bridged when ground lies this far ahead and behind the capsule along the motion (the capsule's
    /// rims span a gap narrower than its diameter).
    inline constexpr float kBridgeReach = 12.f;
    /// ... and that ground is at most this far below the resting capsule (its own floor, not a raised object it hangs from).
    inline constexpr float kBridgeSupport = 6.f;
    /// Horizontal distances under this are not moves.
    inline constexpr float kMinMove = 1e-4f;

    /// The result of sweeping the actor's capsule: `mFraction` of the way along the motion is free.
    struct Sweep
    {
        bool mHit = false;
        float mFraction = 1.f;
        vsg::vec3 mNormal = vsg::vec3(0.f, 0.f, 1.f); ///< of the surface hit, pointing out of it
    };

    /// The world as the mover sees it. `from` is the position of the capsule's feet; `delta` the whole motion.
    class Sweeper
    {
    public:
        virtual ~Sweeper() = default;
        [[nodiscard]] virtual Sweep sweep(const vsg::vec3& from, const vsg::vec3& delta) const = 0;
        /// A ray from `from` straight down for `depth`: the surface under a point, with its true normal.
        [[nodiscard]] virtual Sweep probeDown(const vsg::vec3& from, float depth) const = 0;
    };

    struct Request
    {
        float mVelX = 0.f; ///< units a second, game axes
        float mVelY = 0.f;
        [[nodiscard]] bool isMoving() const;
    };

    struct Result
    {
        vsg::vec3 mFeet;
        bool mMoved = false;
        bool mBlocked = false; ///< a wall, a ledge too high, a slope too steep or no floor stopped some of the move
        bool mEdge = false; ///< a pass was refused because the capsule's centre had no floor within kStepDown
    };

    /// Heading of a velocity in ESM::Position::rot[2] terms: 0 faces +Y, positive turns towards +X. Zero velocity -> 0.
    [[nodiscard]] float headingOf(float velX, float velY);

    /// One frame of `dt` seconds from `feet`. A request that is not moving, not finite, or a dt that is not positive
    /// and finite changes nothing. The move is: raise by up to kStepUp (less under a low ceiling), sweep along the
    /// request, back off kSkin, drop onto the floor within the raise + kStepDown; if the surface hit was a wall, slide
    /// the rest of the motion along it (up to kMaxSlidePasses). A landing with no floor or a steep one refuses the
    /// pass. Deterministic: no clock, no random number, no container.
    [[nodiscard]] Result step(const vsg::vec3& feet, const Request& request, float dt, const Sweeper& world);

    /// The game's Sweeper: sweeps the actor's capsule shape through a Jolt system against the layers in `layerMask`,
    /// ignoring the actor's own body. Main thread; allocation-free.
    class JoltSweeper final : public Sweeper
    {
    public:
        /// `shape` is the capsule in the body's own frame (feet at the origin, upright), `self` the actor's body.
        JoltSweeper(const JPH::PhysicsSystem& system, JPH::RefConst<JPH::Shape> shape, JPH::BodyID self, int layerMask);
        [[nodiscard]] Sweep sweep(const vsg::vec3& from, const vsg::vec3& delta) const override;
        [[nodiscard]] Sweep probeDown(const vsg::vec3& from, float depth) const override;

    private:
        const JPH::PhysicsSystem& mSystem;
        JPH::RefConst<JPH::Shape> mShape;
        JPH::BodyID mSelf;
        int mLayerMask;
    };
}

#endif
