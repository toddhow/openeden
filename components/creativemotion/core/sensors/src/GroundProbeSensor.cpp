#include "opas/sensors/GroundProbeSensor.hpp"

#include <algorithm>
#include <cmath>

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

    } // namespace

    GroundProbeSensor::GroundProbeSensor(std::vector<std::size_t> footBodies, Config config)
        : footBodies_(std::move(footBodies))
        , config_(config)
        , readings_(footBodies_.size())
    {
        for (FootGroundReading& r : readings_)
        {
            r.clearanceM = config_.probeDistanceM;
        }
    }

    void GroundProbeSensor::update(const physics::PhysicsWorld& world, const motor::Ragdoll& ragdoll)
    {
        const math::Vec3 unitDir = math::normalized(config_.rayDirection);
        const math::Vec3 rayVec = unitDir * config_.probeDistanceM;

        for (std::size_t i = 0; i < footBodies_.size(); ++i)
        {
            const std::size_t bodyIdx = footBodies_[i];
            FootGroundReading& r = readings_[i];
            r = FootGroundReading{};
            r.clearanceM = config_.probeDistanceM;

            if (bodyIdx >= ragdoll.bodies.size())
            {
                continue;
            }

            const physics::BodyId bodyId = ragdoll.bodies[bodyIdx].id;
            physics::BodyState state;
            if (!world.readBody(bodyId, state))
            {
                continue;
            }

            const physics::Vec3 origin = state.positionM;
            const physics::Vec3 dir = toPhysics(rayVec);
            const auto hit = world.castRay(origin, dir, bodyId);

            if (hit.has_value())
            {
                r.hit = true;
                r.clearanceM = static_cast<double>(hit->fraction) * config_.probeDistanceM;
                r.contactPointM = toMath(hit->pointM);
                r.surfaceNormal = math::normalized(toMath(hit->normal));
                const double cosAngle = std::clamp(r.surfaceNormal.z, -1.0, 1.0);
                r.slopeRad = std::acos(cosAngle);
            }
        }
    }

    const FootGroundReading& GroundProbeSensor::reading(std::size_t footIndex) const
    {
        static const FootGroundReading kEmpty{};
        if (footIndex < readings_.size())
        {
            return readings_[footIndex];
        }
        return kEmpty;
    }

    bool GroundProbeSensor::allGrounded() const noexcept
    {
        if (readings_.empty())
        {
            return false;
        }
        for (const FootGroundReading& r : readings_)
        {
            if (!r.hit)
            {
                return false;
            }
        }
        return true;
    }

    bool GroundProbeSensor::anyGrounded() const noexcept
    {
        for (const FootGroundReading& r : readings_)
        {
            if (r.hit)
            {
                return true;
            }
        }
        return false;
    }

    double GroundProbeSensor::minClearanceM() const noexcept
    {
        if (readings_.empty())
        {
            return 0.0;
        }
        double minC = readings_[0].clearanceM;
        for (std::size_t i = 1; i < readings_.size(); ++i)
        {
            minC = std::min(minC, readings_[i].clearanceM);
        }
        return minC;
    }

    double GroundProbeSensor::averageSlopeRad() const noexcept
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (const FootGroundReading& r : readings_)
        {
            if (r.hit)
            {
                sum += r.slopeRad;
                ++count;
            }
        }
        if (count == 0)
        {
            return 0.0;
        }
        return sum / static_cast<double>(count);
    }

} // namespace opas::sensors
