#include "actor.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/convert.hpp>
#include <components/physicshelpers/collisionobject.hpp>
#include <components/resource/physicsshape.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/settings/values.hpp>

#include "../mwmechanics/creaturestats.hpp"
#include "../mwworld/class.hpp"

#include "joltlayers.hpp"
#include "mtphysics.hpp"
#include "trace.h"

#include <algorithm>
#include <cmath>
#include <osg/Drawable>

namespace
{
    void computeCapsuleTransformAndHalfHeight(const osg::Vec3f& start, const osg::Vec3f& end, float radius,
        JPH::Vec3& outPos, JPH::Quat& outRot, float& outHalfHeight)
    {
        osg::Vec3f dir = end - start;
        float len = dir.length();
        if (len < 1e-3f)
        {
            dir = osg::Vec3f(0.0f, 0.0f, 1.0f);
            len = 0.01f;
        }
        else
        {
            dir /= len;
        }

        outHalfHeight = std::max(0.05f, (len - 2.0f * radius) * 0.5f);
        const osg::Vec3f center = (start + end) * 0.5f;

        outRot = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), Misc::Convert::toJolt<JPH::Vec3>(dir));
        outPos = Misc::Convert::toJolt<JPH::Vec3>(center);
    }
}

namespace MWPhysics
{

    Actor::Actor(const MWWorld::Ptr& ptr, const Resource::PhysicsShape* shape, PhysicsTaskScheduler* scheduler,
        bool canWaterWalk, DetourNavigator::CollisionShapeType collisionShapeType)
        : PtrHolder(ptr, ptr.getRefData().getPosition().asVec3())
        , mStandingOnPtr(nullptr)
        , mCanWaterWalk(canWaterWalk)
        , mWalkingOnWater(false)
        , mMeshTranslation(shape->mCollisionBox.mCenter)
        , mOriginalHalfExtents(shape->mCollisionBox.mExtents)
        , mScale({ 1.0f, 1.0f, 1.0f })
        , mStuckFrames(0)
        , mLastStuckPosition{ 0, 0, 0 }
        , mForce(0.f, 0.f, 0.f)
        , mOnGround(ptr.getClass().getCreatureStats(ptr).getFallHeight() == 0)
        , mOnSlope(false)
        , mInternalCollisionMode(true)
        , mExternalCollisionMode(true)
        , mActive(false)
        , mTaskScheduler(scheduler)
    {
        // We can not create actor without collisions - he will fall through the ground.
        // In this case we should autogenerate collision box based on mesh shape
        // (NPCs have bodyparts and use a different approach)
        if (!ptr.getClass().isNpc() && mOriginalHalfExtents.length2() == 0.f)
        {
            if (shape->mCollisionShape)
            {
                auto shapeRef = shape->mCollisionShape;
                JPH::AABox bounds = shapeRef->GetLocalBounds();
                osg::Vec3f min = osg::Vec3f(bounds.mMin.GetX(), bounds.mMin.GetY(), bounds.mMin.GetZ());
                osg::Vec3f max = osg::Vec3f(bounds.mMax.GetX(), bounds.mMax.GetY(), bounds.mMax.GetZ());

                mOriginalHalfExtents.x() = (max[0] - min[0]) / 2.f;
                mOriginalHalfExtents.y() = (max[1] - min[1]) / 2.f;
                mOriginalHalfExtents.z() = (max[2] - min[2]) / 2.f;

                mMeshTranslation = osg::Vec3f(0.f, 0.f, mOriginalHalfExtents.z());
            }

            if (mOriginalHalfExtents.length2() == 0.f)
                Log(Debug::Error) << "Error: Failed to calculate bounding box for actor \""
                                  << ptr.getCellRef().getRefId() << "\".";
        }

        float extRatio = 0.f;
        if (mOriginalHalfExtents.y() != 0.f)
            extRatio = mOriginalHalfExtents.x() / mOriginalHalfExtents.y();

        const bool symmetricExtents = std::abs(mMeshTranslation.x()) < 1e-2f && std::abs(mMeshTranslation.y()) < 1e-2f
            && extRatio >= 1.f / 1.25f && extRatio <= 1.25f;
        const bool useCompound = (collisionShapeType == DetourNavigator::CollisionShapeType::Compound);

        if (useCompound)
        {
            const float W = std::max(0.01f, std::min(mOriginalHalfExtents.x(), mOriginalHalfExtents.y()));
            const float H = std::max(0.01f, mOriginalHalfExtents.z());

            const float footRadius = std::clamp(0.18f * W, 3.0f, 4.5f);
            const float calfRadius = std::clamp(0.20f * W, 3.5f, 5.0f);
            const float thighRadius = std::clamp(0.24f * W, 4.2f, 5.8f);
            const float armRadius = std::clamp(0.15f * W, 2.8f, 4.0f);
            const float forearmRadius = std::clamp(0.13f * W, 2.4f, 3.5f);
            const float pelvisRadius = std::clamp(0.36f * W, 6.8f, 9.5f);
            const float chestRadius = std::clamp(0.42f * W, 7.8f, 10.5f);
            const float headRadius = std::clamp(0.26f * W, 5.0f, 7.0f);

            mCompoundShape = new JPH::MutableCompoundShape();

            std::size_t idx = 0;
            auto addBoneCapsule = [&](const osg::Vec3f& start, const osg::Vec3f& end, float radius) {
                JPH::Vec3 pos;
                JPH::Quat rot;
                float halfHeight = 0.05f;
                computeCapsuleTransformAndHalfHeight(start, end, radius, pos, rot, halfHeight);
                mBoneRadii[idx] = radius;
                mBoneHalfHeights[idx] = halfHeight;
                mCompoundShape->AddShape(pos, rot, new JPH::CapsuleShape(halfHeight, radius));
                ++idx;
            };

            // 0..2: Left Leg (Foot, Calf, Thigh)
            addBoneCapsule(osg::Vec3f(-0.30f * W, -0.15f * W, -H + footRadius),
                osg::Vec3f(-0.30f * W, 0.25f * W, -H + footRadius), footRadius);
            addBoneCapsule(osg::Vec3f(-0.30f * W, -0.02f * W, -H + footRadius * 2.0f),
                osg::Vec3f(-0.28f * W, 0.02f * W, -0.42f * H), calfRadius);
            addBoneCapsule(osg::Vec3f(-0.28f * W, 0.02f * W, -0.42f * H),
                osg::Vec3f(-0.20f * W, 0.0f, -0.05f * H), thighRadius);

            // 3..5: Right Leg (Foot, Calf, Thigh)
            addBoneCapsule(osg::Vec3f(0.30f * W, -0.15f * W, -H + footRadius),
                osg::Vec3f(0.30f * W, 0.25f * W, -H + footRadius), footRadius);
            addBoneCapsule(osg::Vec3f(0.30f * W, -0.02f * W, -H + footRadius * 2.0f),
                osg::Vec3f(0.28f * W, 0.02f * W, -0.42f * H), calfRadius);
            addBoneCapsule(osg::Vec3f(0.28f * W, 0.02f * W, -0.42f * H),
                osg::Vec3f(0.20f * W, 0.0f, -0.05f * H), thighRadius);

            // 6: Pelvis / Hips
            addBoneCapsule(osg::Vec3f(0.0f, 0.0f, -0.05f * H),
                osg::Vec3f(0.0f, 0.0f, 0.22f * H), pelvisRadius);

            // 7: Upper Torso / Chest
            addBoneCapsule(osg::Vec3f(0.0f, 0.0f, 0.22f * H),
                osg::Vec3f(0.0f, 0.0f, 0.65f * H), chestRadius);

            // 8: Head
            addBoneCapsule(osg::Vec3f(0.0f, 0.0f, 0.72f * H),
                osg::Vec3f(0.0f, 0.0f, 0.88f * H), headRadius);

            // 9..10: Left Arm (Upper arm + Forearm)
            addBoneCapsule(osg::Vec3f(-0.55f * W, 0.0f, 0.58f * H),
                osg::Vec3f(-0.60f * W, 0.0f, 0.25f * H), armRadius);
            addBoneCapsule(osg::Vec3f(-0.60f * W, 0.0f, 0.25f * H),
                osg::Vec3f(-0.60f * W, 0.0f, -0.05f * H), forearmRadius);

            // 11..12: Right Arm (Upper arm + Forearm)
            addBoneCapsule(osg::Vec3f(0.55f * W, 0.0f, 0.58f * H),
                osg::Vec3f(0.60f * W, 0.0f, 0.25f * H), armRadius);
            addBoneCapsule(osg::Vec3f(0.60f * W, 0.0f, 0.25f * H),
                osg::Vec3f(0.60f * W, 0.0f, -0.05f * H), forearmRadius);

            mBasePhysicsShape = mCompoundShape.GetPtr();
            mRotationallyInvariant = false;
            mCollisionShapeType = DetourNavigator::CollisionShapeType::Compound;

            // Dedicated yaw-invariant movement capsule for MovementSolver / ActorTracer so character movement and
            // slope grounding never bob or snag when skeletal hitboxes animate.
            const float moveRadius = W;
            const float moveHalfHeight = std::max(0.01f, H - moveRadius);
            const JPH::Quat capsuleRotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::DegreesToRadians(90.0f));
            mBaseMovementShape = new JPH::RotatedTranslatedShape(
                JPH::Vec3(0.0f, 0.0f, 0.0f), capsuleRotation, new JPH::CapsuleShape(moveHalfHeight, moveRadius));
            mMovementRotationallyInvariant = true;
        }
        else if (symmetricExtents || collisionShapeType == DetourNavigator::CollisionShapeType::Capsule)
        {
            switch (collisionShapeType)
            {
                case DetourNavigator::CollisionShapeType::Capsule:
                {
                    const float radius = std::max(0.01f, std::min(mOriginalHalfExtents.x(), mOriginalHalfExtents.y()));
                    const float halfHeight = std::max(0.01f, mOriginalHalfExtents.z() - radius);
                    JPH::Quat capsuleRotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::DegreesToRadians(90.0f));
                    mBasePhysicsShape = new JPH::RotatedTranslatedShape(JPH::Vec3(0.0f, 0.0f, 0.0f), capsuleRotation,
                        new JPH::CapsuleShape(halfHeight, radius));
                    mRotationallyInvariant = true;
                    break;
                }
                default:
                case DetourNavigator::CollisionShapeType::Aabb:
                    mBasePhysicsShape = new JPH::BoxShape(Misc::Convert::toJolt<JPH::Vec3>(mOriginalHalfExtents));
                    mRotationallyInvariant = true;
                    break;
                case DetourNavigator::CollisionShapeType::RotatingBox:
                    mBasePhysicsShape = new JPH::BoxShape(Misc::Convert::toJolt<JPH::Vec3>(mOriginalHalfExtents));
                    mRotationallyInvariant = false;
                    break;
                case DetourNavigator::CollisionShapeType::Cylinder:
                    JPH::Quat shapeRotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::DegreesToRadians(90.0f));
                    mBasePhysicsShape = new JPH::RotatedTranslatedShape(JPH::Vec3(0.0f, 0.0f, 0.0f), shapeRotation,
                        new JPH::CylinderShape(mOriginalHalfExtents.z(), mOriginalHalfExtents.x()));
                    mRotationallyInvariant = true;
                    break;
            }
            mCollisionShapeType = collisionShapeType;
            mBaseMovementShape = mBasePhysicsShape;
            mMovementRotationallyInvariant = mRotationallyInvariant;
        }
        else
        {
            mBasePhysicsShape = new JPH::BoxShape(Misc::Convert::toJolt<JPH::Vec3>(mOriginalHalfExtents));
            mRotationallyInvariant = false;
            mCollisionShapeType = DetourNavigator::CollisionShapeType::RotatingBox;
            mBaseMovementShape = mBasePhysicsShape;
            mMovementRotationallyInvariant = mRotationallyInvariant;
        }

        updateScaleUnsafe();

        if (!mRotationallyInvariant)
        {
            const SceneUtil::PositionAttitudeTransform* baseNode = mPtr.getRefData().getBaseNode();
            if (baseNode)
                mRotation = baseNode->getAttitude();
        }

        // Jolt specific
        // NOTE: player must always have scaled shape to prevent runtime cost of updating body/shape if scale requested
        mPhysicsShape = new JPH::ScaledShape(mBasePhysicsShape, Misc::Convert::toJolt<JPH::Vec3>(mScale));
        mMovementShape = new JPH::ScaledShape(mBaseMovementShape.GetPtr(), Misc::Convert::toJolt<JPH::Vec3>(mScale));
        mScaleUpdated = false; // We set the scale just now so cancel update flag set in updateScaleUnsafe

        JPH::BodyCreationSettings bodyCreationSettings = PhysicsSystemHelpers::makePhysicsBodySettings(mPhysicsShape,
            getScaledMeshTranslation() + mPosition, mRotation, Layers::ACTOR, JPH::EMotionType::Kinematic);
        bodyCreationSettings.mEnhancedInternalEdgeRemoval = true;

        mPhysicsBody = mTaskScheduler->createPhysicsBody(bodyCreationSettings);
        mPhysicsBody->SetUserData(reinterpret_cast<uintptr_t>(this));
        mTaskScheduler->addCollisionObject(mPhysicsBody, true);
    }

    Actor::~Actor()
    {
        mTaskScheduler->removeCollisionObject(mPhysicsBody);
        mTaskScheduler->destroyCollisionObject(mPhysicsBody);
    }

    void Actor::enableCollisionMode(bool collision)
    {
        mInternalCollisionMode = collision;
    }

    void Actor::enableCollisionBody(bool collision)
    {
        std::scoped_lock lock(mMutex);
        if (mExternalCollisionMode != collision)
        {
            mExternalCollisionMode = collision;

            // If collision is intended to be disabled on this object (i,e for a corpse)
            // then we need to update the body to a new layer to prevent other things colliding with it
            JPH::BodyInterface& bodyInterface = mTaskScheduler->getBodyInterface();
            bodyInterface.SetObjectLayer(getPhysicsBody(), mExternalCollisionMode ? Layers::ACTOR : Layers::DEBRIS);
        }
    }

    JPH::ObjectLayer Actor::getCollisionMask() const
    {
        std::scoped_lock lock(mMutex);
        JPH::ObjectLayer collisionMask = Layers::WORLD | Layers::HEIGHTMAP;
        if (mExternalCollisionMode)
            collisionMask |= Layers::ACTOR | Layers::PROJECTILE | Layers::DOOR;
        if (mCanWaterWalk)
            collisionMask |= Layers::WATER;
        return collisionMask;
    }

    void Actor::updatePosition()
    {
        std::scoped_lock lock(mMutex);
        const auto worldPosition = mPtr.getRefData().getPosition().asVec3();
        mPreviousPosition = worldPosition;
        mPosition = worldPosition;
        mSimulationPosition = worldPosition;
        mPositionOffset = osg::Vec3f();
        mStandingOnPtr = nullptr;
        mSkipSimulation = true;
    }

    void Actor::setSimulationPosition(const osg::Vec3f& position)
    {
        if (!std::exchange(mSkipSimulation, false))
            mSimulationPosition = position;
    }

    osg::Vec3f Actor::getScaledMeshTranslation() const
    {
        return mRotation * osg::componentMultiply(mMeshTranslation, mScale);
    }

    void Actor::updateCollisionObjectPosition()
    {
        std::scoped_lock lock(mMutex);
        updateCollisionObjectPositionUnsafe();
    }

    void Actor::updateCollisionObjectPositionUnsafe()
    {
        if (getPhysicsBody().IsInvalid())
            return;

        JPH::BodyInterface& bodyInterface = mTaskScheduler->getBodyInterface();

        // If scale was updated, we need to re-create a scaled shape
        // This code assumes mPhysicsShape is a ScaledShape and not equal to mBasePhysicsShape (which is fine for now)
        if (mScaleUpdated)
        {
            // Jolt handles destroying of the original shape when SetShape is called!
            mPhysicsShape = new JPH::ScaledShape(mBasePhysicsShape, Misc::Convert::toJolt<JPH::Vec3>(mScale));
            mMovementShape = new JPH::ScaledShape(mBaseMovementShape.GetPtr(), Misc::Convert::toJolt<JPH::Vec3>(mScale));
            bodyInterface.SetShape(getPhysicsBody(), mPhysicsShape, false, JPH::EActivation::DontActivate);
            mScaleUpdated = false;
        }

        syncBoneHitboxesUnsafe();

        osg::Vec3f newPosition = getScaledMeshTranslation() + mPosition;

        // NOTE: SetPositionAndRotation is thread safe to call
        bodyInterface.SetPositionAndRotation(getPhysicsBody(), Misc::Convert::toJolt<JPH::RVec3>(newPosition),
            Misc::Convert::toJolt(mRotation), JPH::EActivation::Activate);
    }

    void Actor::updateBoneSegments(const std::array<BoneSegment, sNumBoneSegments>& segments)
    {
        std::scoped_lock lock(mMutex);
        mPendingBoneSegments = segments;
        mBoneSegmentsUpdated = true;
    }

    void Actor::syncBoneHitboxes()
    {
        std::scoped_lock lock(mMutex);
        syncBoneHitboxesUnsafe();
    }

    void Actor::syncBoneHitboxesUnsafe()
    {
        if (!mBoneSegmentsUpdated || !mCompoundShape || getPhysicsBody().IsInvalid())
            return;

        const osg::Vec3f invScale(
            mScale.x() != 0.f ? 1.f / mScale.x() : 1.f,
            mScale.y() != 0.f ? 1.f / mScale.y() : 1.f,
            mScale.z() != 0.f ? 1.f / mScale.z() : 1.f);

        std::array<JPH::Vec3, sNumBoneSegments> positions;
        std::array<JPH::Quat, sNumBoneSegments> rotations;

        {
            JPH::BodyLockWrite bodyLock(mTaskScheduler->getBodyLockInterface(), getPhysicsBody());
            if (!bodyLock.Succeeded())
                return;

            for (std::size_t i = 0; i < sNumBoneSegments; ++i)
            {
                const osg::Vec3f start = osg::componentMultiply(mPendingBoneSegments[i].mStart, invScale) - mMeshTranslation;
                const osg::Vec3f end = osg::componentMultiply(mPendingBoneSegments[i].mEnd, invScale) - mMeshTranslation;

                float halfHeight = mBoneHalfHeights[i];
                computeCapsuleTransformAndHalfHeight(start, end, mBoneRadii[i], positions[i], rotations[i], halfHeight);

                if (std::abs(halfHeight - mBoneHalfHeights[i]) > 0.35f)
                {
                    mBoneHalfHeights[i] = halfHeight;
                    mCompoundShape->ModifyShape(
                        static_cast<JPH::uint>(i), positions[i], rotations[i], new JPH::CapsuleShape(halfHeight, mBoneRadii[i]));
                }
            }

            mCompoundShape->ModifyShapes(0, static_cast<JPH::uint>(sNumBoneSegments), positions.data(), rotations.data());
        }

        mTaskScheduler->getBodyInterface().NotifyShapeChanged(
            getPhysicsBody(), JPH::Vec3::sZero(), false, JPH::EActivation::DontActivate);
        mBoneSegmentsUpdated = false;
    }

    osg::Vec3f Actor::getCollisionObjectPosition() const
    {
        std::scoped_lock lock(mMutex);
        return getScaledMeshTranslation() + mPosition;
    }

    bool Actor::setPosition(const osg::Vec3f& position)
    {
        std::scoped_lock lock(mMutex);
        const bool worldPositionChanged = mPositionOffset.length2() != 0;
        applyOffsetChange();
        if (worldPositionChanged || mSkipSimulation)
            return true;
        mPreviousPosition = mPosition;
        mPosition = position;
        return mPreviousPosition != mPosition;
    }

    void Actor::adjustPosition(const osg::Vec3f& offset)
    {
        std::scoped_lock lock(mMutex);
        mPositionOffset += offset;
    }

    osg::Vec3f Actor::applyOffsetChange()
    {
        if (mPositionOffset.length2() != 0)
        {
            mPosition += mPositionOffset;
            mPreviousPosition += mPositionOffset;
            mSimulationPosition += mPositionOffset;
            mPositionOffset = osg::Vec3f();
        }
        return mPosition;
    }

    void Actor::setRotation(osg::Quat quat)
    {
        std::scoped_lock lock(mMutex);
        mRotation = quat;
    }

    bool Actor::isRotationallyInvariant() const
    {
        return mRotationallyInvariant;
    }

    void Actor::updateScale()
    {
        std::scoped_lock lock(mMutex);
        updateScaleUnsafe();
    }

    void Actor::updateScaleUnsafe()
    {
        auto prevScale = mScale;
        float scale = mPtr.getCellRef().getScale();
        osg::Vec3f scaleVec(scale, scale, scale);

        mPtr.getClass().adjustScale(mPtr, scaleVec, false);
        mScale = scaleVec;
        mHalfExtents = osg::componentMultiply(mOriginalHalfExtents, scaleVec);

        scaleVec = osg::Vec3f(scale, scale, scale);
        mPtr.getClass().adjustScale(mPtr, scaleVec, true);
        mRenderingHalfExtents = osg::componentMultiply(mOriginalHalfExtents, scaleVec);
        mScaleUpdated = mScale != prevScale;
    }

    osg::Vec3f Actor::getHalfExtents() const
    {
        return mHalfExtents;
    }

    osg::Vec3f Actor::getOriginalHalfExtents() const
    {
        return mOriginalHalfExtents;
    }

    osg::Vec3f Actor::getRenderingHalfExtents() const
    {
        return mRenderingHalfExtents;
    }

    void Actor::setInertialForce(const osg::Vec3f& force)
    {
        mForce = force;
    }

    void Actor::setOnGround(bool grounded)
    {
        mOnGround = grounded;
    }

    void Actor::setOnSlope(bool slope)
    {
        mOnSlope = slope;
    }

    bool Actor::isWalkingOnWater() const
    {
        return mWalkingOnWater;
    }

    void Actor::setWalkingOnWater(bool walkingOnWater)
    {
        mWalkingOnWater = walkingOnWater;
    }

    void Actor::setCanWaterWalk(bool waterWalk)
    {
        std::scoped_lock lock(mMutex);
        if (waterWalk != mCanWaterWalk)
        {
            mCanWaterWalk = waterWalk;
        }
    }

    MWWorld::Ptr Actor::getStandingOnPtr() const
    {
        std::scoped_lock lock(mMutex);
        return mStandingOnPtr;
    }

    void Actor::setStandingOnPtr(const MWWorld::Ptr& ptr)
    {
        std::scoped_lock lock(mMutex);
        mStandingOnPtr = ptr;
    }

    bool Actor::canMoveToWaterSurface(float waterlevel, const JPH::PhysicsSystem* physicsSystem) const
    {
        const float halfZ = getHalfExtents().z();
        const osg::Vec3f actorPosition = getPosition();
        const osg::Vec3f startingPosition(actorPosition.x(), actorPosition.y(), actorPosition.z() + halfZ);
        const osg::Vec3f destinationPosition(actorPosition.x(), actorPosition.y(), waterlevel + halfZ);
        MWPhysics::ActorTracer tracer;
        tracer.doTrace(getPhysicsBody(), startingPosition, destinationPosition, physicsSystem, getCollisionMask());
        return (tracer.mFraction >= 1.0f);
    }

}
