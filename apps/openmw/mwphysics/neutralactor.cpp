#include "neutralactor.hpp"

#include <algorithm>
#include <cmath>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/constants.hpp>
#include <components/resource/physicsshape.hpp>

namespace MWPhysics::NeutralActor
{
    namespace
    {
        // The engine's gravity in units a second squared: the constant movementsolver.cpp uses for actors.
        constexpr float kGravity = Constants::GravityConst * Constants::UnitsPerMeter;
    }

    Capsule capsuleFor(float heightScale)
    {
        const float scale = std::isfinite(heightScale) && heightScale > 0.f ? heightScale : 1.f;
        const float height = std::clamp(kStandingHeight * scale, kMinHeight, kMaxHeight);
        Capsule capsule;
        // A capsule needs a straight part: the radius is at most a quarter of the height (a short actor is slimmer).
        capsule.mRadius = std::min(kRadius, height * 0.25f);
        capsule.mHalfCylinder = height * 0.5f - capsule.mRadius;
        capsule.mCentreZ = height * 0.5f;
        return capsule;
    }

    vsg::ref_ptr<Resource::PhysicsShape> makeShape(const Capsule& capsule)
    {
        if (!(capsule.mRadius > 0.f) || !(capsule.mHalfCylinder > 0.f) || !std::isfinite(capsule.mCentreZ))
            return {};
        JPH::CapsuleShapeSettings capsuleSettings(capsule.mHalfCylinder, capsule.mRadius);
        const JPH::ShapeSettings::ShapeResult capsuleResult = capsuleSettings.Create();
        if (capsuleResult.HasError())
        {
            Log(Debug::Error) << "Neutral actor capsule error: " << capsuleResult.GetError();
            return {};
        }
        // Jolt's capsule stands along Y; turn it upright (as actor.cpp does) and lift it so the feet are at the origin.
        const JPH::Quat upright = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::DegreesToRadians(90.0f));
        JPH::RotatedTranslatedShapeSettings placed(JPH::Vec3(0.f, 0.f, capsule.mCentreZ), upright, capsuleResult.Get());
        const JPH::ShapeSettings::ShapeResult placedResult = placed.Create();
        if (placedResult.HasError())
        {
            Log(Debug::Error) << "Neutral actor capsule placement error: " << placedResult.GetError();
            return {};
        }

        vsg::ref_ptr<Resource::PhysicsShape> result(new Resource::PhysicsShape);
        result->mCollisionShape = placedResult.Get();
        result->mCollisionBox.mExtents = vsg::vec3(capsule.mRadius, capsule.mRadius, capsule.mCentreZ);
        result->mCollisionBox.mCenter = vsg::vec3(0.f, 0.f, capsule.mCentreZ);
        // Every navigator site reads a type other than None as "not map geometry" (see the header).
        result->mVisualCollisionType = Resource::VisualCollisionType::Camera;
        return result;
    }

    Fall stepFall(const Fall& fall, float dt, const FloorProbe& floor)
    {
        if (fall.mState == State::Grounded || fall.mState == State::Lost)
            return fall;
        if (!(dt > 0.f) || !std::isfinite(dt))
            return fall;

        Fall next = fall;
        ++next.mFrames;
        if (!floor.mHit || !std::isfinite(floor.mZ))
        {
            // A fall that loses its floor, or a wait that has lasted long enough, ends here.
            next.mState
                = (fall.mState == State::Falling || next.mFrames >= kMaxWaitFrames) ? State::Lost : State::Waiting;
            return next;
        }

        const float gap = fall.mZ - floor.mZ; // above zero: the feet are above the floor
        if (gap <= kSettleDistance)
        {
            next.mZ = floor.mZ;
            next.mVelocity = 0.f;
            next.mState = State::Grounded;
            return next;
        }

        next.mVelocity = std::max(fall.mVelocity - kGravity * dt, -kTerminalSpeed);
        const float z = fall.mZ + next.mVelocity * dt;
        if (z <= floor.mZ)
        {
            next.mZ = floor.mZ;
            next.mVelocity = 0.f;
            next.mState = State::Grounded;
        }
        else
        {
            next.mZ = z;
            next.mState = State::Falling;
        }
        return next;
    }
}
