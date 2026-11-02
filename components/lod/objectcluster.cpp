#include "objectcluster.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <tuple>

namespace Lod
{
    namespace
    {
        // Header field offsets (docs/tasks/0059-lod-s4-object-clusters.md, "Draft cluster entry layout").
        constexpr std::size_t offMagic = 0;
        constexpr std::size_t offVersion = 4;
        constexpr std::size_t offFlags = 6;
        constexpr std::size_t offLevel = 8;
        constexpr std::size_t offSide = 12;
        constexpr std::size_t offOriginX = 16;
        constexpr std::size_t offOriginY = 20;
        constexpr std::size_t offMinZ = 24;
        constexpr std::size_t offMaxZ = 28;
        constexpr std::size_t offError = 32;
        constexpr std::size_t offGroupCount = 36;
        constexpr std::size_t offRangeCount = 40;
        constexpr std::size_t offCellCount = 44;
        constexpr std::size_t offRefCount = 48;
        constexpr std::size_t offVertexCount = 52;
        constexpr std::size_t offIndexCount = 56;
        constexpr std::size_t offStringBytes = 60;
        constexpr std::size_t offTableOffsets = 64; // seven u32
        constexpr std::size_t offSourceTriangles = 92;
        constexpr std::size_t offOverhang = 96;
        constexpr std::size_t offReserved = 100;

        constexpr char magic[4] = { 'E', 'T', 'C', 'L' };
        constexpr std::uint32_t knownFlags = clusterFlagAlphaTest | clusterFlagTwoSided;

        LodError fail(std::string field, std::uint64_t offset, std::string message)
        {
            LodError error;
            error.mField = std::move(field);
            error.mOffset = offset;
            error.mMessage = std::move(message);
            return error;
        }

        std::uint64_t align8(std::uint64_t value) noexcept
        {
            return (value + 7) & ~std::uint64_t{ 7 };
        }

        // Strict UTF-8 (no overlong forms, no surrogates, nothing above U+10FFFF).
        bool isValidUtf8(std::string_view text) noexcept
        {
            std::size_t i = 0;
            while (i < text.size())
            {
                const auto lead = static_cast<unsigned char>(text[i]);
                std::size_t length = 0;
                std::uint32_t code = 0;
                if (lead < 0x80)
                {
                    ++i;
                    continue;
                }
                if (lead >= 0xC2 && lead <= 0xDF)
                {
                    length = 2;
                    code = lead & 0x1Fu;
                }
                else if (lead >= 0xE0 && lead <= 0xEF)
                {
                    length = 3;
                    code = lead & 0x0Fu;
                }
                else if (lead >= 0xF0 && lead <= 0xF4)
                {
                    length = 4;
                    code = lead & 0x07u;
                }
                else
                    return false;
                if (i + length > text.size())
                    return false;
                for (std::size_t k = 1; k < length; ++k)
                {
                    const auto next = static_cast<unsigned char>(text[i + k]);
                    if ((next & 0xC0) != 0x80)
                        return false;
                    code = (code << 6) | (next & 0x3Fu);
                }
                if ((length == 3 && code < 0x800) || (length == 4 && (code < 0x10000 || code > 0x10FFFF))
                    || (code >= 0xD800 && code <= 0xDFFF))
                    return false;
                i += length;
            }
            return true;
        }

        // A VFS path as normalised: no backslash, no upper-case ASCII, no leading slash.
        bool isNormalisedPath(std::string_view path) noexcept
        {
            if (path.empty() || path.front() == '/')
                return false;
            for (const char c : path)
                if (c == '\\' || (c >= 'A' && c <= 'Z') || static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
                    return false;
            return true;
        }

        bool finite(float value) noexcept
        {
            return std::isfinite(value);
        }

        struct Layout
        {
            std::uint64_t mGroups = 0, mRanges = 0, mCells = 0, mRefs = 0, mVertices = 0, mIndices = 0, mStrings = 0;
            std::uint64_t mStringBytes = 0;
            std::uint64_t mTotal = 0;
        };

        Layout computeLayout(std::uint64_t groups, std::uint64_t ranges, std::uint64_t cells, std::uint64_t refs,
            std::uint64_t vertices, std::uint64_t indices, std::uint64_t stringBytes)
        {
            Layout layout;
            layout.mGroups = objectClusterHeaderSize;
            layout.mRanges = align8(layout.mGroups + groups * clusterGroupSize);
            layout.mCells = align8(layout.mRanges + ranges * clusterRangeSize);
            layout.mRefs = align8(layout.mCells + cells * clusterCellSize);
            layout.mVertices = align8(layout.mRefs + refs * clusterRefSize);
            layout.mIndices = align8(layout.mVertices + vertices * clusterVertexSize);
            layout.mStrings = align8(layout.mIndices + indices * 4);
            layout.mStringBytes = stringBytes;
            layout.mTotal = align8(layout.mStrings + stringBytes);
            return layout;
        }

        // The checks on the logical content, shared by the writer, validateObjectCluster and the reader. The
        // header-float checks come first, with the header offsets, so a reader reports the field's byte.
        LodError validateContent(const ObjectCluster& c)
        {
            if (c.mLevel < minClusterLevel || c.mLevel > maxClusterLevel)
                return fail("level", offLevel, "the level must be 1 to 3");
            if (!finite(c.mSide) || !(c.mSide > 0))
                return fail("side", offSide, "the side must be finite and above 0");
            if (!finite(c.mOriginX))
                return fail("originX", offOriginX, "not finite");
            if (!finite(c.mOriginY))
                return fail("originY", offOriginY, "not finite");
            if (!finite(c.mMinZ))
                return fail("minZ", offMinZ, "not finite");
            if (!finite(c.mMaxZ) || c.mMaxZ < c.mMinZ)
                return fail("maxZ", offMaxZ, "not finite or below minZ");
            if (!finite(c.mGeometricError) || c.mGeometricError < 0)
                return fail("geometricError", offError, "must be finite and not negative");
            if (!finite(c.mOverhang) || c.mOverhang < 0)
                return fail("overhang", offOverhang, "must be finite and not negative");

            if (c.mGroups.empty() || c.mGroups.size() > maxClusterGroups)
                return fail("groupCount", offGroupCount, "groups must be 1 to " + std::to_string(maxClusterGroups));
            if (c.mRanges.empty() || c.mRanges.size() > maxClusterRanges)
                return fail("rangeCount", offRangeCount, "ranges must be 1 to " + std::to_string(maxClusterRanges));
            if (c.mCells.empty() || c.mCells.size() > maxClusterCells)
                return fail("cellCount", offCellCount, "cells must be 1 to " + std::to_string(maxClusterCells));
            if (c.mRefs.empty() || c.mRefs.size() > maxClusterRefs)
                return fail("refCount", offRefCount, "references must be 1 to " + std::to_string(maxClusterRefs));
            if (c.mVertices.empty() || c.mVertices.size() > maxClusterVertices)
                return fail(
                    "vertexCount", offVertexCount, "vertices must be 1 to " + std::to_string(maxClusterVertices));
            if (c.mIndices.empty() || c.mIndices.size() > maxClusterIndices || c.mIndices.size() % 3 != 0)
                return fail("indexCount", offIndexCount, "indices must be a multiple of 3, 3 to the cap");

            // Groups: sorted, valid, spans tile the index buffer.
            std::uint64_t expectedFirst = 0;
            for (std::size_t i = 0; i < c.mGroups.size(); ++i)
            {
                const ClusterGroup& g = c.mGroups[i];
                const std::string at = "groups[" + std::to_string(i) + "]";
                if (g.mTexture.empty() || g.mTexture.size() > maxClusterTexturePath)
                    return fail(at + ".texture", 0, "the path must be 1 to 1024 bytes");
                if (!isValidUtf8(g.mTexture))
                    return fail(at + ".texture", 0, "the path is not valid UTF-8");
                if (!isNormalisedPath(g.mTexture))
                    return fail(at + ".texture", 0, "the path is not in normalised form");
                if ((g.mFlags & ~knownFlags) != 0)
                    return fail(at + ".flags", 0, "unknown material flag");
                if (!finite(g.mAlphaCutoff) || g.mAlphaCutoff < 0
                    || ((g.mFlags & clusterFlagAlphaTest) == 0 && g.mAlphaCutoff != 0))
                    return fail(at + ".alphaCutoff", 0, "must be finite, not negative, and 0 without alpha test");
                if (i > 0)
                {
                    const ClusterGroup& p = c.mGroups[i - 1];
                    const auto previous = std::tie(p.mTexture, p.mFlags);
                    const auto current = std::tie(g.mTexture, g.mFlags);
                    if (!(previous < current || (previous == current && p.mAlphaCutoff < g.mAlphaCutoff)))
                        return fail(at + ".texture", 0, "groups must be sorted and distinct");
                }
                if (g.mFirstIndex != expectedFirst || g.mIndexCount == 0 || g.mIndexCount % 3 != 0)
                    return fail(at + ".span", 0, "group spans must tile the indices in order");
                expectedFirst += g.mIndexCount;
            }
            if (expectedFirst != c.mIndices.size())
                return fail("groups.span", 0, "group spans must cover every index");

            // Ranges: sorted by (group, slot), tile each group's span.
            std::size_t group = 0;
            std::uint64_t next = 0;
            for (std::size_t i = 0; i < c.mRanges.size(); ++i)
            {
                const ClusterRange& r = c.mRanges[i];
                const std::string at = "ranges[" + std::to_string(i) + "]";
                if (r.mGroup >= c.mGroups.size() || r.mCellSlot >= c.mCells.size())
                    return fail(at, 0, "group or cell slot out of range");
                if (i > 0)
                {
                    const ClusterRange& p = c.mRanges[i - 1];
                    if (!(std::tie(p.mGroup, p.mCellSlot) < std::tie(r.mGroup, r.mCellSlot)))
                        return fail(at, 0, "ranges must be sorted by (group, cell slot) without repeats");
                }
                if (r.mGroup != group)
                {
                    if (r.mGroup < group || next != c.mGroups[group].mFirstIndex + c.mGroups[group].mIndexCount)
                        return fail(at, 0, "ranges must tile their group");
                    // Groups without a range between are an error (their span would be untiled).
                    if (r.mGroup != group + 1)
                        return fail(at, 0, "a group has no ranges");
                    group = r.mGroup;
                    next = c.mGroups[group].mFirstIndex;
                }
                else if (i == 0)
                    next = c.mGroups[0].mFirstIndex;
                if (r.mFirstIndex != next || r.mIndexCount == 0 || r.mIndexCount % 3 != 0)
                    return fail(at, 0, "ranges must tile their group in order, in whole triangles");
                next += r.mIndexCount;
            }
            if (group + 1 != c.mGroups.size() || next != c.mGroups[group].mFirstIndex + c.mGroups[group].mIndexCount)
                return fail("ranges", 0, "ranges must tile every group");

            // Cells and references.
            std::uint64_t expectedRef = 0;
            for (std::size_t i = 0; i < c.mCells.size(); ++i)
            {
                const ClusterCell& cell = c.mCells[i];
                const std::string at = "cells[" + std::to_string(i) + "]";
                if (i > 0)
                {
                    const ClusterCell& p = c.mCells[i - 1];
                    if (!(std::tie(p.mCellY, p.mCellX) < std::tie(cell.mCellY, cell.mCellX)))
                        return fail(at, 0, "cells must be sorted by (y, x) without repeats");
                }
                if (cell.mFirstRef != expectedRef || cell.mRefCount == 0)
                    return fail(at, 0, "cell reference spans must tile the reference table");
                expectedRef += cell.mRefCount;
                if (expectedRef > c.mRefs.size())
                    return fail(at, 0, "cell reference span leaves the table");
                for (std::uint64_t k = cell.mFirstRef + 1; k < expectedRef; ++k)
                    if (!(c.mRefs[k - 1] < c.mRefs[k]))
                        return fail(at + ".refs", 0, "references must be sorted without repeats");
            }
            if (expectedRef != c.mRefs.size())
                return fail("cells", 0, "cell reference spans must cover every reference");

            // Vertices and indices.
            const float low = -c.mOverhang;
            const float highX = c.mSide + c.mOverhang;
            for (std::size_t i = 0; i < c.mVertices.size(); ++i)
            {
                const ClusterVertex& v = c.mVertices[i];
                const auto at = [i] { return "vertices[" + std::to_string(i) + "]"; };
                for (const float value : { v.mPosition[0], v.mPosition[1], v.mPosition[2], v.mUv[0], v.mUv[1] })
                    if (!finite(value))
                        return fail(at(), 0, "a number is not finite");
                if (v.mPosition[0] < low || v.mPosition[0] > highX || v.mPosition[1] < low || v.mPosition[1] > highX)
                    return fail(at() + ".position", 0, "outside the tile square plus the overhang");
                if (v.mPosition[2] < c.mMinZ || v.mPosition[2] > c.mMaxZ)
                    return fail(at() + ".position", 0, "z outside minZ to maxZ");
                if (v.mCellSlot >= c.mCells.size())
                    return fail(at() + ".cellSlot", 0, "cell slot out of range");
            }
            for (std::size_t i = 0; i < c.mIndices.size(); ++i)
                if (c.mIndices[i] >= c.mVertices.size())
                    return fail("indices[" + std::to_string(i) + "]", 0, "index beyond the vertex count");
            // The ranges (used to mask a cell) and the vertices (used to fade it) must agree on the cell.
            for (std::size_t i = 0; i < c.mRanges.size(); ++i)
                for (std::uint64_t k = c.mRanges[i].mFirstIndex;
                     k < std::uint64_t{ c.mRanges[i].mFirstIndex } + c.mRanges[i].mIndexCount; ++k)
                    if (c.mVertices[c.mIndices[k]].mCellSlot != c.mRanges[i].mCellSlot)
                        return fail(
                            "ranges[" + std::to_string(i) + "]", 0, "a vertex has another cell slot than its range");
            return {};
        }

        class Writer
        {
        public:
            explicit Writer(std::vector<std::uint8_t>& out)
                : mOut(out)
            {
            }
            void u16(std::size_t at, std::uint16_t v)
            {
                mOut[at] = static_cast<std::uint8_t>(v & 0xFF);
                mOut[at + 1] = static_cast<std::uint8_t>(v >> 8);
            }
            void u32(std::size_t at, std::uint32_t v)
            {
                for (std::size_t i = 0; i < 4; ++i)
                    mOut[at + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
            }
            void f32(std::size_t at, float v) { u32(at, std::bit_cast<std::uint32_t>(v)); }

        private:
            std::vector<std::uint8_t>& mOut;
        };

        std::uint32_t getU32(std::span<const std::uint8_t> b, std::size_t at) noexcept
        {
            std::uint32_t v = 0;
            for (std::size_t i = 0; i < 4; ++i)
                v |= static_cast<std::uint32_t>(b[at + i]) << (8 * i);
            return v;
        }

        float getF32(std::span<const std::uint8_t> b, std::size_t at) noexcept
        {
            return std::bit_cast<float>(getU32(b, at));
        }
    }

    bool isValidClusterTexturePath(std::string_view path) noexcept
    {
        return !path.empty() && path.size() <= maxClusterTexturePath && isValidUtf8(path) && isNormalisedPath(path);
    }

    std::optional<std::uint64_t> packClusterKey(const TileId& id) noexcept
    {
        const std::optional<std::uint64_t> terrain = packTileKey(id);
        if (!terrain)
            return std::nullopt;
        constexpr std::uint64_t lowMask = (std::uint64_t{ 1 } << 56) - 1;
        return (*terrain & lowMask) | (std::uint64_t{ tagObjectCluster } << 56);
    }

    std::optional<TileId> unpackClusterKey(std::uint64_t key) noexcept
    {
        if ((key >> 56) != tagObjectCluster)
            return std::nullopt;
        constexpr std::uint64_t lowMask = (std::uint64_t{ 1 } << 56) - 1;
        return unpackTileKey((key & lowMask) | (std::uint64_t{ tagTerrainTile } << 56));
    }

    LodError validateObjectCluster(const ObjectCluster& cluster)
    {
        return validateContent(cluster);
    }

    LodError writeObjectCluster(const ObjectCluster& c, std::vector<std::uint8_t>& out)
    {
        out.clear();
        if (LodError error = validateContent(c))
            return error;

        std::uint64_t stringBytes = 0;
        for (const ClusterGroup& g : c.mGroups)
            stringBytes += g.mTexture.size();
        const Layout layout = computeLayout(c.mGroups.size(), c.mRanges.size(), c.mCells.size(), c.mRefs.size(),
            c.mVertices.size(), c.mIndices.size(), stringBytes);

        std::vector<std::uint8_t> bytes(layout.mTotal, 0);
        Writer w(bytes);
        std::memcpy(bytes.data() + offMagic, magic, 4);
        w.u16(offVersion, objectClusterPayloadVersion);
        w.u16(offFlags, 0);
        w.u32(offLevel, c.mLevel);
        w.f32(offSide, c.mSide);
        w.f32(offOriginX, c.mOriginX);
        w.f32(offOriginY, c.mOriginY);
        w.f32(offMinZ, c.mMinZ);
        w.f32(offMaxZ, c.mMaxZ);
        w.f32(offError, c.mGeometricError);
        w.u32(offGroupCount, static_cast<std::uint32_t>(c.mGroups.size()));
        w.u32(offRangeCount, static_cast<std::uint32_t>(c.mRanges.size()));
        w.u32(offCellCount, static_cast<std::uint32_t>(c.mCells.size()));
        w.u32(offRefCount, static_cast<std::uint32_t>(c.mRefs.size()));
        w.u32(offVertexCount, static_cast<std::uint32_t>(c.mVertices.size()));
        w.u32(offIndexCount, static_cast<std::uint32_t>(c.mIndices.size()));
        w.u32(offStringBytes, static_cast<std::uint32_t>(stringBytes));
        const std::uint64_t offsets[7] = { layout.mGroups, layout.mRanges, layout.mCells, layout.mRefs,
            layout.mVertices, layout.mIndices, layout.mStrings };
        for (std::size_t i = 0; i < 7; ++i)
            w.u32(offTableOffsets + 4 * i, static_cast<std::uint32_t>(offsets[i]));
        w.u32(offSourceTriangles, c.mSourceTriangles);
        w.f32(offOverhang, c.mOverhang);

        std::uint32_t pathOffset = 0;
        for (std::size_t i = 0; i < c.mGroups.size(); ++i)
        {
            const ClusterGroup& g = c.mGroups[i];
            const std::size_t at = layout.mGroups + i * clusterGroupSize;
            w.u32(at + 0, pathOffset);
            w.u32(at + 4, static_cast<std::uint32_t>(g.mTexture.size()));
            w.u32(at + 8, g.mFlags);
            w.f32(at + 12, g.mAlphaCutoff);
            w.u32(at + 16, g.mFirstIndex);
            w.u32(at + 20, g.mIndexCount);
            std::memcpy(bytes.data() + layout.mStrings + pathOffset, g.mTexture.data(), g.mTexture.size());
            pathOffset += static_cast<std::uint32_t>(g.mTexture.size());
        }
        for (std::size_t i = 0; i < c.mRanges.size(); ++i)
        {
            const ClusterRange& r = c.mRanges[i];
            const std::size_t at = layout.mRanges + i * clusterRangeSize;
            w.u32(at + 0, r.mGroup);
            w.u32(at + 4, r.mCellSlot);
            w.u32(at + 8, r.mFirstIndex);
            w.u32(at + 12, r.mIndexCount);
        }
        for (std::size_t i = 0; i < c.mCells.size(); ++i)
        {
            const ClusterCell& cell = c.mCells[i];
            const std::size_t at = layout.mCells + i * clusterCellSize;
            w.u32(at + 0, static_cast<std::uint32_t>(cell.mCellX));
            w.u32(at + 4, static_cast<std::uint32_t>(cell.mCellY));
            w.u32(at + 8, cell.mFirstRef);
            w.u32(at + 12, cell.mRefCount);
        }
        for (std::size_t i = 0; i < c.mRefs.size(); ++i)
        {
            const std::size_t at = layout.mRefs + i * clusterRefSize;
            w.u32(at + 0, c.mRefs[i].mIndex);
            w.u32(at + 4, c.mRefs[i].mContentFile);
        }
        for (std::size_t i = 0; i < c.mVertices.size(); ++i)
        {
            const ClusterVertex& v = c.mVertices[i];
            const std::size_t at = layout.mVertices + i * clusterVertexSize;
            w.f32(at + 0, v.mPosition[0]);
            w.f32(at + 4, v.mPosition[1]);
            w.f32(at + 8, v.mPosition[2]);
            for (std::size_t k = 0; k < 3; ++k)
                bytes[at + 12 + k] = static_cast<std::uint8_t>(v.mNormal[k]);
            bytes[at + 15] = v.mCellSlot;
            for (std::size_t k = 0; k < 4; ++k)
                bytes[at + 16 + k] = v.mColor[k];
            w.f32(at + 20, v.mUv[0]);
            w.f32(at + 24, v.mUv[1]);
        }
        for (std::size_t i = 0; i < c.mIndices.size(); ++i)
            w.u32(layout.mIndices + 4 * i, c.mIndices[i]);

        out = std::move(bytes);
        return {};
    }

    LodError readObjectCluster(std::span<const std::uint8_t> b, ObjectCluster& cluster, const TileId* expected)
    {
        cluster = ObjectCluster{};
        if (b.size() < objectClusterHeaderSize)
            return fail("header", 0, "fewer than 104 bytes");
        if (std::memcmp(b.data() + offMagic, magic, 4) != 0)
            return fail("magic", offMagic, "not an object cluster");
        const std::uint32_t version
            = static_cast<std::uint32_t>(b[offVersion]) | (static_cast<std::uint32_t>(b[offVersion + 1]) << 8);
        if (version != objectClusterPayloadVersion)
            return fail("version", offVersion, "only the draft version 0 is understood");
        if (b[offFlags] != 0 || b[offFlags + 1] != 0)
            return fail("flags", offFlags, "flags must be 0 in version 0");

        ObjectCluster c;
        c.mLevel = getU32(b, offLevel);
        c.mSide = getF32(b, offSide);
        c.mOriginX = getF32(b, offOriginX);
        c.mOriginY = getF32(b, offOriginY);
        c.mMinZ = getF32(b, offMinZ);
        c.mMaxZ = getF32(b, offMaxZ);
        c.mGeometricError = getF32(b, offError);
        c.mOverhang = getF32(b, offOverhang);
        c.mSourceTriangles = getU32(b, offSourceTriangles);

        // The header's own checks come first so that each reports its own byte.
        if (c.mLevel < minClusterLevel || c.mLevel > maxClusterLevel)
            return fail("level", offLevel, "the level must be 1 to 3");
        if (expected != nullptr && expected->mLevel != c.mLevel)
            return fail("level", offLevel, "the level differs from the tile the entry was stored under");
        struct Float
        {
            const char* mName;
            std::size_t mOffset;
            float mValue;
        };
        for (const Float& f : { Float{ "side", offSide, c.mSide }, Float{ "originX", offOriginX, c.mOriginX },
                 Float{ "originY", offOriginY, c.mOriginY }, Float{ "minZ", offMinZ, c.mMinZ },
                 Float{ "maxZ", offMaxZ, c.mMaxZ }, Float{ "geometricError", offError, c.mGeometricError },
                 Float{ "overhang", offOverhang, c.mOverhang } })
            if (!finite(f.mValue))
                return fail(f.mName, f.mOffset, "not finite");
        if (getU32(b, offReserved) != 0)
            return fail("reserved", offReserved, "must be zero");

        struct Count
        {
            const char* mName;
            std::size_t mOffset;
            std::uint32_t mCap;
            std::uint32_t mValue;
        };
        const Count counts[] = {
            { "groupCount", offGroupCount, maxClusterGroups, getU32(b, offGroupCount) },
            { "rangeCount", offRangeCount, maxClusterRanges, getU32(b, offRangeCount) },
            { "cellCount", offCellCount, maxClusterCells, getU32(b, offCellCount) },
            { "refCount", offRefCount, maxClusterRefs, getU32(b, offRefCount) },
            { "vertexCount", offVertexCount, maxClusterVertices, getU32(b, offVertexCount) },
            { "indexCount", offIndexCount, maxClusterIndices, getU32(b, offIndexCount) },
        };
        for (const Count& count : counts)
            if (count.mValue == 0 || count.mValue > count.mCap)
                return fail(count.mName, count.mOffset, "count is 0 or above the cap");
        const std::uint32_t stringBytes = getU32(b, offStringBytes);
        if (stringBytes == 0 || stringBytes > counts[0].mValue * maxClusterTexturePath)
            return fail("stringBytes", offStringBytes, "string table size is 0 or too large");
        if (counts[5].mValue % 3 != 0)
            return fail("indexCount", offIndexCount, "not a multiple of 3");

        const Layout layout = computeLayout(counts[0].mValue, counts[1].mValue, counts[2].mValue, counts[3].mValue,
            counts[4].mValue, counts[5].mValue, stringBytes);
        const std::uint64_t expectedOffsets[7] = { layout.mGroups, layout.mRanges, layout.mCells, layout.mRefs,
            layout.mVertices, layout.mIndices, layout.mStrings };
        for (std::size_t i = 0; i < 7; ++i)
            if (getU32(b, offTableOffsets + 4 * i) != expectedOffsets[i])
                return fail("tableOffsets", offTableOffsets + 4 * i, "not the canonical offset");
        if (b.size() < layout.mTotal)
            return fail("size", b.size(), "the entry is shorter than its tables");
        if (b.size() > layout.mTotal)
            return fail("size", layout.mTotal, "bytes after the end of the canonical layout");

        // Tables.
        c.mGroups.resize(counts[0].mValue);
        std::uint64_t pathOffset = 0;
        for (std::size_t i = 0; i < c.mGroups.size(); ++i)
        {
            const std::size_t at = layout.mGroups + i * clusterGroupSize;
            ClusterGroup& g = c.mGroups[i];
            const std::uint32_t pathStart = getU32(b, at + 0);
            const std::uint32_t pathLength = getU32(b, at + 4);
            if (pathStart != pathOffset)
                return fail("groups.pathOffset", at, "paths must follow each other in group order");
            if (pathLength == 0 || pathLength > maxClusterTexturePath || pathOffset + pathLength > stringBytes)
                return fail("groups.pathLength", at + 4, "the path is empty, too long or leaves the string table");
            g.mTexture.assign(reinterpret_cast<const char*>(b.data() + layout.mStrings + pathStart), pathLength);
            pathOffset += pathLength;
            g.mFlags = getU32(b, at + 8);
            g.mAlphaCutoff = getF32(b, at + 12);
            g.mFirstIndex = getU32(b, at + 16);
            g.mIndexCount = getU32(b, at + 20);
            for (std::size_t k = 24; k < clusterGroupSize; ++k)
                if (b[at + k] != 0)
                    return fail("groups.reserved", at + k, "must be zero");
        }
        if (pathOffset != stringBytes)
            return fail("stringBytes", offStringBytes, "the paths do not fill the string table");

        c.mRanges.resize(counts[1].mValue);
        for (std::size_t i = 0; i < c.mRanges.size(); ++i)
        {
            const std::size_t at = layout.mRanges + i * clusterRangeSize;
            c.mRanges[i] = ClusterRange{ getU32(b, at), getU32(b, at + 4), getU32(b, at + 8), getU32(b, at + 12) };
        }
        c.mCells.resize(counts[2].mValue);
        for (std::size_t i = 0; i < c.mCells.size(); ++i)
        {
            const std::size_t at = layout.mCells + i * clusterCellSize;
            c.mCells[i] = ClusterCell{ static_cast<std::int32_t>(getU32(b, at)),
                static_cast<std::int32_t>(getU32(b, at + 4)), getU32(b, at + 8), getU32(b, at + 12) };
        }
        c.mRefs.resize(counts[3].mValue);
        for (std::size_t i = 0; i < c.mRefs.size(); ++i)
        {
            const std::size_t at = layout.mRefs + i * clusterRefSize;
            c.mRefs[i] = ClusterRef{ getU32(b, at), getU32(b, at + 4) };
        }
        c.mVertices.resize(counts[4].mValue);
        for (std::size_t i = 0; i < c.mVertices.size(); ++i)
        {
            const std::size_t at = layout.mVertices + i * clusterVertexSize;
            ClusterVertex& v = c.mVertices[i];
            v.mPosition[0] = getF32(b, at);
            v.mPosition[1] = getF32(b, at + 4);
            v.mPosition[2] = getF32(b, at + 8);
            for (std::size_t k = 0; k < 3; ++k)
                v.mNormal[k] = static_cast<std::int8_t>(b[at + 12 + k]);
            v.mCellSlot = b[at + 15];
            for (std::size_t k = 0; k < 4; ++k)
                v.mColor[k] = b[at + 16 + k];
            v.mUv[0] = getF32(b, at + 20);
            v.mUv[1] = getF32(b, at + 24);
        }
        c.mIndices.resize(counts[5].mValue);
        for (std::size_t i = 0; i < c.mIndices.size(); ++i)
            c.mIndices[i] = getU32(b, layout.mIndices + 4 * i);

        if (LodError error = validateContent(c))
            return error;

        // Padding is zero (canonical bytes).
        const std::uint64_t sectionEnds[7] = { layout.mGroups + c.mGroups.size() * clusterGroupSize,
            layout.mRanges + c.mRanges.size() * clusterRangeSize, layout.mCells + c.mCells.size() * clusterCellSize,
            layout.mRefs + c.mRefs.size() * clusterRefSize, layout.mVertices + c.mVertices.size() * clusterVertexSize,
            layout.mIndices + c.mIndices.size() * 4, layout.mStrings + stringBytes };
        const std::uint64_t nextStarts[7] = { layout.mRanges, layout.mCells, layout.mRefs, layout.mVertices,
            layout.mIndices, layout.mStrings, layout.mTotal };
        for (std::size_t i = 0; i < 7; ++i)
            for (std::uint64_t at = sectionEnds[i]; at < nextStarts[i]; ++at)
                if (b[at] != 0)
                    return fail("padding", at, "padding must be zero");
        // The header bytes between the known fields are all covered above (flags, reserved); the reserved group bytes
        // were checked with the groups.
        cluster = std::move(c);
        return {};
    }
}
