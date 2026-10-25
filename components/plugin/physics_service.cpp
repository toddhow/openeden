#include "physics_service.hpp"
#include "collision_layer_registry.hpp"

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace openeden::plugin
{
    namespace
    {
        using namespace openeden::sdk;

        constexpr std::uint32_t kTempAllocatorBytes = 10U * 1024U * 1024U;
        constexpr std::uint32_t kPairsPerBody = 4;
        constexpr std::uint32_t kMinPairs = 1024;
        constexpr float kAxisTolerance = 1e-3f;
        constexpr JPH::uint kJointPositionSteps = 10;
        constexpr std::uint32_t kMaxJointPositionSteps = 255;
        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr std::uint32_t kMaxSubStepsPerStep = 16;
        constexpr std::uint32_t kMaxSubGroups = 65536;
        constexpr std::uint32_t kScalingJobKinds = 8;
        constexpr std::uint32_t kSingleJobs = 11;
        constexpr std::uint32_t kJoltMaxConcurrency = 32;

        void ensureJoltRuntimeInitialized()
        {
            static std::mutex sRuntimeMutex;
            static std::unique_ptr<JPH::Factory> sFactory;
            const std::scoped_lock lock(sRuntimeMutex);
            if (JPH::Factory::sInstance == nullptr)
            {
                JPH::RegisterDefaultAllocator();
                sFactory = std::make_unique<JPH::Factory>();
                JPH::Factory::sInstance = sFactory.get();
                JPH::RegisterTypes();
            }
        }

        std::uint32_t usableThreads(std::uint32_t workerThreads, std::uint32_t subStepsPerStep)
        {
            const std::uint32_t safeSubSteps = std::clamp(subStepsPerStep, 1u, kMaxSubStepsPerStep);
            const std::uint32_t jobsPerSubStep
                = static_cast<std::uint32_t>(JPH::cMaxPhysicsJobs) / safeSubSteps;
            const std::uint32_t fit = (jobsPerSubStep - kSingleJobs) / kScalingJobKinds;
            return std::max(1U, std::min({ workerThreads, kJoltMaxConcurrency, fit }));
        }

        std::unique_ptr<JPH::JobSystem> makeJobSystem(
            std::uint32_t workerThreads, std::uint32_t subStepsPerStep)
        {
            const std::uint32_t threads = usableThreads(workerThreads, subStepsPerStep);
            if (threads <= 1)
            {
                return std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
            }
            return std::make_unique<JPH::JobSystemThreadPool>(
                JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(threads) - 1);
        }

        namespace Layers
        {
            constexpr JPH::ObjectLayer kNonMoving = 0;
            constexpr JPH::ObjectLayer kMoving = 1;
        } // namespace Layers

        namespace BroadPhaseLayers
        {
            constexpr JPH::BroadPhaseLayer kNonMoving(0);
            constexpr JPH::BroadPhaseLayer kMoving(1);
            constexpr JPH::uint kCount = 2;
        } // namespace BroadPhaseLayers

        class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface
        {
        public:
            [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override
            {
                return BroadPhaseLayers::kCount;
            }

            [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return layer == Layers::kNonMoving ? BroadPhaseLayers::kNonMoving
                                                   : BroadPhaseLayers::kMoving;
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return layer == BroadPhaseLayers::kNonMoving ? "NonMoving" : "Moving";
            }
#endif
        };

        class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            [[nodiscard]] bool ShouldCollide(
                JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
            {
                return layer == Layers::kMoving || broadPhaseLayer == BroadPhaseLayers::kMoving;
            }
        };

        class ObjectPairFilter final : public JPH::ObjectLayerPairFilter
        {
        public:
            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
            {
                return a == Layers::kMoving || b == Layers::kMoving;
            }
        };

        JPH::Vec3 toJolt(PhysicsVec3 v)
        {
            return { v.x, v.y, v.z };
        }

        JPH::RVec3 toJoltR(PhysicsVec3 v)
        {
            return { v.x, v.y, v.z };
        }

        JPH::Quat toJolt(PhysicsQuat q)
        {
            return { q.x, q.y, q.z, q.w };
        }

        PhysicsVec3 fromJolt(JPH::Vec3Arg v)
        {
            return { v.GetX(), v.GetY(), v.GetZ() };
        }

        PhysicsVec3 fromJoltR(JPH::RVec3Arg v)
        {
            return { static_cast<float>(v.GetX()), static_cast<float>(v.GetY()),
                static_cast<float>(v.GetZ()) };
        }

        PhysicsQuat fromJolt(JPH::QuatArg q)
        {
            return { q.GetX(), q.GetY(), q.GetZ(), q.GetW() };
        }

        float length(PhysicsVec3 v)
        {
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }

        float dot(PhysicsVec3 a, PhysicsVec3 b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        bool isUnit(PhysicsVec3 v)
        {
            return std::abs(length(v) - 1.0f) <= kAxisTolerance;
        }

        bool isUnit(PhysicsQuat q)
        {
            const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            return std::abs(len - 1.0f) <= kAxisTolerance;
        }

        bool isFinite(PhysicsVec3 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        template <typename Id>
        CreateResult<Id> failure(PhysicsErrorCode code, std::string message)
        {
            return CreateResult<Id>{ Id{}, code, std::move(message) };
        }

        std::string validateShape(const PhysicsShape& shape)
        {
            if (const auto* box = std::get_if<BoxShape>(&shape))
            {
                const PhysicsVec3 h = box->halfExtentsM;
                if (!(h.x > 0.0f && h.y > 0.0f && h.z > 0.0f) || !isFinite(h))
                {
                    return "BoxShape.halfExtentsM must be > 0 on every axis";
                }
            }
            else if (const auto* sphere = std::get_if<SphereShape>(&shape))
            {
                if (!(sphere->radiusM > 0.0f) || !std::isfinite(sphere->radiusM))
                {
                    return "SphereShape.radiusM must be > 0";
                }
            }
            else if (const auto* capsule = std::get_if<CapsuleShape>(&shape))
            {
                if (!(capsule->radiusM > 0.0f) || !std::isfinite(capsule->radiusM))
                {
                    return "CapsuleShape.radiusM must be > 0";
                }
                if (!(capsule->halfHeightM > 0.0f) || !std::isfinite(capsule->halfHeightM))
                {
                    return "CapsuleShape.halfHeightM must be > 0";
                }
            }
            return {};
        }

        std::string validateBody(const BodyDesc& desc)
        {
            if (std::string shapeError = validateShape(desc.shape); !shapeError.empty())
            {
                return shapeError;
            }
            if (desc.motion == MotionType::Dynamic
                && (!(desc.massKg > 0.0f) || !std::isfinite(desc.massKg)))
            {
                return "massKg must be > 0 for a dynamic body";
            }
            if (!isFinite(desc.positionM))
            {
                return "positionM must be finite";
            }
            if (!isUnit(desc.rotation))
            {
                return "rotation must be a unit quaternion";
            }
            if (!(desc.friction >= 0.0f))
            {
                return "friction must be >= 0";
            }
            if (!(desc.restitution >= 0.0f && desc.restitution <= 1.0f))
            {
                return "restitution must be in [0, 1]";
            }
            if (!(desc.linearDamping >= 0.0f))
            {
                return "linearDamping must be >= 0";
            }
            if (!(desc.angularDamping >= 0.0f))
            {
                return "angularDamping must be >= 0";
            }
            return {};
        }

        std::string validateSwingRange(
            SwingShape shape, const std::string& name, float minRad, float maxRad)
        {
            if (!(minRad >= -kPi && minRad <= kPi))
            {
                return name + "MinRad must be in [-pi, pi]";
            }
            if (!(maxRad >= -kPi && maxRad <= kPi))
            {
                return name + "MaxRad must be in [-pi, pi]";
            }
            if (minRad > maxRad)
            {
                return name + "MinRad must be <= " + name + "MaxRad";
            }
            if (shape == SwingShape::Cone && minRad != -maxRad)
            {
                return name + "MinRad must be -" + name + "MaxRad for a cone";
            }
            if (shape == SwingShape::Pyramid && (minRad > 0.0f || maxRad < 0.0f))
            {
                return name + " range must contain 0 for a pyramid";
            }
            return {};
        }

        std::string validateJointGeometry(const JointDesc& desc)
        {
            if (!isFinite(desc.anchorM))
            {
                return "anchorM must be finite";
            }
            if (!isUnit(desc.twistAxis))
            {
                return "twistAxis must be unit length";
            }
            if (!isUnit(desc.planeAxis))
            {
                return "planeAxis must be unit length";
            }
            if (std::abs(dot(desc.twistAxis, desc.planeAxis)) > kAxisTolerance)
            {
                return "planeAxis must be perpendicular to twistAxis";
            }
            if (desc.swingShape != SwingShape::Cone && desc.swingShape != SwingShape::Pyramid)
            {
                return "swingShape must be Cone or Pyramid";
            }
            if (std::string error = validateSwingRange(
                    desc.swingShape, "planeSwing", desc.planeSwingMinRad, desc.planeSwingMaxRad);
                !error.empty())
            {
                return error;
            }
            if (std::string error = validateSwingRange(desc.swingShape, "normalSwing",
                    desc.normalSwingMinRad, desc.normalSwingMaxRad);
                !error.empty())
            {
                return error;
            }
            if (!(desc.twistMinRad >= -kPi && desc.twistMinRad <= kPi))
            {
                return "twistMinRad must be in [-pi, pi]";
            }
            if (!(desc.twistMaxRad >= -kPi && desc.twistMaxRad <= kPi))
            {
                return "twistMaxRad must be in [-pi, pi]";
            }
            if (desc.twistMinRad > desc.twistMaxRad)
            {
                return "twistMinRad must be <= twistMaxRad";
            }
            return {};
        }

        bool isValidMotor(const JointMotor& motor)
        {
            return motor.stiffnessNmPerRad >= 0.0f && std::isfinite(motor.stiffnessNmPerRad)
                && motor.dampingNmsPerRad >= 0.0f && std::isfinite(motor.dampingNmsPerRad)
                && motor.maxTorqueNm >= 0.0f && std::isfinite(motor.maxTorqueNm);
        }

        JPH::ShapeRefC createJoltShape(const PhysicsShape& shape)
        {
            JPH::ShapeSettings::ShapeResult result;
            if (const auto* box = std::get_if<BoxShape>(&shape))
            {
                const PhysicsVec3 h = box->halfExtentsM;
                const float smallest = std::min({ h.x, h.y, h.z });
                const float convexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * smallest);
                result = JPH::BoxShapeSettings(toJolt(h), convexRadius).Create();
            }
            else if (const auto* sphere = std::get_if<SphereShape>(&shape))
            {
                result = JPH::SphereShapeSettings(sphere->radiusM).Create();
            }
            else
            {
                const auto& capsule = std::get<CapsuleShape>(shape);
                const JPH::Quat yToZ = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * kPi);
                // JPH::RefConst takes intrusive reference-counted ownership of JPH::ShapeSettings.
                const JPH::RefConst<JPH::ShapeSettings> inner
                    = new JPH::CapsuleShapeSettings(capsule.halfHeightM, capsule.radiusM);
                result
                    = JPH::RotatedTranslatedShapeSettings(JPH::Vec3::sZero(), yToZ, inner).Create();
            }
            return result.HasError() ? nullptr : result.Get();
        }

        struct BodySlot
        {
            JPH::BodyID joltId;
            std::uint32_t generation = 1;
            bool alive = false;
            MotionType motion = MotionType::Static;
            bool kinematicTargetPending = false;
        };

        struct JointSlot
        {
            JPH::Ref<JPH::TwoBodyConstraint> constraint;
            SwingShape shape = SwingShape::Cone;
            std::uint32_t generation = 1;
            bool alive = false;
            std::uint32_t bodyA = 0;
            std::uint32_t bodyB = 0;
        };

        struct GroupSlot
        {
            JPH::Ref<JPH::GroupFilterTable> table;
            std::uint32_t generation = 1;
            bool alive = false;
            std::uint32_t subGroupCount = 0;
            JPH::CollisionGroup::GroupID joltGroupId = 0;
        };

        template <typename Slot>
        std::optional<std::uint32_t> takeSlot(std::vector<Slot>& slots,
            std::vector<std::uint32_t>& freeList, std::uint32_t capacity)
        {
            if (!freeList.empty())
            {
                const std::uint32_t index = freeList.back();
                freeList.pop_back();
                return index;
            }
            if (slots.size() >= capacity)
            {
                return std::nullopt;
            }
            slots.emplace_back();
            return static_cast<std::uint32_t>(slots.size() - 1);
        }
    } // namespace

    struct PhysicsServiceImpl::Impl
    {
        PhysicsWorldConfig config;
        BroadPhaseLayerMap broadPhaseLayers;
        ObjectVsBroadPhaseFilter objectVsBroadPhase;
        ObjectPairFilter objectPairs;
        JPH::TempAllocatorImpl tempAllocator{ kTempAllocatorBytes };
        std::unique_ptr<JPH::JobSystem> jobSystem;
        std::unique_ptr<JPH::PhysicsSystem> ownedSystem;
        JPH::PhysicsSystem* system = nullptr;

        std::vector<BodySlot> bodySlots;
        std::vector<std::uint32_t> freeBodySlots;
        std::vector<JointSlot> jointSlots;
        std::vector<std::uint32_t> freeJointSlots;
        std::vector<GroupSlot> groupSlots;
        std::vector<std::uint32_t> freeGroupSlots;
        JPH::CollisionGroup::GroupID nextGroupId = 0;
        std::vector<std::uint32_t> kinematicTargets;
        CollisionLayerRegistry* layerRegistry = nullptr;
        bool inStep = false;

        explicit Impl(const PhysicsWorldConfig& cfg, JPH::PhysicsSystem* externalSys = nullptr)
            : config(cfg)
            , jobSystem(makeJobSystem(cfg.workerThreads, cfg.subStepsPerStep))
        {
            ensureJoltRuntimeInitialized();

            if (externalSys != nullptr)
            {
                system = externalSys;
            }
            else
            {
                ownedSystem = std::make_unique<JPH::PhysicsSystem>();
                const std::uint32_t maxPairs = std::max(kMinPairs, config.maxBodies * kPairsPerBody);
                ownedSystem->Init(config.maxBodies, 0, maxPairs, maxPairs, broadPhaseLayers,
                    objectVsBroadPhase, objectPairs);
                ownedSystem->SetGravity(toJolt(config.gravityMps2));
                system = ownedSystem.get();
            }

            bodySlots.reserve(config.maxBodies);
            jointSlots.reserve(config.maxJoints);
            freeBodySlots.reserve(config.maxBodies);
            freeJointSlots.reserve(config.maxJoints);
            groupSlots.reserve(config.maxBodies);
            freeGroupSlots.reserve(config.maxBodies);
            kinematicTargets.reserve(config.maxBodies);
        }

        ~Impl()
        {
            if (system == nullptr)
            {
                return;
            }
            for (const JointSlot& slot : jointSlots)
            {
                if (slot.alive && slot.constraint != nullptr)
                {
                    system->RemoveConstraint(slot.constraint);
                }
            }
            JPH::BodyInterface& bi = system->GetBodyInterface();
            for (const BodySlot& slot : bodySlots)
            {
                if (slot.alive && !slot.joltId.IsInvalid())
                {
                    bi.RemoveBody(slot.joltId);
                    bi.DestroyBody(slot.joltId);
                }
            }
        }

        [[nodiscard]] JPH::BodyInterface& bodies() { return system->GetBodyInterface(); }
        [[nodiscard]] const JPH::BodyInterface& bodies() const { return system->GetBodyInterface(); }

        [[nodiscard]] const BodySlot* findBody(BodyId body) const
        {
            if (!body.isValid() || body.index >= bodySlots.size())
            {
                return nullptr;
            }
            const BodySlot& slot = bodySlots[body.index];
            return (slot.alive && slot.generation == body.generation) ? &slot : nullptr;
        }

        [[nodiscard]] JointSlot* findJoint(JointId joint)
        {
            if (!joint.isValid() || joint.index >= jointSlots.size())
            {
                return nullptr;
            }
            JointSlot& slot = jointSlots[joint.index];
            return (slot.alive && slot.generation == joint.generation) ? &slot : nullptr;
        }

        [[nodiscard]] const JointSlot* findJoint(JointId joint) const
        {
            if (!joint.isValid() || joint.index >= jointSlots.size())
            {
                return nullptr;
            }
            const JointSlot& slot = jointSlots[joint.index];
            return (slot.alive && slot.generation == joint.generation) ? &slot : nullptr;
        }

        [[nodiscard]] GroupSlot* findGroup(CollisionGroupId group)
        {
            if (!group.isValid() || group.index >= groupSlots.size())
            {
                return nullptr;
            }
            GroupSlot& slot = groupSlots[group.index];
            return (slot.alive && slot.generation == group.generation) ? &slot : nullptr;
        }

        void destroyJointSlot(std::uint32_t index)
        {
            JointSlot& slot = jointSlots[index];
            if (slot.constraint != nullptr)
            {
                system->RemoveConstraint(slot.constraint);
            }
            slot.constraint = nullptr;
            slot.alive = false;
            ++slot.generation;
            freeJointSlots.push_back(index);
        }

        void wakeJointBodies(const JointSlot& slot)
        {
            for (const std::uint32_t bodyIndex : { slot.bodyA, slot.bodyB })
            {
                const BodySlot& body = bodySlots[bodyIndex];
                if (body.motion != MotionType::Static)
                {
                    bodies().ActivateBody(body.joltId);
                }
            }
        }

        void stopKinematicBodiesAtTargets()
        {
            for (const std::uint32_t index : kinematicTargets)
            {
                BodySlot& slot = bodySlots[index];
                if (slot.alive && slot.kinematicTargetPending)
                {
                    bodies().SetLinearAndAngularVelocity(
                        slot.joltId, JPH::Vec3::sZero(), JPH::Vec3::sZero());
                    slot.kinematicTargetPending = false;
                }
            }
            kinematicTargets.clear();
        }
    };

    PhysicsServiceImpl::PhysicsServiceImpl(const PhysicsWorldConfig& config)
        : mImpl(std::make_unique<Impl>(config, nullptr))
    {
    }

    PhysicsServiceImpl::PhysicsServiceImpl(
        JPH::PhysicsSystem* externalSystem, const PhysicsWorldConfig& config)
        : mImpl(std::make_unique<Impl>(config, externalSystem))
    {
    }

    PhysicsServiceImpl::~PhysicsServiceImpl() = default;
    PhysicsServiceImpl::PhysicsServiceImpl(PhysicsServiceImpl&&) noexcept = default;
    PhysicsServiceImpl& PhysicsServiceImpl::operator=(PhysicsServiceImpl&&) noexcept = default;

    CreateResult<BodyId> PhysicsServiceImpl::createBody(const BodyDesc& desc)
    {
        if (mImpl->inStep)
        {
            return failure<BodyId>(PhysicsErrorCode::CalledDuringStep,
                "createBody cannot be called during physics step");
        }
        if (std::string error = validateBody(desc); !error.empty())
        {
            return failure<BodyId>(PhysicsErrorCode::InvalidDescription, std::move(error));
        }

        const GroupSlot* group = nullptr;
        if (desc.collisionGroup.isValid())
        {
            group = mImpl->findGroup(desc.collisionGroup);
            if (group == nullptr)
            {
                return failure<BodyId>(
                    PhysicsErrorCode::InvalidHandle, "collisionGroup is not a live group");
            }
            if (desc.subGroup >= group->subGroupCount)
            {
                return failure<BodyId>(PhysicsErrorCode::InvalidDescription,
                    "subGroup must be < the group's subGroupCount");
            }
        }

        const JPH::ShapeRefC shape = createJoltShape(desc.shape);
        if (shape == nullptr)
        {
            return failure<BodyId>(
                PhysicsErrorCode::InvalidDescription, "shape could not be created");
        }

        const std::optional<std::uint32_t> index
            = takeSlot(mImpl->bodySlots, mImpl->freeBodySlots, mImpl->config.maxBodies);
        if (!index)
        {
            return failure<BodyId>(PhysicsErrorCode::CapacityExceeded, "maxBodies reached");
        }

        JPH::EMotionType motion = JPH::EMotionType::Static;
        JPH::ObjectLayer layer = Layers::kNonMoving;
        if (desc.motion == MotionType::Dynamic)
        {
            motion = JPH::EMotionType::Dynamic;
            layer = Layers::kMoving;
        }
        else if (desc.motion == MotionType::Kinematic)
        {
            motion = JPH::EMotionType::Kinematic;
            layer = Layers::kMoving;
        }

        JPH::BodyCreationSettings settings(shape, toJoltR(desc.positionM),
            toJolt(desc.rotation).Normalized(), motion, layer);
        settings.mFriction = desc.friction;
        settings.mRestitution = desc.restitution;
        settings.mLinearDamping = desc.linearDamping;
        settings.mAngularDamping = desc.angularDamping;
        settings.mUserData = *index;
        if (group != nullptr)
        {
            settings.mCollisionGroup
                = JPH::CollisionGroup(group->table, group->joltGroupId, desc.subGroup);
        }
        if (desc.motion == MotionType::Dynamic
            || (desc.motion == MotionType::Kinematic && desc.massKg > 0.0f))
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = desc.massKg;
        }

        const JPH::EActivation activation = desc.motion == MotionType::Static
            ? JPH::EActivation::DontActivate
            : JPH::EActivation::Activate;
        const JPH::BodyID joltId = mImpl->bodies().CreateAndAddBody(settings, activation);
        if (joltId.IsInvalid())
        {
            mImpl->freeBodySlots.push_back(*index);
            return failure<BodyId>(PhysicsErrorCode::CapacityExceeded, "Jolt body limit reached");
        }

        BodySlot& slot = mImpl->bodySlots[*index];
        slot.joltId = joltId;
        slot.alive = true;
        slot.motion = desc.motion;
        slot.kinematicTargetPending = false;
        return CreateResult<BodyId>{ BodyId{ *index, slot.generation }, PhysicsErrorCode::None, {} };
    }

    bool PhysicsServiceImpl::destroyBody(BodyId body)
    {
        if (mImpl->inStep || mImpl->findBody(body) == nullptr)
        {
            return false;
        }
        for (std::uint32_t i = 0; i < mImpl->jointSlots.size(); ++i)
        {
            const JointSlot& joint = mImpl->jointSlots[i];
            if (joint.alive && (joint.bodyA == body.index || joint.bodyB == body.index))
            {
                mImpl->destroyJointSlot(i);
            }
        }
        BodySlot& slot = mImpl->bodySlots[body.index];
        if (slot.kinematicTargetPending)
        {
            std::erase(mImpl->kinematicTargets, body.index);
            slot.kinematicTargetPending = false;
        }
        mImpl->bodies().RemoveBody(slot.joltId);
        mImpl->bodies().DestroyBody(slot.joltId);
        slot.alive = false;
        ++slot.generation;
        mImpl->freeBodySlots.push_back(body.index);
        return true;
    }

    bool PhysicsServiceImpl::isBodyValid(BodyId id) const
    {
        return mImpl->findBody(id) != nullptr;
    }

    CreateResult<JointId> PhysicsServiceImpl::createJoint(const JointDesc& desc)
    {
        if (mImpl->inStep)
        {
            return failure<JointId>(PhysicsErrorCode::CalledDuringStep,
                "createJoint cannot be called during physics step");
        }
        const BodySlot* bodyA = mImpl->findBody(desc.bodyA);
        if (bodyA == nullptr)
        {
            return failure<JointId>(PhysicsErrorCode::InvalidHandle, "bodyA is not a live body");
        }
        const BodySlot* bodyB = mImpl->findBody(desc.bodyB);
        if (bodyB == nullptr)
        {
            return failure<JointId>(PhysicsErrorCode::InvalidHandle, "bodyB is not a live body");
        }
        if (desc.bodyA == desc.bodyB)
        {
            return failure<JointId>(PhysicsErrorCode::InvalidDescription,
                "bodyB must be a different body from bodyA");
        }
        if (std::string error = validateJointGeometry(desc); !error.empty())
        {
            return failure<JointId>(PhysicsErrorCode::InvalidDescription, std::move(error));
        }

        const std::optional<std::uint32_t> index
            = takeSlot(mImpl->jointSlots, mImpl->freeJointSlots, mImpl->config.maxJoints);
        if (!index)
        {
            return failure<JointId>(PhysicsErrorCode::CapacityExceeded, "maxJoints reached");
        }

        const JPH::TwoBodyConstraintSettings* settings = nullptr;
        JPH::SwingTwistConstraintSettings cone;
        JPH::SixDOFConstraintSettings pyramid;
        if (desc.swingShape == SwingShape::Cone)
        {
            cone.mSpace = JPH::EConstraintSpace::WorldSpace;
            cone.mPosition1 = cone.mPosition2 = toJoltR(desc.anchorM);
            cone.mTwistAxis1 = cone.mTwistAxis2 = toJolt(desc.twistAxis).Normalized();
            cone.mPlaneAxis1 = cone.mPlaneAxis2 = toJolt(desc.planeAxis).Normalized();
            cone.mNormalHalfConeAngle = desc.normalSwingMaxRad;
            cone.mPlaneHalfConeAngle = desc.planeSwingMaxRad;
            cone.mTwistMinAngle = desc.twistMinRad;
            cone.mTwistMaxAngle = desc.twistMaxRad;
            cone.mNumPositionStepsOverride = kJointPositionSteps;
            settings = &cone;
        }
        else
        {
            const JPH::Vec3 twist = toJolt(desc.twistAxis).Normalized();
            const JPH::Vec3 plane = toJolt(desc.planeAxis).Normalized();
            pyramid.mSpace = JPH::EConstraintSpace::WorldSpace;
            pyramid.mPosition1 = pyramid.mPosition2 = toJoltR(desc.anchorM);
            pyramid.mAxisX1 = pyramid.mAxisX2 = twist;
            pyramid.mAxisY1 = pyramid.mAxisY2 = plane.Cross(twist).Normalized();
            pyramid.mSwingType = JPH::ESwingType::Pyramid;
            using Axis = JPH::SixDOFConstraintSettings::EAxis;
            pyramid.MakeFixedAxis(Axis::TranslationX);
            pyramid.MakeFixedAxis(Axis::TranslationY);
            pyramid.MakeFixedAxis(Axis::TranslationZ);
            pyramid.SetLimitedAxis(Axis::RotationX, desc.twistMinRad, desc.twistMaxRad);
            pyramid.SetLimitedAxis(Axis::RotationY, desc.planeSwingMinRad, desc.planeSwingMaxRad);
            pyramid.SetLimitedAxis(Axis::RotationZ, desc.normalSwingMinRad, desc.normalSwingMaxRad);
            pyramid.mNumPositionStepsOverride = kJointPositionSteps;
            settings = &pyramid;
        }

        JPH::TwoBodyConstraint* created
            = mImpl->bodies().CreateConstraint(settings, bodyA->joltId, bodyB->joltId);
        if (created == nullptr)
        {
            mImpl->freeJointSlots.push_back(*index);
            return failure<JointId>(
                PhysicsErrorCode::InvalidHandle, "bodyA or bodyB could not be locked");
        }

        JointSlot& slot = mImpl->jointSlots[*index];
        slot.constraint = created;
        slot.shape = desc.swingShape;
        slot.alive = true;
        slot.bodyA = desc.bodyA.index;
        slot.bodyB = desc.bodyB.index;
        mImpl->system->AddConstraint(slot.constraint);
        return CreateResult<JointId>{
            JointId{ *index, slot.generation }, PhysicsErrorCode::None, {}
        };
    }

    bool PhysicsServiceImpl::destroyJoint(JointId joint)
    {
        if (mImpl->inStep || mImpl->findJoint(joint) == nullptr)
        {
            return false;
        }
        mImpl->destroyJointSlot(joint.index);
        return true;
    }

    bool PhysicsServiceImpl::isConstraintValid(ConstraintId id) const
    {
        return mImpl->findJoint(id) != nullptr;
    }

    CreateResult<CollisionGroupId> PhysicsServiceImpl::createCollisionGroup(
        std::uint32_t subGroupCount)
    {
        if (mImpl->inStep)
        {
            return failure<CollisionGroupId>(PhysicsErrorCode::CalledDuringStep,
                "createCollisionGroup cannot be called during physics step");
        }
        if (subGroupCount == 0
            || subGroupCount > std::min(mImpl->config.maxBodies, kMaxSubGroups))
        {
            return failure<CollisionGroupId>(PhysicsErrorCode::InvalidDescription,
                "subGroupCount must be in [1, min(maxBodies, 65536)]");
        }
        if (mImpl->nextGroupId == JPH::CollisionGroup::cInvalidGroup)
        {
            return failure<CollisionGroupId>(
                PhysicsErrorCode::CapacityExceeded, "collision group IDs are used up");
        }
        const std::optional<std::uint32_t> index
            = takeSlot(mImpl->groupSlots, mImpl->freeGroupSlots, mImpl->config.maxBodies);
        if (!index)
        {
            return failure<CollisionGroupId>(
                PhysicsErrorCode::CapacityExceeded, "too many collision groups");
        }

        // JPH::Ref takes intrusive reference-counted ownership of JPH::GroupFilterTable.
        const JPH::Ref<JPH::GroupFilterTable> table = new JPH::GroupFilterTable(subGroupCount);
        GroupSlot& slot = mImpl->groupSlots[*index];
        slot.table = table;
        slot.alive = true;
        slot.subGroupCount = subGroupCount;
        slot.joltGroupId = mImpl->nextGroupId++;
        return CreateResult<CollisionGroupId>{
            CollisionGroupId{ *index, slot.generation }, PhysicsErrorCode::None, {}
        };
    }

    bool PhysicsServiceImpl::setSubGroupsCollide(
        CollisionGroupId group, std::uint32_t a, std::uint32_t b, bool collide)
    {
        const GroupSlot* slot = mImpl->findGroup(group);
        if (mImpl->inStep || slot == nullptr || a == b || a >= slot->subGroupCount
            || b >= slot->subGroupCount)
        {
            return false;
        }
        const std::uint32_t low = std::min(a, b);
        const std::uint32_t high = std::max(a, b);
        if (collide)
        {
            slot->table->EnableCollision(low, high);
        }
        else
        {
            slot->table->DisableCollision(low, high);
        }
        return true;
    }

    bool PhysicsServiceImpl::destroyCollisionGroup(CollisionGroupId group)
    {
        GroupSlot* slot = mImpl->findGroup(group);
        if (mImpl->inStep || slot == nullptr)
        {
            return false;
        }
        slot->table = nullptr;
        slot->alive = false;
        ++slot->generation;
        mImpl->freeGroupSlots.push_back(group.index);
        return true;
    }

    CollisionLayer PhysicsServiceImpl::registerCollisionLayer(std::string_view name)
    {
        if (mImpl->layerRegistry == nullptr)
        {
            return 0;
        }
        return mImpl->layerRegistry->registerLayer(name);
    }

    void PhysicsServiceImpl::declareCollisionPair(std::string_view layerA, std::string_view layerB)
    {
        if (mImpl->layerRegistry != nullptr)
        {
            mImpl->layerRegistry->declareCollisionPair(layerA, layerB);
        }
    }

    void PhysicsServiceImpl::setCollisionLayerRegistry(CollisionLayerRegistry* registry) noexcept
    {
        mImpl->layerRegistry = registry;
    }

    bool PhysicsServiceImpl::setJointPositionSteps(JointId joint, std::uint32_t steps)
    {
        const JointSlot* slot = mImpl->findJoint(joint);
        if (mImpl->inStep || slot == nullptr || steps < 1 || steps > kMaxJointPositionSteps)
        {
            return false;
        }
        slot->constraint->SetNumPositionStepsOverride(steps);
        return true;
    }

    bool PhysicsServiceImpl::setJointMotor(JointId joint, const JointMotor& motor)
    {
        const JointSlot* slot = mImpl->findJoint(joint);
        if (slot == nullptr || !isValidMotor(motor))
        {
            return false;
        }
        const bool off = motor.stiffnessNmPerRad == 0.0f && motor.dampingNmsPerRad == 0.0f;
        const auto configure = [&motor](JPH::MotorSettings& settings) {
            const float effectiveStiffness = (motor.stiffnessNmPerRad == 0.0f && motor.dampingNmsPerRad > 0.0f)
                ? 1e-5f
                : motor.stiffnessNmPerRad;
            settings.mSpringSettings = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping,
                effectiveStiffness, motor.dampingNmsPerRad);
            settings.SetTorqueLimit(motor.maxTorqueNm);
        };
        const JPH::EMotorState state
            = off ? JPH::EMotorState::Off : JPH::EMotorState::Position;
        if (slot->shape == SwingShape::Cone)
        {
            JPH_ASSERT(slot->constraint->GetSubType() == JPH::EConstraintSubType::SwingTwist);
            auto& constraint = static_cast<JPH::SwingTwistConstraint&>(*slot->constraint);
            if (!off)
            {
                configure(constraint.GetSwingMotorSettings());
                configure(constraint.GetTwistMotorSettings());
                constraint.SetTargetAngularVelocityCS(JPH::Vec3::sZero());
            }
            constraint.SetSwingMotorState(state);
            constraint.SetTwistMotorState(state);
        }
        else
        {
            JPH_ASSERT(slot->constraint->GetSubType() == JPH::EConstraintSubType::SixDOF);
            auto& constraint = static_cast<JPH::SixDOFConstraint&>(*slot->constraint);
            using Axis = JPH::SixDOFConstraintSettings::EAxis;
            if (!off)
            {
                constraint.SetTargetAngularVelocityCS(JPH::Vec3::sZero());
            }
            for (const Axis axis : { Axis::RotationX, Axis::RotationY, Axis::RotationZ })
            {
                if (!off)
                {
                    configure(constraint.GetMotorSettings(axis));
                }
                constraint.SetMotorState(axis, state);
            }
        }
        mImpl->wakeJointBodies(*slot);
        return true;
    }

    bool PhysicsServiceImpl::setJointTarget(JointId joint, PhysicsQuat targetRotation)
    {
        return setJointTarget(joint, targetRotation, PhysicsVec3{});
    }

    bool PhysicsServiceImpl::setJointTarget(
        JointId joint, PhysicsQuat targetRotation, PhysicsVec3 targetAngularVelocityRadps)
    {
        const JointSlot* slot = mImpl->findJoint(joint);
        if (slot == nullptr || !isUnit(targetRotation) || !isFinite(targetAngularVelocityRadps))
        {
            return false;
        }
        const JPH::Quat target = toJolt(targetRotation).Normalized();
        const JPH::Vec3 targetOmega = toJolt(targetAngularVelocityRadps);
        if (slot->shape == SwingShape::Cone)
        {
            JPH_ASSERT(slot->constraint->GetSubType() == JPH::EConstraintSubType::SwingTwist);
            auto& constraint = static_cast<JPH::SwingTwistConstraint&>(*slot->constraint);
            constraint.SetTargetOrientationCS(target);
            constraint.SetTargetAngularVelocityCS(targetOmega);
        }
        else
        {
            JPH_ASSERT(slot->constraint->GetSubType() == JPH::EConstraintSubType::SixDOF);
            auto& constraint = static_cast<JPH::SixDOFConstraint&>(*slot->constraint);
            constraint.SetTargetOrientationCS(target);
            constraint.SetTargetAngularVelocityCS(targetOmega);
        }
        mImpl->wakeJointBodies(*slot);
        return true;
    }

    bool PhysicsServiceImpl::applyImpulse(
        BodyId body, PhysicsVec3 impulseNs, PhysicsVec3 worldPointM)
    {
        const BodySlot* slot = mImpl->findBody(body);
        if (slot == nullptr || slot->motion != MotionType::Dynamic || !isFinite(impulseNs)
            || !isFinite(worldPointM))
        {
            return false;
        }
        mImpl->bodies().AddImpulse(slot->joltId, toJolt(impulseNs), toJoltR(worldPointM));
        return true;
    }

    bool PhysicsServiceImpl::setVelocity(
        BodyId body, PhysicsVec3 linearMps, PhysicsVec3 angularRadps)
    {
        const BodySlot* slot = mImpl->findBody(body);
        if (slot == nullptr || slot->motion == MotionType::Static || !isFinite(linearMps)
            || !isFinite(angularRadps))
        {
            return false;
        }
        mImpl->bodies().SetLinearAndAngularVelocity(
            slot->joltId, toJolt(linearMps), toJolt(angularRadps));
        return true;
    }

    bool PhysicsServiceImpl::setBodyDamping(BodyId body, float linearPerS, float angularPerS)
    {
        const BodySlot* slot = mImpl->findBody(body);
        const bool valid = linearPerS >= 0.0f && std::isfinite(linearPerS) && angularPerS >= 0.0f
            && std::isfinite(angularPerS);
        if (mImpl->inStep || slot == nullptr || slot->motion != MotionType::Dynamic || !valid)
        {
            return false;
        }
        const JPH::BodyLockWrite lock(mImpl->system->GetBodyLockInterface(), slot->joltId);
        if (!lock.Succeeded())
        {
            return false;
        }
        JPH::MotionProperties* motion = lock.GetBody().GetMotionProperties();
        motion->SetLinearDamping(linearPerS);
        motion->SetAngularDamping(angularPerS);
        return true;
    }

    bool PhysicsServiceImpl::setKinematicTarget(
        BodyId body, PhysicsVec3 positionM, PhysicsQuat rotation)
    {
        const BodySlot* found = mImpl->findBody(body);
        if (found == nullptr || found->motion != MotionType::Kinematic || !isFinite(positionM)
            || !isUnit(rotation))
        {
            return false;
        }
        BodySlot& slot = mImpl->bodySlots[body.index];
        mImpl->bodies().MoveKinematic(slot.joltId, toJoltR(positionM),
            toJolt(rotation).Normalized(), mImpl->config.fixedStepSeconds);
        if (!slot.kinematicTargetPending)
        {
            slot.kinematicTargetPending = true;
            mImpl->kinematicTargets.push_back(body.index);
        }
        return true;
    }

    bool PhysicsServiceImpl::setMotionType(BodyId body, MotionType motion)
    {
        const BodySlot* found = mImpl->findBody(body);
        if (mImpl->inStep || found == nullptr)
        {
            return false;
        }
        if (found->motion == motion)
        {
            return true;
        }
        if (found->motion == MotionType::Static)
        {
            return false;
        }

        BodySlot& slot = mImpl->bodySlots[body.index];
        JPH::EMotionType joltMotion = JPH::EMotionType::Static;
        switch (motion)
        {
            case MotionType::Static:
                joltMotion = JPH::EMotionType::Static;
                break;
            case MotionType::Dynamic:
                joltMotion = JPH::EMotionType::Dynamic;
                break;
            case MotionType::Kinematic:
                joltMotion = JPH::EMotionType::Kinematic;
                break;
            default:
                return false;
        }

        if (motion == MotionType::Static)
        {
            mImpl->bodies().DeactivateBody(slot.joltId);
            mImpl->bodies().SetObjectLayer(slot.joltId, Layers::kNonMoving);
        }
        else if (slot.motion == MotionType::Static)
        {
            mImpl->bodies().SetObjectLayer(slot.joltId, Layers::kMoving);
        }

        const JPH::EActivation activation = (motion == MotionType::Dynamic)
            ? JPH::EActivation::Activate
            : JPH::EActivation::DontActivate;
        mImpl->bodies().SetMotionType(slot.joltId, joltMotion, activation);

        if (slot.kinematicTargetPending && motion != MotionType::Kinematic)
        {
            slot.kinematicTargetPending = false;
            std::erase(mImpl->kinematicTargets, body.index);
        }

        slot.motion = motion;
        return true;
    }

    std::optional<MotionType> PhysicsServiceImpl::getMotionType(BodyId body) const
    {
        const BodySlot* slot = mImpl->findBody(body);
        if (slot == nullptr)
        {
            return std::nullopt;
        }
        return slot->motion;
    }

    bool PhysicsServiceImpl::readBody(BodyId body, BodyState& out) const
    {
        const BodySlot* slot = mImpl->findBody(body);
        if (slot == nullptr)
        {
            return false;
        }
        const JPH::BodyInterface& bi = mImpl->bodies();
        JPH::RVec3 position;
        JPH::Quat rotation;
        bi.GetPositionAndRotation(slot->joltId, position, rotation);
        out.positionM = fromJoltR(position);
        out.rotation = fromJolt(rotation);
        out.linearVelocityMps = fromJolt(bi.GetLinearVelocity(slot->joltId));
        out.angularVelocityRadps = fromJolt(bi.GetAngularVelocity(slot->joltId));
        out.awake = bi.IsActive(slot->joltId);
        return true;
    }

    std::size_t PhysicsServiceImpl::readBodies(
        std::span<const BodyId> bodies, std::span<BodyState> out) const
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

    std::optional<RayHit> PhysicsServiceImpl::castRay(
        PhysicsVec3 originM, PhysicsVec3 directionTimesLengthM, BodyId ignore) const
    {
        if (!isFinite(originM) || !isFinite(directionTimesLengthM))
        {
            return std::nullopt;
        }
        const JPH::RRayCast ray{ toJoltR(originM), toJolt(directionTimesLengthM) };
        JPH::RayCastResult hit;
        const BodySlot* ignored = mImpl->findBody(ignore);
        const JPH::IgnoreSingleBodyFilter ignoreFilter(
            ignored != nullptr ? ignored->joltId : JPH::BodyID());
        const bool found
            = mImpl->system->GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, ignoreFilter);
        if (!found)
        {
            return std::nullopt;
        }

        const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
        const JPH::BodyLockRead lock(mImpl->system->GetBodyLockInterface(), hit.mBodyID);
        if (!lock.Succeeded())
        {
            return std::nullopt;
        }
        const JPH::Body& body = lock.GetBody();
        const auto index = static_cast<std::uint32_t>(body.GetUserData());

        RayHit result;
        if (index < mImpl->bodySlots.size() && mImpl->bodySlots[index].alive
            && mImpl->bodySlots[index].joltId == hit.mBodyID)
        {
            result.body = BodyId{ index, mImpl->bodySlots[index].generation };
        }
        else
        {
            result.body = BodyId{};
        }
        result.pointM = fromJoltR(point);
        result.normal = fromJolt(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
        result.fraction = hit.mFraction;
        return result;
    }

    void PhysicsServiceImpl::step(float dtSeconds)
    {
        if (mImpl->inStep)
        {
            return;
        }
        const float dt = (dtSeconds > 0.0f && std::isfinite(dtSeconds))
            ? dtSeconds
            : mImpl->config.fixedStepSeconds;
        mImpl->inStep = true;
        const auto collisionSteps = static_cast<int>(mImpl->config.subStepsPerStep);
        mImpl->system->Update(
            dt, collisionSteps, &mImpl->tempAllocator, mImpl->jobSystem.get());
        mImpl->stopKinematicBodiesAtTargets();
        mImpl->inStep = false;
    }

    float PhysicsServiceImpl::fixedStepSeconds() const
    {
        return mImpl->config.fixedStepSeconds;
    }

    void PhysicsServiceImpl::setInStep(bool inStep) noexcept
    {
        mImpl->inStep = inStep;
    }

    bool PhysicsServiceImpl::isInStep() const noexcept
    {
        return mImpl->inStep;
    }
} // namespace openeden::plugin
