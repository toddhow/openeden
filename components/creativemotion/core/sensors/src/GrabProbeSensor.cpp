#include "opas/sensors/GrabProbeSensor.hpp"

#include "opas/math/Quat.hpp"

#include <algorithm>

namespace opas::sensors
{
    namespace
    {

        physics::Vec3 toPhysics(math::Vec3 v)
        {
            return { static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) };
        }

        math::Vec3 toMath(physics::Vec3 v)
        {
            return { v.x, v.y, v.z };
        }

        math::Quat toMath(physics::Quat q)
        {
            return { q.x, q.y, q.z, q.w };
        }

    } // namespace

    GrabProbeSensor::GrabProbeSensor(std::vector<HandProbe> probes)
        : probes_(std::move(probes))
        , targets_(probes_.size(), std::nullopt)
    {
    }

    void GrabProbeSensor::update(const physics::PhysicsWorld& world, const motor::Ragdoll& ragdoll)
    {
        for (std::size_t i = 0; i < probes_.size(); ++i)
        {
            targets_[i] = std::nullopt;
            const HandProbe& probe = probes_[i];

            if (probe.bodyIndex >= ragdoll.bodies.size())
            {
                continue;
            }

            const physics::BodyId bodyId = ragdoll.bodies[probe.bodyIndex].id;
            physics::BodyState state;
            if (!world.readBody(bodyId, state))
            {
                continue;
            }

            const math::Vec3 unitDir = math::normalized(probe.localRayDirection);
            const math::Quat handRot = toMath(state.rotation);
            const math::Vec3 worldDir = math::rotate(handRot, unitDir);
            const math::Vec3 rayVec = worldDir * probe.maxReachM;

            const auto hit = world.castRay(state.positionM, toPhysics(rayVec), bodyId);
            if (hit.has_value())
            {
                GrabTarget target;
                target.distanceM = static_cast<double>(hit->fraction) * probe.maxReachM;
                target.positionM = toMath(hit->pointM);
                target.surfaceNormal = math::normalized(toMath(hit->normal));
                target.handBodyIndex = probe.bodyIndex;
                target.handPart = probe.part;
                targets_[i] = target;
            }
        }
    }

    bool GrabProbeSensor::hasTarget() const noexcept
    {
        for (const auto& t : targets_)
        {
            if (t.has_value())
            {
                return true;
            }
        }
        return false;
    }

    std::optional<GrabTarget> GrabProbeSensor::bestTarget() const noexcept
    {
        std::optional<GrabTarget> best = std::nullopt;
        for (const auto& t : targets_)
        {
            if (t.has_value())
            {
                if (!best.has_value() || t->distanceM < best->distanceM)
                {
                    best = t;
                }
            }
        }
        return best;
    }

    std::optional<GrabTarget> GrabProbeSensor::targetForHand(arbiter::BodyPart handPart) const noexcept
    {
        std::optional<GrabTarget> best = std::nullopt;
        for (const auto& t : targets_)
        {
            if (t.has_value() && t->handPart == handPart)
            {
                if (!best.has_value() || t->distanceM < best->distanceM)
                {
                    best = t;
                }
            }
        }
        return best;
    }

} // namespace opas::sensors
