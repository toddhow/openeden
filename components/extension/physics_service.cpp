#include "physics_service.hpp"

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <variant>

namespace openeden::extension
{
    namespace
    {
        using namespace openeden::sdk;

        constexpr float kUnitTolerance = 1e-3f;
        constexpr float kPi = std::numbers::pi_v<float>;

        // This file is the one boundary between the SDK's meters and the world's length unit.
        JPH::Vec3 toWorldVec(PhysicsVec3 v, float worldUnitsPerMeter)
        {
            return JPH::Vec3(v.x, v.y, v.z) * worldUnitsPerMeter;
        }

        JPH::RVec3 toWorldPos(PhysicsVec3 v, float worldUnitsPerMeter)
        {
            return JPH::RVec3(v.x, v.y, v.z) * static_cast<JPH::Real>(worldUnitsPerMeter);
        }

        JPH::Quat toJolt(PhysicsQuat q)
        {
            return { q.x, q.y, q.z, q.w };
        }

        PhysicsVec3 fromWorldVec(JPH::Vec3Arg v, float worldUnitsPerMeter)
        {
            const JPH::Vec3 m = v / worldUnitsPerMeter;
            return { m.GetX(), m.GetY(), m.GetZ() };
        }

        PhysicsVec3 fromWorldPos(JPH::RVec3Arg v, float worldUnitsPerMeter)
        {
            const JPH::RVec3 m = v / static_cast<JPH::Real>(worldUnitsPerMeter);
            return { static_cast<float>(m.GetX()), static_cast<float>(m.GetY()), static_cast<float>(m.GetZ()) };
        }

        // For values with no length in them: normals and angular velocities.
        PhysicsVec3 fromJolt(JPH::Vec3Arg v)
        {
            return { v.GetX(), v.GetY(), v.GetZ() };
        }

        PhysicsQuat fromJolt(JPH::QuatArg q)
        {
            return { q.GetX(), q.GetY(), q.GetZ(), q.GetW() };
        }

        bool isFinite(PhysicsVec3 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool isUnit(PhysicsQuat q)
        {
            const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            return std::abs(len - 1.0f) <= kUnitTolerance;
        }

        PhysicsVec3 scaled(PhysicsVec3 v, float factor)
        {
            return { v.x * factor, v.y * factor, v.z * factor };
        }

        bool isPositiveFinite(float value)
        {
            return value > 0.0f && std::isfinite(value);
        }

        // A handle an extension holds may hold anything, so range check it before Jolt's constructor asserts on it.
        std::optional<JPH::BodyID> toJoltId(BodyId body)
        {
            if (!body.isValid() || body.index > JPH::BodyID::cMaxBodyIndex
                || body.generation > JPH::BodyID::cMaxSequenceNumber)
            {
                return std::nullopt;
            }
            return JPH::BodyID(body.index, static_cast<JPH::uint8>(body.generation));
        }

        // Object layers in a world taking extension queries are bit patterns (MWPhysics::Layers).
        class QueryMaskLayerFilter final : public JPH::ObjectLayerFilter
        {
        public:
            explicit QueryMaskLayerFilter(QueryMask mask)
                : mMask(mask)
            {
            }

            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer) const override
            {
                return mMask == kQueryMaskAll || (mMask & static_cast<QueryMask>(layer)) != 0;
            }

        private:
            QueryMask mMask;
        };

        // Ignoring no body: BodyID's default value is invalid and matches no body in the world.
        JPH::IgnoreSingleBodyFilter makeIgnoreFilter(BodyId ignore)
        {
            return JPH::IgnoreSingleBodyFilter(toJoltId(ignore).value_or(JPH::BodyID()));
        }
    } // namespace

    PhysicsServiceImpl::PhysicsServiceImpl(JPH::PhysicsSystem& world, float worldUnitsPerMeter)
        : mWorld(&world)
        , mWorldUnitsPerMeter(worldUnitsPerMeter)
    {
    }

    BodyId PhysicsServiceImpl::toBodyId(const JPH::BodyID& id)
    {
        if (id.IsInvalid())
        {
            return BodyId{};
        }
        return BodyId{ id.GetIndex(), id.GetSequenceNumber() };
    }

    BodyId PhysicsServiceImpl::bodyOfEntity(EntityId entity) const
    {
        return mEntityBodies != nullptr ? mEntityBodies->bodyOfEntity(entity) : BodyId{};
    }

    EntityId PhysicsServiceImpl::entityOfBody(BodyId body) const
    {
        return mEntityBodies != nullptr ? mEntityBodies->entityOfBody(body) : EntityId{};
    }

    void PhysicsServiceImpl::publishQueryMask(std::string_view name, QueryMask mask)
    {
        mQueryMasks.insert_or_assign(std::string(name), mask);
    }

    bool PhysicsServiceImpl::isBodyValid(BodyId id) const
    {
        const std::optional<JPH::BodyID> joltId = toJoltId(id);
        return joltId && mWorld->GetBodyInterface().IsAdded(*joltId);
    }

    std::optional<MotionType> PhysicsServiceImpl::getMotionType(BodyId body) const
    {
        const std::optional<JPH::BodyID> joltId = toJoltId(body);
        if (!joltId)
        {
            return std::nullopt;
        }
        const JPH::BodyLockRead lock(mWorld->GetBodyLockInterface(), *joltId);
        if (!lock.Succeeded() || !lock.GetBody().IsInBroadPhase())
        {
            return std::nullopt;
        }
        switch (lock.GetBody().GetMotionType())
        {
            case JPH::EMotionType::Static:
                return MotionType::Static;
            case JPH::EMotionType::Kinematic:
                return MotionType::Kinematic;
            case JPH::EMotionType::Dynamic:
                return MotionType::Dynamic;
        }
        return std::nullopt;
    }

    bool PhysicsServiceImpl::readBody(BodyId body, BodyState& out) const
    {
        const std::optional<JPH::BodyID> joltId = toJoltId(body);
        if (!joltId)
        {
            return false;
        }
        const JPH::BodyLockRead lock(mWorld->GetBodyLockInterface(), *joltId);
        if (!lock.Succeeded() || !lock.GetBody().IsInBroadPhase())
        {
            return false;
        }
        const JPH::Body& joltBody = lock.GetBody();
        out.positionM = fromWorldPos(joltBody.GetPosition(), mWorldUnitsPerMeter);
        out.rotation = fromJolt(joltBody.GetRotation());
        out.linearVelocityMps = fromWorldVec(joltBody.GetLinearVelocity(), mWorldUnitsPerMeter);
        out.angularVelocityRadps = fromJolt(joltBody.GetAngularVelocity());
        out.awake = joltBody.IsActive();
        return true;
    }

    std::size_t PhysicsServiceImpl::readBodies(std::span<const BodyId> bodies, std::span<BodyState> out) const
    {
        const std::size_t count = std::min(bodies.size(), out.size());
        std::size_t read = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (readBody(bodies[i], out[i]))
            {
                ++read;
            }
        }
        return read;
    }

    QueryMask PhysicsServiceImpl::queryMask(std::string_view name) const
    {
        const auto it = mQueryMasks.find(name);
        return it != mQueryMasks.end() ? it->second : kQueryMaskNone;
    }

    std::optional<RayHit> PhysicsServiceImpl::castRay(
        PhysicsVec3 originM, PhysicsVec3 directionTimesLengthM, BodyId ignore, QueryMask mask) const
    {
        // Checked in world units: a huge finite value in meters can overflow when scaled
        if (!isFinite(scaled(originM, mWorldUnitsPerMeter))
            || !isFinite(scaled(directionTimesLengthM, mWorldUnitsPerMeter)))
        {
            return std::nullopt;
        }
        const JPH::RRayCast ray{ toWorldPos(originM, mWorldUnitsPerMeter),
            toWorldVec(directionTimesLengthM, mWorldUnitsPerMeter) };
        JPH::RayCastResult hit;
        const QueryMaskLayerFilter layerFilter(mask);
        const JPH::IgnoreSingleBodyFilter ignoreFilter = makeIgnoreFilter(ignore);
        if (!mWorld->GetNarrowPhaseQuery().CastRay(ray, hit, {}, layerFilter, ignoreFilter))
        {
            return std::nullopt;
        }

        const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
        const JPH::BodyLockRead lock(mWorld->GetBodyLockInterface(), hit.mBodyID);
        if (!lock.Succeeded())
        {
            return std::nullopt;
        }

        RayHit result;
        result.body = toBodyId(hit.mBodyID);
        result.pointM = fromWorldPos(point, mWorldUnitsPerMeter);
        result.normal = fromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
        result.fraction = hit.mFraction;
        return result;
    }

    std::optional<RayHit> PhysicsServiceImpl::castShape(const PhysicsShape& shape, PhysicsQuat rotation,
        PhysicsVec3 originM, PhysicsVec3 directionTimesLengthM, BodyId ignore, QueryMask mask) const
    {
        if (!isFinite(scaled(originM, mWorldUnitsPerMeter))
            || !isFinite(scaled(directionTimesLengthM, mWorldUnitsPerMeter)) || !isUnit(rotation))
        {
            return std::nullopt;
        }

        const JPH::Quat worldRotation = toJolt(rotation).Normalized();
        const float scale = mWorldUnitsPerMeter;
        const JPH::Vec3 direction = toWorldVec(directionTimesLengthM, scale);
        const JPH::RVec3 baseOffset = toWorldPos(originM, scale);

        const auto cast = [&](const JPH::Shape& joltShape, JPH::QuatArg shapeRotation) -> std::optional<RayHit> {
            const JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
                &joltShape, JPH::Vec3::sOne(), JPH::RMat44::sRotationTranslation(shapeRotation, baseOffset), direction);

            JPH::ShapeCastSettings settings;
            settings.mReturnDeepestPoint = true;
            // A shape starting inside a body counts as a hit, whatever way it is moving.
            settings.mBackFaceModeConvex = JPH::EBackFaceMode::CollideWithBackFaces;
            settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;

            // Contact points come back relative to baseOffset, which keeps them precise far from the world origin.
            JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
            const QueryMaskLayerFilter layerFilter(mask);
            const JPH::IgnoreSingleBodyFilter ignoreFilter = makeIgnoreFilter(ignore);
            mWorld->GetNarrowPhaseQuery().CastShape(
                shapeCast, settings, baseOffset, collector, {}, layerFilter, ignoreFilter);
            if (!collector.HadHit())
            {
                return std::nullopt;
            }

            const JPH::ShapeCastResult& hit = collector.mHit;
            RayHit result;
            result.body = toBodyId(hit.mBodyID2);
            result.pointM = fromWorldPos(baseOffset + hit.mContactPointOn2, scale);
            // The penetration axis points from the cast shape into the body it hit.
            result.normal = fromJolt((-hit.mPenetrationAxis).NormalizedOr(JPH::Vec3::sZero()));
            result.fraction = hit.mFraction;
            return result;
        };

        // The shapes live on the stack, so a cast does not allocate; SetEmbedded allows that for a reference-counted
        // object. Their dimensions are checked first because Jolt asserts on non-positive ones.
        if (const auto* box = std::get_if<BoxShape>(&shape))
        {
            const PhysicsVec3 h = box->halfExtentsM;
            if (!isPositiveFinite(h.x * scale) || !isPositiveFinite(h.y * scale) || !isPositiveFinite(h.z * scale))
            {
                return std::nullopt;
            }
            const float smallest = std::min({ h.x, h.y, h.z });
            JPH::BoxShape joltBox(toWorldVec(h, scale), std::min(JPH::cDefaultConvexRadius, 0.5f * smallest) * scale);
            joltBox.SetEmbedded();
            return cast(joltBox, worldRotation);
        }
        if (const auto* sphere = std::get_if<SphereShape>(&shape))
        {
            if (!isPositiveFinite(sphere->radiusM * scale))
            {
                return std::nullopt;
            }
            JPH::SphereShape joltSphere(sphere->radiusM * scale);
            joltSphere.SetEmbedded();
            return cast(joltSphere, worldRotation);
        }

        const auto& capsule = std::get<CapsuleShape>(shape);
        if (!isPositiveFinite(capsule.halfHeightM * scale) || !isPositiveFinite(capsule.radiusM * scale))
        {
            return std::nullopt;
        }
        JPH::CapsuleShape joltCapsule(capsule.halfHeightM * scale, capsule.radiusM * scale);
        joltCapsule.SetEmbedded();
        // Jolt's capsule runs along its local Y axis, the SDK's along Z.
        const JPH::Quat yToZ = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * kPi);
        return cast(joltCapsule, worldRotation * yToZ);
    }
} // namespace openeden::extension
