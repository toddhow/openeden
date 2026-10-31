#ifndef OPENMW_MWPHYSICS_PROJECTILE_H
#define OPENMW_MWPHYSICS_PROJECTILE_H

#include <atomic>
#include <memory>
#include <mutex>

#include "ptrholder.hpp"

namespace osg
{
    class Vec3f;
}

namespace JPH
{
    class Body;
}

namespace MWPhysics
{
    class PhysicsTaskScheduler;
    class PhysicsSystem;

    class Projectile final : public PtrHolder
    {
    public:
        Projectile(const MWWorld::Ptr& caster, const vsg::vec3& position, float radius, PhysicsTaskScheduler* scheduler,
            PhysicsSystem* physicssystem);
        ~Projectile() override;

        bool isActive() const { return mActive.load(std::memory_order_acquire); }

        MWWorld::Ptr getTarget() const;

        MWWorld::Ptr getCaster() const;
        void setCaster(const MWWorld::Ptr& caster);
        const JPH::BodyID getCasterCollisionObject() const { return mCasterColObj; }

        void setHitWater() { mHitWater = true; }

        bool getHitWater() const { return mHitWater.load(std::memory_order_acquire); }

        void hit(const JPH::BodyID target, vsg::vec3 pos, vsg::vec3 normal);

        void setValidTargets(const std::vector<MWWorld::Ptr>& targets);
        bool isValidTarget(const JPH::BodyID target) const;

        vsg::vec3 getHitPosition() const { return mHitPosition; }

        void onContactAdded(const JPH::Body& withBody, const JPH::ContactManifold& inManifold,
            JPH::ContactSettings& ioSettings) override;
        bool onContactValidate(const JPH::Body& withBody) override;
        void setVelocity(vsg::vec3 velocity) override;

        vsg::vec3 getSimulationPosition() const override;

    private:
        std::atomic<bool> mHitWater;
        std::atomic<bool> mActive;
        MWWorld::Ptr mCaster;
        JPH::BodyID mCasterColObj;
        JPH::BodyID mHitTarget;
        vsg::vec3 mHitPosition;
        vsg::vec3 mHitNormal;

        std::vector<JPH::BodyID> mValidTargets;

        mutable std::mutex mMutex;

        PhysicsSystem* mPhysics;
        PhysicsTaskScheduler* mTaskScheduler;

        Projectile(const Projectile&);
        Projectile& operator=(const Projectile&);
    };

}

#endif
