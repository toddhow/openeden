#include "opas/physics/jolt/JoltWorld.hpp"

#include "JoltRuntime.hpp"

// Jolt requires Jolt.h before any other Jolt header.
#include <Jolt/Jolt.h>

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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace opas::physics::jolt {
namespace {

// --- Tuning constants ---------------------------------------------------------

// Scratch memory Jolt uses during each step; 10 MB is what Jolt's HelloWorld
// sample uses and is far more than a few ragdolls need.
constexpr std::uint32_t kTempAllocatorBytes = 10U * 1024U * 1024U;
// Jolt asks for upper bounds on broad-phase pairs and contact constraints.
// A ragdoll body touches few others; 4 per body leaves generous headroom.
constexpr std::uint32_t kPairsPerBody = 4;
constexpr std::uint32_t kMinPairs = 1024;
// How far an axis may be from unit length, or two axes from perpendicular,
// before a description is rejected. Loose enough for float round-off from
// authored data.
constexpr float kAxisTolerance = 1e-3f;

// Position-solver iterations for joints (Jolt's world default is 2).
// Jolt corrects a joint-limit violation over several iterations rather than
// preventing it, so a fast limb overshoots its limit for a few steps. Measured
// with the M1 limit test (10 rad/s into a 30 degree twist limit at 60 Hz):
// 2 -> 35.2 deg, 4 -> 33.3, 6 -> 32.1, 8 -> ~31.4, 10 -> ~31. 10 keeps the
// spec's 2 degree tolerance with margin. Costs solver time per joint island;
// revisit against docs/budgets.md.
constexpr JPH::uint kJointPositionSteps = 10;
// Jolt keeps a constraint's position steps in 8 bits
// (Constraint::SetNumPositionStepsOverride asserts below 256).
constexpr std::uint32_t kMaxJointPositionSteps = 255;

constexpr float kPi = std::numbers::pi_v<float>;

// Upper bound on WorldConfig::subStepsPerStep: 16 sub-steps of a 30 Hz step
// is 480 Hz, far beyond any need, and keeps a typo from costing 1000x.
constexpr std::uint32_t kMaxSubStepsPerStep = 16;

// Upper bound on WorldConfig::workerThreads: well above any desktop CPU, and
// keeps a typo from starting thousands of threads. Jolt itself uses at most
// 32 threads on a step (PhysicsUpdateContext::cMaxConcurrency), so more only
// adds idle threads.
constexpr std::uint32_t kMaxWorkerThreads = 64;

// One worker: Jolt's single-threaded job system, exactly as before task 0018.
// More: Jolt's thread pool. Its thread count excludes the calling thread,
// which also runs jobs while it waits (JobSystemThreadPool::Init), so
// workerThreads - 1 threads are started and workerThreads work on a step.
//
// How many threads may work on a step. PhysicsSystem::Update creates every
// sub-step's jobs up front and waits on all of them through one fixed array of
// cMaxPhysicsJobs handles and one barrier of JobSystemWithBarrier::cMaxJobs
// (both 2048; PhysicsSystem.cpp "Build job barrier"). Per sub-step, at most
// kScalingJobKinds kinds of job grow with the concurrency (step listeners,
// active constraints, gravity, collisions, velocity setup, velocity solve,
// integrate, position solve; PhysicsSystem::Update) and kSingleJobs are one
// each, so more threads than this overflow the array (found by the task 0018
// P2 review and its tests: 32 threads at 16 sub-steps). Jolt also never uses
// more than PhysicsUpdateContext::cMaxConcurrency = 32. Jobs created after the
// barrier is built are not counted: continuous-collision jobs (up to the
// concurrency per sub-step) and soft-body jobs (1 + 2 x concurrency). They
// fit only because this backend registers no Jolt step listeners (c free
// slots per sub-step) and creates no continuous-collision or soft bodies;
// adding any of those means revisiting this bound (task 0018 P3 review).
constexpr std::uint32_t kScalingJobKinds = 8;
constexpr std::uint32_t kSingleJobs = 11;
constexpr std::uint32_t kJoltMaxConcurrency = 32;

std::uint32_t usableThreads(std::uint32_t workerThreads, std::uint32_t subStepsPerStep)
{
    const std::uint32_t jobsPerSubStep =
        static_cast<std::uint32_t>(JPH::cMaxPhysicsJobs) / subStepsPerStep;
    const std::uint32_t fit = (jobsPerSubStep - kSingleJobs) / kScalingJobKinds;
    return std::max(1U, std::min({workerThreads, kJoltMaxConcurrency, fit}));
}

// One worker: Jolt's single-threaded job system, exactly as before task 0018.
// More: Jolt's thread pool. Its thread count excludes the calling thread,
// which also runs jobs while it waits (JobSystemThreadPool::Init), so
// threads - 1 are started and `threads` work on a step.
std::unique_ptr<JPH::JobSystem> makeJobSystem(std::uint32_t workerThreads,
                                              std::uint32_t subStepsPerStep)
{
    const std::uint32_t threads = usableThreads(workerThreads, subStepsPerStep);
    if (threads <= 1) {
        return std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
    }
    return std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(threads) - 1);
}

// Most sub-groups in one collision group: 65536 * 65535 / 2 bits is the
// largest table whose size fits Jolt's 32-bit arithmetic and int bit index
// (GroupFilterTable.h, constructor and GetBit).
constexpr std::uint32_t kMaxSubGroups = 65536;

// --- Collision layers -----------------------------------------------------------
// Two layers, as in Jolt's HelloWorld sample: static bodies never need to be
// tested against each other.

namespace Layers {
constexpr JPH::ObjectLayer kNonMoving = 0;
constexpr JPH::ObjectLayer kMoving = 1;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer kNonMoving(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr JPH::uint kCount = 2;
} // namespace BroadPhaseLayers

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
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

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer,
                                     JPH::BroadPhaseLayer broadPhaseLayer) const override
    {
        return layer == Layers::kMoving || broadPhaseLayer == BroadPhaseLayers::kMoving;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return a == Layers::kMoving || b == Layers::kMoving;
    }
};

// --- Conversions between our types and Jolt's ---------------------------------

JPH::Vec3 toJolt(Vec3 v)
{
    return {v.x, v.y, v.z};
}

JPH::RVec3 toJoltR(Vec3 v)
{
    return {v.x, v.y, v.z};
}

JPH::Quat toJolt(Quat q)
{
    return {q.x, q.y, q.z, q.w};
}

Vec3 fromJolt(JPH::Vec3Arg v)
{
    return {v.GetX(), v.GetY(), v.GetZ()};
}

Vec3 fromJoltR(JPH::RVec3Arg v)
{
    return {static_cast<float>(v.GetX()), static_cast<float>(v.GetY()),
            static_cast<float>(v.GetZ())};
}

Quat fromJolt(JPH::QuatArg q)
{
    return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

// --- Validation helpers ---------------------------------------------------------

float length(Vec3 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

bool isUnit(Vec3 v)
{
    return std::abs(length(v) - 1.0f) <= kAxisTolerance;
}

bool isUnit(Quat q)
{
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return std::abs(len - 1.0f) <= kAxisTolerance;
}

bool isFinite(Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

template <typename Id>
CreateResult<Id> failure(ErrorCode code, std::string message)
{
    return CreateResult<Id>{Id{}, code, std::move(message)};
}

// Returns an empty string if the shape is valid, otherwise a message naming
// the bad field.
std::string validateShape(const Shape& shape)
{
    if (const auto* box = std::get_if<BoxShape>(&shape)) {
        const Vec3 h = box->halfExtentsM;
        if (!(h.x > 0.0f && h.y > 0.0f && h.z > 0.0f) || !isFinite(h)) {
            return "BoxShape.halfExtentsM must be > 0 on every axis";
        }
    } else if (const auto* sphere = std::get_if<SphereShape>(&shape)) {
        if (!(sphere->radiusM > 0.0f) || !std::isfinite(sphere->radiusM)) {
            return "SphereShape.radiusM must be > 0";
        }
    } else if (const auto* capsule = std::get_if<CapsuleShape>(&shape)) {
        if (!(capsule->radiusM > 0.0f) || !std::isfinite(capsule->radiusM)) {
            return "CapsuleShape.radiusM must be > 0";
        }
        if (!(capsule->halfHeightM > 0.0f) || !std::isfinite(capsule->halfHeightM)) {
            return "CapsuleShape.halfHeightM must be > 0";
        }
    }
    return {};
}

std::string validateBody(const BodyDesc& desc)
{
    if (std::string shapeError = validateShape(desc.shape); !shapeError.empty()) {
        return shapeError;
    }
    if (desc.motion == MotionType::Dynamic &&
        (!(desc.massKg > 0.0f) || !std::isfinite(desc.massKg))) {
        return "massKg must be > 0 for a dynamic body";
    }
    if (!isFinite(desc.positionM)) {
        return "positionM must be finite";
    }
    if (!isUnit(desc.rotation)) {
        return "rotation must be a unit quaternion";
    }
    if (!(desc.friction >= 0.0f)) {
        return "friction must be >= 0";
    }
    if (!(desc.restitution >= 0.0f && desc.restitution <= 1.0f)) {
        return "restitution must be in [0, 1]";
    }
    if (!(desc.linearDamping >= 0.0f)) {
        return "linearDamping must be >= 0";
    }
    if (!(desc.angularDamping >= 0.0f)) {
        return "angularDamping must be >= 0";
    }
    return {};
}

// One swing axis of a JointDesc (docs/tasks/0014). Ranges are within [-pi, pi]
// as Jolt documents for both constraint types. A cone must be symmetric,
// because Jolt's cone limit is (ESwingType::Cone in SwingTwistConstraintPart.h);
// a pyramid range must contain 0 so the joint starts inside its limits.
std::string validateSwingRange(SwingShape shape, const std::string& name, float minRad,
                               float maxRad)
{
    if (!(minRad >= -kPi && minRad <= kPi)) {
        return name + "MinRad must be in [-pi, pi]";
    }
    if (!(maxRad >= -kPi && maxRad <= kPi)) {
        return name + "MaxRad must be in [-pi, pi]";
    }
    if (minRad > maxRad) {
        return name + "MinRad must be <= " + name + "MaxRad";
    }
    if (shape == SwingShape::Cone && minRad != -maxRad) {
        return name + "MinRad must be -" + name + "MaxRad for a cone";
    }
    if (shape == SwingShape::Pyramid && (minRad > 0.0f || maxRad < 0.0f)) {
        return name + " range must contain 0 for a pyramid";
    }
    return {};
}

// Body/handle checks are done by the world; this covers the geometric fields.
// Angle ranges are the ones Jolt documents for SwingTwistConstraintSettings
// and SixDOFConstraintSettings (both [-pi, pi]).
std::string validateJointGeometry(const JointDesc& desc)
{
    if (!isFinite(desc.anchorM)) {
        return "anchorM must be finite";
    }
    if (!isUnit(desc.twistAxis)) {
        return "twistAxis must be unit length";
    }
    if (!isUnit(desc.planeAxis)) {
        return "planeAxis must be unit length";
    }
    if (std::abs(dot(desc.twistAxis, desc.planeAxis)) > kAxisTolerance) {
        return "planeAxis must be perpendicular to twistAxis";
    }
    if (desc.swingShape != SwingShape::Cone && desc.swingShape != SwingShape::Pyramid) {
        return "swingShape must be Cone or Pyramid";
    }
    if (std::string error = validateSwingRange(desc.swingShape, "planeSwing", desc.planeSwingMinRad,
                                               desc.planeSwingMaxRad);
        !error.empty()) {
        return error;
    }
    if (std::string error = validateSwingRange(desc.swingShape, "normalSwing",
                                               desc.normalSwingMinRad, desc.normalSwingMaxRad);
        !error.empty()) {
        return error;
    }
    if (!(desc.twistMinRad >= -kPi && desc.twistMinRad <= kPi)) {
        return "twistMinRad must be in [-pi, pi]";
    }
    if (!(desc.twistMaxRad >= -kPi && desc.twistMaxRad <= kPi)) {
        return "twistMaxRad must be in [-pi, pi]";
    }
    if (desc.twistMinRad > desc.twistMaxRad) {
        return "twistMinRad must be <= twistMaxRad";
    }
    return {};
}

bool isValidMotor(const JointMotor& motor)
{
    // ">= 0" also rejects NaN. "Unlimited" torque is float max (the
    // JointMotor default), which is finite; infinity itself is rejected.
    return motor.stiffnessNmPerRad >= 0.0f && std::isfinite(motor.stiffnessNmPerRad) &&
           motor.dampingNmsPerRad >= 0.0f && std::isfinite(motor.dampingNmsPerRad) &&
           motor.maxTorqueNm >= 0.0f && std::isfinite(motor.maxTorqueNm);
}

std::string validateConfig(const WorldConfig& config)
{
    if (!(config.fixedStepSeconds > 0.0f) || !std::isfinite(config.fixedStepSeconds)) {
        return "fixedStepSeconds must be > 0";
    }
    if (!isFinite(config.gravityMps2)) {
        return "gravityMps2 must be finite";
    }
    if (config.maxBodies == 0 || config.maxBodies > JPH::BodyID::cMaxBodyIndex + 1) {
        return "maxBodies must be > 0 and within Jolt's body limit";
    }
    if (config.subStepsPerStep < 1 || config.subStepsPerStep > kMaxSubStepsPerStep) {
        return "subStepsPerStep must be in [1, 16]";
    }
    if (config.maxJoints == 0) {
        return "maxJoints must be > 0";
    }
    if (config.workerThreads < 1 || config.workerThreads > kMaxWorkerThreads) {
        return "workerThreads must be in [1, 64]";
    }
    return {};
}

JPH::ShapeRefC createJoltShape(const Shape& shape)
{
    JPH::ShapeSettings::ShapeResult result;
    if (const auto* box = std::get_if<BoxShape>(&shape)) {
        const Vec3 h = box->halfExtentsM;
        // Jolt rounds box corners by a convex radius that must not exceed any
        // half extent; keep its default unless the box is very thin.
        const float smallest = std::min({h.x, h.y, h.z});
        const float convexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * smallest);
        result = JPH::BoxShapeSettings(toJolt(h), convexRadius).Create();
    } else if (const auto* sphere = std::get_if<SphereShape>(&shape)) {
        result = JPH::SphereShapeSettings(sphere->radiusM).Create();
    } else {
        const auto& capsule = std::get<CapsuleShape>(shape);
        // Jolt capsules run along local Y; ours run along local Z (Z up).
        // Rotating +90 degrees about X turns +Y into +Z.
        const JPH::Quat yToZ = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * kPi);
        const JPH::RefConst<JPH::ShapeSettings> inner =
            new JPH::CapsuleShapeSettings(capsule.halfHeightM, capsule.radiusM);
        result = JPH::RotatedTranslatedShapeSettings(JPH::Vec3::sZero(), yToZ, inner).Create();
    }
    return result.HasError() ? nullptr : result.Get();
}

// --- Handle tables ------------------------------------------------------------
// Slots are reused after destruction; the generation counter goes up each time
// a slot is freed, so old handles never match a new occupant.

struct BodySlot {
    JPH::BodyID joltId;
    std::uint32_t generation = 1;
    bool alive = false;
    MotionType motion = MotionType::Static;
    bool kinematicTargetPending = false; // set by setKinematicTarget until the next step ends
};

// A cone joint is a SwingTwistConstraint; a pyramid joint is a
// SixDOFConstraint, because only SixDOFConstraintSettings passes asymmetric
// swing limits on to Jolt (SwingTwistConstraint::UpdateLimits always passes
// -half, +half; docs/tasks/0014).
struct JointSlot {
    JPH::Ref<JPH::TwoBodyConstraint> constraint;
    SwingShape shape = SwingShape::Cone; // says which of the two types `constraint` is
    std::uint32_t generation = 1;
    bool alive = false;
    std::uint32_t bodyA = 0; // slot indices, used to find joints attached to a body
    std::uint32_t bodyB = 0;
};

// A collision group: one Jolt GroupFilterTable shared (by reference count) by
// the group and every body in it, so bodies keep working after the group is
// destroyed. joltGroupId is a serial number, never reused, so a body of a
// destroyed group can never be mistaken for a member of a later group.
struct GroupSlot {
    JPH::Ref<JPH::GroupFilterTable> table;
    std::uint32_t generation = 1;
    bool alive = false;
    std::uint32_t subGroupCount = 0;
    JPH::CollisionGroup::GroupID joltGroupId = 0;
};

struct ListenerEntry {
    StepListener* listener = nullptr; // not owned, see StepListener
    StepListenerId id;
};

// Picks a free slot index (reusing freed ones first), or nullopt when full.
template <typename Slot>
std::optional<std::uint32_t> takeSlot(std::vector<Slot>& slots,
                                      std::vector<std::uint32_t>& freeList, std::uint32_t capacity)
{
    if (!freeList.empty()) {
        const std::uint32_t index = freeList.back();
        freeList.pop_back();
        return index;
    }
    if (slots.size() >= capacity) {
        return std::nullopt;
    }
    slots.emplace_back();
    return static_cast<std::uint32_t>(slots.size() - 1);
}

// --- The world ------------------------------------------------------------------

class JoltWorld final : public PhysicsWorld {
public:
    explicit JoltWorld(const WorldConfig& config);
    ~JoltWorld() override;
    JoltWorld(const JoltWorld&) = delete;
    JoltWorld& operator=(const JoltWorld&) = delete;
    JoltWorld(JoltWorld&&) = delete;
    JoltWorld& operator=(JoltWorld&&) = delete;

    CreateResult<BodyId> createBody(const BodyDesc& desc) override;
    bool destroyBody(BodyId body) override;
    CreateResult<JointId> createJoint(const JointDesc& desc) override;
    bool destroyJoint(JointId joint) override;

    CreateResult<CollisionGroupId> createCollisionGroup(std::uint32_t subGroupCount) override;
    bool setSubGroupsCollide(CollisionGroupId group, std::uint32_t a, std::uint32_t b,
                             bool collide) override;
    bool destroyCollisionGroup(CollisionGroupId group) override;

    bool setJointMotor(JointId joint, const JointMotor& motor) override;
    bool setJointPositionSteps(JointId joint, std::uint32_t steps) override;
    bool setJointTarget(JointId joint, Quat targetRotation) override
    {
        return setJointTarget(joint, targetRotation, Vec3{});
    }
    bool setJointTarget(JointId joint, Quat targetRotation,
                        Vec3 targetAngularVelocityRadps) override;

    bool applyImpulse(BodyId body, Vec3 impulseNs, Vec3 worldPointM) override;
    bool setVelocity(BodyId body, Vec3 linearMps, Vec3 angularRadps) override;
    bool setBodyDamping(BodyId body, float linearPerS, float angularPerS) override;
    bool setKinematicTarget(BodyId body, Vec3 positionM, Quat rotation) override;
    bool setMotionType(BodyId body, MotionType motion) override;
    std::optional<MotionType> getMotionType(BodyId body) const override;

    bool readBody(BodyId body, BodyState& out) const override;
    std::size_t readBodies(std::span<const BodyId> bodies, std::span<BodyState> out) const override;

    std::optional<RayHit> castRay(Vec3 originM, Vec3 directionTimesLengthM,
                                  BodyId ignore) const override;

    void step() override;
    [[nodiscard]] float fixedStepSeconds() const override { return config_.fixedStepSeconds; }
    StepListenerId addStepListener(StepListener& listener) override;
    bool removeStepListener(StepListenerId id) override;

private:
    [[nodiscard]] const BodySlot* findBody(BodyId body) const;
    [[nodiscard]] JointSlot* findJoint(JointId joint);
    [[nodiscard]] GroupSlot* findGroup(CollisionGroupId group);
    void destroyJointSlot(std::uint32_t index);
    void stopKinematicBodiesAtTargets();
    // Wakes both bodies of a joint so a changed motor takes effect.
    void wakeJointBodies(const JointSlot& slot);
    JPH::BodyInterface& bodies() { return system_.GetBodyInterface(); }
    [[nodiscard]] const JPH::BodyInterface& bodies() const { return system_.GetBodyInterface(); }

    // Declaration order matters: members are destroyed in reverse order, and
    // the runtime must outlive everything Jolt, the layer objects must
    // outlive the physics system that references them.
    detail::JoltRuntimeRef runtime_;
    WorldConfig config_;
    BroadPhaseLayerMap broadPhaseLayers_;
    ObjectVsBroadPhaseFilter objectVsBroadPhase_;
    ObjectPairFilter objectPairs_;
    JPH::TempAllocatorImpl tempAllocator_{kTempAllocatorBytes};
    std::unique_ptr<JPH::JobSystem> jobSystem_ =
        makeJobSystem(config_.workerThreads, config_.subStepsPerStep);
    JPH::PhysicsSystem system_;

    std::vector<BodySlot> bodySlots_;
    std::vector<std::uint32_t> freeBodySlots_;
    std::vector<JointSlot> jointSlots_;
    std::vector<std::uint32_t> freeJointSlots_;
    std::vector<GroupSlot> groupSlots_;
    std::vector<std::uint32_t> freeGroupSlots_;
    JPH::CollisionGroup::GroupID nextGroupId_ = 0;
    std::vector<std::uint32_t> kinematicTargets_; // body slot indices moved this step
    std::vector<ListenerEntry> listeners_;
    std::uint32_t nextListenerSerial_ = 0;
    bool inStep_ = false;
};

JoltWorld::JoltWorld(const WorldConfig& config) : config_(config)
{
    // maxBodies is capped by validateConfig, so this cannot overflow.
    const std::uint32_t maxPairs = std::max(kMinPairs, config.maxBodies * kPairsPerBody);
    // 0 body mutexes lets Jolt pick a default.
    system_.Init(config.maxBodies, 0, maxPairs, maxPairs, broadPhaseLayers_, objectVsBroadPhase_,
                 objectPairs_);
    system_.SetGravity(toJolt(config.gravityMps2));
    bodySlots_.reserve(config.maxBodies);
    jointSlots_.reserve(config.maxJoints);
    // Reserved up front so freeing a slot never allocates.
    freeBodySlots_.reserve(config.maxBodies);
    freeJointSlots_.reserve(config.maxJoints);
    // A group only matters with bodies in it, so maxBodies bounds them too.
    groupSlots_.reserve(config.maxBodies);
    freeGroupSlots_.reserve(config.maxBodies);
    kinematicTargets_.reserve(config.maxBodies);
}

JoltWorld::~JoltWorld()
{
    // Constraints reference bodies, so remove them first. No slot bookkeeping
    // here: the tables die with the world.
    for (const JointSlot& slot : jointSlots_) {
        if (slot.alive) {
            system_.RemoveConstraint(slot.constraint);
        }
    }
    for (const BodySlot& slot : bodySlots_) {
        if (slot.alive) {
            bodies().RemoveBody(slot.joltId);
            bodies().DestroyBody(slot.joltId);
        }
    }
}

const BodySlot* JoltWorld::findBody(BodyId body) const
{
    if (!body.isValid() || body.index >= bodySlots_.size()) {
        return nullptr;
    }
    const BodySlot& slot = bodySlots_[body.index];
    return (slot.alive && slot.generation == body.generation) ? &slot : nullptr;
}

JointSlot* JoltWorld::findJoint(JointId joint)
{
    if (!joint.isValid() || joint.index >= jointSlots_.size()) {
        return nullptr;
    }
    JointSlot& slot = jointSlots_[joint.index];
    return (slot.alive && slot.generation == joint.generation) ? &slot : nullptr;
}

CreateResult<BodyId> JoltWorld::createBody(const BodyDesc& desc)
{
    if (inStep_) {
        return failure<BodyId>(ErrorCode::CalledDuringStep,
                               "createBody cannot be called from a step listener");
    }
    if (std::string error = validateBody(desc); !error.empty()) {
        return failure<BodyId>(ErrorCode::InvalidDescription, std::move(error));
    }
    const GroupSlot* group = nullptr;
    if (desc.collisionGroup.isValid()) {
        group = findGroup(desc.collisionGroup);
        if (group == nullptr) {
            return failure<BodyId>(ErrorCode::InvalidHandle, "collisionGroup is not a live group");
        }
        if (desc.subGroup >= group->subGroupCount) {
            return failure<BodyId>(ErrorCode::InvalidDescription,
                                   "subGroup must be < the group's subGroupCount");
        }
    }
    const JPH::ShapeRefC shape = createJoltShape(desc.shape);
    if (shape == nullptr) {
        return failure<BodyId>(ErrorCode::InvalidDescription, "shape could not be created");
    }
    const std::optional<std::uint32_t> index =
        takeSlot(bodySlots_, freeBodySlots_, config_.maxBodies);
    if (!index) {
        return failure<BodyId>(ErrorCode::CapacityExceeded, "maxBodies reached");
    }

    JPH::EMotionType motion = JPH::EMotionType::Static;
    JPH::ObjectLayer layer = Layers::kNonMoving;
    if (desc.motion == MotionType::Dynamic) {
        motion = JPH::EMotionType::Dynamic;
        layer = Layers::kMoving;
    } else if (desc.motion == MotionType::Kinematic) {
        motion = JPH::EMotionType::Kinematic;
        layer = Layers::kMoving;
    }

    JPH::BodyCreationSettings settings(shape, toJoltR(desc.positionM),
                                       toJolt(desc.rotation).Normalized(), motion, layer);
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mLinearDamping = desc.linearDamping;
    settings.mAngularDamping = desc.angularDamping;
    settings.mUserData = *index; // lets ray hits find our handle
    if (group != nullptr) {
        settings.mCollisionGroup =
            JPH::CollisionGroup(group->table, group->joltGroupId, desc.subGroup);
    }
    if (desc.motion == MotionType::Dynamic ||
        (desc.motion == MotionType::Kinematic && desc.massKg > 0.0f)) {
        // Use the authored mass; Jolt derives inertia from the shape.
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.massKg;
    }

    const JPH::EActivation activation = desc.motion == MotionType::Static
                                            ? JPH::EActivation::DontActivate
                                            : JPH::EActivation::Activate;
    const JPH::BodyID joltId = bodies().CreateAndAddBody(settings, activation);
    if (joltId.IsInvalid()) {
        freeBodySlots_.push_back(*index);
        return failure<BodyId>(ErrorCode::CapacityExceeded, "Jolt body limit reached");
    }

    BodySlot& slot = bodySlots_[*index];
    slot.joltId = joltId;
    slot.alive = true;
    slot.motion = desc.motion;
    slot.kinematicTargetPending = false;
    return CreateResult<BodyId>{BodyId{*index, slot.generation}, ErrorCode::None, {}};
}

bool JoltWorld::destroyBody(BodyId body)
{
    if (inStep_ || findBody(body) == nullptr) {
        return false;
    }
    for (std::uint32_t i = 0; i < jointSlots_.size(); ++i) {
        const JointSlot& joint = jointSlots_[i];
        if (joint.alive && (joint.bodyA == body.index || joint.bodyB == body.index)) {
            destroyJointSlot(i);
        }
    }
    BodySlot& slot = bodySlots_[body.index];
    if (slot.kinematicTargetPending) {
        std::erase(kinematicTargets_, body.index);
        slot.kinematicTargetPending = false;
    }
    bodies().RemoveBody(slot.joltId);
    bodies().DestroyBody(slot.joltId);
    slot.alive = false;
    ++slot.generation;
    freeBodySlots_.push_back(body.index);
    return true;
}

CreateResult<JointId> JoltWorld::createJoint(const JointDesc& desc)
{
    if (inStep_) {
        return failure<JointId>(ErrorCode::CalledDuringStep,
                                "createJoint cannot be called from a step listener");
    }
    const BodySlot* bodyA = findBody(desc.bodyA);
    if (bodyA == nullptr) {
        return failure<JointId>(ErrorCode::InvalidHandle, "bodyA is not a live body");
    }
    const BodySlot* bodyB = findBody(desc.bodyB);
    if (bodyB == nullptr) {
        return failure<JointId>(ErrorCode::InvalidHandle, "bodyB is not a live body");
    }
    if (desc.bodyA == desc.bodyB) {
        return failure<JointId>(ErrorCode::InvalidDescription,
                                "bodyB must be a different body from bodyA");
    }
    if (std::string error = validateJointGeometry(desc); !error.empty()) {
        return failure<JointId>(ErrorCode::InvalidDescription, std::move(error));
    }
    const std::optional<std::uint32_t> index =
        takeSlot(jointSlots_, freeJointSlots_, config_.maxJoints);
    if (!index) {
        return failure<JointId>(ErrorCode::CapacityExceeded, "maxJoints reached");
    }

    const JPH::TwoBodyConstraintSettings* settings = nullptr;
    JPH::SwingTwistConstraintSettings cone;
    JPH::SixDOFConstraintSettings pyramid;
    if (desc.swingShape == SwingShape::Cone) {
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
    } else {
        // Same constraint space as the cone: X = twist, Y = plane x twist, and
        // Jolt makes Z = X x Y (SixDOFConstraint constructor), which is the
        // plane axis again. So targets and limits mean the same for both.
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

    JPH::TwoBodyConstraint* created =
        bodies().CreateConstraint(settings, bodyA->joltId, bodyB->joltId);
    if (created == nullptr) {
        freeJointSlots_.push_back(*index);
        return failure<JointId>(ErrorCode::InvalidHandle, "bodyA or bodyB could not be locked");
    }
    JointSlot& slot = jointSlots_[*index];
    // Jolt constraints are reference counted (JPH::Ref); the slot holds one
    // reference and the physics system another while the joint is added.
    slot.constraint = created;
    slot.shape = desc.swingShape;
    slot.alive = true;
    slot.bodyA = desc.bodyA.index;
    slot.bodyB = desc.bodyB.index;
    system_.AddConstraint(slot.constraint);
    return CreateResult<JointId>{JointId{*index, slot.generation}, ErrorCode::None, {}};
}

void JoltWorld::destroyJointSlot(std::uint32_t index)
{
    JointSlot& slot = jointSlots_[index];
    system_.RemoveConstraint(slot.constraint);
    slot.constraint = nullptr;
    slot.alive = false;
    ++slot.generation;
    freeJointSlots_.push_back(index);
}

bool JoltWorld::destroyJoint(JointId joint)
{
    if (inStep_ || findJoint(joint) == nullptr) {
        return false;
    }
    destroyJointSlot(joint.index);
    return true;
}

GroupSlot* JoltWorld::findGroup(CollisionGroupId group)
{
    if (!group.isValid() || group.index >= groupSlots_.size()) {
        return nullptr;
    }
    GroupSlot& slot = groupSlots_[group.index];
    return (slot.alive && slot.generation == group.generation) ? &slot : nullptr;
}

CreateResult<CollisionGroupId> JoltWorld::createCollisionGroup(std::uint32_t subGroupCount)
{
    if (inStep_) {
        return failure<CollisionGroupId>(
            ErrorCode::CalledDuringStep,
            "createCollisionGroup cannot be called from a step listener");
    }
    // A sub-group only matters with a body in it, so maxBodies bounds the
    // count, and kMaxSubGroups keeps Jolt's bit table from overflowing.
    if (subGroupCount == 0 || subGroupCount > std::min(config_.maxBodies, kMaxSubGroups)) {
        return failure<CollisionGroupId>(ErrorCode::InvalidDescription,
                                         "subGroupCount must be in [1, min(maxBodies, 65536)]");
    }
    if (nextGroupId_ == JPH::CollisionGroup::cInvalidGroup) {
        return failure<CollisionGroupId>(ErrorCode::CapacityExceeded,
                                         "collision group IDs are used up");
    }
    const std::optional<std::uint32_t> index =
        takeSlot(groupSlots_, freeGroupSlots_, config_.maxBodies);
    if (!index) {
        return failure<CollisionGroupId>(ErrorCode::CapacityExceeded,
                                         "too many collision groups (limit maxBodies)");
    }
    // Jolt objects are reference counted: the Ref takes ownership of the new
    // table at once (Jolt's idiom, as for the shape settings above), so this
    // is not the raw ownership the guide forbids. Everything collides at first.
    const JPH::Ref<JPH::GroupFilterTable> table = new JPH::GroupFilterTable(subGroupCount);
    GroupSlot& slot = groupSlots_[*index];
    slot.table = table;
    slot.alive = true;
    slot.subGroupCount = subGroupCount;
    slot.joltGroupId = nextGroupId_++;
    return CreateResult<CollisionGroupId>{
        CollisionGroupId{*index, slot.generation}, ErrorCode::None, {}};
}

bool JoltWorld::setSubGroupsCollide(CollisionGroupId group, std::uint32_t a, std::uint32_t b,
                                    bool collide)
{
    const GroupSlot* slot = findGroup(group);
    if (inStep_ || slot == nullptr || a == b || a >= slot->subGroupCount ||
        b >= slot->subGroupCount) {
        return false;
    }
    // GroupFilterTable expects the smaller sub-group first (GetBit asserts it).
    const std::uint32_t low = std::min(a, b);
    const std::uint32_t high = std::max(a, b);
    if (collide) {
        slot->table->EnableCollision(low, high);
    } else {
        slot->table->DisableCollision(low, high);
    }
    return true;
}

bool JoltWorld::destroyCollisionGroup(CollisionGroupId group)
{
    GroupSlot* slot = findGroup(group);
    if (inStep_ || slot == nullptr) {
        return false;
    }
    slot->table = nullptr; // member bodies keep their own references
    slot->alive = false;
    ++slot->generation;
    freeGroupSlots_.push_back(group.index);
    return true;
}

void JoltWorld::wakeJointBodies(const JointSlot& slot)
{
    for (const std::uint32_t bodyIndex : {slot.bodyA, slot.bodyB}) {
        const BodySlot& body = bodySlots_[bodyIndex];
        if (body.motion != MotionType::Static) {
            bodies().ActivateBody(body.joltId);
        }
    }
}

bool JoltWorld::setJointPositionSteps(JointId joint, std::uint32_t steps)
{
    const JointSlot* slot = findJoint(joint);
    if (inStep_ || slot == nullptr || steps < 1 || steps > kMaxJointPositionSteps) {
        return false;
    }
    // Both constraint types inherit the override from JPH::Constraint.
    slot->constraint->SetNumPositionStepsOverride(steps);
    return true;
}

bool JoltWorld::setJointMotor(JointId joint, const JointMotor& motor)
{
    const JointSlot* slot = findJoint(joint);
    if (slot == nullptr || !isValidMotor(motor)) {
        return false;
    }
    const bool off = motor.stiffnessNmPerRad == 0.0f && motor.dampingNmsPerRad == 0.0f;
    const auto configure = [&motor](JPH::MotorSettings& settings) {
        settings.mSpringSettings = JPH::SpringSettings(
            JPH::ESpringMode::StiffnessAndDamping, motor.stiffnessNmPerRad, motor.dampingNmsPerRad);
        settings.SetTorqueLimit(motor.maxTorqueNm);
    };
    // PositionAndVelocity with a zero target velocity applies
    // stiffness * angle error - damping * relative velocity, the same as
    // Position mode, but unlike Position mode it still damps when stiffness
    // is 0 (HasStiffness vs HasStiffnessOrDamping checks, in both
    // SwingTwistConstraint.cpp and SixDOFConstraint.cpp).
    const JPH::EMotorState state =
        off ? JPH::EMotorState::Off : JPH::EMotorState::PositionAndVelocity;
    if (slot->shape == SwingShape::Cone) {
        JPH_ASSERT(slot->constraint->GetSubType() == JPH::EConstraintSubType::SwingTwist);
        auto& constraint = static_cast<JPH::SwingTwistConstraint&>(*slot->constraint);
        if (!off) {
            configure(constraint.GetSwingMotorSettings());
            configure(constraint.GetTwistMotorSettings());
            constraint.SetTargetAngularVelocityCS(JPH::Vec3::sZero());
        }
        constraint.SetSwingMotorState(state);
        constraint.SetTwistMotorState(state);
    } else {
        // One motor per rotation axis, all with the same gains, standing in
        // for the cone's one swing and one twist motor.
        JPH_ASSERT(slot->constraint->GetSubType() == JPH::EConstraintSubType::SixDOF);
        auto& constraint = static_cast<JPH::SixDOFConstraint&>(*slot->constraint);
        using Axis = JPH::SixDOFConstraintSettings::EAxis;
        if (!off) {
            constraint.SetTargetAngularVelocityCS(JPH::Vec3::sZero());
        }
        for (const Axis axis : {Axis::RotationX, Axis::RotationY, Axis::RotationZ}) {
            if (!off) {
                configure(constraint.GetMotorSettings(axis));
            }
            constraint.SetMotorState(axis, state);
        }
    }
    wakeJointBodies(*slot);
    return true;
}

bool JoltWorld::setJointTarget(JointId joint, Quat targetRotation,
                               Vec3 targetAngularVelocityRadps)
{
    const JointSlot* slot = findJoint(joint);
    if (slot == nullptr || !isUnit(targetRotation) || !isFinite(targetAngularVelocityRadps)) {
        return false;
    }
    // Jolt's "constraint space" target: body B's joint frame relative to body
    // A's: X = twist axis, Y = plane x twist, Z = plane axis (Jolt builds it
    // that way in the SwingTwistConstraint and SixDOFConstraint constructors).
    // Both clamp the target to the limits (SetTargetOrientationCS).
    const JPH::Quat target = toJolt(targetRotation).Normalized();
    const JPH::Vec3 targetOmega = toJolt(targetAngularVelocityRadps);
    if (slot->shape == SwingShape::Cone) {
        JPH_ASSERT(slot->constraint->GetSubType() == JPH::EConstraintSubType::SwingTwist);
        auto& constraint = static_cast<JPH::SwingTwistConstraint&>(*slot->constraint);
        constraint.SetTargetOrientationCS(target);
        constraint.SetTargetAngularVelocityCS(targetOmega);
    } else {
        JPH_ASSERT(slot->constraint->GetSubType() == JPH::EConstraintSubType::SixDOF);
        auto& constraint = static_cast<JPH::SixDOFConstraint&>(*slot->constraint);
        constraint.SetTargetOrientationCS(target);
        constraint.SetTargetAngularVelocityCS(targetOmega);
    }
    wakeJointBodies(*slot);
    return true;
}

bool JoltWorld::applyImpulse(BodyId body, Vec3 impulseNs, Vec3 worldPointM)
{
    const BodySlot* slot = findBody(body);
    if (slot == nullptr || slot->motion != MotionType::Dynamic) {
        return false;
    }
    bodies().AddImpulse(slot->joltId, toJolt(impulseNs), toJoltR(worldPointM));
    return true;
}

bool JoltWorld::setVelocity(BodyId body, Vec3 linearMps, Vec3 angularRadps)
{
    const BodySlot* slot = findBody(body);
    if (slot == nullptr || slot->motion == MotionType::Static) {
        return false;
    }
    bodies().SetLinearAndAngularVelocity(slot->joltId, toJolt(linearMps), toJolt(angularRadps));
    return true;
}

bool JoltWorld::setBodyDamping(BodyId body, float linearPerS, float angularPerS)
{
    const BodySlot* slot = findBody(body);
    const bool valid = linearPerS >= 0.0f && std::isfinite(linearPerS) && angularPerS >= 0.0f &&
                       std::isfinite(angularPerS);
    if (inStep_ || slot == nullptr || slot->motion != MotionType::Dynamic || !valid) {
        return false;
    }
    // Damping lives in the body's motion properties, reached through a write
    // lock (MotionProperties::SetLinearDamping / SetAngularDamping).
    const JPH::BodyLockWrite lock(system_.GetBodyLockInterface(), slot->joltId);
    if (!lock.Succeeded()) {
        return false;
    }
    JPH::MotionProperties* motion = lock.GetBody().GetMotionProperties();
    motion->SetLinearDamping(linearPerS);
    motion->SetAngularDamping(angularPerS);
    return true;
}

bool JoltWorld::setKinematicTarget(BodyId body, Vec3 positionM, Quat rotation)
{
    const BodySlot* found = findBody(body);
    if (found == nullptr || found->motion != MotionType::Kinematic || !isUnit(rotation)) {
        return false;
    }
    BodySlot& slot = bodySlots_[body.index];
    bodies().MoveKinematic(slot.joltId, toJoltR(positionM), toJolt(rotation).Normalized(),
                           config_.fixedStepSeconds);
    if (!slot.kinematicTargetPending) {
        // Capacity reserved up front, and each body is listed at most once.
        slot.kinematicTargetPending = true;
        kinematicTargets_.push_back(body.index);
    }
    return true;
}

bool JoltWorld::setMotionType(BodyId body, MotionType motion)
{
    const BodySlot* found = findBody(body);
    if (inStep_ || found == nullptr) {
        return false;
    }
    if (found->motion == motion) {
        return true;
    }
    if (found->motion == MotionType::Static) {
        return false;
    }

    BodySlot& slot = bodySlots_[body.index];
    JPH::EMotionType joltMotion;
    switch (motion) {
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

    if (motion == MotionType::Static) {
        bodies().DeactivateBody(slot.joltId);
        bodies().SetObjectLayer(slot.joltId, Layers::kNonMoving);
    } else if (slot.motion == MotionType::Static) {
        bodies().SetObjectLayer(slot.joltId, Layers::kMoving);
    }

    const JPH::EActivation activation = (motion == MotionType::Dynamic)
                                            ? JPH::EActivation::Activate
                                            : JPH::EActivation::DontActivate;
    bodies().SetMotionType(slot.joltId, joltMotion, activation);

    if (slot.kinematicTargetPending && motion != MotionType::Kinematic) {
        slot.kinematicTargetPending = false;
        std::erase(kinematicTargets_, body.index);
    }

    slot.motion = motion;
    return true;
}

std::optional<MotionType> JoltWorld::getMotionType(BodyId body) const
{
    const BodySlot* slot = findBody(body);
    if (slot == nullptr) {
        return std::nullopt;
    }
    return slot->motion;
}

bool JoltWorld::readBody(BodyId body, BodyState& out) const
{
    const BodySlot* slot = findBody(body);
    if (slot == nullptr) {
        return false;
    }
    const JPH::BodyInterface& bi = bodies();
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

std::size_t JoltWorld::readBodies(std::span<const BodyId> ids, std::span<BodyState> out) const
{
    const std::size_t count = std::min(ids.size(), out.size());
    std::size_t read = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (readBody(ids[i], out[i])) {
            ++read;
        }
    }
    return read;
}

std::optional<RayHit> JoltWorld::castRay(Vec3 originM, Vec3 directionTimesLengthM,
                                         BodyId ignore) const
{
    const JPH::RRayCast ray{toJoltR(originM), toJolt(directionTimesLengthM)};
    JPH::RayCastResult hit;
    const BodySlot* ignored = findBody(ignore);
    const JPH::IgnoreSingleBodyFilter ignoreFilter(ignored != nullptr ? ignored->joltId
                                                                      : JPH::BodyID());
    const bool found = system_.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, ignoreFilter);
    if (!found) {
        return std::nullopt;
    }

    const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
    const JPH::BodyLockRead lock(system_.GetBodyLockInterface(), hit.mBodyID);
    if (!lock.Succeeded()) {
        return std::nullopt;
    }
    const JPH::Body& body = lock.GetBody();
    const auto index = static_cast<std::uint32_t>(body.GetUserData());

    RayHit result;
    result.body = BodyId{index, bodySlots_[index].generation};
    result.pointM = fromJoltR(point);
    result.normal = fromJolt(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
    result.fraction = hit.mFraction;
    return result;
}

void JoltWorld::step()
{
    if (inStep_) {
        return; // a listener called step(): ignore rather than recurse
    }
    const float dt = config_.fixedStepSeconds;
    inStep_ = true;
    // Only listeners registered before this step are called, in both phases.
    // A listener added during the step starts next step; one removed during
    // the step is nulled out (not erased) so indices stay put, and is skipped.
    const std::size_t listenerCount = listeners_.size();
    for (std::size_t i = 0; i < listenerCount; ++i) {
        if (listeners_[i].listener != nullptr) {
            listeners_[i].listener->onPreStep(*this, dt);
        }
    }
    // Jolt divides the step into this many collision and integration steps
    // (PhysicsSystem::Update, inCollisionSteps).
    const auto collisionSteps = static_cast<int>(config_.subStepsPerStep);
    system_.Update(dt, collisionSteps, &tempAllocator_, jobSystem_.get());
    stopKinematicBodiesAtTargets();
    for (std::size_t i = 0; i < listenerCount; ++i) {
        if (listeners_[i].listener != nullptr) {
            listeners_[i].listener->onPostStep(*this, dt);
        }
    }
    std::erase_if(listeners_, [](const ListenerEntry& entry) { return entry.listener == nullptr; });
    inStep_ = false;
}

void JoltWorld::stopKinematicBodiesAtTargets()
{
    // MoveKinematic works by giving the body the velocity that reaches the
    // target in one step; Jolt keeps that velocity afterwards. Zero it so the
    // body stays at its target until it is moved again.
    for (const std::uint32_t index : kinematicTargets_) {
        BodySlot& slot = bodySlots_[index];
        if (slot.alive && slot.kinematicTargetPending) {
            bodies().SetLinearAndAngularVelocity(slot.joltId, JPH::Vec3::sZero(),
                                                 JPH::Vec3::sZero());
            slot.kinematicTargetPending = false;
        }
    }
    kinematicTargets_.clear();
}

StepListenerId JoltWorld::addStepListener(StepListener& listener)
{
    // Serial numbers are never reused, so a removed id never matches again.
    const StepListenerId id{nextListenerSerial_++, 1};
    listeners_.push_back(ListenerEntry{&listener, id});
    return id;
}

bool JoltWorld::removeStepListener(StepListenerId id)
{
    const auto it =
        std::find_if(listeners_.begin(), listeners_.end(),
                     [id](const ListenerEntry& e) { return e.listener != nullptr && e.id == id; });
    if (it == listeners_.end()) {
        return false;
    }
    if (inStep_) {
        it->listener = nullptr; // erased after the step, see step()
    } else {
        listeners_.erase(it);
    }
    return true;
}

} // namespace

std::unique_ptr<PhysicsWorld> createJoltWorld(const WorldConfig& config, std::string* errorOut)
{
    if (std::string error = validateConfig(config); !error.empty()) {
        if (errorOut != nullptr) {
            *errorOut = std::move(error);
        }
        return nullptr;
    }
    return std::make_unique<JoltWorld>(config);
}

} // namespace opas::physics::jolt
