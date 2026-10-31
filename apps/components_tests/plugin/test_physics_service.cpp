// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include <components/plugin/host_impl.hpp>
#include <components/plugin/physics_service.hpp>
#include <openeden/physics.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
    using namespace openeden::sdk;
    using openeden::plugin::HostImpl;
    using openeden::plugin::PhysicsServiceImpl;

    // Decision 0003: the service only ever attaches to a world the host owns.
    static_assert(!std::is_default_constructible_v<PhysicsServiceImpl>);
    static_assert(!std::is_constructible_v<PhysicsServiceImpl, std::nullptr_t>);
    static_assert(!std::is_constructible_v<PhysicsServiceImpl, JPH::PhysicsSystem*>);
    static_assert(std::is_constructible_v<PhysicsServiceImpl, JPH::PhysicsSystem&>);
    static_assert(std::is_base_of_v<PhysicsQueryService, PhysicsServiceImpl>);

    // Object layers are bit patterns (physics_service.hpp), like MWPhysics::Layers.
    constexpr JPH::ObjectLayer kLayerZero = 0;
    constexpr JPH::ObjectLayer kLayerStatic = 1 << 0;
    constexpr JPH::ObjectLayer kLayerMoving = 1 << 1;
    constexpr QueryMask kMaskStatic = 1u << 0;
    constexpr QueryMask kMaskMoving = 1u << 1;
    constexpr QueryMask kMaskUnused = 1u << 5;

    const JPH::BroadPhaseLayer kBroadPhaseStatic{ 0 };
    const JPH::BroadPhaseLayer kBroadPhaseMoving{ 1 };

    // Jolt's solid-shape casts converge to about 1e-4 m (ShapeCastSettings), and values are floats at the interface.
    // A fraction of 1e-3 is 1 cm along the 10 m casts used here.
    constexpr float kPointTolerance = 2e-3f;
    constexpr float kNormalTolerance = 1e-2f;
    constexpr float kFractionTolerance = 1e-3f;

    // sin and cos of 45 degrees: the parts of a 90 degree rotation quaternion.
    constexpr float kSinHalf90 = 0.70710678f;

    constexpr PhysicsVec3 kOrigin{ 0.0f, 0.0f, 0.0f };
    constexpr PhysicsVec3 kTenMetersX{ 10.0f, 0.0f, 0.0f };
    constexpr PhysicsVec3 kTenMetersDownZ{ 0.0f, 0.0f, -10.0f };
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();

    void ensureJoltInitialized()
    {
        // The test binary's main() normally did this already. The Factory is a process-lifetime singleton owned by
        // Jolt's global pointer, so it is never torn down: other tests in this binary share it.
        if (JPH::Factory::sInstance != nullptr)
            return;
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    class TestBroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
    {
    public:
        unsigned int GetNumBroadPhaseLayers() const override { return 2; }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
        {
            return layer == kLayerMoving ? kBroadPhaseMoving : kBroadPhaseStatic;
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
        {
            return layer == kBroadPhaseMoving ? "MOVING" : "STATIC";
        }
#endif
    };

    class TestObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
    };

    class TestObjectPairs final : public JPH::ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override { return true; }
    };

    /// A small Jolt world the test owns, standing in for the host's. The test creates bodies directly through
    /// BodyInterface because the service cannot. The world is never stepped.
    class TestWorld
    {
    public:
        TestWorld()
        {
            ensureJoltInitialized();
            mSystem.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, mBroadPhaseLayers, mObjectVsBroadPhase,
                mObjectPairs);
        }

        JPH::PhysicsSystem& system() { return mSystem; }
        JPH::BodyInterface& bodies() { return mSystem.GetBodyInterface(); }

        JPH::BodyID addBox(JPH::Vec3Arg halfExtents, JPH::RVec3Arg position, JPH::ObjectLayer layer = kLayerStatic,
            JPH::EMotionType motion = JPH::EMotionType::Static, JPH::QuatArg rotation = JPH::Quat::sIdentity(),
            JPH::EActivation activation = JPH::EActivation::DontActivate)
        {
            // Jolt shapes are reference counted: new + RefConst is its own ownership idiom, and the body keeps a
            // reference. Half extents here stay above BoxShape's default 0.05 m convex radius.
            const JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(halfExtents);
            const JPH::BodyCreationSettings settings(shape, position, rotation, motion, layer);
            return bodies().CreateAndAddBody(settings, activation);
        }

        void destroy(const JPH::BodyID& id)
        {
            bodies().RemoveBody(id);
            bodies().DestroyBody(id);
        }

    private:
        static constexpr unsigned int kMaxBodies = 64;
        static constexpr unsigned int kMaxBodyPairs = 64;
        static constexpr unsigned int kMaxContactConstraints = 64;

        TestBroadPhaseLayers mBroadPhaseLayers;
        TestObjectVsBroadPhase mObjectVsBroadPhase;
        TestObjectPairs mObjectPairs;
        JPH::PhysicsSystem mSystem;
    };

    const JPH::Vec3 kUnitBoxHalfExtents(0.5f, 0.5f, 0.5f);

    /// Boxes of half extent 0.5 at x = 3 (near face x = 2.5) and x = 6 (near face x = 5.5).
    struct TwoBoxes
    {
        JPH::BodyID mNear;
        JPH::BodyID mFar;

        explicit TwoBoxes(TestWorld& world)
            : mNear(world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0)))
            , mFar(world.addBox(kUnitBoxHalfExtents, JPH::RVec3(6.0, 0.0, 0.0)))
        {
        }
    };

    /// Static floor whose top surface is z = 0.
    JPH::BodyID addFloor(TestWorld& world)
    {
        return world.addBox(JPH::Vec3(20.0f, 20.0f, 0.5f), JPH::RVec3(0.0, 0.0, -0.5));
    }

    void expectVecNear(const PhysicsVec3& actual, const PhysicsVec3& expected, float tolerance)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
        EXPECT_NEAR(actual.z, expected.z, tolerance);
    }

    /// A state no real body in these tests has, to detect writes that should not happen.
    BodyState sentinelState()
    {
        BodyState state;
        state.positionM = { 123.0f, 456.0f, 789.0f };
        state.rotation = { 0.5f, 0.5f, 0.5f, 0.5f };
        state.linearVelocityMps = { -1.0f, -2.0f, -3.0f };
        state.angularVelocityRadps = { -4.0f, -5.0f, -6.0f };
        state.awake = true;
        return state;
    }

    void expectSameState(const BodyState& actual, const BodyState& expected)
    {
        EXPECT_EQ(actual.positionM, expected.positionM);
        EXPECT_EQ(actual.rotation, expected.rotation);
        EXPECT_EQ(actual.linearVelocityMps, expected.linearVelocityMps);
        EXPECT_EQ(actual.angularVelocityRadps, expected.angularVelocityRadps);
        EXPECT_EQ(actual.awake, expected.awake);
    }

    void expectHandleRejected(const PhysicsServiceImpl& service, BodyId id)
    {
        EXPECT_FALSE(service.isBodyValid(id));
        EXPECT_FALSE(service.getMotionType(id).has_value());

        BodyState out = sentinelState();
        EXPECT_FALSE(service.readBody(id, out));
        expectSameState(out, sentinelState());

        std::array<BodyState, 1> outs{ sentinelState() };
        const std::array<BodyId, 1> ids{ id };
        EXPECT_EQ(service.readBodies(ids, outs), 0u);
        expectSameState(outs[0], sentinelState());
    }

    bool bitIdentical(float a, float b)
    {
        std::uint32_t ua = 0;
        std::uint32_t ub = 0;
        std::memcpy(&ua, &a, sizeof(float));
        std::memcpy(&ub, &b, sizeof(float));
        return ua == ub;
    }

    bool bitIdentical(const PhysicsVec3& a, const PhysicsVec3& b)
    {
        return bitIdentical(a.x, b.x) && bitIdentical(a.y, b.y) && bitIdentical(a.z, b.z);
    }

    bool bitIdentical(const PhysicsQuat& a, const PhysicsQuat& b)
    {
        return bitIdentical(a.x, b.x) && bitIdentical(a.y, b.y) && bitIdentical(a.z, b.z) && bitIdentical(a.w, b.w);
    }

    bool bitIdentical(const BodyState& a, const BodyState& b)
    {
        return bitIdentical(a.positionM, b.positionM) && bitIdentical(a.rotation, b.rotation)
            && bitIdentical(a.linearVelocityMps, b.linearVelocityMps)
            && bitIdentical(a.angularVelocityRadps, b.angularVelocityRadps) && a.awake == b.awake;
    }

    bool bitIdentical(const std::optional<RayHit>& a, const std::optional<RayHit>& b)
    {
        if (a.has_value() != b.has_value())
            return false;
        if (!a.has_value())
            return true;
        return a->body == b->body && bitIdentical(a->pointM, b->pointM) && bitIdentical(a->normal, b->normal)
            && bitIdentical(a->fraction, b->fraction);
    }

    // ---- The world stays the host's --------------------------------------------------------------------------------

    struct BodySnapshot
    {
        JPH::RVec3 mPosition;
        JPH::Quat mRotation;
        JPH::Vec3 mLinearVelocity;
        JPH::Vec3 mAngularVelocity;
        bool mActive = false;
        JPH::EMotionType mMotion = JPH::EMotionType::Static;
    };

    std::vector<BodySnapshot> snapshot(TestWorld& world, std::span<const JPH::BodyID> ids)
    {
        std::vector<BodySnapshot> out;
        for (const JPH::BodyID& id : ids)
        {
            JPH::BodyInterface& bodies = world.bodies();
            out.push_back({ bodies.GetPosition(id), bodies.GetRotation(id), bodies.GetLinearVelocity(id),
                bodies.GetAngularVelocity(id), bodies.IsActive(id), bodies.GetMotionType(id) });
        }
        return out;
    }

    TEST(PhysicsServiceTest, AttachingAndQueryingDoesNotChangeTheWorld)
    {
        TestWorld world;
        const JPH::BodyID staticBody = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0));
        const JPH::BodyID dynamicBody = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(6.0, 1.0, 0.0), kLayerMoving,
            JPH::EMotionType::Dynamic, JPH::Quat::sIdentity(), JPH::EActivation::Activate);
        const JPH::BodyID sleepingBody = world.addBox(
            kUnitBoxHalfExtents, JPH::RVec3(9.0, 2.0, 0.0), kLayerMoving, JPH::EMotionType::Dynamic);
        world.bodies().SetLinearAndAngularVelocity(
            dynamicBody, JPH::Vec3(1.0f, 2.0f, 3.0f), JPH::Vec3(0.1f, 0.2f, 0.3f));
        const std::array jphIds{ staticBody, dynamicBody, sleepingBody };

        const unsigned int bodiesBefore = world.system().GetNumBodies();
        const std::uint32_t activeBefore = world.system().GetNumActiveBodies(JPH::EBodyType::RigidBody);
        const std::vector<BodySnapshot> before = snapshot(world, jphIds);

        {
            PhysicsServiceImpl service(world.system());
            const std::array ids{ PhysicsServiceImpl::toBodyId(staticBody), PhysicsServiceImpl::toBodyId(dynamicBody),
                PhysicsServiceImpl::toBodyId(sleepingBody) };

            for (const BodyId id : ids)
            {
                EXPECT_TRUE(service.isBodyValid(id));
                EXPECT_TRUE(service.getMotionType(id).has_value());
                BodyState state;
                EXPECT_TRUE(service.readBody(id, state));
            }
            std::array<BodyState, 3> states{};
            EXPECT_EQ(service.readBodies(ids, states), 3u);
            EXPECT_TRUE(service.castRay(kOrigin, kTenMetersX).has_value());
            EXPECT_TRUE(service.castShape(SphereShape{ 0.5f }, PhysicsQuat{}, kOrigin, kTenMetersX).has_value());
            EXPECT_TRUE(
                service.castShape(BoxShape{ { 0.5f, 0.5f, 0.5f } }, PhysicsQuat{}, kOrigin, kTenMetersX).has_value());
            EXPECT_TRUE(service.castShape(CapsuleShape{ 0.5f, 0.25f }, PhysicsQuat{}, kOrigin, kTenMetersX).has_value());
            service.publishQueryMask("static", kMaskStatic);
            EXPECT_EQ(service.queryMask("static"), kMaskStatic);
        }

        EXPECT_EQ(world.system().GetNumBodies(), bodiesBefore);
        EXPECT_EQ(world.system().GetNumActiveBodies(JPH::EBodyType::RigidBody), activeBefore);
        const std::vector<BodySnapshot> after = snapshot(world, jphIds);
        ASSERT_EQ(after.size(), before.size());
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            // Exact comparison on purpose: nothing may have touched these values at all.
            EXPECT_TRUE(after[i].mPosition == before[i].mPosition) << "body " << i;
            EXPECT_TRUE(after[i].mRotation == before[i].mRotation) << "body " << i;
            EXPECT_TRUE(after[i].mLinearVelocity == before[i].mLinearVelocity) << "body " << i;
            EXPECT_TRUE(after[i].mAngularVelocity == before[i].mAngularVelocity) << "body " << i;
            EXPECT_EQ(after[i].mActive, before[i].mActive) << "body " << i;
            EXPECT_TRUE(after[i].mMotion == before[i].mMotion) << "body " << i;
        }
    }

    // Added by the implementer after review: the game's world is not in meters, and most of it is meshes.
    TEST(PhysicsServiceTest, WorldUnitsConvertToMetersAtTheBoundary)
    {
        // A box 70 world units from the origin, half extent 7, at 10 world units per meter.
        constexpr float kUnitsPerMeter = 10.0f;
        TestWorld world;
        const JPH::BodyID box = world.addBox(JPH::Vec3(7.0f, 7.0f, 7.0f), JPH::RVec3(70.0, 0.0, 0.0), kLayerMoving,
            JPH::EMotionType::Dynamic);
        world.bodies().SetLinearVelocity(box, JPH::Vec3(20.0f, 0.0f, 0.0f));
        const PhysicsServiceImpl service(world.system(), kUnitsPerMeter);

        BodyState state;
        ASSERT_TRUE(service.readBody(PhysicsServiceImpl::toBodyId(box), state));
        expectVecNear(state.positionM, { 7.0f, 0.0f, 0.0f }, 1e-4f);
        expectVecNear(state.linearVelocityMps, { 2.0f, 0.0f, 0.0f }, 1e-4f);

        // The near face is at 6.3 m: a 10 m ray hits at 0.63.
        const auto ray = service.castRay({ 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f });
        ASSERT_TRUE(ray.has_value());
        expectVecNear(ray->pointM, { 6.3f, 0.0f, 0.0f }, 1e-3f);
        expectVecNear(ray->normal, { -1.0f, 0.0f, 0.0f }, 1e-3f);
        EXPECT_NEAR(ray->fraction, 0.63f, 1e-3f);

        // A sphere of radius 0.3 m stops 0.3 m short of that face: at 0.6 of a 10 m sweep.
        const auto sphere = service.castShape(SphereShape{ 0.3f }, {}, { 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f });
        ASSERT_TRUE(sphere.has_value());
        EXPECT_NEAR(sphere->fraction, 0.6f, 1e-3f);
        expectVecNear(sphere->pointM, { 6.3f, 0.0f, 0.0f }, 1e-3f);
    }

    TEST(PhysicsServiceTest, ValuesThatOverflowInWorldUnitsFindNothing)
    {
        // Finite in meters, infinite once multiplied by 10.
        constexpr float kHuge = std::numeric_limits<float>::max();
        TestWorld world;
        const PhysicsServiceImpl service(world.system(), 10.0f);
        EXPECT_FALSE(service.castRay({ kHuge, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }).has_value());
        EXPECT_FALSE(service.castRay({ 0.0f, 0.0f, 0.0f }, { kHuge, 0.0f, 0.0f }).has_value());
        EXPECT_FALSE(service.castShape(SphereShape{ kHuge }, {}, {}, { 1.0f, 0.0f, 0.0f }).has_value());
        EXPECT_FALSE(service.castShape(SphereShape{ 1.0f }, {}, {}, { kHuge, 0.0f, 0.0f }).has_value());
    }

    TEST(PhysicsServiceTest, CastsCountTheBackOfAMesh)
    {
        // One triangle in the plane x = 3, its front facing -X (Jolt: counter-clockwise from the front).
        TestWorld world;
        JPH::TriangleList triangles;
        triangles.push_back(JPH::Triangle(JPH::Float3(3.0f, -5.0f, -5.0f), JPH::Float3(3.0f, 5.0f, -5.0f),
            JPH::Float3(3.0f, 0.0f, 5.0f)));
        JPH::MeshShapeSettings mesh(triangles);
        const JPH::BodyCreationSettings settings(
            mesh.Create().Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, kLayerStatic);
        const JPH::BodyID body = world.bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        const PhysicsServiceImpl service(world.system());
        const BodyId id = PhysicsServiceImpl::toBodyId(body);

        for (const float side : { -1.0f, 1.0f })
        {
            const PhysicsVec3 start{ 3.0f + side * 3.0f, 0.0f, 0.0f };
            const PhysicsVec3 towards{ -side * 10.0f, 0.0f, 0.0f };

            const auto ray = service.castRay(start, towards);
            ASSERT_TRUE(ray.has_value());
            EXPECT_EQ(ray->body, id);
            EXPECT_NEAR(ray->fraction, 0.3f, 1e-3f);

            // A sphere of radius 0.5 touches 0.5 m earlier, and its normal always faces the cast.
            const auto sphere = service.castShape(SphereShape{ 0.5f }, {}, start, towards);
            ASSERT_TRUE(sphere.has_value());
            EXPECT_EQ(sphere->body, id);
            EXPECT_NEAR(sphere->fraction, 0.25f, 1e-3f);
            EXPECT_NEAR(sphere->normal.x, side, 1e-3f);
        }
    }

    TEST(PhysicsServiceTest, ServiceSeesBodiesAddedAfterAttaching)
    {
        TestWorld world;
        const PhysicsServiceImpl service(world.system());
        EXPECT_FALSE(service.castRay(kOrigin, kTenMetersX).has_value());

        const JPH::BodyID box = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0));
        EXPECT_TRUE(service.isBodyValid(PhysicsServiceImpl::toBodyId(box)));
        EXPECT_TRUE(service.castRay(kOrigin, kTenMetersX).has_value());
    }

    // ---- Reading bodies --------------------------------------------------------------------------------------------

    TEST(PhysicsServiceTest, ReadsStaticBody)
    {
        TestWorld world;
        const JPH::Quat rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), 0.5f);
        const JPH::BodyID body = world.addBox(
            kUnitBoxHalfExtents, JPH::RVec3(1.0, -2.0, 3.0), kLayerStatic, JPH::EMotionType::Static, rotation);
        const PhysicsServiceImpl service(world.system());
        const BodyId id = PhysicsServiceImpl::toBodyId(body);

        EXPECT_TRUE(id.isValid());
        EXPECT_TRUE(service.isBodyValid(id));
        EXPECT_EQ(service.getMotionType(id), MotionType::Static);

        BodyState state = sentinelState();
        ASSERT_TRUE(service.readBody(id, state));
        expectVecNear(state.positionM, { 1.0f, -2.0f, 3.0f }, 1e-5f);
        EXPECT_NEAR(state.rotation.x, rotation.GetX(), 1e-6f);
        EXPECT_NEAR(state.rotation.y, rotation.GetY(), 1e-6f);
        EXPECT_NEAR(state.rotation.z, rotation.GetZ(), 1e-6f);
        EXPECT_NEAR(state.rotation.w, rotation.GetW(), 1e-6f);
        expectVecNear(state.linearVelocityMps, kOrigin, 1e-6f);
        expectVecNear(state.angularVelocityRadps, kOrigin, 1e-6f);
        EXPECT_FALSE(state.awake);
    }

    TEST(PhysicsServiceTest, ReadsDynamicBodyAwakeAndAsleep)
    {
        TestWorld world;
        const JPH::Quat rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 1.0f);
        const JPH::BodyID awake = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(4.0, 5.0, 6.0), kLayerMoving,
            JPH::EMotionType::Dynamic, rotation, JPH::EActivation::Activate);
        world.bodies().SetLinearAndAngularVelocity(awake, JPH::Vec3(1.0f, 2.0f, 3.0f), JPH::Vec3(0.1f, 0.2f, 0.3f));
        const JPH::BodyID asleep
            = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(-4.0, 5.0, 6.0), kLayerMoving, JPH::EMotionType::Dynamic);
        const PhysicsServiceImpl service(world.system());

        EXPECT_EQ(service.getMotionType(PhysicsServiceImpl::toBodyId(awake)), MotionType::Dynamic);

        BodyState state;
        ASSERT_TRUE(service.readBody(PhysicsServiceImpl::toBodyId(awake), state));
        expectVecNear(state.positionM, { 4.0f, 5.0f, 6.0f }, 1e-5f);
        EXPECT_NEAR(state.rotation.x, rotation.GetX(), 1e-6f);
        EXPECT_NEAR(state.rotation.w, rotation.GetW(), 1e-6f);
        expectVecNear(state.linearVelocityMps, { 1.0f, 2.0f, 3.0f }, 1e-5f);
        expectVecNear(state.angularVelocityRadps, { 0.1f, 0.2f, 0.3f }, 1e-5f);
        EXPECT_TRUE(state.awake);

        ASSERT_TRUE(service.readBody(PhysicsServiceImpl::toBodyId(asleep), state));
        expectVecNear(state.positionM, { -4.0f, 5.0f, 6.0f }, 1e-5f);
        expectVecNear(state.linearVelocityMps, kOrigin, 1e-6f);
        EXPECT_FALSE(state.awake);
    }

    TEST(PhysicsServiceTest, ReadsKinematicBody)
    {
        TestWorld world;
        const JPH::BodyID body = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(0.0, 4.0, 1.0), kLayerMoving,
            JPH::EMotionType::Kinematic, JPH::Quat::sIdentity(), JPH::EActivation::Activate);
        world.bodies().SetLinearAndAngularVelocity(body, JPH::Vec3(0.0f, -1.0f, 0.0f), JPH::Vec3(0.0f, 0.0f, 0.5f));
        const PhysicsServiceImpl service(world.system());
        const BodyId id = PhysicsServiceImpl::toBodyId(body);

        EXPECT_EQ(service.getMotionType(id), MotionType::Kinematic);
        BodyState state;
        ASSERT_TRUE(service.readBody(id, state));
        expectVecNear(state.positionM, { 0.0f, 4.0f, 1.0f }, 1e-5f);
        expectVecNear(state.linearVelocityMps, { 0.0f, -1.0f, 0.0f }, 1e-5f);
        expectVecNear(state.angularVelocityRadps, { 0.0f, 0.0f, 0.5f }, 1e-5f);
        EXPECT_TRUE(state.awake);
    }

    TEST(PhysicsServiceTest, BatchReadReturnsValidCountWithStaleIdInTheMiddle)
    {
        TestWorld world;
        const JPH::BodyID first = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(1.0, 0.0, 0.0));
        const JPH::BodyID stale = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(2.0, 0.0, 0.0));
        const JPH::BodyID last = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0));
        const BodyId staleId = PhysicsServiceImpl::toBodyId(stale);
        world.destroy(stale);
        const PhysicsServiceImpl service(world.system());

        const std::array ids{ PhysicsServiceImpl::toBodyId(first), staleId, PhysicsServiceImpl::toBodyId(last) };
        std::array<BodyState, 3> states{ sentinelState(), sentinelState(), sentinelState() };

        EXPECT_EQ(service.readBodies(ids, states), 2u);
        // out[i] belongs to bodies[i], and a stale id leaves its slot untouched, as readBody does.
        EXPECT_NEAR(states[0].positionM.x, 1.0f, 1e-5f);
        expectSameState(states[1], sentinelState());
        EXPECT_NEAR(states[2].positionM.x, 3.0f, 1e-5f);
    }

    TEST(PhysicsServiceTest, BatchReadStopsAtTheShorterSpan)
    {
        TestWorld world;
        const std::array ids{ PhysicsServiceImpl::toBodyId(world.addBox(kUnitBoxHalfExtents, JPH::RVec3(1.0, 0.0, 0.0))),
            PhysicsServiceImpl::toBodyId(world.addBox(kUnitBoxHalfExtents, JPH::RVec3(2.0, 0.0, 0.0))),
            PhysicsServiceImpl::toBodyId(world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0))) };
        const PhysicsServiceImpl service(world.system());

        // Shorter out: the element past the span stays untouched.
        std::array<BodyState, 3> states{ sentinelState(), sentinelState(), sentinelState() };
        EXPECT_EQ(service.readBodies(ids, std::span<BodyState>(states.data(), 2)), 2u);
        EXPECT_NEAR(states[0].positionM.x, 1.0f, 1e-5f);
        EXPECT_NEAR(states[1].positionM.x, 2.0f, 1e-5f);
        expectSameState(states[2], sentinelState());

        // Shorter ids
        states = { sentinelState(), sentinelState(), sentinelState() };
        EXPECT_EQ(service.readBodies(std::span<const BodyId>(ids.data(), 1), states), 1u);
        EXPECT_NEAR(states[0].positionM.x, 1.0f, 1e-5f);
        expectSameState(states[1], sentinelState());
        expectSameState(states[2], sentinelState());

        // Empty
        EXPECT_EQ(service.readBodies({}, states), 0u);
        EXPECT_EQ(service.readBodies(ids, {}), 0u);
    }

    // ---- Stale and garbage handles ---------------------------------------------------------------------------------

    TEST(PhysicsServiceTest, HandleGoesStaleWhenTheWorldsOwnerDestroysTheBody)
    {
        TestWorld world;
        const JPH::BodyID body = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0));
        const BodyId id = PhysicsServiceImpl::toBodyId(body);
        const PhysicsServiceImpl service(world.system());
        ASSERT_TRUE(service.isBodyValid(id));

        world.destroy(body);

        expectHandleRejected(service, id);
        EXPECT_FALSE(service.castRay(kOrigin, kTenMetersX).has_value());
    }

    TEST(PhysicsServiceTest, HandleStaysStaleWhenTheSlotIsRecycled)
    {
        TestWorld world;
        const JPH::BodyID oldBody = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0));
        const BodyId oldId = PhysicsServiceImpl::toBodyId(oldBody);
        const PhysicsServiceImpl service(world.system());

        world.destroy(oldBody);
        const JPH::BodyID newBody = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(4.0, 0.0, 0.0));
        const BodyId newId = PhysicsServiceImpl::toBodyId(newBody);

        // Jolt reuses the freed slot with a new sequence number (BodyManager's free list).
        ASSERT_EQ(newBody.GetIndex(), oldBody.GetIndex());
        EXPECT_NE(newId, oldId);

        expectHandleRejected(service, oldId);
        EXPECT_TRUE(service.isBodyValid(newId));
        BodyState state;
        ASSERT_TRUE(service.readBody(newId, state));
        EXPECT_NEAR(state.positionM.x, 4.0f, 1e-5f);
    }

    TEST(PhysicsServiceTest, StaleIgnoreHandleDoesNotHideTheBodyInTheRecycledSlot)
    {
        TestWorld world;
        const JPH::BodyID oldBody = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0));
        const BodyId oldId = PhysicsServiceImpl::toBodyId(oldBody);
        const PhysicsServiceImpl service(world.system());

        world.destroy(oldBody);
        const JPH::BodyID newBody = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0));
        ASSERT_EQ(newBody.GetIndex(), oldBody.GetIndex());
        const BodyId newId = PhysicsServiceImpl::toBodyId(newBody);

        const auto rayHit = service.castRay(kOrigin, kTenMetersX, oldId);
        ASSERT_TRUE(rayHit.has_value());
        EXPECT_EQ(rayHit->body, newId);

        const auto shapeHit = service.castShape(SphereShape{ 0.5f }, PhysicsQuat{}, kOrigin, kTenMetersX, oldId);
        ASSERT_TRUE(shapeHit.has_value());
        EXPECT_EQ(shapeHit->body, newId);
    }

    TEST(PhysicsServiceTest, GarbageHandlesAreRejected)
    {
        TestWorld world;
        const BodyId live = PhysicsServiceImpl::toBodyId(world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0)));
        const PhysicsServiceImpl service(world.system());
        ASSERT_TRUE(service.isBodyValid(live));

        constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();

        // Default-constructed, and huge index or generation
        EXPECT_FALSE(BodyId{}.isValid());
        expectHandleRejected(service, BodyId{});
        expectHandleRejected(service, BodyId{ BodyId::kInvalidIndex, 0 });
        expectHandleRejected(service, BodyId{ BodyId::kInvalidIndex, kMax });
        expectHandleRejected(service, BodyId{ kMax - 1, 0 });
        expectHandleRejected(service, BodyId{ kMax - 1, kMax });
        expectHandleRejected(service, BodyId{ live.index, kMax });

        // Jolt's BodyID is narrower than the handle (a 23-bit index and an 8-bit sequence number in
        // Jolt/Physics/Body/BodyID.h). Handles that only match the live body after truncation are not that body.
        expectHandleRejected(service, BodyId{ live.index + (1u << 23), live.generation });
        expectHandleRejected(service, BodyId{ live.index + (1u << 31), live.generation });
        expectHandleRejected(service, BodyId{ live.index, live.generation + (1u << 8) });
        expectHandleRejected(service, BodyId{ live.index, live.generation + (1u << 31) });

        // A slot nothing lives in
        expectHandleRejected(service, BodyId{ live.index + 1, live.generation });

        // Jolt's own invalid ID
        expectHandleRejected(service, PhysicsServiceImpl::toBodyId(JPH::BodyID()));

        EXPECT_TRUE(service.isBodyValid(live));
    }

    // ---- castRay ---------------------------------------------------------------------------------------------------

    TEST(PhysicsServiceTest, RayCastFindsClosestHitAndHonorsIgnoreBody)
    {
        TestWorld world;
        const TwoBoxes boxes(world);
        const BodyId nearBody = PhysicsServiceImpl::toBodyId(boxes.mNear);
        const BodyId farBody = PhysicsServiceImpl::toBodyId(boxes.mFar);
        const PhysicsServiceImpl service(world.system());

        // Ray along +X of length 10m hits nearBody at x = 2.5m (fraction 0.25)
        const auto hit1 = service.castRay(kOrigin, kTenMetersX);
        ASSERT_TRUE(hit1.has_value());
        EXPECT_EQ(hit1->body, nearBody);
        EXPECT_NEAR(hit1->pointM.x, 2.5f, 1e-3f);
        EXPECT_NEAR(hit1->normal.x, -1.0f, 1e-3f);
        EXPECT_NEAR(hit1->normal.y, 0.0f, 1e-3f);
        EXPECT_NEAR(hit1->normal.z, 0.0f, 1e-3f);
        EXPECT_NEAR(hit1->fraction, 0.25f, 1e-3f);

        // Ignoring nearBody hits farBody at x = 5.5m (fraction 0.55)
        const auto hit2 = service.castRay(kOrigin, kTenMetersX, nearBody);
        ASSERT_TRUE(hit2.has_value());
        EXPECT_EQ(hit2->body, farBody);
        EXPECT_NEAR(hit2->pointM.x, 5.5f, 1e-3f);
        EXPECT_NEAR(hit2->fraction, 0.55f, 1e-3f);
    }

    TEST(PhysicsServiceTest, RayCastMisses)
    {
        TestWorld world;
        const TwoBoxes boxes(world);
        const PhysicsServiceImpl service(world.system());

        EXPECT_FALSE(service.castRay(kOrigin, { 0.0f, 10.0f, 0.0f }).has_value()); // Wrong direction
        EXPECT_FALSE(service.castRay(kOrigin, { 2.0f, 0.0f, 0.0f }).has_value()); // Ends before x = 2.5
        EXPECT_FALSE(service.castRay({ 0.0f, 5.0f, 0.0f }, kTenMetersX).has_value()); // Passes beside both boxes
    }

    TEST(PhysicsServiceTest, RayCastWithNonFiniteInputFindsNothing)
    {
        TestWorld world;
        const TwoBoxes boxes(world);
        const PhysicsServiceImpl service(world.system());

        for (const float bad : { kNaN, kInf, -kInf })
        {
            EXPECT_FALSE(service.castRay({ bad, 0.0f, 0.0f }, kTenMetersX).has_value());
            EXPECT_FALSE(service.castRay({ 0.0f, bad, 0.0f }, kTenMetersX).has_value());
            EXPECT_FALSE(service.castRay({ 0.0f, 0.0f, bad }, kTenMetersX).has_value());
            EXPECT_FALSE(service.castRay(kOrigin, { bad, 0.0f, 0.0f }).has_value());
            EXPECT_FALSE(service.castRay(kOrigin, { 10.0f, bad, 0.0f }).has_value());
            EXPECT_FALSE(service.castRay(kOrigin, { 10.0f, 0.0f, bad }).has_value());
        }
    }

    TEST(PhysicsServiceTest, ZeroLengthCastsDoNotCrash)
    {
        // The header does not say what a zero-length cast returns, so only safety is checked.
        TestWorld world;
        const TwoBoxes boxes(world);
        const PhysicsServiceImpl service(world.system());

        EXPECT_NO_THROW((void)service.castRay(kOrigin, kOrigin));
        EXPECT_NO_THROW((void)service.castShape(SphereShape{ 0.5f }, PhysicsQuat{}, kOrigin, kOrigin));
    }

    TEST(PhysicsServiceTest, HitPointsAreInWorldSpaceAwayFromTheOrigin)
    {
        // Jolt reports shape-cast contact points relative to a base offset; the result must still be world space.
        TestWorld world;
        const JPH::BodyID box = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(1000.0, 0.0, 0.0));
        const PhysicsServiceImpl service(world.system());
        const PhysicsVec3 start{ 990.0f, 0.0f, 0.0f };

        const auto rayHit = service.castRay(start, kTenMetersX);
        ASSERT_TRUE(rayHit.has_value());
        EXPECT_EQ(rayHit->body, PhysicsServiceImpl::toBodyId(box));
        EXPECT_NEAR(rayHit->pointM.x, 999.5f, kPointTolerance);
        EXPECT_NEAR(rayHit->fraction, 0.95f, kFractionTolerance); // (999.5 - 990) / 10

        const auto shapeHit = service.castShape(SphereShape{ 0.5f }, PhysicsQuat{}, start, kTenMetersX);
        ASSERT_TRUE(shapeHit.has_value());
        EXPECT_NEAR(shapeHit->pointM.x, 999.5f, kPointTolerance);
        EXPECT_NEAR(shapeHit->fraction, 0.9f, kFractionTolerance); // (999.5 - 0.5 - 990) / 10
    }

    // ---- Query masks -----------------------------------------------------------------------------------------------

    TEST(PhysicsServiceTest, QueryMaskPublishRoundTripAndReplace)
    {
        TestWorld world;
        PhysicsServiceImpl service(world.system());
        const PhysicsQueryService& plugins = service;

        EXPECT_EQ(plugins.queryMask("world"), kQueryMaskNone);
        EXPECT_EQ(plugins.queryMask(""), kQueryMaskNone);

        service.publishQueryMask("world", kMaskStatic);
        service.publishQueryMask("actor", kMaskMoving);
        EXPECT_EQ(plugins.queryMask("world"), kMaskStatic);
        EXPECT_EQ(plugins.queryMask("actor"), kMaskMoving);
        EXPECT_EQ(plugins.queryMask("unknown"), kQueryMaskNone);

        // Lookup goes by the view's own length, not a terminating NUL
        EXPECT_EQ(plugins.queryMask(std::string_view("world_and_more").substr(0, 5)), kMaskStatic);

        service.publishQueryMask("world", kMaskStatic | kMaskUnused);
        EXPECT_EQ(plugins.queryMask("world"), kMaskStatic | kMaskUnused);
        EXPECT_EQ(plugins.queryMask("actor"), kMaskMoving);
    }

    TEST(PhysicsServiceTest, QueryMasksFilterRaysAndShapesByObjectLayer)
    {
        TestWorld world;
        const BodyId staticBody = PhysicsServiceImpl::toBodyId(world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0)));
        const BodyId movingBody = PhysicsServiceImpl::toBodyId(world.addBox(
            kUnitBoxHalfExtents, JPH::RVec3(6.0, 0.0, 0.0), kLayerMoving, JPH::EMotionType::Kinematic));
        const PhysicsServiceImpl service(world.system());

        const auto ray = [&](QueryMask mask) { return service.castRay(kOrigin, kTenMetersX, BodyId{}, mask); };
        const auto shape = [&](QueryMask mask) {
            return service.castShape(SphereShape{ 0.5f }, PhysicsQuat{}, kOrigin, kTenMetersX, BodyId{}, mask);
        };

        // A body matches when (mask & objectLayer) != 0
        const auto expectBody = [](const std::optional<RayHit>& hit, BodyId expected) {
            ASSERT_TRUE(hit.has_value());
            EXPECT_EQ(hit->body, expected);
        };
        expectBody(ray(kMaskStatic), staticBody);
        expectBody(shape(kMaskStatic), staticBody);
        expectBody(ray(kMaskMoving), movingBody);
        expectBody(shape(kMaskMoving), movingBody);

        // Combined masks find the closest of either, and the default and kQueryMaskAll find all
        expectBody(ray(kMaskStatic | kMaskMoving), staticBody);
        expectBody(shape(kMaskStatic | kMaskMoving), staticBody);
        expectBody(ray(kQueryMaskAll), staticBody);
        expectBody(shape(kQueryMaskAll), staticBody);
        expectBody(service.castRay(kOrigin, kTenMetersX), staticBody);

        // kQueryMaskNone and a bit no body has find nothing
        EXPECT_FALSE(ray(kQueryMaskNone).has_value());
        EXPECT_FALSE(shape(kQueryMaskNone).has_value());
        EXPECT_FALSE(ray(kMaskUnused).has_value());
        EXPECT_FALSE(shape(kMaskUnused).has_value());

        // A mask from queryMask() for a name that was not published finds nothing
        const QueryMask unpublished = service.queryMask("nobody published this");
        EXPECT_FALSE(ray(unpublished).has_value());
        EXPECT_FALSE(shape(unpublished).has_value());
    }

    TEST(PhysicsServiceTest, MaskAllMatchesABodyInObjectLayerZero)
    {
        // physics_service.hpp: kQueryMaskAll matches every body, including one in object layer 0.
        TestWorld world;
        const BodyId body
            = PhysicsServiceImpl::toBodyId(world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0), kLayerZero));
        const PhysicsServiceImpl service(world.system());

        const auto ray = service.castRay(kOrigin, kTenMetersX, BodyId{}, kQueryMaskAll);
        ASSERT_TRUE(ray.has_value());
        EXPECT_EQ(ray->body, body);

        const auto shape = service.castShape(SphereShape{ 0.5f }, PhysicsQuat{}, kOrigin, kTenMetersX);
        ASSERT_TRUE(shape.has_value());
        EXPECT_EQ(shape->body, body);

        // Any narrower mask has no bit in common with layer 0
        EXPECT_FALSE(service.castRay(kOrigin, kTenMetersX, BodyId{}, kMaskStatic | kMaskMoving).has_value());
        EXPECT_FALSE(service.castRay(kOrigin, kTenMetersX, BodyId{}, kQueryMaskNone).has_value());
    }

    // ---- castShape: results against a known box --------------------------------------------------------------------

    TEST(PhysicsServiceTest, SphereCastHitsClosestBodyWithExpectedResult)
    {
        TestWorld world;
        const TwoBoxes boxes(world);
        const PhysicsServiceImpl service(world.system());

        // Radius 0.5 from the origin along +X: touches the face x = 2.5 with its centre at x = 2, so 2 / 10 = 0.2.
        const auto hit = service.castShape(SphereShape{ 0.5f }, PhysicsQuat{}, kOrigin, kTenMetersX);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->body, PhysicsServiceImpl::toBodyId(boxes.mNear));
        EXPECT_NEAR(hit->fraction, 0.2f, kFractionTolerance);
        expectVecNear(hit->pointM, { 2.5f, 0.0f, 0.0f }, kPointTolerance);
        expectVecNear(hit->normal, { -1.0f, 0.0f, 0.0f }, kNormalTolerance);
    }

    TEST(PhysicsServiceTest, SphereCastHonoursIgnoreBody)
    {
        TestWorld world;
        const TwoBoxes boxes(world);
        const PhysicsServiceImpl service(world.system());

        // The far box's face is x = 5.5: the centre stops at x = 5, fraction 0.5.
        const auto hit = service.castShape(
            SphereShape{ 0.5f }, PhysicsQuat{}, kOrigin, kTenMetersX, PhysicsServiceImpl::toBodyId(boxes.mNear));
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->body, PhysicsServiceImpl::toBodyId(boxes.mFar));
        EXPECT_NEAR(hit->fraction, 0.5f, kFractionTolerance);
        EXPECT_NEAR(hit->pointM.x, 5.5f, kPointTolerance);
    }

    TEST(PhysicsServiceTest, ShapeCastsMiss)
    {
        TestWorld world;
        const TwoBoxes boxes(world);
        const PhysicsServiceImpl service(world.system());

        const std::array<PhysicsShape, 3> shapes{ SphereShape{ 0.5f }, BoxShape{ { 0.5f, 0.5f, 0.5f } },
            CapsuleShape{ 0.5f, 0.25f } };
        for (const PhysicsShape& shape : shapes)
        {
            // Wrong direction
            EXPECT_FALSE(service.castShape(shape, PhysicsQuat{}, kOrigin, { 0.0f, 10.0f, 0.0f }).has_value());
            // Ends before reaching x = 2.5: none of these shapes reaches more than 0.5 along X, so at most x = 1.5
            EXPECT_FALSE(service.castShape(shape, PhysicsQuat{}, kOrigin, { 1.0f, 0.0f, 0.0f }).has_value());
            // Passes beside both boxes
            EXPECT_FALSE(service.castShape(shape, PhysicsQuat{}, { 0.0f, 5.0f, 0.0f }, kTenMetersX).has_value());
        }
    }

    TEST(PhysicsServiceTest, ShapeInitiallyOverlappingABodyHitsWithFractionZero)
    {
        TestWorld world;
        const TwoBoxes boxes(world);
        const BodyId nearBody = PhysicsServiceImpl::toBodyId(boxes.mNear);
        const PhysicsServiceImpl service(world.system());

        const std::array<PhysicsShape, 3> shapes{ SphereShape{ 0.5f }, BoxShape{ { 0.2f, 0.2f, 0.2f } },
            CapsuleShape{ 0.2f, 0.1f } };
        // Partly overlapping: the smallest reach along X is the capsule's radius, 0.1, so x = 2.45 crosses the face
        // x = 2.5 for every shape. Then with the shape's centre inside the box.
        const std::array<PhysicsVec3, 2> starts{ PhysicsVec3{ 2.45f, 0.0f, 0.0f }, PhysicsVec3{ 2.9f, 0.1f, 0.0f } };

        for (const PhysicsShape& shape : shapes)
        {
            for (const PhysicsVec3& start : starts)
            {
                const auto hit = service.castShape(shape, PhysicsQuat{}, start, kTenMetersX);
                ASSERT_TRUE(hit.has_value());
                EXPECT_EQ(hit->body, nearBody);
                EXPECT_EQ(hit->fraction, 0.0f);
            }
        }
    }

    TEST(PhysicsServiceTest, CapsuleLongAxisIsLocalZ)
    {
        TestWorld world;
        const BodyId floor = PhysicsServiceImpl::toBodyId(addFloor(world));
        const PhysicsServiceImpl service(world.system());

        constexpr float kHalfHeight = 0.5f;
        constexpr float kRadius = 0.25f;
        const CapsuleShape capsule{ kHalfHeight, kRadius };
        const PhysicsVec3 start{ 0.0f, 0.0f, 5.0f };

        // Identity rotation, standing: the centre stops at halfHeight + radius above the floor.
        const float standingFraction = (5.0f - (kHalfHeight + kRadius)) / 10.0f;
        const auto standing = service.castShape(capsule, PhysicsQuat{}, start, kTenMetersDownZ);
        ASSERT_TRUE(standing.has_value());
        EXPECT_EQ(standing->body, floor);
        EXPECT_NEAR(standing->fraction, standingFraction, kFractionTolerance);
        expectVecNear(standing->pointM, { 0.0f, 0.0f, 0.0f }, kPointTolerance);
        expectVecNear(standing->normal, { 0.0f, 0.0f, 1.0f }, kNormalTolerance);

        // Turning about Z leaves the long axis where it is.
        const auto spun = service.castShape(capsule, PhysicsQuat{ 0.0f, 0.0f, kSinHalf90, kSinHalf90 }, start, kTenMetersDownZ);
        ASSERT_TRUE(spun.has_value());
        EXPECT_NEAR(spun->fraction, standingFraction, kFractionTolerance);

        // 90 degrees about X or Y lays it down: the centre stops at radius above the floor.
        const float lyingFraction = (5.0f - kRadius) / 10.0f;
        for (const PhysicsQuat& rotation :
            { PhysicsQuat{ kSinHalf90, 0.0f, 0.0f, kSinHalf90 }, PhysicsQuat{ 0.0f, kSinHalf90, 0.0f, kSinHalf90 } })
        {
            const auto lying = service.castShape(capsule, rotation, start, kTenMetersDownZ);
            ASSERT_TRUE(lying.has_value());
            EXPECT_EQ(lying->body, floor);
            EXPECT_NEAR(lying->fraction, lyingFraction, kFractionTolerance);
            EXPECT_NEAR(lying->pointM.z, 0.0f, kPointTolerance);
            expectVecNear(lying->normal, { 0.0f, 0.0f, 1.0f }, kNormalTolerance);
        }
    }

    TEST(PhysicsServiceTest, BoxCastHitsAndHonoursRotation)
    {
        TestWorld world;
        const TwoBoxes boxes(world);
        const BodyId nearBody = PhysicsServiceImpl::toBodyId(boxes.mNear);
        const PhysicsServiceImpl service(world.system());

        // Long along local X. Flat face against flat face, so any convex radius the cast uses does not change these.
        const BoxShape box{ { 1.0f, 0.25f, 0.25f } };

        // Identity: leading face at centre + 1, reaches x = 2.5 with its centre at 1.5.
        const auto identity = service.castShape(box, PhysicsQuat{}, kOrigin, kTenMetersX);
        ASSERT_TRUE(identity.has_value());
        EXPECT_EQ(identity->body, nearBody);
        EXPECT_NEAR(identity->fraction, 0.15f, kFractionTolerance);
        EXPECT_NEAR(identity->pointM.x, 2.5f, kPointTolerance);
        expectVecNear(identity->normal, { -1.0f, 0.0f, 0.0f }, kNormalTolerance);

        // 90 degrees about Z: leading face at centre + 0.25, centre stops at 2.25.
        const auto turned
            = service.castShape(box, PhysicsQuat{ 0.0f, 0.0f, kSinHalf90, kSinHalf90 }, kOrigin, kTenMetersX);
        ASSERT_TRUE(turned.has_value());
        EXPECT_EQ(turned->body, nearBody);
        EXPECT_NEAR(turned->fraction, 0.225f, kFractionTolerance);
        EXPECT_NEAR(turned->pointM.x, 2.5f, kPointTolerance);
        expectVecNear(turned->normal, { -1.0f, 0.0f, 0.0f }, kNormalTolerance);
    }

    TEST(PhysicsServiceTest, BoxCastTurned45DegreesLeadsWithItsCorner)
    {
        TestWorld world;
        const TwoBoxes boxes(world);
        const PhysicsServiceImpl service(world.system());

        // sin and cos of 22.5 degrees. A sharp corner reaches 0.5 * sqrt(2) = 0.7071, fraction 0.1793. A cast box with
        // Jolt's default 0.05 m convex radius has a rounded corner reaching 0.6864, fraction 0.1814. The header does
        // not fix the convex radius, so the tolerance accepts both; the unrotated answer 0.2 is well outside it.
        const PhysicsQuat rotation{ 0.0f, 0.0f, 0.38268343f, 0.92387953f };
        const auto hit = service.castShape(BoxShape{ { 0.5f, 0.5f, 0.5f } }, rotation, kOrigin, kTenMetersX);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->body, PhysicsServiceImpl::toBodyId(boxes.mNear));
        EXPECT_NEAR(hit->fraction, 0.1803f, 0.003f);
        EXPECT_NEAR(hit->pointM.x, 2.5f, kPointTolerance);
        EXPECT_NEAR(hit->pointM.y, 0.0f, 0.01f);
    }

    TEST(PhysicsServiceTest, VerySmallShapesWork)
    {
        // These are smaller than Jolt's default 0.05 m convex radius; Jolt must never be given a shape smaller than
        // its convex radius. Near face x = 2.5, so a half length of 0.001 gives fraction 0.2499.
        TestWorld world;
        const TwoBoxes boxes(world);
        const BodyId nearBody = PhysicsServiceImpl::toBodyId(boxes.mNear);
        const PhysicsServiceImpl service(world.system());

        const auto box = service.castShape(BoxShape{ { 0.001f, 0.001f, 0.001f } }, PhysicsQuat{}, kOrigin, kTenMetersX);
        ASSERT_TRUE(box.has_value());
        EXPECT_EQ(box->body, nearBody);
        EXPECT_NEAR(box->fraction, 0.2499f, kFractionTolerance);

        // One small dimension only: the whole box must not be treated as small
        const auto plate = service.castShape(BoxShape{ { 0.001f, 1.0f, 1.0f } }, PhysicsQuat{}, kOrigin, kTenMetersX);
        ASSERT_TRUE(plate.has_value());
        EXPECT_EQ(plate->body, nearBody);
        EXPECT_NEAR(plate->fraction, 0.2499f, kFractionTolerance);

        const auto sphere = service.castShape(SphereShape{ 0.001f }, PhysicsQuat{}, kOrigin, kTenMetersX);
        ASSERT_TRUE(sphere.has_value());
        EXPECT_NEAR(sphere->fraction, 0.2499f, kFractionTolerance);

        // Laid along X by 90 degrees about Y: half length 0.001 + 0.001
        const auto capsule = service.castShape(CapsuleShape{ 0.001f, 0.001f },
            PhysicsQuat{ 0.0f, kSinHalf90, 0.0f, kSinHalf90 }, kOrigin, kTenMetersX);
        ASSERT_TRUE(capsule.has_value());
        EXPECT_NEAR(capsule->fraction, 0.2498f, kFractionTolerance);
    }

    TEST(PhysicsServiceTest, InvalidShapesFindNothing)
    {
        TestWorld world;
        const TwoBoxes boxes(world);
        const PhysicsServiceImpl service(world.system());

        const std::array<PhysicsShape, 21> invalid{
            BoxShape{ { 0.0f, 0.5f, 0.5f } },
            BoxShape{ { 0.5f, 0.0f, 0.5f } },
            BoxShape{ { 0.5f, 0.5f, 0.0f } },
            BoxShape{ { -0.5f, 0.5f, 0.5f } },
            BoxShape{ { 0.5f, -0.5f, 0.5f } },
            BoxShape{ { 0.5f, 0.5f, -0.5f } },
            BoxShape{ { kNaN, 0.5f, 0.5f } },
            BoxShape{ { 0.5f, kNaN, 0.5f } },
            BoxShape{ { 0.5f, 0.5f, kInf } },
            SphereShape{ 0.0f },
            SphereShape{ -0.5f },
            SphereShape{ kNaN },
            SphereShape{ kInf },
            CapsuleShape{ 0.0f, 0.25f },
            CapsuleShape{ 0.5f, 0.0f },
            CapsuleShape{ -0.5f, 0.25f },
            CapsuleShape{ 0.5f, -0.25f },
            CapsuleShape{ kNaN, 0.25f },
            CapsuleShape{ 0.5f, kNaN },
            CapsuleShape{ kInf, 0.25f },
            CapsuleShape{ 0.5f, kInf },
        };

        for (std::size_t i = 0; i < invalid.size(); ++i)
        {
            EXPECT_FALSE(service.castShape(invalid[i], PhysicsQuat{}, kOrigin, kTenMetersX).has_value())
                << "shape " << i;
        }
    }

    TEST(PhysicsServiceTest, InvalidRotationOrInputFindsNothingForEveryShape)
    {
        TestWorld world;
        const TwoBoxes boxes(world);
        const PhysicsServiceImpl service(world.system());

        const std::array<PhysicsShape, 3> shapes{ SphereShape{ 0.5f }, BoxShape{ { 0.5f, 0.5f, 0.5f } },
            CapsuleShape{ 0.5f, 0.25f } };
        const std::array<PhysicsQuat, 5> badRotations{
            PhysicsQuat{ 0.0f, 0.0f, 0.0f, 0.0f },
            PhysicsQuat{ 0.0f, 0.0f, 0.0f, 2.0f },
            PhysicsQuat{ 0.5f, 0.0f, 0.0f, 0.5f },
            PhysicsQuat{ kNaN, 0.0f, 0.0f, 1.0f },
            PhysicsQuat{ 0.0f, 0.0f, 0.0f, kInf },
        };

        for (const PhysicsShape& shape : shapes)
        {
            // Control: this shape does hit with good input
            EXPECT_TRUE(service.castShape(shape, PhysicsQuat{}, kOrigin, kTenMetersX).has_value());

            for (const PhysicsQuat& rotation : badRotations)
                EXPECT_FALSE(service.castShape(shape, rotation, kOrigin, kTenMetersX).has_value());

            for (const float bad : { kNaN, kInf, -kInf })
            {
                EXPECT_FALSE(service.castShape(shape, PhysicsQuat{}, { bad, 0.0f, 0.0f }, kTenMetersX).has_value());
                EXPECT_FALSE(service.castShape(shape, PhysicsQuat{}, { 0.0f, bad, 0.0f }, kTenMetersX).has_value());
                EXPECT_FALSE(service.castShape(shape, PhysicsQuat{}, { 0.0f, 0.0f, bad }, kTenMetersX).has_value());
                EXPECT_FALSE(service.castShape(shape, PhysicsQuat{}, kOrigin, { bad, 0.0f, 0.0f }).has_value());
                EXPECT_FALSE(service.castShape(shape, PhysicsQuat{}, kOrigin, { 10.0f, bad, 0.0f }).has_value());
                EXPECT_FALSE(service.castShape(shape, PhysicsQuat{}, kOrigin, { 10.0f, 0.0f, bad }).has_value());
            }
        }
    }

    // ---- Determinism -----------------------------------------------------------------------------------------------

    struct QueryRun
    {
        std::array<BodyState, 2> mStates;
        std::array<std::optional<RayHit>, 5> mHits;
    };

    QueryRun runQueries()
    {
        TestWorld world;
        const JPH::BodyID dynamicBody = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.1, -0.2), kLayerMoving,
            JPH::EMotionType::Dynamic, JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), 0.3f), JPH::EActivation::Activate);
        world.bodies().SetLinearAndAngularVelocity(
            dynamicBody, JPH::Vec3(1.5f, -2.5f, 3.5f), JPH::Vec3(0.25f, 0.5f, 0.75f));
        const JPH::BodyID staticBody = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(6.0, 0.0, 0.0));
        addFloor(world);

        const PhysicsServiceImpl service(world.system());
        const std::array ids{ PhysicsServiceImpl::toBodyId(dynamicBody), PhysicsServiceImpl::toBodyId(staticBody) };

        QueryRun run;
        EXPECT_EQ(service.readBodies(ids, run.mStates), 2u);

        const PhysicsVec3 start{ 0.1f, 0.05f, 0.2f };
        const PhysicsVec3 direction{ 9.0f, 0.3f, -1.0f };
        const PhysicsQuat rotation{ 0.0f, kSinHalf90, 0.0f, kSinHalf90 };
        run.mHits[0] = service.castRay(start, direction);
        run.mHits[1] = service.castRay(start, direction, ids[0]);
        run.mHits[2] = service.castShape(SphereShape{ 0.3f }, rotation, start, direction);
        run.mHits[3] = service.castShape(BoxShape{ { 0.3f, 0.2f, 0.1f } }, rotation, start, direction);
        run.mHits[4] = service.castShape(CapsuleShape{ 0.4f, 0.1f }, rotation, start, direction);
        return run;
    }

    TEST(PhysicsServiceTest, TwoIdenticalRunsReturnBitIdenticalResults)
    {
        const QueryRun first = runQueries();
        const QueryRun second = runQueries();

        for (std::size_t i = 0; i < first.mStates.size(); ++i)
            EXPECT_TRUE(bitIdentical(first.mStates[i], second.mStates[i])) << "state " << i;

        for (std::size_t i = 0; i < first.mHits.size(); ++i)
        {
            EXPECT_TRUE(first.mHits[i].has_value()) << "hit " << i; // Otherwise the comparison proves little
            EXPECT_TRUE(bitIdentical(first.mHits[i], second.mHits[i])) << "hit " << i;
        }
    }

    // ---- HostImpl --------------------------------------------------------------------------------------------------

    TEST(PhysicsServiceTest, HostHasNoPhysicsServiceBeforeAttachOrAfterDetach)
    {
        HostImpl host;
        EXPECT_EQ(host.getPhysicsQueryService(), nullptr);
        EXPECT_EQ(host.physicsService(), nullptr);

        TestWorld world;
        const JPH::BodyID box = world.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0));

        host.attachPhysicsWorld(world.system());
        PhysicsQueryService* physics = host.getPhysicsQueryService();
        ASSERT_NE(physics, nullptr);
        EXPECT_NE(host.physicsService(), nullptr);
        EXPECT_TRUE(physics->isBodyValid(PhysicsServiceImpl::toBodyId(box)));
        const auto hit = physics->castRay(kOrigin, kTenMetersX);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->body, PhysicsServiceImpl::toBodyId(box));

        host.detachPhysicsWorld();
        EXPECT_EQ(host.getPhysicsQueryService(), nullptr);
        EXPECT_EQ(host.physicsService(), nullptr);

        // Detaching with nothing attached is harmless
        EXPECT_NO_THROW(host.detachPhysicsWorld());
        EXPECT_EQ(host.getPhysicsQueryService(), nullptr);

        // The world itself is untouched by detaching
        EXPECT_EQ(world.system().GetNumBodies(), 1u);
    }

    TEST(PhysicsServiceTest, HostReattachedToASecondWorldUsesIt)
    {
        TestWorld firstWorld;
        TestWorld secondWorld;
        const JPH::BodyID firstBox = firstWorld.addBox(kUnitBoxHalfExtents, JPH::RVec3(3.0, 0.0, 0.0));
        const JPH::BodyID secondBox = secondWorld.addBox(kUnitBoxHalfExtents, JPH::RVec3(6.0, 0.0, 0.0));

        HostImpl host;
        host.attachPhysicsWorld(firstWorld.system());
        const auto firstHit = host.getPhysicsQueryService()->castRay(kOrigin, kTenMetersX);
        ASSERT_TRUE(firstHit.has_value());
        EXPECT_NEAR(firstHit->pointM.x, 2.5f, 1e-3f);
        EXPECT_EQ(firstHit->body, PhysicsServiceImpl::toBodyId(firstBox));

        host.attachPhysicsWorld(secondWorld.system());
        ASSERT_NE(host.getPhysicsQueryService(), nullptr);
        const auto secondHit = host.getPhysicsQueryService()->castRay(kOrigin, kTenMetersX);
        ASSERT_TRUE(secondHit.has_value());
        EXPECT_NEAR(secondHit->pointM.x, 5.5f, 1e-3f);
        EXPECT_EQ(secondHit->body, PhysicsServiceImpl::toBodyId(secondBox));

        // And again after a detach
        host.detachPhysicsWorld();
        host.attachPhysicsWorld(firstWorld.system());
        const auto againHit = host.getPhysicsQueryService()->castRay(kOrigin, kTenMetersX);
        ASSERT_TRUE(againHit.has_value());
        EXPECT_NEAR(againHit->pointM.x, 2.5f, 1e-3f);
    }

    TEST(PhysicsServiceTest, HostPublishesQueryMasksThroughItsService)
    {
        TestWorld world;
        HostImpl host;
        host.attachPhysicsWorld(world.system());
        ASSERT_NE(host.physicsService(), nullptr);

        host.physicsService()->publishQueryMask("world", kMaskStatic);
        EXPECT_EQ(host.getPhysicsQueryService()->queryMask("world"), kMaskStatic);
    }
} // namespace
