#include "neutralsight.hpp"

#include <cmath>

#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <components/misc/convert.hpp>

#include "joltfilters.hpp"

namespace MWPhysics::NeutralSight
{
    namespace
    {
        bool finite(const vsg::vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // Skips the bodies of a span (the neutral actors' own capsules). Holds the span by reference: no allocation.
        class IgnoreListFilter final : public JPH::BodyFilter
        {
        public:
            explicit IgnoreListFilter(std::span<const JPH::BodyID> ignore)
                : mIgnore(ignore)
            {
            }
            bool ShouldCollide(const JPH::BodyID& id) const override
            {
                for (const JPH::BodyID& ignored : mIgnore)
                    if (ignored == id)
                        return false;
                return true;
            }

        private:
            std::span<const JPH::BodyID> mIgnore;
        };
    }

    bool blocked(const JPH::PhysicsSystem& system, const vsg::vec3& from, const vsg::vec3& to, int layerMask,
        std::span<const JPH::BodyID> ignore)
    {
        if (!finite(from) || !finite(to))
            return false;
        const vsg::vec3 delta(to.x - from.x, to.y - from.y, to.z - from.z);
        const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (!(length > kEndTrim))
            return false;
        const float scale = (length - kEndTrim) / length;
        const JPH::RRayCast ray(Misc::Convert::toJolt<JPH::RVec3>(from),
            JPH::Vec3(delta.x * scale, delta.y * scale, delta.z * scale));
        JPH::BroadPhaseLayerFilter allBroadPhaseLayers;
        MaskedObjectLayerFilter objectLayers(layerMask);
        const IgnoreListFilter bodies(ignore);
        JPH::RayCastResult hit;
        return system.GetNarrowPhaseQuery().CastRay(ray, hit, allBroadPhaseLayers, objectLayers, bodies);
    }
}
