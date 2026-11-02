#include <gtest/gtest.h>

#include <components/terrain/coverage.hpp>

namespace
{
    using namespace Terrain;

    Coverage makeCoverage()
    {
        Coverage coverage;
        // One 2 x 2 square at cells (-2, 0) to (0, 2), one at (4, 4) to (6, 6).
        coverage.squares.push_back(CoveredSquare{ Bounds{ 2.f, vsg::vec2(-2.f, 0.f) }, 2 });
        coverage.squares.push_back(CoveredSquare{ Bounds{ 2.f, vsg::vec2(4.f, 4.f) }, 3 });
        return coverage;
    }

    TEST(TerrainCoverage, an_empty_coverage_contains_and_intersects_nothing)
    {
        const Coverage coverage;
        EXPECT_FALSE(coverage.contains(Bounds{ 1.f, vsg::vec2(0.f, 0.f) }));
        EXPECT_FALSE(coverage.intersects(Bounds{ 1.f, vsg::vec2(0.f, 0.f) }));
        EXPECT_EQ(coverage.find(vsg::vec2(0.f, 0.f)), nullptr);
    }

    TEST(TerrainCoverage, a_square_contains_itself_and_the_chunks_inside_it)
    {
        const Coverage coverage = makeCoverage();
        EXPECT_TRUE(coverage.contains(Bounds{ 2.f, vsg::vec2(-2.f, 0.f) }));
        EXPECT_TRUE(coverage.contains(Bounds{ 1.f, vsg::vec2(-1.f, 1.f) }));
        EXPECT_TRUE(coverage.contains(Bounds{ 0.125f, vsg::vec2(-0.125f, 1.875f) }));
        EXPECT_TRUE(coverage.contains(Bounds{ 1.f, vsg::vec2(5.f, 4.f) }));
    }

    TEST(TerrainCoverage, a_chunk_bigger_than_the_square_is_not_contained_but_intersects)
    {
        const Coverage coverage = makeCoverage();
        const Bounds big{ 4.f, vsg::vec2(-4.f, 0.f) };
        EXPECT_FALSE(coverage.contains(big));
        EXPECT_TRUE(coverage.intersects(big));
    }

    TEST(TerrainCoverage, touching_edges_do_not_intersect)
    {
        const Coverage coverage = makeCoverage();
        EXPECT_FALSE(coverage.intersects(Bounds{ 2.f, vsg::vec2(0.f, 0.f) }));
        EXPECT_FALSE(coverage.intersects(Bounds{ 2.f, vsg::vec2(-2.f, 2.f) }));
        EXPECT_FALSE(coverage.contains(Bounds{ 2.f, vsg::vec2(0.f, 0.f) }));
    }

    TEST(TerrainCoverage, a_chunk_far_away_is_neither_contained_nor_intersecting)
    {
        const Coverage coverage = makeCoverage();
        const Bounds far{ 1.f, vsg::vec2(20.f, 20.f) };
        EXPECT_FALSE(coverage.contains(far));
        EXPECT_FALSE(coverage.intersects(far));
    }

    TEST(TerrainCoverage, find_returns_the_square_with_its_vertex_lod)
    {
        const Coverage coverage = makeCoverage();
        const CoveredSquare* first = coverage.find(vsg::vec2(-1.f, 1.f));
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(first->vertexLod, 2u);
        const CoveredSquare* second = coverage.find(vsg::vec2(5.5f, 4.5f));
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(second->vertexLod, 3u);
        EXPECT_EQ(coverage.find(vsg::vec2(2.f, 2.f)), nullptr);
    }

    TEST(TerrainCoverage, touched_vertex_lod_is_minus_one_when_nothing_is_touched)
    {
        const Coverage coverage = makeCoverage();
        EXPECT_EQ(coverage.touchedVertexLod(Bounds{ 1.f, vsg::vec2(20.f, 20.f) }), -1);
        EXPECT_EQ(Coverage{}.touchedVertexLod(Bounds{ 1.f, vsg::vec2(0.f, 0.f) }), -1);
    }

    TEST(TerrainCoverage, touched_vertex_lod_counts_an_edge_and_a_corner_as_touching)
    {
        const Coverage coverage = makeCoverage();
        // Shares the edge x = 0 with the first square (lod 2).
        EXPECT_EQ(coverage.touchedVertexLod(Bounds{ 2.f, vsg::vec2(0.f, 0.f) }), 2);
        // Shares only the corner (0, 2) with it.
        EXPECT_EQ(coverage.touchedVertexLod(Bounds{ 1.f, vsg::vec2(0.f, 2.f) }), 2);
        // Inside it.
        EXPECT_EQ(coverage.touchedVertexLod(Bounds{ 1.f, vsg::vec2(-1.f, 0.f) }), 2);
        // One cell away from it.
        EXPECT_EQ(coverage.touchedVertexLod(Bounds{ 1.f, vsg::vec2(1.f, 2.f) }), -1);
    }

    TEST(TerrainCoverage, touched_vertex_lod_is_the_smallest_of_the_squares_touched)
    {
        Coverage coverage;
        coverage.squares.push_back(CoveredSquare{ Bounds{ 2.f, vsg::vec2(0.f, 0.f) }, 4 });
        coverage.squares.push_back(CoveredSquare{ Bounds{ 2.f, vsg::vec2(2.f, 0.f) }, 2 });
        // A chunk over both squares touches both.
        EXPECT_EQ(coverage.touchedVertexLod(Bounds{ 4.f, vsg::vec2(0.f, 0.f) }), 2);
    }
}
