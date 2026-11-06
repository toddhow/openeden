#include <components/nif/exception.hpp>
#include <components/nifscene/build.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ssebuilder.hpp"

// Task 0082: the scene of generated Skyrim SE files. Every number below is written by the test that expects it.
namespace
{
    using namespace NifScene;
    using namespace NifScene::Testing;

    constexpr VFS::Path::NormalizedView path("scene-test.nif");

    Scene read(const GamebryoFile& file, const SceneOptions& options = {})
    {
        auto input = std::make_unique<std::istringstream>(file.build());
        input->exceptions(std::ios::failbit | std::ios::badbit);
        return readScene(std::move(input), path, options);
    }

    std::string errorOf(const GamebryoFile& file, const SceneOptions& options = {})
    {
        try
        {
            read(file, options);
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        return {};
    }

    Vertex vertex(float x, float y, float z, float u, float v)
    {
        Vertex result;
        result.mPosition = { x, y, z };
        result.mUV = { u, v };
        return result;
    }

    constexpr std::uint16_t plainFlags = VF_Vertex | VF_UVs | VF_Normals | VF_Tangents | VF_Colors;

    // Root (0) with a plain quad (1).
    GamebryoFile quadFile()
    {
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "Root", { 1 }, { 1.f, 2.f, 3.f }, 2.f, "BSFadeNode"));
        ShapeSpec spec;
        spec.mName = "Quad";
        spec.mFlags = plainFlags;
        Vertex a = vertex(0, 0, 0, 0, 0);
        Vertex b = vertex(1, 0, 0, 1, 0);
        Vertex c = vertex(1, 1, 0, 1, 1);
        Vertex d = vertex(0, 1, 0, 0, 1);
        b.mColor = { 0, 51, 255, 128 };
        a.mBitangentX = 0.f;
        spec.mVertices = { a, b, c, d };
        spec.mTriangles = { { 0, 1, 2 }, { 0, 2, 3 } };
        spec.mTranslation = { 0.f, 5.f, 0.f };
        file.mBlocks.push_back(shape(file, spec));
        return file;
    }

    TEST(NifSceneBuild, a_plain_shape_becomes_a_node_and_a_geometry)
    {
        const Scene scene = read(quadFile());
        EXPECT_EQ(scene.mVersion, 0x14020007u);
        EXPECT_EQ(scene.mUserVersion, 12u);
        EXPECT_EQ(scene.mBethVersion, 100u);
        ASSERT_EQ(scene.mNodes.size(), 2u);
        EXPECT_EQ(scene.mNodes[0].mName, "Root");
        EXPECT_EQ(scene.mNodes[0].mRecordType, "BSFadeNode");
        EXPECT_EQ(scene.mNodes[0].mParent, sNone);
        EXPECT_EQ(scene.mNodes[1].mName, "Quad");
        EXPECT_EQ(scene.mNodes[1].mParent, 0u);
        EXPECT_EQ(scene.mNodes[1].mRecordIndex, 1u);
        EXPECT_EQ(scene.mNodes[1].mTransform.mTranslation, (Vec3{ 0.f, 5.f, 0.f }));
        EXPECT_EQ(scene.mNodes[1].mGeometry, 0u);
        ASSERT_EQ(scene.mRoots.size(), 1u);
        EXPECT_EQ(scene.mRoots[0], 0u);
        EXPECT_TRUE(scene.mWarnings.empty()) << scene.mWarnings.front();
    }

    TEST(NifSceneBuild, the_roots_transform_is_kept_because_the_morrowind_rule_is_off_for_scenes)
    {
        const Scene scene = read(quadFile());
        EXPECT_EQ(scene.mNodes[0].mTransform.mTranslation, (Vec3{ 1.f, 2.f, 3.f }));
        EXPECT_EQ(scene.mNodes[0].mTransform.mScale, 2.f);
    }

    TEST(NifSceneBuild, the_morrowind_rule_can_be_asked_for)
    {
        SceneOptions options;
        options.mRead.mMorrowindRootRule = true;
        const Scene scene = read(quadFile(), options);
        EXPECT_EQ(scene.mNodes[0].mTransform.mTranslation, (Vec3{ 0.f, 0.f, 0.f }));
        EXPECT_EQ(scene.mNodes[0].mTransform.mScale, 1.f);
    }

    TEST(NifSceneBuild, the_vertex_arrays_hold_what_the_file_holds)
    {
        const Scene scene = read(quadFile());
        ASSERT_EQ(scene.mGeometries.size(), 1u);
        const Geometry& g = scene.mGeometries[0];
        EXPECT_EQ(g.mName, "Quad");
        EXPECT_EQ(g.mKind, GeometryKind::TriShape);
        EXPECT_EQ(g.mNode, 1u);
        EXPECT_FALSE(g.mVerticesFromSkinPartition);
        EXPECT_FALSE(g.mNoVertexData);
        ASSERT_EQ(g.mPositions.size(), 4u);
        EXPECT_EQ(g.mPositions[2], (Vec3{ 1.f, 1.f, 0.f }));
        ASSERT_EQ(g.mUV0.size(), 4u);
        EXPECT_EQ(g.mUV0[1], (Vec2{ 1.f, 0.f }));
        EXPECT_EQ(g.mUV0[3], (Vec2{ 0.f, 1.f }));
        ASSERT_EQ(g.mNormals.size(), 4u);
        EXPECT_NEAR(g.mNormals[0][2], 1.f, 1e-6f);
        ASSERT_EQ(g.mTangents.size(), 4u);
        ASSERT_EQ(g.mColors.size(), 4u);
        EXPECT_FLOAT_EQ(g.mColors[1][1], 0.2f);
        EXPECT_FLOAT_EQ(g.mColors[1][3], 128.f / 255.f);
        EXPECT_EQ(g.mTriangles, (std::vector<std::uint32_t>{ 0, 1, 2, 0, 2, 3 }));
        EXPECT_TRUE(g.mInfluences.empty());
        EXPECT_TRUE(g.mInfluenceStart.empty());
        EXPECT_EQ(g.mSkin, sNone);
    }

    // Root (0), shape (1) with Data Size 0 and a skin, bones A (2) and B (3), skin instance (4), skin data (5),
    // skin partition (6) holding the vertices.
    struct SkinnedCase
    {
        GamebryoFile mFile;
        std::vector<Vertex> mStream;
        std::vector<PartitionSpec> mPartitions;
        std::uint16_t mFlags = VF_Vertex | VF_UVs | VF_Normals | VF_Skinned;
        std::size_t mSkinBones = 2;

        SkinnedCase()
        {
            Vertex v0 = vertex(0, 0, 0, 0, 0);
            v0.mWeights = { 1.f, 0.f, 0.f, 0.f };
            v0.mBones = { 1, 0, 0, 0 };
            Vertex v1 = vertex(1, 0, 0, 1, 0);
            v1.mWeights = { 0.5f, 0.5f, 0.f, 0.f };
            v1.mBones = { 0, 1, 0, 0 };
            Vertex v2 = vertex(0, 1, 0, 0, 1);
            v2.mWeights = { 0.75f, 0.25f, 0.f, 0.f };
            v2.mBones = { 1, 0, 0, 0 };
            Vertex v3 = vertex(1, 1, 0, 1, 1);
            v3.mWeights = { 0.25f, 0.5f, 0.125f, 0.125f };
            v3.mBones = { 0, 1, 0, 1 };
            mStream = { v0, v1, v2, v3 };
            PartitionSpec first;
            first.mBones = { 0, 1 };
            first.mVertexMap = { 0, 1, 2 };
            first.mTriangles = { { 0, 1, 2 } };
            PartitionSpec second;
            second.mBones = { 0, 1 };
            second.mVertexMap = { 1, 2, 3 };
            second.mTriangles = { { 1, 3, 2 } };
            mPartitions = { first, second };
        }

        GamebryoFile build()
        {
            GamebryoFile file;
            file.mBlocks.push_back(node(file, "Root", { 1, 2, 3 }));
            ShapeSpec spec;
            spec.mName = "Body";
            spec.mFlags = mFlags;
            spec.mSkin = 4;
            spec.mStoreData = false;
            spec.mVertices.resize(mStream.size()); // the count is in the shape, the data is not
            file.mBlocks.push_back(shape(file, spec));
            file.mBlocks.push_back(node(file, "Bone A", {}));
            file.mBlocks.push_back(node(file, "Bone B", {}, { 0.f, 0.f, 1.f }));
            std::vector<std::int32_t> bones = { 2, 3 };
            bones.resize(mSkinBones, 3);
            file.mBlocks.push_back(skinInstance(5, 6, 0, bones));
            file.mBlocks.push_back(skinData(mSkinBones));
            file.mBlocks.push_back(skinPartition(mStream, mFlags, mPartitions));
            return file;
        }
    };

    TEST(NifSceneBuild, a_skinned_shape_with_data_size_zero_takes_its_vertices_from_the_skin_partition)
    {
        SkinnedCase c;
        const Scene scene = read(c.build());
        ASSERT_EQ(scene.mGeometries.size(), 1u);
        const Geometry& g = scene.mGeometries[0];
        EXPECT_TRUE(g.mVerticesFromSkinPartition);
        EXPECT_FALSE(g.mNoVertexData);
        ASSERT_EQ(g.mPositions.size(), 4u);
        EXPECT_EQ(g.mPositions[3], (Vec3{ 1.f, 1.f, 0.f }));
        ASSERT_EQ(g.mUV0.size(), 4u);
        EXPECT_EQ(g.mUV0[2], (Vec2{ 0.f, 1.f }));
        // The triangles are the partitions' own lists, in partition order, with global vertex indices.
        EXPECT_EQ(g.mTriangles, (std::vector<std::uint32_t>{ 0, 1, 2, 1, 3, 2 }));
    }

    TEST(NifSceneBuild, partitions_remember_which_triangles_are_theirs)
    {
        SkinnedCase c;
        const Scene scene = read(c.build());
        ASSERT_EQ(scene.mSkins.size(), 1u);
        const Skin& skin = scene.mSkins[0];
        ASSERT_EQ(skin.mPartitions.size(), 2u);
        EXPECT_EQ(skin.mPartitions[0].mFirstTriangle, 0u);
        EXPECT_EQ(skin.mPartitions[0].mTriangleCount, 1u);
        EXPECT_EQ(skin.mPartitions[1].mFirstTriangle, 1u);
        EXPECT_EQ(skin.mPartitions[1].mTriangleCount, 1u);
        EXPECT_EQ(skin.mPartitions[0].mBones, (std::vector<std::uint16_t>{ 0, 1 }));
        EXPECT_TRUE(skin.mPartitions[0].mGlobalVB);
    }

    TEST(NifSceneBuild, influences_use_skin_bone_indices_and_drop_the_zero_padding)
    {
        SkinnedCase c;
        const Scene scene = read(c.build());
        const Geometry& g = scene.mGeometries[0];
        ASSERT_EQ(g.mInfluenceStart.size(), 5u);
        EXPECT_EQ(g.mInfluenceStart, (std::vector<std::uint32_t>{ 0, 1, 3, 5, 9 }));
        ASSERT_EQ(g.mInfluences.size(), 9u);
        EXPECT_EQ(g.mInfluences[0].mBone, 1u);
        EXPECT_FLOAT_EQ(g.mInfluences[0].mWeight, 1.f);
        EXPECT_EQ(g.mInfluences[1].mBone, 0u);
        EXPECT_FLOAT_EQ(g.mInfluences[1].mWeight, 0.5f);
        EXPECT_EQ(g.mInfluences[2].mBone, 1u);
        // Vertex 3 has four influences, two of them on the same bone: all four are kept as the file has them.
        EXPECT_EQ(g.mInfluences[5].mBone, 0u);
        EXPECT_FLOAT_EQ(g.mInfluences[5].mWeight, 0.25f);
        EXPECT_EQ(g.mInfluences[8].mBone, 1u);
        EXPECT_FLOAT_EQ(g.mInfluences[8].mWeight, 0.125f);
    }

    TEST(NifSceneBuild, zero_weights_are_kept_when_asked)
    {
        SkinnedCase c;
        SceneOptions options;
        options.mKeepZeroWeights = true;
        const Scene scene = read(c.build(), options);
        const Geometry& g = scene.mGeometries[0];
        ASSERT_EQ(g.mInfluenceStart.size(), 5u);
        EXPECT_EQ(g.mInfluenceStart.back(), 16u);
        EXPECT_EQ(g.mInfluences.size(), 16u);
    }

    TEST(NifSceneBuild, the_skin_names_its_bones_and_keeps_their_bind_transforms)
    {
        SkinnedCase c;
        const Scene scene = read(c.build());
        const Skin& skin = scene.mSkins[0];
        EXPECT_EQ(scene.mGeometries[0].mSkin, 0u);
        ASSERT_EQ(skin.mBoneNames.size(), 2u);
        EXPECT_EQ(skin.mBoneNames[0], "Bone A");
        EXPECT_EQ(skin.mBoneNames[1], "Bone B");
        ASSERT_EQ(skin.mBoneNodes.size(), 2u);
        EXPECT_EQ(scene.mNodes[skin.mBoneNodes[1]].mName, "Bone B");
        ASSERT_EQ(skin.mBindTransforms.size(), 2u);
        EXPECT_EQ(skin.mBindTransforms[1].mTranslation, (Vec3{ 1.f, 0.f, 0.f }));
        EXPECT_EQ(skin.mSkinTransform.mTranslation, (Vec3{ 0.f, 0.f, 0.5f }));
    }

    TEST(NifSceneBuild, partition_bone_lists_that_are_not_the_identity_are_counted_not_refused)
    {
        SkinnedCase c;
        c.mPartitions[1].mBones = { 1, 0 };
        const Scene scene = read(c.build());
        const Skin& skin = scene.mSkins[0];
        EXPECT_EQ(skin.mNonIdentityPartitionBoneMaps, 1u);
        EXPECT_EQ(skin.mInfluencesOutsidePartitionBones, 0u);
        ASSERT_FALSE(scene.mWarnings.empty());
        EXPECT_NE(scene.mWarnings[0].find("Body"), std::string::npos) << scene.mWarnings[0];
        // The vertex stream is still read as skin bone indices.
        EXPECT_EQ(scene.mGeometries[0].mInfluences[0].mBone, 1u);
    }

    TEST(NifSceneBuild, an_influence_on_a_bone_the_partition_does_not_list_is_counted)
    {
        SkinnedCase c;
        c.mPartitions[0].mBones = { 0 };
        const Scene scene = read(c.build());
        // Partition 0 holds vertices 0, 1, 2; vertices 0 and 2 weight bone 1 and vertex 1 weights bone 1 at 0.5.
        EXPECT_EQ(scene.mSkins[0].mInfluencesOutsidePartitionBones, 3u);
    }

    TEST(NifSceneBuild, a_bone_index_outside_the_skin_is_a_located_error)
    {
        SkinnedCase c;
        c.mStream[2].mBones = { 5, 0, 0, 0 };
        const std::string error = errorOf(c.build());
        EXPECT_NE(error.find("bone index 5"), std::string::npos) << error;
        EXPECT_NE(error.find("scene-test.nif"), std::string::npos) << error;
        EXPECT_NE(error.find("Body"), std::string::npos) << error;
    }

    TEST(NifSceneBuild, a_bone_index_with_zero_weight_is_ignored)
    {
        SkinnedCase c;
        c.mStream[0].mBones = { 1, 9, 9, 9 }; // the padding slots carry weight 0
        EXPECT_EQ(errorOf(c.build()), "");
    }

    TEST(NifSceneBuild, a_partition_triangle_outside_the_vertices_is_a_located_error)
    {
        SkinnedCase c;
        c.mPartitions[1].mTriangles = { { 1, 9, 2 } };
        const std::string error = errorOf(c.build());
        EXPECT_NE(error.find("triangle index 9"), std::string::npos) << error;
        EXPECT_NE(error.find("scene-test.nif"), std::string::npos) << error;
        EXPECT_NE(error.find("Body"), std::string::npos) << error;
    }

    TEST(NifSceneBuild, a_shape_triangle_outside_the_vertices_is_a_located_error)
    {
        GamebryoFile file = quadFile();
        ShapeSpec spec;
        spec.mName = "Bad";
        spec.mFlags = VF_Vertex;
        spec.mVertices = { vertex(0, 0, 0, 0, 0), vertex(1, 0, 0, 0, 0) };
        spec.mTriangles = { { 0, 1, 2 } };
        file.mBlocks[1] = shape(file, spec);
        const std::string error = errorOf(file);
        EXPECT_NE(error.find("triangle index 2"), std::string::npos) << error;
        EXPECT_NE(error.find("Bad"), std::string::npos) << error;
    }

    TEST(NifSceneBuild, a_skinned_shape_without_any_vertex_data_is_empty_and_says_so)
    {
        SkinnedCase c;
        GamebryoFile file = c.build();
        // Replace the skin partition by an empty one: no vertex data anywhere.
        file.mBlocks[6] = skinPartition({}, c.mFlags, {});
        const Scene scene = read(file);
        const Geometry& g = scene.mGeometries[0];
        EXPECT_TRUE(g.mNoVertexData);
        EXPECT_TRUE(g.mPositions.empty());
        EXPECT_TRUE(g.mTriangles.empty());
    }

    TEST(NifSceneBuild, a_vertex_size_that_disagrees_with_the_attribute_flags_is_a_warning)
    {
        GamebryoFile file = quadFile();
        ShapeSpec spec;
        spec.mName = "Odd";
        spec.mFlags = VF_Vertex;
        spec.mDeclaredStride = 24; // the flags make 16 bytes
        spec.mVertices = { vertex(0, 0, 0, 0, 0) };
        file.mBlocks[1] = shape(file, spec);
        const Scene scene = read(file);
        ASSERT_FALSE(scene.mWarnings.empty());
        EXPECT_NE(scene.mWarnings[0].find("Odd"), std::string::npos) << scene.mWarnings[0];
        EXPECT_NE(scene.mWarnings[0].find("vertex size"), std::string::npos) << scene.mWarnings[0];
    }

    TEST(NifSceneBuild, a_dynamic_tri_shape_takes_positions_from_the_dynamic_array)
    {
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "Root", { 1 }));
        ShapeSpec spec;
        spec.mName = "Head";
        spec.mType = "BSDynamicTriShape";
        spec.mFlags = VF_Vertex | VF_UVs;
        spec.mVertices = { vertex(9, 9, 9, 0.5f, 0.5f), vertex(8, 8, 8, 0.25f, 0.25f) };
        spec.mTriangles = { { 0, 1, 0 } };
        spec.mDynamic = { { 1.f, 2.f, 3.f, 0.f }, { 4.f, 5.f, 6.f, 0.f } };
        file.mBlocks.push_back(shape(file, spec));
        const Scene scene = read(file);
        const Geometry& g = scene.mGeometries[0];
        EXPECT_EQ(g.mKind, GeometryKind::DynamicTriShape);
        ASSERT_EQ(g.mPositions.size(), 2u);
        EXPECT_EQ(g.mPositions[0], (Vec3{ 1.f, 2.f, 3.f }));
        EXPECT_EQ(g.mPositions[1], (Vec3{ 4.f, 5.f, 6.f }));
        EXPECT_EQ(g.mUV0[1], (Vec2{ 0.25f, 0.25f }));
    }

    TEST(NifSceneBuild, a_dynamic_tri_shape_without_a_position_attribute_still_has_its_positions)
    {
        // Skyrim SE heads: the vertex descriptor has no Vertex attribute and the positions are in the dynamic array.
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "Root", { 1 }));
        ShapeSpec spec;
        spec.mName = "Head";
        spec.mType = "BSDynamicTriShape";
        spec.mFlags = VF_UVs | VF_Normals;
        spec.mVertices = { vertex(0, 0, 0, 0.5f, 0.5f), vertex(0, 0, 0, 0.25f, 0.25f), vertex(0, 0, 0, 0.75f, 0.75f) };
        spec.mTriangles = { { 0, 1, 2 } };
        spec.mDynamic = { { 1.f, 0.f, 0.f, 0.f }, { 0.f, 2.f, 0.f, 0.f }, { 0.f, 0.f, 3.f, 0.f } };
        file.mBlocks.push_back(shape(file, spec));
        const Scene scene = read(file);
        const Geometry& g = scene.mGeometries[0];
        ASSERT_EQ(g.mPositions.size(), 3u);
        EXPECT_EQ(g.mPositions[2], (Vec3{ 0.f, 0.f, 3.f }));
        EXPECT_EQ(g.mTriangles, (std::vector<std::uint32_t>{ 0, 1, 2 }));
        EXPECT_EQ(g.mUV0.size(), 3u);
        EXPECT_TRUE(scene.mWarnings.empty());
    }

    TEST(NifSceneBuild, a_sub_index_tri_shape_is_a_tri_shape_of_its_own_kind)
    {
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "Root", { 1 }));
        ShapeSpec spec;
        spec.mName = "Segments";
        spec.mType = "BSSubIndexTriShape";
        spec.mFlags = VF_Vertex;
        spec.mVertices = { vertex(0, 0, 0, 0, 0), vertex(1, 0, 0, 0, 0), vertex(0, 1, 0, 0, 0) };
        spec.mTriangles = { { 0, 1, 2 } };
        file.mBlocks.push_back(shape(file, spec));
        const Scene scene = read(file);
        ASSERT_EQ(scene.mGeometries.size(), 1u);
        EXPECT_EQ(scene.mGeometries[0].mKind, GeometryKind::SubIndexTriShape);
        EXPECT_EQ(scene.mGeometries[0].mPositions.size(), 3u);
        EXPECT_EQ(scene.mGeometries[0].mTriangles.size(), 3u);
    }

    TEST(NifSceneBuild, other_bethesda_streams_load_with_the_any_set)
    {
        // Skyrim LE (stream 83) with the Any set; the default scene options refuse it.
        GamebryoFile file;
        file.mStreamVersion = 83;
        file.mBlocks.push_back(node(file, "Root", {}));
        SceneOptions options;
        options.mRead.mAllowedVersions = Nif::VersionSet::Any;
        const Scene scene = read(file, options);
        ASSERT_EQ(scene.mNodes.size(), 1u);
        EXPECT_EQ(scene.mBethVersion, 83u);
        EXPECT_NE(errorOf(file).find("stream version 83"), std::string::npos);
    }

    TEST(NifSceneBuild, building_twice_gives_the_same_text)
    {
        SkinnedCase c;
        const GamebryoFile file = c.build();
        const std::string first = dump(read(file));
        const std::string second = dump(read(file));
        EXPECT_FALSE(first.empty());
        EXPECT_EQ(first, second);
    }

    TEST(NifSceneBuild, the_dump_shows_values)
    {
        const std::string text = dump(read(quadFile()));
        EXPECT_NE(text.find("Quad"), std::string::npos);
        EXPECT_NE(text.find("triangles"), std::string::npos);
        // A different file gives different text.
        SkinnedCase c;
        EXPECT_NE(text, dump(read(c.build())));
    }

    TEST(NifSceneBuild, a_file_the_morrowind_set_does_not_allow_is_refused_with_its_name)
    {
        SceneOptions options;
        options.mRead.mAllowedVersions = Nif::VersionSet::Morrowind;
        const std::string error = errorOf(quadFile(), options);
        EXPECT_NE(error.find("Unsupported NIF version"), std::string::npos) << error;
        EXPECT_NE(error.find("scene-test.nif"), std::string::npos) << error;
    }

    TEST(NifSceneBuild, a_morrowind_file_builds_a_scene_with_its_shape_and_the_files_values)
    {
        // Version 4.0.0.2 through the same entry point: a NiNode and a NiTriShape with data.
        Nif::Testing::ByteWriter out;
        out.raw("NetImmerse File Format, Version 4.0.0.2\n");
        out.put<std::uint32_t>(0x04000002);
        out.put<std::uint32_t>(3);
        auto avObject = [&](const std::string& name, float x) {
            out.sized(name);
            out.put<std::int32_t>(-1);
            out.put<std::int32_t>(-1);
            out.put<std::uint16_t>(0);
            out.floats({ x, 0.f, 0.f });
            out.floats({ 1, 0, 0, 0, 1, 0, 0, 0, 1 });
            out.put(1.f);
            out.floats({ 0, 0, 0 });
            out.put<std::int32_t>(0);
            out.put<std::int32_t>(0);
        };
        out.sized("NiNode");
        avObject("Root", 7.f);
        out.put<std::int32_t>(1);
        out.put<std::int32_t>(1);
        out.put<std::int32_t>(0);
        out.sized("NiTriShape");
        avObject("Tri", 1.f);
        out.put<std::int32_t>(2);
        out.put<std::int32_t>(-1);
        out.sized("NiTriShapeData");
        out.put<std::uint16_t>(3);
        out.put<std::int32_t>(1);
        out.floats({ 0, 0, 0, 1, 0, 0, 0, 1, 0 });
        out.put<std::int32_t>(0);
        out.floats({ 0, 0, 0, 1 });
        out.put<std::int32_t>(0);
        out.put<std::uint16_t>(0);
        out.put<std::int32_t>(0);
        out.put<std::uint16_t>(1);
        out.put<std::uint32_t>(3);
        for (const std::uint16_t index : std::array<std::uint16_t, 3>{ 0, 1, 2 })
            out.put(index);
        out.put<std::uint16_t>(0);
        out.put<std::uint32_t>(1);
        out.put<std::int32_t>(0);

        auto input = std::make_unique<std::istringstream>(out.bytes());
        input->exceptions(std::ios::failbit | std::ios::badbit);
        const Scene scene = readScene(std::move(input), path, SceneOptions{});
        ASSERT_EQ(scene.mNodes.size(), 2u);
        EXPECT_EQ(scene.mNodes[1].mName, "Tri");
        ASSERT_EQ(scene.mGeometries.size(), 1u);
        EXPECT_EQ(scene.mGeometries[0].mPositions.size(), 3u);
        EXPECT_EQ(scene.mGeometries[0].mPositions[1], (Vec3{ 1.f, 0.f, 0.f }));
        EXPECT_EQ(scene.mGeometries[0].mTriangles, (std::vector<std::uint32_t>{ 0, 1, 2 }));
        // The scene reader leaves the Morrowind rule off: the root keeps its translation.
        EXPECT_EQ(scene.mNodes[0].mTransform.mTranslation, (Vec3{ 7.f, 0.f, 0.f }));
    }
}
