#include <gtest/gtest.h>

#include <limits>
#include <span>

#include <components/lod/clusterbuild.hpp>
#include <components/lod/objectcluster.hpp>

// Cases found by the review of stages S4a and S4b (task 0059).
namespace
{
    using namespace Lod;

    SourcePiece triangle(const std::string& texture)
    {
        SourcePiece piece;
        piece.mTexture = texture;
        piece.mVertices.resize(3);
        piece.mVertices[1].mPosition[0] = 400;
        piece.mVertices[2].mPosition[1] = 400;
        piece.mIndices = { 0, 1, 2 };
        return piece;
    }

    ClusterBuildInput tileWith(SourcePiece piece)
    {
        ClusterBuildInput input;
        input.mLevel.mLevel = 1;
        input.mLevel.mError = 1;
        input.mSide = 16384;
        input.mOriginCellX = 0;
        input.mOriginCellY = 0;
        SourceObject object;
        object.mRef = { 1, 0 };
        object.mTraits.mRadius = 500;
        object.mTraits.mTriangleCount = 1;
        object.mPieces.push_back(std::move(piece));
        input.mObjects.push_back(std::move(object));
        return input;
    }

    TEST(LodClusterReview, a_piece_without_a_texture_is_left_out_and_does_not_fail_the_tile)
    {
        ObjectCluster cluster;
        const LodError error = buildObjectCluster(tileWith(triangle("")), cluster);
        EXPECT_TRUE(error.ok()) << error.describe();
        EXPECT_TRUE(cluster.mGroups.empty());
    }

    TEST(LodClusterReview, a_texture_path_is_made_normal)
    {
        ObjectCluster cluster;
        const LodError error = buildObjectCluster(tileWith(triangle("Textures\\Tx_Rock.DDS")), cluster);
        ASSERT_TRUE(error.ok()) << error.describe();
        ASSERT_EQ(cluster.mGroups.size(), 1u);
        EXPECT_EQ(cluster.mGroups[0].mTexture, "textures/tx_rock.dds");
    }

    TEST(LodClusterReview, a_transform_that_overflows_a_float_leaves_the_piece_out)
    {
        ClusterBuildInput input = tileWith(triangle("a.dds"));
        input.mObjects[0].mTransform[0] = 1e300;
        ObjectCluster cluster;
        const LodError error = buildObjectCluster(input, cluster);
        EXPECT_TRUE(error.ok()) << error.describe();
        EXPECT_TRUE(cluster.mGroups.empty());
    }

    TEST(LodClusterReview, a_vertex_far_outside_the_tile_still_gives_a_valid_cluster)
    {
        ClusterBuildInput input = tileWith(triangle("a.dds"));
        input.mObjects[0].mTransform[3] = 123456.789;
        ObjectCluster cluster;
        // The cell check would refuse a far object only by cell, which is inside the tile here.
        const LodError error = buildObjectCluster(input, cluster);
        ASSERT_TRUE(error.ok()) << error.describe();
        EXPECT_TRUE(validateObjectCluster(cluster).ok());
        EXPECT_GT(cluster.mOverhang, 0.f);
    }

    TEST(LodClusterReview, a_vertex_with_another_cell_slot_than_its_range_is_refused)
    {
        ClusterBuildInput input = tileWith(triangle("a.dds"));
        ObjectCluster cluster;
        ASSERT_TRUE(buildObjectCluster(input, cluster).ok());
        ASSERT_FALSE(cluster.mGroups.empty());
        cluster.mCells.push_back(ClusterCell{ 1, 0, static_cast<std::uint32_t>(cluster.mRefs.size()), 1 });
        cluster.mRefs.push_back(ClusterRef{ 9, 0 });
        cluster.mVertices[0].mCellSlot = 1;
        EXPECT_FALSE(validateObjectCluster(cluster).ok());
    }

    TEST(LodClusterReview, control_characters_in_a_path_are_refused)
    {
        EXPECT_FALSE(isValidClusterTexturePath(std::string_view("a\nb.dds")));
        EXPECT_FALSE(isValidClusterTexturePath(std::string_view("a\0b.dds", 7)));
        EXPECT_TRUE(isValidClusterTexturePath("textures/a.dds"));
    }

    // Stage d: the header read that the runtime's selection uses.
    ObjectCluster simpleCluster()
    {
        ClusterBuildInput input = tileWith(triangle("a.dds"));
        ObjectCluster cluster;
        EXPECT_TRUE(buildObjectCluster(input, cluster).ok());
        return cluster;
    }

    TEST(LodClusterReview, header_of_a_written_cluster_gives_the_selection_numbers)
    {
        const ObjectCluster cluster = simpleCluster();
        std::vector<std::uint8_t> bytes;
        ASSERT_TRUE(writeObjectCluster(cluster, bytes).ok());
        ClusterHeaderInfo info;
        const LodError error = readObjectClusterHeader(std::span<const std::uint8_t>(bytes.data(), objectClusterHeaderSize), info);
        ASSERT_TRUE(error.ok()) << error.describe();
        EXPECT_EQ(info.mLevel, cluster.mLevel);
        EXPECT_EQ(info.mSide, cluster.mSide);
        EXPECT_EQ(info.mOriginX, cluster.mOriginX);
        EXPECT_EQ(info.mOriginY, cluster.mOriginY);
        EXPECT_EQ(info.mMinZ, cluster.mMinZ);
        EXPECT_EQ(info.mMaxZ, cluster.mMaxZ);
        EXPECT_EQ(info.mGeometricError, cluster.mGeometricError);
        EXPECT_EQ(info.mOverhang, cluster.mOverhang);
    }

    TEST(LodClusterReview, header_read_refuses_short_input_and_bad_numbers)
    {
        const ObjectCluster cluster = simpleCluster();
        std::vector<std::uint8_t> bytes;
        ASSERT_TRUE(writeObjectCluster(cluster, bytes).ok());
        ClusterHeaderInfo info;
        EXPECT_FALSE(readObjectClusterHeader(std::span<const std::uint8_t>(bytes.data(), 50), info).ok());
        std::vector<std::uint8_t> bad = bytes;
        bad[0] = 'X';
        EXPECT_FALSE(readObjectClusterHeader(std::span<const std::uint8_t>(bad.data(), objectClusterHeaderSize), info).ok());
        bad = bytes;
        bad[8] = 9; // level
        EXPECT_FALSE(readObjectClusterHeader(std::span<const std::uint8_t>(bad.data(), objectClusterHeaderSize), info).ok());
        bad = bytes;
        bad[15] = 0x7F; // side becomes huge but finite; then NaN below
        bad[12] = bad[13] = bad[14] = 0xFF;
        bad[15] = 0xFF;
        EXPECT_FALSE(readObjectClusterHeader(std::span<const std::uint8_t>(bad.data(), objectClusterHeaderSize), info).ok());
    }
}
