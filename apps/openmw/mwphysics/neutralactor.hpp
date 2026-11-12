#ifndef OPENMW_MWPHYSICS_NEUTRALACTOR_H
#define OPENMW_MWPHYSICS_NEUTRALACTOR_H

// Task 0133 (decision 0017, stage N5-3): the physics body of an actor of a neutral (non-Morrowind) session, and the rule
// that stands it on the floor. Pure maths and shape building, no PhysicsSystem and no Ptr, so the tests reach it.
//
// A neutral actor is NOT an MWPhysics::Actor: that class is driven by CreatureStats and MovementSolver, which a neutral
// object has not (Class::isActor() stays false, decision 0017 section 4). Its body is an MWPhysics::Object whose shape
// is a capsule; PhysicsSystem::addNeutralActor owns it and PhysicsSystem::updateNeutralActors moves it. Both run on the
// main thread only.

#include <cstdint>

#include <vsg/core/ref_ptr.h>

namespace Resource
{
    struct PhysicsShape;
}

namespace MWPhysics::NeutralActor
{
    /// Height in game units of a standard adult at heightScale 1. A named estimate (Skyrim's character controller is
    /// about 1.8 m tall at 69.99 units a metre); RECALLED-VERIFY, to be replaced by a measurement of the skeleton.
    inline constexpr float kStandingHeight = 128.f;
    /// Radius of the standing capsule (about 0.3 m). Reduced for a very short actor so the capsule stays a capsule.
    inline constexpr float kRadius = 22.f;
    /// Bounds of the capsule's total height. A height scale of 0.25 to 4 (the pack's clamp) is inside them.
    inline constexpr float kMinHeight = 48.f;
    inline constexpr float kMaxHeight = 640.f;

    struct Capsule
    {
        float mRadius = kRadius;
        float mHalfCylinder = 0.f; ///< half the length of the straight part
        float mCentreZ = 0.f; ///< height of the capsule's centre above the feet
    };

    /// The capsule of an actor of the given height scale (1 = standard). A non-finite or non-positive scale means 1.
    [[nodiscard]] Capsule capsuleFor(float heightScale);

    /// The capsule as a physics shape in the actor's own frame (feet at the origin, upright). Null if Jolt refuses it.
    /// Its visual-collision type is Camera, which every navigator site reads as "not map geometry", so an actor's body
    /// never becomes part of the navigation mesh.
    [[nodiscard]] vsg::ref_ptr<Resource::PhysicsShape> makeShape(const Capsule& capsule);

    // ---- standing on the floor ------------------------------------------------------------------------------------

    /// Floor higher than the feet by up to this much is stepped up onto.
    inline constexpr float kStepUp = 24.f;
    /// How far below the feet the floor is looked for.
    inline constexpr float kProbeDepth = 2048.f;
    /// Feet this close above the floor count as standing on it; a placement that is nearly on the floor does not fall.
    inline constexpr float kSettleDistance = 48.f;
    /// Frames to wait for a floor that is not there yet (the cell's objects are added over several frames).
    inline constexpr std::uint32_t kMaxWaitFrames = 180;
    /// Fastest a falling actor goes, units a second.
    inline constexpr float kTerminalSpeed = 4000.f;

    enum class State : std::uint8_t
    {
        Waiting, ///< no floor found yet; tries again next frame, up to kMaxWaitFrames
        Falling,
        Grounded, ///< on the floor; not looked at again
        Lost, ///< no floor was ever found; stays where the placement put it
    };

    struct Fall
    {
        float mZ = 0.f; ///< height of the feet
        float mVelocity = 0.f; ///< vertical speed, units a second, down is negative
        State mState = State::Waiting;
        std::uint32_t mFrames = 0; ///< frames evaluated while not yet Grounded
    };

    /// The result of a ray cast from kStepUp above the feet down kProbeDepth + kStepUp: the highest surface hit.
    struct FloorProbe
    {
        bool mHit = false;
        float mZ = 0.f;
    };

    /// Advances one frame of `dt` seconds. A Grounded or Lost actor is returned unchanged, as is any state for a dt that
    /// is not positive and finite. Rules: a floor that is within kSettleDistance below the feet, or above them up to
    /// kStepUp, puts the actor on it at once; a floor farther below makes it fall under gravity (the engine's, in
    /// units a second squared) until the next position would be at or below the floor, where it lands; no floor makes
    /// it wait, and after kMaxWaitFrames it is Lost.
    [[nodiscard]] Fall stepFall(const Fall& fall, float dt, const FloorProbe& floor);
}

#endif
