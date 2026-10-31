#ifndef OPENMW_MWPHYSICS_JOLTLISTENERS_H
#define OPENMW_MWPHYSICS_JOLTLISTENERS_H

#include <atomic>

#include <components/debug/debuglog.hpp>
#include <components/misc/convert.hpp>

#include "ptrholder.hpp"

namespace MWPhysics
{
    // Note: this is called from a job so whatever you do here needs to be thread safe.
    /// Told about contacts involving an actor or a projectile. Called on physics threads with bodies locked, so it must
    /// not call back into Jolt or the game; OpenEden's extension host queues them for the main thread (task 0017).
    class ContactObserver
    {
    public:
        virtual ~ContactObserver() = default;
        virtual void contactAdded(const JPH::BodyID& body1, const JPH::BodyID& body2, JPH::RVec3Arg point,
            JPH::Vec3Arg normal, bool projectileHit)
            = 0;
        /// Jolt does not say what kind of bodies these are at this point, so it is called for every contact.
        virtual void contactRemoved(const JPH::BodyID& body1, const JPH::BodyID& body2) = 0;
    };

    class JoltContactListener : public JPH::ContactListener
    {
    public:
        JoltContactListener() {}

        void setObserver(ContactObserver* observer) { mObserver.store(observer, std::memory_order_release); }

        // Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
        virtual JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2,
            JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override
        {
            bool canCollide = true;
            PtrHolder* ptr;

            ptr = Misc::Convert::toPointerFromUserData<PtrHolder>(inBody1.GetUserData());
            if (ptr)
                canCollide = ptr->onContactValidate(inBody2);

            if (canCollide)
            {
                ptr = Misc::Convert::toPointerFromUserData<PtrHolder>(inBody2.GetUserData());
                if (ptr)
                    canCollide = ptr->onContactValidate(inBody1);
            }

            if (canCollide)
            {
                return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
            }

            return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
        }

        // NOTE: In the OnContactAdded callback all bodies are locked already
        virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
            const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
        {
            ContactObserver* const observer = mObserver.load(std::memory_order_acquire);
            PtrHolder* ptr1 = Misc::Convert::toPointerFromUserData<PtrHolder>(inBody1.GetUserData());
            PtrHolder* ptr2 = Misc::Convert::toPointerFromUserData<PtrHolder>(inBody2.GetUserData());
            const bool wasActive1 = observer != nullptr && isActiveProjectile(ptr1);
            const bool wasActive2 = observer != nullptr && isActiveProjectile(ptr2);

            if (ptr1)
                ptr1->onContactAdded(inBody2, inManifold, ioSettings);

            if (ptr2)
                ptr2->onContactAdded(inBody1, inManifold, ioSettings);

            if (observer != nullptr && (isActorOrProjectile(ptr1) || isActorOrProjectile(ptr2)))
            {
                // mBaseOffset is only body 1's centre of mass
                const JPH::RVec3 point = inManifold.mRelativeContactPointsOn1.empty()
                    ? inManifold.mBaseOffset
                    : inManifold.GetWorldSpaceContactPointOn1(0);
                const bool hit1 = wasActive1 && !isActiveProjectile(ptr1);
                const bool hit2 = wasActive2 && !isActiveProjectile(ptr2);
                // A hit names the projectile first, whichever way round Jolt ordered the bodies
                if (hit2 && !hit1)
                    observer->contactAdded(inBody2.GetID(), inBody1.GetID(), point, inManifold.mWorldSpaceNormal, true);
                else
                    observer->contactAdded(
                        inBody1.GetID(), inBody2.GetID(), point, inManifold.mWorldSpaceNormal, hit1);
            }
        }

        // NOTE: Jolt allows no body access in OnContactRemoved, only their IDs
        virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
        {
            if (ContactObserver* const observer = mObserver.load(std::memory_order_acquire))
                observer->contactRemoved(inSubShapePair.GetBody1ID(), inSubShapePair.GetBody2ID());
        }

    private:
        // In physicssystem.cpp, which knows those classes
        static bool isActiveProjectile(const PtrHolder* ptr);
        static bool isActorOrProjectile(const PtrHolder* ptr);

        std::atomic<ContactObserver*> mObserver{ nullptr };
    };
}

#endif
