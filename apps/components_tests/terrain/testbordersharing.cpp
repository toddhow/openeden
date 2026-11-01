// Tests for hairline cracks between terrain meshes (task 0012, stage log "terrain crack").
//
// A crack along a batch or chunk border shows sky or water through the ground. Two things have to hold for the
// border to be closed, and each is checked here without a GPU:
//   1. The index buffers of a chunk, stitched to coarser neighbours by CreateIndexBuffer, leave open (used by one
//      triangle only) exactly the coarse segments of the neighbours and nothing inside the chunk.
//   2. Both meshes at a shared border place the vertex at the same height. terrain/main.vert reads the height texture
//      one vertex per texel; the mapping below mirrors its `texelFetch` index. The shader itself cannot be run here,
//      so this tests a model of it: change main.vert and the model together.
// The crack itself was seen as dashed 1 pixel lines along a batch border in the Seyda Neen start view; the shader
// used to sample the border heights with a repeating, filtering sampler (see OldSamplingBledTheFarSideIntoTheBorderVertex).

#ifdef _MSC_VER
// indexbuffer.hpp narrows size_t to the 16-bit index type in its templates; Terrain::Builder compiles the same warning.
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <components/terrain/indexbuffer.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <vsg/core/Array.h>
// MSVC needs Array<T>::accept, which these headers define, in this translation unit.
#include <vsg/core/ConstVisitor.h>
#include <vsg/core/Visitor.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace
{
    using Edge = std::pair<uint32_t, uint32_t>;

    Edge makeEdge(uint32_t a, uint32_t b)
    {
        return a < b ? Edge{ a, b } : Edge{ b, a };
    }

    std::vector<uint32_t> readIndices(const vsg::BindIndexBuffer& bind)
    {
        std::vector<uint32_t> result;
        const vsg::Data* data = bind.indices->data.get();
        if (const auto* shorts = dynamic_cast<const vsg::ushortArray*>(data))
            result.assign(shorts->begin(), shorts->end());
        else if (const auto* ints = dynamic_cast<const vsg::uintArray*>(data))
            result.assign(ints->begin(), ints->end());
        return result;
    }

    // Vertex (col, row) is index verts * col + row; the sides are as in CreateIndexBuffer: South is row 0, North is
    // row verts - 1, West is col 0 and East is col verts - 1.
    std::vector<uint32_t> sideVertices(uint32_t verts, int direction)
    {
        std::vector<uint32_t> side;
        for (uint32_t i = 0; i < verts; ++i)
        {
            switch (direction)
            {
                case Terrain::CreateIndexBuffer::South:
                    side.push_back(verts * i + 0);
                    break;
                case Terrain::CreateIndexBuffer::North:
                    side.push_back(verts * i + verts - 1);
                    break;
                case Terrain::CreateIndexBuffer::West:
                    side.push_back(verts * 0 + i);
                    break;
                default:
                    side.push_back(verts * (verts - 1) + i);
                    break;
            }
        }
        return side;
    }

    // The edges a chunk must leave open: its outline, where each side runs in steps of 2^delta, the size of the
    // neighbour's cells, so that the neighbour's vertices are the only ones on the line.
    std::map<Edge, int> expectedOutline(uint32_t verts, const uint32_t (&deltas)[4])
    {
        std::map<Edge, int> outline;
        for (int direction = 0; direction < 4; ++direction)
        {
            const auto side = sideVertices(verts, direction);
            const uint32_t step = 1u << deltas[direction];
            for (uint32_t i = 0; i + step < verts; i += step)
                outline[makeEdge(side[i], side[i + step])] += 1;
        }
        return outline;
    }

    void expectWatertight(uint32_t verts, const uint32_t (&deltas)[4])
    {
        const uint32_t flags = deltas[0] | (deltas[1] << 4) | (deltas[2] << 8) | (deltas[3] << 12);
        SCOPED_TRACE(testing::Message() << "verts " << verts << " flags " << std::hex << flags);
        const auto bind = Terrain::CreateIndexBuffer().create({ verts, flags });
        const auto indices = readIndices(*bind);
        ASSERT_EQ(indices.size() % 3, 0u);

        std::map<Edge, int> uses;
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
            ASSERT_LT(std::max({ a, b, c }), verts * verts);
            ASSERT_TRUE(a != b && b != c && a != c) << "degenerate triangle at " << i;
            ++uses[makeEdge(a, b)];
            ++uses[makeEdge(b, c)];
            ++uses[makeEdge(c, a)];
        }

        std::map<Edge, int> open;
        for (const auto& [edge, count] : uses)
        {
            ASSERT_LE(count, 2) << "an edge used by three triangles: " << edge.first << " - " << edge.second;
            if (count == 1)
                open[edge] = 1;
        }

        const auto expected = expectedOutline(verts, deltas);
        // Corner-to-corner edges of a side with a coarser step: the expected outline holds each once.
        EXPECT_EQ(open, expected);
    }
}

namespace Terrain
{
    // Tests 1: the index buffers.

    TEST(TerrainBorderSharing, UnstitchedChunkIsOpenOnlyAlongItsOutline)
    {
        for (uint32_t verts : { 5u, 9u, 17u, 65u })
            expectWatertight(verts, { 0, 0, 0, 0 });
    }

    TEST(TerrainBorderSharing, ChunkStitchedToOneCoarserNeighbourFollowsThatNeighbour)
    {
        for (uint32_t verts : { 9u, 17u, 65u })
        {
            for (int side = 0; side < 4; ++side)
            {
                for (uint32_t delta = 1; (1u << delta) <= verts - 1; ++delta)
                {
                    uint32_t deltas[4] = { 0, 0, 0, 0 };
                    deltas[side] = delta;
                    expectWatertight(verts, deltas);
                }
            }
        }
    }

    TEST(TerrainBorderSharing, ChunkStitchedOnEverySideFollowsEveryNeighbour)
    {
        for (uint32_t verts : { 17u, 65u })
        {
            for (uint32_t n = 0; n < 3; ++n)
                for (uint32_t e = 0; e < 3; ++e)
                    for (uint32_t s = 0; s < 3; ++s)
                        for (uint32_t w = 0; w < 3; ++w)
                        {
                            const uint32_t deltas[4] = { n, e, s, w };
                            expectWatertight(verts, deltas);
                        }
        }
    }
}

namespace
{
    // A model of terrain/main.vert for one batch, in one dimension. `size` is the batch's width in cells, `origin` its
    // first cell coordinate and `verts` the texel count of its height texture (one texel per vertex at the batch's own
    // level of detail). `lodStep` is the stride of the chunk in texels.

    // The sampling the shader used to do, for the record of what went wrong: normalised coordinate, repeat addressing,
    // linear filtering. At the first and last vertex it blends in 1/(2 * (verts + 1)) of the texel on the far side
    // of the batch, which is a height error of that fraction of the batch's whole height range.
    float oldHeight(const std::vector<float>& texels, float uv)
    {
        const float n = static_cast<float>(texels.size());
        const float coord = (uv - 0.5f) * n / (n + 1.f) + 0.5f;
        const float pos = coord * n - 0.5f;
        const int i0 = static_cast<int>(std::floor(pos));
        const float f = pos - static_cast<float>(i0);
        const int size = static_cast<int>(texels.size());
        const int a = ((i0 % size) + size) % size;
        const int b = (((i0 + 1) % size) + size) % size;
        return texels[a] * (1.f - f) + texels[b] * f;
    }

    // The vertex's own texel, as `texelFetch(heightMap, ivec2(round(uv * (texSize - 1))))`.
    float newHeight(const std::vector<float>& texels, float uv)
    {
        const int index = static_cast<int>(std::lround(uv * static_cast<float>(texels.size() - 1)));
        return texels[std::clamp(index, 0, static_cast<int>(texels.size()) - 1)];
    }

    std::vector<float> slope(uint32_t verts, float base, float rise)
    {
        std::vector<float> texels(verts);
        for (uint32_t i = 0; i < verts; ++i)
            texels[i] = base + rise * static_cast<float>(i) / static_cast<float>(verts - 1);
        return texels;
    }
}

namespace Terrain
{
    // Tests 2: heights at a shared border.

    TEST(TerrainBorderSharing, OldSamplingBledTheFarSideIntoTheBorderVertex)
    {
        // A hillside rising 1000 units across a 65 vertex batch: the last vertex came out about 8 units low.
        const auto texels = slope(65, 0.f, 1000.f);
        EXPECT_GT(std::abs(oldHeight(texels, 1.f) - texels.back()), 5.f);
        EXPECT_GT(std::abs(oldHeight(texels, 0.f) - texels.front()), 5.f);
    }

    TEST(TerrainBorderSharing, BorderVertexTakesItsOwnTexelWhateverTheBatch)
    {
        for (uint32_t verts : { 9u, 17u, 33u, 65u, 129u })
        {
            const auto texels = slope(verts, 100.f, 1000.f);
            EXPECT_EQ(newHeight(texels, 0.f), texels.front());
            EXPECT_EQ(newHeight(texels, 1.f), texels.back());
        }
    }

    TEST(TerrainBorderSharing, EveryVertexOfAChunkTakesItsOwnTexel)
    {
        // A chunk of numVerts vertices over a batch texture of N texels, at stride (N - 1) / (numVerts - 1).
        const auto texels = slope(65, 0.f, 6400.f);
        for (uint32_t numVerts : { 65u, 33u, 17u, 9u, 5u })
        {
            const uint32_t stride = 64 / (numVerts - 1);
            for (uint32_t v = 0; v < numVerts; ++v)
            {
                const float uv = static_cast<float>(v) / static_cast<float>(numVerts - 1);
                EXPECT_EQ(newHeight(texels, uv), texels[v * stride]);
            }
        }
    }

    TEST(TerrainBorderSharing, TwoBatchesSharingABorderAgreeOnItsHeight)
    {
        // The last row of cell A and the first row of cell B are one row of land; each batch has its own texture.
        auto a = slope(65, 0.f, 800.f);
        auto b = slope(65, 0.f, -300.f);
        b.front() = a.back();
        EXPECT_EQ(newHeight(a, 1.f), newHeight(b, 0.f));
        // A coarser batch beside a finer one: still the same texel of the same row.
        auto coarse = slope(17, 0.f, 500.f);
        coarse.front() = a.back();
        EXPECT_EQ(newHeight(a, 1.f), newHeight(coarse, 0.f));
    }
}
