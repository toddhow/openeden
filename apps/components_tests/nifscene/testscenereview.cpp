#include <components/nif/exception.hpp>
#include <components/nif/niffile.hpp>
#include <components/nifscene/build.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ssebuilder.hpp"

// Task 0082 review fixes (finding 6): hostile scene shapes the builder must refuse instead of passing on.
namespace
{
    using namespace NifScene;
    using namespace NifScene::Testing;

    constexpr VFS::Path::NormalizedView path("scene-review-test.nif");

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

    // Root (0), shape (1) with Data Size 0 and a skin, bones A (2) and B (3), skin instance (4), skin data (5),
    // skin partition (6) holding the vertices. The vertex count the shape declares can differ from the stream's.
    GamebryoFile skinned(std::vector<Vertex> stream, std::size_t declaredVertices, bool dynamic,
        std::vector<std::array<float, 4>> dynamicPositions = {})
    {
        const std::uint16_t flags = VF_Vertex | VF_UVs | VF_Normals | VF_Skinned;
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "Root", { 1, 2, 3 }));
        ShapeSpec spec;
        spec.mName = "Body";
        spec.mType = dynamic ? "BSDynamicTriShape" : "BSTriShape";
        spec.mFlags = flags;
        spec.mSkin = 4;
        spec.mStoreData = false;
        spec.mVertices.resize(declaredVertices);
        spec.mDynamic = std::move(dynamicPositions);
        file.mBlocks.push_back(shape(file, spec));
        file.mBlocks.push_back(node(file, "Bone A", {}));
        file.mBlocks.push_back(node(file, "Bone B", {}, { 0.f, 0.f, 1.f }));
        file.mBlocks.push_back(skinInstance(5, 6, 0, { 2, 3 }));
        file.mBlocks.push_back(skinData(2));
        PartitionSpec partition;
        partition.mBones = { 0, 1 };
        partition.mVertexMap = { 0, 1, 2 };
        partition.mTriangles = { { 0, 1, 2 } };
        file.mBlocks.push_back(skinPartition(stream, flags, { partition }));
        return file;
    }

    std::vector<Vertex> threeSkinnedVertices()
    {
        std::vector<Vertex> stream = { vertex(0, 0, 0, 0, 0), vertex(1, 0, 0, 1, 0), vertex(0, 1, 0, 0, 1) };
        for (Vertex& v : stream)
        {
            v.mWeights = { 1.f, 0.f, 0.f, 0.f };
            v.mBones = { 1, 0, 0, 0 };
        }
        return stream;
    }

    TEST(NifSceneReview, a_parent_cycle_is_refused)
    {
        // Root (0) lists node B (1) as a child and B lists the root: neither has any other parent.
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "A", { 1 }));
        file.mBlocks.push_back(node(file, "B", { 0 }));
        const std::string error = errorOf(file);
        EXPECT_NE(error.find("cycle"), std::string::npos) << error;
        EXPECT_NE(error.find("scene-review-test.nif"), std::string::npos) << error;
    }

    TEST(NifSceneReview, a_node_that_lists_itself_is_not_a_cycle_error_and_gets_no_parent)
    {
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "A", { 0 }));
        const Scene scene = read(file);
        ASSERT_EQ(scene.mNodes.size(), 1u);
        EXPECT_EQ(scene.mNodes[0].mParent, sNone);
    }

    TEST(NifSceneReview, a_tree_still_builds_with_its_parents)
    {
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "A", { 1 }));
        file.mBlocks.push_back(node(file, "B", { 2 }));
        file.mBlocks.push_back(node(file, "C", {}));
        const Scene scene = read(file);
        ASSERT_EQ(scene.mNodes.size(), 3u);
        EXPECT_EQ(scene.mNodes[0].mParent, sNone);
        EXPECT_EQ(scene.mNodes[1].mParent, 0u);
        EXPECT_EQ(scene.mNodes[2].mParent, 1u);
    }

    TEST(NifSceneReview, a_zero_weight_bone_index_outside_the_skin_is_ignored_by_default)
    {
        std::vector<Vertex> stream = threeSkinnedVertices();
        stream[0].mBones = { 1, 9, 9, 9 };
        EXPECT_EQ(errorOf(skinned(stream, 3, false)), "");
    }

    TEST(NifSceneReview, a_zero_weight_bone_index_outside_the_skin_is_an_error_when_zero_weights_are_kept)
    {
        std::vector<Vertex> stream = threeSkinnedVertices();
        stream[0].mBones = { 1, 9, 9, 9 };
        SceneOptions options;
        options.mKeepZeroWeights = true;
        const std::string error = errorOf(skinned(stream, 3, false), options);
        EXPECT_NE(error.find("bone index 9"), std::string::npos) << error;
        EXPECT_NE(error.find("Body"), std::string::npos) << error;
    }

    TEST(NifSceneReview, kept_zero_weights_inside_the_skin_still_build)
    {
        SceneOptions options;
        options.mKeepZeroWeights = true;
        const Scene scene = read(skinned(threeSkinnedVertices(), 3, false), options);
        EXPECT_EQ(scene.mGeometries[0].mInfluences.size(), 12u);
    }

    TEST(NifSceneReview, a_dynamic_shape_whose_partition_vertices_do_not_match_its_vertex_count_is_refused)
    {
        // The shape declares 4 vertices (and so 4 dynamic positions), the skin partition holds 3.
        const std::vector<std::array<float, 4>> positions
            = { { 1.f, 0.f, 0.f, 0.f }, { 2.f, 0.f, 0.f, 0.f }, { 3.f, 0.f, 0.f, 0.f }, { 4.f, 0.f, 0.f, 0.f } };
        const std::string error = errorOf(skinned(threeSkinnedVertices(), 4, true, positions));
        EXPECT_NE(error.find("Body"), std::string::npos) << error;
        EXPECT_NE(error.find("vertices"), std::string::npos) << error;
    }

    TEST(NifSceneReview, a_dynamic_shape_whose_partition_vertices_match_builds)
    {
        const std::vector<std::array<float, 4>> positions
            = { { 1.f, 0.f, 0.f, 0.f }, { 2.f, 0.f, 0.f, 0.f }, { 3.f, 0.f, 0.f, 0.f } };
        const Scene scene = read(skinned(threeSkinnedVertices(), 3, true, positions));
        const Geometry& g = scene.mGeometries[0];
        ASSERT_EQ(g.mPositions.size(), 3u);
        EXPECT_EQ(g.mPositions[2], (Vec3{ 3.f, 0.f, 0.f }));
        EXPECT_EQ(g.mUV0.size(), 3u);
    }

    // ---- Lane B review, B6: the scene keeps Inf/NaN weights (half exponent 31) and says so ----

    TEST(NifSceneReview, a_non_finite_weight_is_kept_and_named_in_a_warning)
    {
        std::vector<Vertex> stream = threeSkinnedVertices();
        stream[1].mWeights = { std::numeric_limits<float>::infinity(), 0.f, 0.f, 0.f };
        const Scene scene = read(skinned(stream, 3, false));
        const Geometry& g = scene.mGeometries[0];
        bool anyNonFinite = false;
        for (const Influence& influence : g.mInfluences)
            anyNonFinite = anyNonFinite || !std::isfinite(influence.mWeight);
        EXPECT_TRUE(anyNonFinite) << "the value itself is kept: consumers filter";
        bool warned = false;
        for (const std::string& warning : scene.mWarnings)
            warned = warned
                || (warning.find("Body") != std::string::npos && warning.find("non-finite") != std::string::npos);
        EXPECT_TRUE(warned);
    }

    TEST(NifSceneReview, finite_weights_give_no_non_finite_warning)
    {
        const Scene scene = read(skinned(threeSkinnedVertices(), 3, false));
        for (const std::string& warning : scene.mWarnings)
        {
            EXPECT_EQ(warning.find("non-finite"), std::string::npos) << warning;
        }
    }

    // ---- Lane B review, B7: the scene reader never uses the process-wide default read options ----

    TEST(NifSceneReview, readScene_uses_its_own_options_whatever_the_process_default_is)
    {
        const Nif::ReadOptions saved = Nif::Reader::getDefaultReadOptions();
        Nif::ReadOptions morrowindOnly;
        morrowindOnly.mAllowedVersions = Nif::VersionSet::Morrowind;
        Nif::Reader::setDefaultReadOptions(morrowindOnly);
        std::string error;
        try
        {
            const Scene scene = read(skinned(threeSkinnedVertices(), 3, false));
            EXPECT_EQ(scene.mGeometries.size(), 1u);
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        Nif::Reader::setDefaultReadOptions(saved);
        EXPECT_EQ(error, "");
    }
}
