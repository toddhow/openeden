#ifndef OPENMW_MWPHYSICS_RAYCASTING_H
#define OPENMW_MWPHYSICS_RAYCASTING_H

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec3.h>

#include "../mwworld/ptr.hpp"

#include "joltlayers.hpp"

namespace MWPhysics
{
    class RayCastingResult
    {
    public:
        bool mHit;
        vsg::vec3 mHitPos;
        vsg::vec3 mHitNormal;
        MWWorld::Ptr mHitObject;
    };

    class RayCastingInterface
    {
    public:
        virtual ~RayCastingInterface() = default;

        /// @param ignore Optional, a list of Ptr to ignore in the list of results. targets are actors to filter for,
        /// ignoring all other actors.
        virtual RayCastingResult castRay(const vsg::vec3& from, const vsg::vec3& to,
            const std::vector<MWWorld::ConstPtr>& ignore = {}, const std::vector<MWWorld::Ptr>& targets = {},
            int mask = CollisionMask_Default, int group = 0xff) const = 0;

        RayCastingResult castRay(const vsg::vec3& from, const vsg::vec3& to, int mask) const
        {
            return castRay(from, to, {}, {}, mask);
        }

        virtual RayCastingResult castSphere(const vsg::vec3& from, const vsg::vec3& to, float radius,
            int mask = CollisionMask_Default, int group = 0xff) const = 0;

        /// Return true if actor1 can see actor2.
        virtual bool getLineOfSight(const MWWorld::ConstPtr& actor1, const MWWorld::ConstPtr& actor2) const = 0;
    };
}

#endif
