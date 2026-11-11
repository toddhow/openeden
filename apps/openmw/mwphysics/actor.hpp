#ifndef OPENMW_MWPHYSICS_ACTOR_H
#define OPENMW_MWPHYSICS_ACTOR_H

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "ptrholder.hpp"

#include <components/detournavigator/collisionshapetype.hpp>
#include <components/inertialmotion/locomotionstate.hpp>

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

namespace Resource
{
    struct PhysicsShape;
}

namespace JPH
{
    class Body;
}

namespace MWPhysics
{
    class PhysicsTaskScheduler;

    class Actor final : public PtrHolder
    {
    public:
        Actor(const MWWorld::Ptr& ptr, const Resource::PhysicsShape* shape, PhysicsTaskScheduler* scheduler,
            bool canWaterWalk, DetourNavigator::CollisionShapeType collisionShapeType);
        ~Actor() override;

        Actor(const Actor&) = delete;

        Actor& operator=(const Actor&) = delete;

        /**
         * Sets the collisionMode for this actor. If disabled, the actor can fly and clip geometry.
         */
        void enableCollisionMode(bool collision);

        bool getCollisionMode() const { return mInternalCollisionMode; }

        /**
         * Enables or disables the *external* collision body. If disabled, other actors will not collide with this
         * actor.
         */
        void enableCollisionBody(bool collision);

        void updateScale();
        void setRotation(vsg::dquat quat);

        /**
         * Return true if the collision shape looks the same no matter how its Z rotated.
         */
        bool isRotationallyInvariant() const;

        /**
         * Used by the physics simulation to store the simulation result. Used in conjunction with mWorldPosition
         * to account for e.g. scripted movements
         */
        void setSimulationPosition(const vsg::vec3& position);

        void updateCollisionObjectPosition();

        /**
         * Returns the half extents of the collision body (scaled according to collision scale)
         */
        vsg::vec3 getHalfExtents() const;

        /**
         * Returns the half extents of the collision body (not scaled)
         */
        vsg::vec3 getOriginalHalfExtents() const;

        /**
         * Returns the position of the collision body
         * @note The collision shape's origin is in its center, so the position returned can be described as center of
         * the actor collision box in world space.
         */
        vsg::vec3 getCollisionObjectPosition() const;

        /**
         * Store the current position into mPreviousPosition, then move to this position.
         * Returns true if the new position is different.
         */
        bool setPosition(const vsg::vec3& position);

        // force set actor position to be as in Ptr::RefData
        void updatePosition();

        // register a position offset that will be applied during simulation.
        void adjustPosition(const vsg::vec3& offset);

        // apply position offset. Can't be called during simulation
        vsg::vec3 applyOffsetChange();

        /**
         * Returns the half extents of the collision body (scaled according to rendering scale)
         * @note The reason we need this extra method is because of an inconsistency in MW - NPC race scales aren't
         * applied to the collision shape, most likely to make environment collision testing easier. However in some
         * cases (swimming level) we want the actual scale.
         */
        vsg::vec3 getRenderingHalfExtents() const;

        /**
         * Sets the current amount of inertial force (incl. gravity) affecting this physic actor
         */
        void setInertialForce(const vsg::vec3& force);

        /**
         * Gets the current amount of inertial force (incl. gravity) affecting this physic actor
         */
        const vsg::vec3& getInertialForce() const { return mForce; }

        void setOnGround(bool grounded);

        bool getOnGround() const { return mOnGround; }

        void setOnSlope(bool slope);

        bool getOnSlope() const { return mOnSlope; }

        /// Sets whether this actor should be able to collide with the water surface
        void setCanWaterWalk(bool waterWalk);

        /// Sets whether this actor has been walking on the water surface in the last frame
        void setWalkingOnWater(bool walkingOnWater);
        bool isWalkingOnWater() const;

        MWWorld::Ptr getStandingOnPtr() const;
        void setStandingOnPtr(const MWWorld::Ptr& ptr);

        unsigned int getStuckFrames() const { return mStuckFrames; }
        void setStuckFrames(unsigned int frames) { mStuckFrames = frames; }

        const vsg::vec3& getLastStuckPosition() const { return mLastStuckPosition; }
        void setLastStuckPosition(vsg::vec3 position) { mLastStuckPosition = position; }

        bool canMoveToWaterSurface(float waterlevel, const JPH::PhysicsSystem* physicsSystem) const;

        bool isScaleIdentity() const { return mScale == vsg::vec3(1.0f, 1.0f, 1.0f); }

        bool isActive() const { return mActive; }

        void setActive(bool value) { mActive = value; }

        JPH::ObjectLayer getCollisionMask() const;

        DetourNavigator::CollisionShapeType getCollisionShapeType() const { return mCollisionShapeType; }

        struct BoneSegment
        {
            vsg::vec3 mStart;
            vsg::vec3 mEnd;
        };
        static constexpr std::size_t sNumBoneSegments = 13;

        const JPH::Shape* getMovementShape() const { return mMovementShape.GetPtr(); }
        bool isMovementRotationallyInvariant() const { return mMovementRotationallyInvariant; }
        bool hasCompoundHitboxes() const { return mCompoundShape != nullptr; }

        void updateBoneSegments(const std::array<BoneSegment, sNumBoneSegments>& segments);
        void syncBoneHitboxes();

        InertialMotion::LocomotionState& getLocomotionState() noexcept { return mLocomotionState; }
        const InertialMotion::LocomotionState& getLocomotionState() const noexcept { return mLocomotionState; }

        /// Task 0108: the movement clip's root speed this frame over its mean (InertialMotion::strideShape), set by
        /// the character controller on the main thread before the physics step and applied to the smoothed ground
        /// speed there. One unless [Game] inertial stride sync is on and a movement cycle plays.
        void setStrideShape(float shape) noexcept { mStrideShape.store(shape, std::memory_order_relaxed); }
        float getStrideShape() const noexcept { return mStrideShape.load(std::memory_order_relaxed); }

    private:
        MWWorld::Ptr mStandingOnPtr;

        vsg::vec3 getScaledMeshTranslation() const;

        bool mCanWaterWalk;
        bool mWalkingOnWater;

        bool mRotationallyInvariant;
        bool mMovementRotationallyInvariant = true;

        DetourNavigator::CollisionShapeType mCollisionShapeType;

        JPH::Shape* mPhysicsShape;
        JPH::Shape* mBasePhysicsShape;
        JPH::Ref<JPH::Shape> mBaseMovementShape;
        JPH::Ref<JPH::Shape> mMovementShape;
        JPH::Ref<JPH::MutableCompoundShape> mCompoundShape;

        std::array<float, sNumBoneSegments> mBoneRadii{};
        std::array<float, sNumBoneSegments> mBoneHalfHeights{};
        std::array<BoneSegment, sNumBoneSegments> mPendingBoneSegments{};
        bool mBoneSegmentsUpdated = false;

        vsg::vec3 mSimulationPosition;
        vsg::vec3 mMeshTranslation;
        vsg::vec3 mOriginalHalfExtents;
        vsg::vec3 mHalfExtents;
        vsg::vec3 mRenderingHalfExtents;
        vsg::dquat mRotation;

        vsg::vec3 mScale;
        vsg::vec3 mPositionOffset;
        bool mSkipSimulation = true;
        bool mScaleUpdated = false;
        mutable std::mutex mMutex;

        unsigned int mStuckFrames;
        vsg::vec3 mLastStuckPosition;

        vsg::vec3 mForce;
        bool mOnGround;
        bool mOnSlope;
        bool mInternalCollisionMode;
        bool mExternalCollisionMode;
        bool mActive;

        InertialMotion::LocomotionState mLocomotionState;
        std::atomic<float> mStrideShape{ 1.f };

        PhysicsTaskScheduler* mTaskScheduler;

        inline void updateScaleUnsafe();

        inline void updateCollisionObjectPositionUnsafe();

        void syncBoneHitboxesUnsafe();
    };

}

#endif
