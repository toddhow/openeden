#ifndef OPENMW_MWPHYSICS_PTRHOLDER_H
#define OPENMW_MWPHYSICS_PTRHOLDER_H

#include <memory>
#include <mutex>
#include <utility>

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec3.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "../mwworld/ptr.hpp"

namespace MWPhysics
{
    class PtrHolder
    {
    public:
        explicit PtrHolder(const MWWorld::Ptr& ptr, const vsg::vec3& position)
            : mPtr(ptr)
            , mPosition(position)
            , mPreviousPosition(position)
        {
        }

        virtual ~PtrHolder() = default;

        void updatePtr(const MWWorld::Ptr& updated) { mPtr = updated; }

        MWWorld::Ptr getPtr() const { return mPtr; }

        const JPH::BodyID getPhysicsBody() const
        {
            assert(mPhysicsBody != nullptr);
            if (mPhysicsBody == nullptr)
            {
                return JPH::BodyID();
            }
            return mPhysicsBody->GetID();
        }

        virtual void setVelocity(vsg::vec3 velocity) { mVelocity = velocity; }

        vsg::vec3 velocity() { return std::exchange(mVelocity, vsg::vec3()); }

        // Assumed static by default, override if not
        virtual vsg::vec3 getSimulationPosition() const { return vsg::vec3(mPosition); }

        void setPosition(const vsg::vec3& position)
        {
            mPreviousPosition = mPosition;
            mPosition = vsg::dvec3(position);
        }

        vsg::dvec3 getPosition() const { return mPosition; }

        vsg::dvec3 getPreviousPosition() const { return mPreviousPosition; }

        virtual void onContactAdded(
            const JPH::Body& withBody, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
        {
        }

        virtual bool onContactValidate(const JPH::Body& withBody) { return true; }

    protected:
        MWWorld::Ptr mPtr;
        JPH::Body* mPhysicsBody; // NOTE: memory is managed by Jolt!
        vsg::vec3 mVelocity;
        vsg::dvec3 mPosition;
        vsg::dvec3 mPreviousPosition;
    };
}

#endif
