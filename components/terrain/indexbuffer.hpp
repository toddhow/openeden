#ifndef VSGOPENMW_TERRAIN_INDEXBUFFER_H
#define VSGOPENMW_TERRAIN_INDEXBUFFER_H

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <vsg/commands/BindIndexBuffer.h>

#include <components/debug/debuglog.hpp>

namespace Terrain
{
    using IndexBufferKey = std::pair<uint32_t /*numVerts*/, uint32_t /*flags*/>;

    // Task 0090: the largest LOD step an edge of a grid of numVerts x numVerts vertices can be stitched down by. An
    // edge of N = numVerts - 1 quads is stitched in groups of 1 << delta quads, so the group must divide N: the
    // largest power of two that divides N is the limit (65 vertices: 6, 129: 7, 49: 4). A larger step writes more
    // indices than the buffer was sized for (a heap overflow; found with a LOD tile of 64 cells next to a one-cell
    // chunk). A grid with fewer than 3 vertices has no edge that can be stitched.
    inline uint32_t maxStitchDelta(uint32_t numVerts)
    {
        if (numVerts < 3)
            return 0;
        uint32_t quads = numVerts - 1;
        uint32_t delta = 0;
        while ((quads & 1u) == 0)
        {
            quads >>= 1;
            ++delta;
        }
        return delta;
    }

    // The four 4-bit steps of `flags` (north, east, south, west), each cut down to maxStitchDelta(numVerts). Bits
    // above the fourth nibble are ignored. The edge stays unstitched against a much coarser neighbour (a visible
    // crack), which is better than a corrupted heap.
    inline uint32_t safeLodFlags(uint32_t numVerts, uint32_t flags)
    {
        const uint32_t limit = maxStitchDelta(numVerts);
        uint32_t result = 0;
        for (uint32_t i = 0; i < 4; ++i)
        {
            uint32_t delta = (flags >> (4 * i)) & 0xfu;
            if (delta > limit)
                delta = limit;
            result |= delta << (4 * i);
        }
        return result;
    }

    struct CreateIndexBuffer
    {
        enum Direction
        {
            North = 0,
            East = 1,
            South = 2,
            West = 3
        };

        vsg::ref_ptr<vsg::BindIndexBuffer> create(const IndexBufferKey& key) const
        {
            const auto numVerts = key.first;
            const IndexBufferKey safeKey{ numVerts, safeLodFlags(numVerts, key.second) };
            if (safeKey.second != key.second)
            {
                // Once per session: a stitch step the grid cannot take was asked for (a very coarse LOD tile next to a
                // small live chunk); the edge is left with a crack instead of writing past the buffer.
                static std::atomic<bool> warned{ false };
                if (!warned.exchange(true))
                {
                    Log(Debug::Warning) << "Terrain chunk of " << numVerts << " vertices a side asked for edge steps 0x"
                                        << std::hex << key.second << " but can take 0x" << safeKey.second << std::dec
                                        << " at most (task 0090); the stitching is cut, a crack may show";
                }
            }
            // 64 bits: numVerts * numVerts wraps in 32 bits above 65535 vertices
            if (static_cast<uint64_t>(numVerts) * numVerts <= 0xffffu)
                return t_create<vsg::ushortArray>(safeKey);
            else
                return t_create<vsg::uintArray>(safeKey);
        }

        template <class IndexArrayType>
        vsg::ref_ptr<vsg::BindIndexBuffer> t_create(const IndexBufferKey& key) const
        {
            auto verts = key.first;
            size_t lodDeltas[4];
            for (int i = 0; i < 4; ++i)
                lodDeltas[i] = (key.second >> (4 * i)) & (0xf);
            bool anyDeltas = (lodDeltas[North] || lodDeltas[South] || lodDeltas[West] || lodDeltas[East]);
            size_t indexCount = (verts - 1) * (verts - 1) * 2 * 3;

            for (int i = 0; i < 4; ++i)
            {
                if (lodDeltas[i])
                {
                    size_t outer = (verts - 1) * 3 / (1 << lodDeltas[i]);
                    size_t inner = (verts - 1) * 3;
                    indexCount -= inner - outer;
                }
            }
            auto indices = IndexArrayType::create(indexCount);
            auto ptr = indices->data();
            size_t rowStart = 0, colStart = 0, rowEnd = verts - 1, colEnd = verts - 1;
            // If any edge needs stitching we'll skip all edges at this point,
            // mainly because stitching one edge would have an effect on corners and on the adjacent edges
            if (anyDeltas)
            {
                ++colStart;
                ++rowStart;
                --colEnd;
                --rowEnd;
            }
            for (size_t row = rowStart; row < rowEnd; ++row)
            {
                for (size_t col = colStart; col < colEnd; ++col)
                {
                    // diamond pattern
                    if ((row + col % 2) % 2 == 1)
                    {
                        *(ptr++) = (verts * (col + 1) + row);
                        *(ptr++) = (verts * (col + 1) + row + 1);
                        *(ptr++) = (verts * col + row + 1);

                        *(ptr++) = (verts * col + row);
                        *(ptr++) = (verts * (col + 1) + row);
                        *(ptr++) = (verts * (col) + row + 1);
                    }
                    else
                    {
                        *(ptr++) = (verts * col + row);
                        *(ptr++) = (verts * (col + 1) + row + 1);
                        *(ptr++) = (verts * col + row + 1);

                        *(ptr++) = (verts * col + row);
                        *(ptr++) = (verts * (col + 1) + row);
                        *(ptr++) = (verts * (col + 1) + row + 1);
                    }
                }
            }

            if (anyDeltas)
            {
                // Now configure LOD transitions at the edges - this is pretty tedious,
                // and some very long and boring code, but it works great

                // South
                size_t row = 0;
                size_t outerStep = static_cast<size_t>(1) << lodDeltas[South];
                for (size_t col = 0; col < verts - 1; col += outerStep)
                {
                    *(ptr++) = (verts * col + row);
                    *(ptr++) = (verts * (col + outerStep) + row);
                    // Make sure not to touch the right edge
                    if (col + outerStep == verts - 1)
                        *(ptr++) = (verts * (col + outerStep - 1) + row + 1);
                    else
                        *(ptr++) = (verts * (col + outerStep) + row + 1);

                    for (size_t i = 0; i < outerStep; i += 1)
                    {
                        // Make sure not to touch the left or right edges
                        if (col + i == 0 || col + i == verts - 1 - 1)
                            continue;
                        *(ptr++) = (verts * (col) + row);
                        *(ptr++) = (verts * (col + i + 1) + row + 1);
                        *(ptr++) = (verts * (col + i) + row + 1);
                    }
                }

                // North
                row = verts - 1;
                outerStep = size_t(1) << lodDeltas[North];
                for (size_t col = 0; col < verts - 1; col += outerStep)
                {
                    *(ptr++) = (verts * (col + outerStep) + row);
                    *(ptr++) = (verts * col + row);
                    // Make sure not to touch the left edge
                    if (col == 0)
                        *(ptr++) = (verts * (col + 1) + row - 1);
                    else
                        *(ptr++) = (verts * col + row - 1);

                    for (size_t i = 0; i < outerStep; i += 1)
                    {
                        // Make sure not to touch the left or right edges
                        if (col + i == 0 || col + i == verts - 1 - 1)
                            continue;
                        *(ptr++) = (verts * (col + i) + row - 1);
                        *(ptr++) = (verts * (col + i + 1) + row - 1);
                        *(ptr++) = (verts * (col + outerStep) + row);
                    }
                }

                // West
                size_t col = 0;
                outerStep = size_t(1) << lodDeltas[West];
                for (row = 0; row < verts - 1; row += outerStep)
                {
                    *(ptr++) = (verts * col + row + outerStep);
                    *(ptr++) = (verts * col + row);
                    // Make sure not to touch the top edge
                    if (row + outerStep == verts - 1)
                        *(ptr++) = (verts * (col + 1) + row + outerStep - 1);
                    else
                        *(ptr++) = (verts * (col + 1) + row + outerStep);

                    for (size_t i = 0; i < outerStep; i += 1)
                    {
                        // Make sure not to touch the top or bottom edges
                        if (row + i == 0 || row + i == verts - 1 - 1)
                            continue;
                        *(ptr++) = (verts * col + row);
                        *(ptr++) = (verts * (col + 1) + row + i);
                        *(ptr++) = (verts * (col + 1) + row + i + 1);
                    }
                }

                // East
                col = verts - 1;
                outerStep = size_t(1) << lodDeltas[East];
                for (row = 0; row < verts - 1; row += outerStep)
                {
                    *(ptr++) = (verts * col + row);
                    *(ptr++) = (verts * col + row + outerStep);
                    // Make sure not to touch the bottom edge
                    if (row == 0)
                        *(ptr++) = (verts * (col - 1) + row + 1);
                    else
                        *(ptr++) = (verts * (col - 1) + row);

                    for (size_t i = 0; i < outerStep; i += 1)
                    {
                        // Make sure not to touch the top or bottom edges
                        if (row + i == 0 || row + i == verts - 1 - 1)
                            continue;
                        *(ptr++) = (verts * col + row + outerStep);
                        *(ptr++) = (verts * (col - 1) + row + i + 1);
                        *(ptr++) = (verts * (col - 1) + row + i);
                    }
                }
            }
            // Task 0090: checked in every build (the assert it replaces was compiled out of the game), after the fact
            if (static_cast<size_t>(ptr - indices->data()) != indices->valueCount())
                throw std::logic_error("terrain index buffer: " + std::to_string(ptr - indices->data())
                    + " indices written, " + std::to_string(indices->valueCount()) + " expected");
            return vsg::BindIndexBuffer::create(indices);
        }
    };
}

#endif
