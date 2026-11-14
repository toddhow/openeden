#include <components/nifscene/build.hpp>
#include <components/nifscene/havok.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <sstream>

#include "ssebuilder.hpp"

// Task 0144 (stage b): the bhk collision reader. Layouts are written here by hand from Nif::*::read in
// components/nif/physics.cpp for Skyrim SE (stream version 100); every expected number is written by the test.
namespace
{
    using namespace NifScene;
    using namespace NifScene::Testing;
    namespace H = NifScene::Havok;

    constexpr VFS::Path::NormalizedView path("bhk-test.nif");

    Scene read(const GamebryoFile& file)
    {
        SceneOptions options;
        options.mReadHavok = true;
        auto input = std::make_unique<std::istringstream>(file.build());
        input->exceptions(std::ios::failbit | std::ios::badbit);
        return readScene(std::move(input), path, options);
    }

    // A node whose collision object is `collision` (a record index).
    Block nodeWithCollision(GamebryoFile& file, std::int32_t collision)
    {
        Block block;
        block.mType = "BSFadeNode";
        auto& out = block.mData;
        out.put(file.string("Root"));
        out.put(static_cast<std::uint32_t>(0));
        out.put(static_cast<std::int32_t>(-1));
        out.put(static_cast<std::uint32_t>(14));
        out.floats({ 0, 0, 0 });
        out.floats({ 1, 0, 0, 0, 1, 0, 0, 0, 1 });
        out.put(1.f);
        out.put(collision);
        out.put(static_cast<std::uint32_t>(0)); // children
        out.put(static_cast<std::uint32_t>(0)); // effects (stream version below 130)
        return block;
    }

    Block collisionObject(std::int32_t target, std::int32_t body)
    {
        Block block;
        block.mType = "bhkCollisionObject";
        block.mData.put(target);
        block.mData.put(static_cast<std::uint16_t>(0x81));
        block.mData.put(body);
        return block;
    }

    struct BodySpec
    {
        std::int32_t mShape = -1;
        bool mT = false;
        std::uint8_t mLayer = 1;
        std::uint8_t mMotion = 7;
        std::array<float, 4> mTranslation{ 0, 0, 0, 0 };
        std::array<float, 4> mRotationFile{ 0, 0, 0, 1 }; // as in the file
    };

    Block rigidBody(const BodySpec& spec)
    {
        Block block;
        block.mType = spec.mT ? "bhkRigidBodyT" : "bhkRigidBody";
        auto& out = block.mData;
        out.put(spec.mShape);
        out.put(spec.mLayer); // filter of the world object
        out.put(static_cast<std::uint8_t>(0));
        out.put(static_cast<std::uint16_t>(0));
        out.put(static_cast<std::uint32_t>(0)); // unused
        out.put(static_cast<std::uint8_t>(1)); // phase type
        out.put(static_cast<std::uint8_t>(0));
        out.put(static_cast<std::uint8_t>(0));
        out.put(static_cast<std::uint8_t>(0));
        out.put(static_cast<std::uint32_t>(0)); // property data, size, capacity
        out.put(static_cast<std::uint32_t>(0));
        out.put(static_cast<std::uint32_t>(0));
        out.put(static_cast<std::uint8_t>(1)); // entity info: response
        out.put(static_cast<std::uint8_t>(0));
        out.put(static_cast<std::uint16_t>(0));
        // rigid body info
        out.put(static_cast<std::uint32_t>(0));
        out.put(spec.mLayer);
        out.put(static_cast<std::uint8_t>(0));
        out.put(static_cast<std::uint16_t>(0));
        out.put(static_cast<std::uint32_t>(0));
        out.put(static_cast<std::uint32_t>(0)); // bethversion >= 83
        out.put(static_cast<std::uint8_t>(1));
        out.put(static_cast<std::uint8_t>(0));
        out.put(static_cast<std::uint16_t>(0));
        out.floats({ spec.mTranslation[0], spec.mTranslation[1], spec.mTranslation[2], spec.mTranslation[3] });
        out.floats({ spec.mRotationFile[0], spec.mRotationFile[1], spec.mRotationFile[2], spec.mRotationFile[3] });
        out.floats({ 0, 0, 0, 0 }); // linear velocity
        out.floats({ 0, 0, 0, 0 }); // angular velocity
        for (int i = 0; i < 3; ++i)
            out.floats({ 0, 0, 0, 0 }); // inertia rows with padding
        out.floats({ 0, 0, 0, 0 }); // centre
        out.floats({ 5.f, 0.1f, 0.2f, 1.f, 1.f, 0.5f, 1.f, 0.4f, 100.f, 100.f, 0.15f });
        // mass, linear damping, angular damping, time factor, gravity, friction, rolling friction, restitution,
        // max linear, max angular, penetration depth
        out.put(spec.mMotion);
        out.put(static_cast<std::uint8_t>(1)); // enable deactivation
        out.put(static_cast<std::uint8_t>(1)); // solver deactivation
        out.put(static_cast<std::uint8_t>(1)); // quality
        out.put(static_cast<std::uint8_t>(0));
        out.put(static_cast<std::uint8_t>(0));
        out.put(static_cast<std::uint8_t>(0));
        out.put(static_cast<std::uint8_t>(0));
        for (int i = 0; i < 12; ++i)
            out.put(static_cast<std::uint8_t>(0));
        out.put(static_cast<std::uint32_t>(0)); // constraints
        out.put(static_cast<std::uint16_t>(0)); // body flags
        return block;
    }

    Block boxShape(float x, float y, float z, float radius)
    {
        Block block;
        block.mType = "bhkBoxShape";
        block.mData.put(static_cast<std::uint32_t>(7)); // material
        block.mData.put(radius);
        block.mData.put(static_cast<std::uint64_t>(0));
        block.mData.floats({ x, y, z });
        block.mData.put(0.f);
        return block;
    }

    Block moppShape(std::int32_t child)
    {
        Block block;
        block.mType = "bhkMoppBvTreeShape";
        block.mData.put(child);
        for (int i = 0; i < 12; ++i)
            block.mData.put(static_cast<std::uint8_t>(0));
        block.mData.put(1.f);
        block.mData.put(static_cast<std::uint32_t>(2)); // code size
        block.mData.floats({ 0, 0, 0, 0 });
        block.mData.put(static_cast<std::uint8_t>(0));
        block.mData.put(static_cast<std::uint8_t>(1));
        block.mData.put(static_cast<std::uint8_t>(2));
        return block;
    }

    Block listShape(const std::vector<std::int32_t>& children)
    {
        Block block;
        block.mType = "bhkListShape";
        block.mData.put(static_cast<std::uint32_t>(children.size()));
        for (std::int32_t c : children)
            block.mData.put(c);
        block.mData.put(static_cast<std::uint32_t>(7));
        for (int i = 0; i < 6; ++i) // two 12 byte properties
            block.mData.put(static_cast<std::uint32_t>(0));
        block.mData.put(static_cast<std::uint32_t>(0)); // filters
        return block;
    }

    Block unknownShape()
    {
        // bhkPlaneShape: material, 12 unused, plane (4 floats), extents (4), center (4)
        Block block;
        block.mType = "bhkPlaneShape";
        block.mData.put(static_cast<std::uint32_t>(0));
        for (int i = 0; i < 3; ++i)
            block.mData.put(static_cast<std::uint32_t>(0));
        block.mData.floats({ 0, 0, 1, 0 });
        block.mData.floats({ 1, 1, 1, 0 });
        block.mData.floats({ 0, 0, 0, 0 });
        return block;
    }

    // Root (0), collision object (1), body (2), then the shapes.
    GamebryoFile fileWith(const BodySpec& body, std::vector<Block> shapes)
    {
        GamebryoFile file;
        file.mBlocks.push_back(nodeWithCollision(file, 1));
        file.mBlocks.push_back(collisionObject(0, 2));
        BodySpec spec = body;
        spec.mShape = shapes.empty() ? -1 : 3;
        file.mBlocks.push_back(rigidBody(spec));
        for (Block& b : shapes)
            file.mBlocks.push_back(std::move(b));
        return file;
    }
}

TEST(BhkCollision, a_box_in_a_fixed_body_is_read_with_its_filter_and_motion)
{
    BodySpec body;
    body.mLayer = 2;
    body.mMotion = 7;
    const Scene scene = read(fileWith(body, { boxShape(1.f, 2.f, 3.f, 0.25f) }));
    ASSERT_EQ(scene.mHavok.mBodies.size(), 1u);
    const H::Body& b = scene.mHavok.mBodies[0];
    EXPECT_EQ(b.mNodeRecord, 0u);
    EXPECT_EQ(b.mLayer, 2);
    EXPECT_EQ(b.mWorldObjectLayer, 2);
    EXPECT_EQ(b.mMotionType, 7);
    EXPECT_FALSE(b.mHasTransform);
    ASSERT_NE(b.mShape, H::sNone);
    const H::Shape& s = scene.mHavok.mShapes[b.mShape];
    EXPECT_EQ(s.mKind, H::ShapeKind::Box);
    EXPECT_EQ(s.mHalfExtents, (H::Vec3{ 1.f, 2.f, 3.f }));
    EXPECT_FLOAT_EQ(s.mRadius, 0.25f);
    EXPECT_EQ(s.mMaterial, 7u);
    EXPECT_FLOAT_EQ(b.mMass, 5.f);
    EXPECT_FLOAT_EQ(b.mFriction, 0.5f);
}

TEST(BhkCollision, the_reader_is_off_unless_asked)
{
    BodySpec body;
    auto input = std::make_unique<std::istringstream>(fileWith(body, { boxShape(1, 1, 1, 0) }).build());
    const Scene scene = readScene(std::move(input), path, SceneOptions{});
    EXPECT_TRUE(scene.mHavok.mBodies.empty());
}

TEST(BhkCollision, a_mopp_wrapper_is_transparent_and_a_rigid_body_t_is_marked)
{
    BodySpec body;
    body.mT = true;
    body.mTranslation = { 1.f, 2.f, 3.f, 0.f };
    const Scene scene = read(fileWith(body, { moppShape(4), boxShape(1, 1, 1, 0) }));
    ASSERT_EQ(scene.mHavok.mBodies.size(), 1u);
    const H::Body& b = scene.mHavok.mBodies[0];
    EXPECT_TRUE(b.mHasTransform);
    EXPECT_EQ(b.mTranslation, (H::Vec3{ 1.f, 2.f, 3.f }));
    const H::Shape& mopp = scene.mHavok.mShapes[b.mShape];
    EXPECT_EQ(mopp.mKind, H::ShapeKind::List);
    ASSERT_EQ(mopp.mChildren.size(), 1u);
    EXPECT_EQ(scene.mHavok.mShapes[mopp.mChildren[0]].mKind, H::ShapeKind::Box);
}

TEST(BhkCollision, a_list_keeps_its_children_in_order_and_an_unknown_block_is_marked_unsupported)
{
    BodySpec body;
    const Scene scene = read(fileWith(body, { listShape({ 4, 5 }), boxShape(1, 1, 1, 0), unknownShape() }));
    ASSERT_EQ(scene.mHavok.mBodies.size(), 1u);
    const H::Shape& list = scene.mHavok.mShapes[scene.mHavok.mBodies[0].mShape];
    ASSERT_EQ(list.mChildren.size(), 2u);
    EXPECT_EQ(scene.mHavok.mShapes[list.mChildren[0]].mKind, H::ShapeKind::Box);
    EXPECT_EQ(scene.mHavok.mShapes[list.mChildren[1]].mKind, H::ShapeKind::Unsupported);
    EXPECT_EQ(scene.mHavok.mShapes[list.mChildren[1]].mTypeName, "bhkPlaneShape");
    EXPECT_EQ(scene.mHavok.mReport.mShapesUnsupported, 1u);
}

TEST(BhkCollision, a_shape_shared_by_two_parents_is_read_once)
{
    BodySpec body;
    const Scene scene = read(fileWith(body, { listShape({ 4, 4 }), boxShape(1, 1, 1, 0) }));
    const H::Shape& list = scene.mHavok.mShapes[scene.mHavok.mBodies[0].mShape];
    ASSERT_EQ(list.mChildren.size(), 2u);
    EXPECT_EQ(list.mChildren[0], list.mChildren[1]);
    EXPECT_EQ(scene.mHavok.mShapes.size(), 2u);
}

TEST(BhkCollision, a_shape_that_is_not_a_number_is_dropped_and_counted)
{
    BodySpec body;
    const Scene scene = read(fileWith(body, { boxShape(std::nanf(""), 1, 1, 0) }));
    const H::Shape& s = scene.mHavok.mShapes[scene.mHavok.mBodies[0].mShape];
    EXPECT_EQ(s.mKind, H::ShapeKind::Unsupported);
    EXPECT_EQ(scene.mHavok.mReport.mShapesNotFinite, 1u);
}

TEST(BhkCollision, a_node_without_a_collision_object_gives_nothing)
{
    GamebryoFile file;
    file.mBlocks.push_back(node(file, "Root", {}));
    const Scene scene = read(file);
    EXPECT_TRUE(scene.mHavok.mBodies.empty());
    EXPECT_EQ(scene.mHavok.mReport.mCollisionObjects, 0u);
}

// ---- the compressed mesh chunk decoder
// -------------------------------------------------------------------------------

TEST(BhkChunk, vertices_are_translation_plus_step_times_the_stored_value)
{
    H::Chunk chunk;
    chunk.mTranslation = { 10.f, 20.f, 30.f, 0.f };
    chunk.mVertices = { 0, 0, 0, 1000, 2000, 3000, 0, 1000, 0 };
    chunk.mIndices = { 0, 1, 2 };
    std::vector<H::Vec3> v;
    std::vector<std::array<std::uint32_t, 3>> t;
    EXPECT_EQ(H::decodeChunk(chunk, nullptr, v, t), 0u);
    ASSERT_EQ(v.size(), 3u);
    EXPECT_FLOAT_EQ(v[1][0], 11.f);
    EXPECT_FLOAT_EQ(v[1][1], 22.f);
    EXPECT_FLOAT_EQ(v[1][2], 33.f);
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(t[0], (std::array<std::uint32_t, 3>{ 0, 1, 2 }));
}

TEST(BhkChunk, a_strip_alternates_its_winding_and_the_list_follows_the_strips)
{
    H::Chunk chunk;
    chunk.mVertices.assign(15, 0); // five vertices
    chunk.mStrips = { 4 };
    chunk.mIndices = { 0, 1, 2, 3, /* list: */ 2, 3, 4 };
    std::vector<H::Vec3> v;
    std::vector<std::array<std::uint32_t, 3>> t;
    H::decodeChunk(chunk, nullptr, v, t);
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0], (std::array<std::uint32_t, 3>{ 0, 1, 2 }));
    EXPECT_EQ(t[1], (std::array<std::uint32_t, 3>{ 2, 1, 3 }));
    EXPECT_EQ(t[2], (std::array<std::uint32_t, 3>{ 2, 3, 4 }));
}

TEST(BhkChunk, an_index_outside_the_vertices_drops_that_triangle_only)
{
    H::Chunk chunk;
    chunk.mVertices.assign(9, 0);
    chunk.mIndices = { 0, 1, 2, 0, 1, 3 };
    std::vector<H::Vec3> v;
    std::vector<std::array<std::uint32_t, 3>> t;
    EXPECT_EQ(H::decodeChunk(chunk, nullptr, v, t), 1u);
    EXPECT_EQ(t.size(), 1u);
}

TEST(BhkChunk, a_strip_longer_than_the_indices_is_dropped_without_reading_past_them)
{
    H::Chunk chunk;
    chunk.mVertices.assign(9, 0);
    chunk.mStrips = { 200 };
    chunk.mIndices = { 0, 1, 2 };
    std::vector<H::Vec3> v;
    std::vector<std::array<std::uint32_t, 3>> t;
    EXPECT_EQ(H::decodeChunk(chunk, nullptr, v, t), 1u);
    EXPECT_TRUE(t.empty());
}

TEST(BhkChunk, a_chunk_transform_rotates_then_translates)
{
    H::Chunk chunk;
    chunk.mVertices = { 1000, 0, 0 };
    H::ChunkTransform transform;
    const float s = std::sqrt(0.5f);
    transform.mRotation = { 0.f, 0.f, s, s }; // a quarter turn about z
    transform.mTranslation = { 5.f, 0.f, 0.f, 0.f };
    std::vector<H::Vec3> v;
    std::vector<std::array<std::uint32_t, 3>> t;
    H::decodeChunk(chunk, &transform, v, t);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_NEAR(v[0][0], 5.f, 1e-5f);
    EXPECT_NEAR(v[0][1], 1.f, 1e-5f);
    EXPECT_NEAR(v[0][2], 0.f, 1e-5f);
}

TEST(BhkChunk, degenerate_strip_joins_are_skipped_without_counting_as_errors)
{
    H::Chunk chunk;
    chunk.mVertices.assign(12, 0);
    chunk.mStrips = { 5 };
    chunk.mIndices = { 0, 1, 2, 2, 3 };
    std::vector<H::Vec3> v;
    std::vector<std::array<std::uint32_t, 3>> t;
    EXPECT_EQ(H::decodeChunk(chunk, nullptr, v, t), 0u);
    EXPECT_EQ(t.size(), 1u); // (0,1,2) only: (2,1,2) and (2,2,3) are degenerate
}
