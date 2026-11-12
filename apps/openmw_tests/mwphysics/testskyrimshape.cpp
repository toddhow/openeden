// Task 0110 (neutral world stage N3a-4b): collision from a Skyrim SE model's visual triangles, the Jolt half
// (apps/openmw/mwphysics/skyrimshape.*) and its hook in the physics shape manager. The files are generated
// (nifscene/ssebuilder.hpp, shaderbuilder.hpp); no game asset is read. A floor and a table-like block are real Jolt
// bodies in a game-like world, so "the character stands on the floor" and "is stopped by the table" are proved with
// Jolt itself, not assumed.

#include "ragdolltestworld.hpp"

#include <apps/openmw/mwphysics/skyrimshape.hpp>

#include <components/nif/niffile.hpp>
#include <components/resource/niffilemanager.hpp>
#include <components/resource/physicsshape.hpp>
#include <components/resource/physicsshapemanager.hpp>
#include <components/testing/util.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "apps/components_tests/nifscene/shaderbuilder.hpp"

namespace
{
    using namespace NifScene::Testing;
    using namespace MWPhysicsTest;

    struct MeshSpec
    {
        std::vector<std::array<float, 3>> mVertices;
        std::vector<std::array<std::uint16_t, 3>> mTriangles;
        std::int32_t mAlphaFlags = -1; // NiAlphaProperty flags; -1: no alpha property
    };

    // Records: 0 root, 1 shape, 2 shader property, 3 texture set, (4 alpha property).
    std::string skyrimFile(const MeshSpec& mesh)
    {
        GamebryoFile file;
        file.mStreamVersion = 100;
        ShapeSpec shapeSpec;
        shapeSpec.mName = "Mesh";
        shapeSpec.mFlags = VF_Vertex | VF_UVs | VF_Normals;
        shapeSpec.mShader = 2;
        if (mesh.mAlphaFlags >= 0)
            shapeSpec.mAlphaProperty = 4;
        for (const auto& p : mesh.mVertices)
        {
            Vertex v;
            v.mPosition = p;
            v.mUV = { 0.f, 0.f };
            shapeSpec.mVertices.push_back(v);
        }
        shapeSpec.mTriangles = mesh.mTriangles;
        LightingSpec spec;
        spec.mTextureSet = 3;
        spec.mFlags2 = 0x10;
        spec.mUVOffset = { 0.f, 0.f };
        spec.mUVScale = { 1.f, 1.f };
        spec.mAlpha = 1.f;
        file.mBlocks.push_back(node(file, "Root", { 1 }));
        file.mBlocks.push_back(shape(file, shapeSpec));
        file.mBlocks.push_back(lightingShader(file, spec));
        file.mBlocks.push_back(textureSet({ "textures\\test\\wall_d.dds" }));
        if (mesh.mAlphaFlags >= 0)
            file.mBlocks.push_back(alphaProperty(file, static_cast<std::uint16_t>(mesh.mAlphaFlags), 128));
        return file.build();
    }

    // A flat floor, 1000 x 1000 units, top at z = 0, corners (0,0) to (1000,1000).
    MeshSpec floorMesh()
    {
        MeshSpec m;
        m.mVertices = { { 0, 0, 0 }, { 1000, 0, 0 }, { 1000, 1000, 0 }, { 0, 1000, 0 } };
        m.mTriangles = { { 0, 1, 2 }, { 0, 2, 3 } };
        return m;
    }

    // A closed block from (x0,y0,0) to (x1,y1,z1): a table or counter.
    MeshSpec blockMesh(float x0, float y0, float x1, float y1, float z1)
    {
        MeshSpec m;
        m.mVertices = { { x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 }, { x0, y0, z1 }, { x1, y0, z1 },
            { x1, y1, z1 }, { x0, y1, z1 } };
        m.mTriangles = { { 0, 2, 1 }, { 0, 3, 2 }, { 4, 5, 6 }, { 4, 6, 7 }, { 0, 1, 5 }, { 0, 5, 4 }, { 1, 2, 6 },
            { 1, 6, 5 }, { 2, 3, 7 }, { 2, 7, 6 }, { 3, 0, 4 }, { 3, 4, 7 } };
        return m;
    }

    vsg::ref_ptr<Resource::PhysicsShape> shapeFrom(const MeshSpec& mesh)
    {
        const auto reader = MWPhysics::SkyrimShape::makeSceneShapeReader();
        return reader->read(skyrimFile(mesh), "meshes/test.nif");
    }

    JPH::BodyID addStatic(GameLikeWorld& world, const JPH::Shape* shape, JPH::RVec3 position = JPH::RVec3::sZero())
    {
        const JPH::BodyCreationSettings settings(
            shape, position, JPH::Quat::sIdentity(), JPH::EMotionType::Static, MWPhysics::Layers::WORLD);
        return world.bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    }

    // The height of the first thing a ray from straight above (x, y, startZ) meets, or -1e9.
    double rayDownHeight(GameLikeWorld& world, double x, double y, double startZ)
    {
        world.system().OptimizeBroadPhase();
        const JPH::RRayCast ray(JPH::RVec3(x, y, startZ), JPH::Vec3(0.f, 0.f, -static_cast<float>(startZ + 1000.0)));
        JPH::RayCastResult hit;
        if (!world.system().GetNarrowPhaseQuery().CastRay(ray, hit))
            return -1.0e9;
        return startZ - hit.mFraction * (startZ + 1000.0);
    }

    // A sphere the size of a small character that falls DOWN (the game's Jolt gravity points up, so the body's factor
    // is -1, as ragdolls do), then the world runs `steps` steps.
    JPH::BodyID addSphere(GameLikeWorld& world, JPH::RVec3 position, float radius, JPH::Vec3 velocity)
    {
        const JPH::RefConst<JPH::Shape> shape = new JPH::SphereShape(radius);
        JPH::BodyCreationSettings settings(
            shape, position, JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, MWPhysics::Layers::RAGDOLL);
        settings.mGravityFactor = -1.f;
        settings.mLinearVelocity = velocity;
        settings.mFriction = 0.5f;
        return world.bodies().CreateAndAddBody(settings, JPH::EActivation::Activate);
    }

    void run(GameLikeWorld& world, int steps)
    {
        world.system().OptimizeBroadPhase();
        for (int i = 0; i < steps; ++i)
            world.update();
    }
}

TEST(SkyrimShape, an_empty_mesh_gives_no_shape_and_a_reason)
{
    ensureJoltInitialized();
    std::string error;
    EXPECT_EQ(MWPhysics::SkyrimShape::makeShape(NifScene::Collision::Mesh{}, &error), nullptr);
    EXPECT_FALSE(error.empty());
}

TEST(SkyrimShape, the_hook_accepts_skyrim_files_and_nothing_else)
{
    const auto reader = MWPhysics::SkyrimShape::makeSceneShapeReader();
    const std::string skyrim = skyrimFile(floorMesh());
    EXPECT_TRUE(reader->accepts(std::string_view(skyrim).substr(0, 128)));
    EXPECT_FALSE(reader->accepts("NetImmerse File Format, Version 4.0.0.2\n"));
    EXPECT_FALSE(reader->accepts(""));
}

TEST(SkyrimShape, a_floor_model_gives_a_mesh_shape_with_the_models_bounds)
{
    ensureJoltInitialized();
    const auto shape = shapeFrom(floorMesh());
    ASSERT_NE(shape, nullptr);
    ASSERT_NE(shape->mCollisionShape, nullptr);
    EXPECT_NEAR(shape->mCollisionBox.mCenter.x, 500.f, 0.1f);
    EXPECT_NEAR(shape->mCollisionBox.mCenter.y, 500.f, 0.1f);
    EXPECT_NEAR(shape->mCollisionBox.mExtents.x, 500.f, 0.1f);
    EXPECT_NEAR(shape->mCollisionBox.mExtents.y, 500.f, 0.1f);
    EXPECT_EQ(shape->mVisualCollisionType, Resource::VisualCollisionType::None);
}

TEST(SkyrimShape, a_ray_from_above_hits_the_floor_at_its_height)
{
    GameLikeWorld world(false);
    const auto shape = shapeFrom(floorMesh());
    ASSERT_NE(shape, nullptr);
    addStatic(world, shape->mCollisionShape.GetPtr());
    EXPECT_NEAR(rayDownHeight(world, 500, 500, 300), 0.0, 0.05);
    EXPECT_NEAR(rayDownHeight(world, 10, 990, 300), 0.0, 0.05);
    EXPECT_LT(rayDownHeight(world, 1500, 500, 300), -1.0e8) << "outside the floor there is nothing";
}

TEST(SkyrimShape, the_player_stands_on_the_floor_and_does_not_fall_through)
{
    GameLikeWorld world(false);
    const auto shape = shapeFrom(floorMesh());
    ASSERT_NE(shape, nullptr);
    addStatic(world, shape->mCollisionShape.GetPtr());
    const float radius = 30.f;
    const JPH::BodyID sphere = addSphere(world, JPH::RVec3(500, 500, 200), radius, JPH::Vec3::sZero());
    run(world, 240);
    const double z = world.bodies().GetCenterOfMassPosition(sphere).GetZ();
    EXPECT_NEAR(z, radius, 1.5) << "it rests on the floor, one radius above it";
}

TEST(SkyrimShape, a_table_stops_a_character_walking_into_it)
{
    GameLikeWorld world(false);
    const auto floor = shapeFrom(floorMesh());
    const auto table = shapeFrom(blockMesh(400, 300, 600, 700, 80));
    ASSERT_NE(floor, nullptr);
    ASSERT_NE(table, nullptr);
    addStatic(world, floor->mCollisionShape.GetPtr());
    addStatic(world, table->mCollisionShape.GetPtr());
    const float radius = 30.f;
    // Starts west of the table, on the floor, moving east at 600 units per second for two seconds: 1,200 units, far
    // more than the 300 that separate it from the table.
    const JPH::BodyID sphere = addSphere(world, JPH::RVec3(100, 500, radius + 1.f), radius, JPH::Vec3(600.f, 0.f, 0.f));
    run(world, 120);
    const double x = world.bodies().GetCenterOfMassPosition(sphere).GetX();
    EXPECT_LE(x, 400.0 - radius + 2.0) << "it is stopped by the table's west face";
    EXPECT_GT(x, 100.0) << "and it did move toward it";
}

TEST(SkyrimShape, the_same_walk_with_no_table_passes_the_spot)
{
    GameLikeWorld world(false);
    const auto floor = shapeFrom(floorMesh());
    ASSERT_NE(floor, nullptr);
    addStatic(world, floor->mCollisionShape.GetPtr());
    const float radius = 30.f;
    const JPH::BodyID sphere = addSphere(world, JPH::RVec3(100, 500, radius + 1.f), radius, JPH::Vec3(600.f, 0.f, 0.f));
    run(world, 60);
    EXPECT_GT(world.bodies().GetCenterOfMassPosition(sphere).GetX(), 450.0) << "the control: nothing stops it";
}

TEST(SkyrimShape, a_scaled_placement_scales_the_collision)
{
    GameLikeWorld world(false);
    const auto table = shapeFrom(blockMesh(0, 0, 100, 100, 50));
    ASSERT_NE(table, nullptr);
    const JPH::RefConst<JPH::Shape> scaled
        = new JPH::ScaledShape(table->mCollisionShape.GetPtr(), JPH::Vec3::sReplicate(2.f));
    addStatic(world, scaled.GetPtr());
    EXPECT_NEAR(rayDownHeight(world, 150, 150, 400), 100.0, 0.1) << "a 50 high table at scale 2 is 100 high";
    EXPECT_LT(rayDownHeight(world, 250, 150, 400), -1.0e8) << "and 200 wide, not 100";
}

TEST(SkyrimShape, a_blended_only_model_has_no_collision)
{
    ensureJoltInitialized();
    MeshSpec glass = floorMesh();
    glass.mAlphaFlags = 1; // blend on, test off
    const std::uint64_t before = MWPhysics::SkyrimShape::counters().mNoMesh.load();
    EXPECT_EQ(shapeFrom(glass), nullptr);
    EXPECT_EQ(MWPhysics::SkyrimShape::counters().mNoMesh.load(), before + 1);
}

// Task 0140: the stand-in box of a model with nothing to collide with threw the player 1,000 units in one frame.
TEST(SkyrimShape, a_model_read_fine_with_nothing_to_collide_with_is_remembered_as_collision_free)
{
    ensureJoltInitialized();
    MeshSpec glass = floorMesh();
    glass.mAlphaFlags = 1; // every shape blended: nothing collides
    const auto reader = MWPhysics::SkyrimShape::makeSceneShapeReader();
    EXPECT_FALSE(MWPhysics::SkyrimShape::modelReadWithNoCollision("meshes/effects/glassonly.nif"));
    EXPECT_EQ(reader->read(skyrimFile(glass), "meshes/effects/glassonly.nif"), nullptr);
    EXPECT_TRUE(MWPhysics::SkyrimShape::modelReadWithNoCollision("meshes/effects/glassonly.nif"));
    EXPECT_FALSE(MWPhysics::SkyrimShape::modelReadWithNoCollision("meshes/effects/other.nif"));
}

TEST(SkyrimShape, a_model_with_a_mesh_is_not_collision_free)
{
    ensureJoltInitialized();
    const auto reader = MWPhysics::SkyrimShape::makeSceneShapeReader();
    EXPECT_NE(reader->read(skyrimFile(floorMesh()), "meshes/architecture/solidfloor.nif"), nullptr);
    EXPECT_FALSE(MWPhysics::SkyrimShape::modelReadWithNoCollision("meshes/architecture/solidfloor.nif"));
}

TEST(SkyrimShape, a_model_that_failed_to_read_is_not_collision_free)
{
    ensureJoltInitialized();
    const std::string whole = skyrimFile(floorMesh());
    const auto reader = MWPhysics::SkyrimShape::makeSceneShapeReader();
    EXPECT_EQ(reader->read(whole.substr(0, whole.size() / 2), "meshes/architecture/cutfloor.nif"), nullptr);
    EXPECT_FALSE(MWPhysics::SkyrimShape::modelReadWithNoCollision("meshes/architecture/cutfloor.nif"));
}

TEST(SkyrimShape, the_stand_in_box_is_only_for_a_missing_mesh_with_bounds_that_is_not_known_collision_free)
{
    using MWPhysics::SkyrimShape::standInBoxWanted;
    EXPECT_TRUE(standInBoxWanted(false, true, false)); // read failed, over the cap, or no model: the old fallback
    EXPECT_FALSE(standInBoxWanted(false, true, true)); // read fine, nothing to collide with: no collision
    EXPECT_FALSE(standInBoxWanted(true, true, false)); // the mesh is the collision
    EXPECT_FALSE(standInBoxWanted(false, false, false)); // no bounds, no box
    EXPECT_FALSE(standInBoxWanted(true, false, true));
}

TEST(SkyrimShape, a_cutout_model_collides)
{
    ensureJoltInitialized();
    MeshSpec leaf = floorMesh();
    leaf.mAlphaFlags = 0x201; // blend on, test on
    EXPECT_NE(shapeFrom(leaf), nullptr);
}

TEST(SkyrimShape, a_truncated_file_gives_no_shape_and_is_counted_not_thrown)
{
    ensureJoltInitialized();
    const std::string whole = skyrimFile(floorMesh());
    const auto reader = MWPhysics::SkyrimShape::makeSceneShapeReader();
    const std::uint64_t before = MWPhysics::SkyrimShape::counters().mFailed.load();
    vsg::ref_ptr<Resource::PhysicsShape> shape;
    EXPECT_NO_THROW(shape = reader->read(whole.substr(0, whole.size() / 2), "meshes/cut.nif"));
    EXPECT_EQ(shape, nullptr);
    EXPECT_GT(MWPhysics::SkyrimShape::counters().mFailed.load(), before);
}

TEST(SkyrimShape, the_counters_describe_what_was_built)
{
    ensureJoltInitialized();
    ASSERT_NE(shapeFrom(floorMesh()), nullptr);
    const std::string line = MWPhysics::SkyrimShape::describeCounters();
    EXPECT_NE(line.find("Skyrim collision:"), std::string::npos);
    EXPECT_NE(line.find("mesh shapes"), std::string::npos);
    EXPECT_GT(MWPhysics::SkyrimShape::counters().mBytes.load(), 0u);
}

namespace
{
    struct SkyrimShapeManagerTest : ::testing::Test
    {
        const ToUTF8::Utf8Encoder mEncoder{ ToUTF8::WINDOWS_1252 };
        std::vector<std::unique_ptr<TestingOpenMW::VFSTestFile>> mFiles;
        std::unique_ptr<VFS::Manager> mVfs;
        std::unique_ptr<Resource::NifFileManager> mNifFileManager;
        std::unique_ptr<Resource::PhysicsShapeManager> mManager;

        void SetUp() override
        {
            ensureJoltInitialized();
            VFS::FileMap fileMap;
            mFiles.push_back(std::make_unique<TestingOpenMW::VFSTestFile>(skyrimFile(floorMesh())));
            fileMap.emplace(VFS::Path::Normalized("meshes/floor.nif"), mFiles.back().get());
            mVfs = TestingOpenMW::createTestVFS(std::move(fileMap));
            mNifFileManager = std::make_unique<Resource::NifFileManager>(mVfs.get(), &mEncoder.getStatelessEncoder());
            mManager = std::make_unique<Resource::PhysicsShapeManager>(mVfs.get(), mNifFileManager.get(), 1.0);
        }

        void TearDown() override
        {
            mManager.reset();
            mNifFileManager.reset();
            mVfs.reset();
        }
    };

    constexpr VFS::Path::NormalizedView floorPath("meshes/floor.nif");
}

TEST_F(SkyrimShapeManagerTest, with_the_hook_a_skyrim_file_gets_a_cached_mesh_shape)
{
    mManager->setSceneShapeReader(MWPhysics::SkyrimShape::makeSceneShapeReader());
    vsg::ref_ptr<const Resource::PhysicsShape> first = mManager->getShape(floorPath);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(mManager->getShape(floorPath), first) << "the second call comes from the cache";
    vsg::ref_ptr<Resource::PhysicsShapeInstance> instance = mManager->getInstance(floorPath);
    ASSERT_NE(instance, nullptr);
    EXPECT_NE(instance->mCollisionShape, nullptr);
}

TEST_F(SkyrimShapeManagerTest, without_the_hook_the_manager_does_what_it_always_did_with_the_file)
{
    // The Morrowind reader refuses the file; the manager's old behaviour (a throw or no shape) is unchanged and no
    // mesh is made. Either is the old behaviour; what must not happen is a shape.
    vsg::ref_ptr<const Resource::PhysicsShape> shape;
    try
    {
        shape = mManager->getShape(floorPath);
    }
    catch (const std::exception&)
    {
    }
    EXPECT_EQ(shape, nullptr);
}
