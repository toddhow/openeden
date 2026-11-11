#include "puddlemap.hpp"

#include <cmath>
#include <cstring>

namespace Puddle
{
    namespace
    {
        constexpr char magic[8] = { 'E', 'D', 'E', 'N', 'P', 'U', 'D', 0 };
        constexpr float maxAbsBase = 1.0e7f;

        Error fail(const char* field, std::uint64_t offset, std::string message)
        {
            return Error{ field, offset, std::move(message) };
        }

        void put32(std::vector<std::uint8_t>& out, std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }

        void put16(std::vector<std::uint8_t>& out, std::uint16_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value));
            out.push_back(static_cast<std::uint8_t>(value >> 8));
        }

        void putFloat(std::vector<std::uint8_t>& out, float value)
        {
            std::uint32_t bits;
            std::memcpy(&bits, &value, 4);
            put32(out, bits);
        }

        std::uint32_t get32(const std::uint8_t* p)
        {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
                | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        std::uint16_t get16(const std::uint8_t* p)
        {
            return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        }

        float getFloat(const std::uint8_t* p)
        {
            const std::uint32_t bits = get32(p);
            float value;
            std::memcpy(&value, &bits, 4);
            return value;
        }

        // The consistency rules, shared by the writer and the reader.
        Error validate(const CellMap& cell)
        {
            if (cell.mSide < minSide || cell.mSide > maxSide)
                return fail("side", 20, "the side must be from 8 to 1024 texels");
            if ((cell.mFlags & ~knownFlags) != 0)
                return fail("flags", 32, "unknown flag bits");
            if (!std::isfinite(cell.mSpacing) || !(cell.mSpacing > 0.f))
                return fail("spacing", 24, "the spacing must be a finite number above zero");
            if (!std::isfinite(cell.mHeightBase) || std::abs(cell.mHeightBase) > maxAbsBase)
                return fail("heightBase", 28, "the height base is not finite or beyond +-1e7");
            const std::size_t n = static_cast<std::size_t>(cell.mSide) * cell.mSide;
            const bool noPits = cell.noPits();
            if (cell.mTexelFlags.size() != n || cell.mTop.size() != n)
                return fail("channels", 0, "the texel flags or the top channel have the wrong size");
            for (std::size_t i = 0; i < n; ++i)
            {
                if ((cell.mTexelFlags[i] & ~texelKnownMask) != 0)
                    return fail("texelFlags", headerSize + n * (noPits ? 0 : 3) + i, "unknown texel flag bits");
            }
            if (noPits)
            {
                if (!cell.mCapacity.empty() || !cell.mBasinMax.empty() || !cell.mCatchment.empty()
                    || !cell.mBasinId.empty() || !cell.mBasins.empty())
                    return fail("channels", 0, "a cell without pits must have no pit channels or basins");
                return {};
            }
            if (cell.mCapacity.size() != n || cell.mBasinMax.size() != n || cell.mCatchment.size() != n
                || cell.mBasinId.size() != n)
                return fail("channels", 0, "a pit channel has the wrong size");
            if (cell.mBasins.size() > maxBasins)
                return fail("basinCount", 36, "too many basins");
            const std::uint64_t basinCount = cell.mBasins.size();
            std::vector<std::uint8_t> seen(basinCount, 0);
            std::uint64_t next = 0;
            for (std::size_t i = 0; i < n; ++i)
            {
                const std::uint16_t id = cell.mBasinId[i];
                const bool wet = cell.mCapacity[i] != 0;
                if (id == noBasin)
                {
                    if (wet)
                        return fail(
                            "basinId", headerSize + 3 * n + n + 2 * n + 2 * i, "a texel has capacity but no pit");
                    continue;
                }
                if (id >= basinCount)
                    return fail("basinId", headerSize + 6 * n + 2 * i, "a pit id beyond the basin count");
                if (!wet)
                    return fail("capacity", headerSize + i, "a texel in a pit has no capacity");
                if (cell.mBasinMax[i] < cell.mCapacity[i])
                    return fail("basinMax", headerSize + n + i, "the pit maximum is below the texel's capacity");
                if (id == next)
                {
                    seen[id] = 1;
                    ++next;
                }
                else if (id > next)
                    return fail(
                        "basinId", headerSize + 6 * n + 2 * i, "pit ids must be numbered in order of first use");
            }
            if (next != basinCount)
                return fail("basinCount", 36, "a pit has no texel in the cell");
            return {};
        }
    }

    std::uint64_t payloadSize(std::uint32_t side, std::uint32_t flags, std::uint32_t basinCount) noexcept
    {
        const std::uint64_t n = static_cast<std::uint64_t>(side) * side;
        std::uint64_t size = headerSize + n /*flags*/ + 2 * n /*top*/;
        if ((flags & flagNoPits) == 0)
            size += 3 * n + 2 * n + 8ull * basinCount;
        return size;
    }

    Error writeCell(const CellMap& cell, std::vector<std::uint8_t>& out)
    {
        out.clear();
        if (Error error = validate(cell))
            return error;
        out.reserve(payloadSize(cell.mSide, cell.mFlags, static_cast<std::uint32_t>(cell.mBasins.size())));
        out.insert(out.end(), magic, magic + 8);
        put32(out, layoutVersion);
        put32(out, static_cast<std::uint32_t>(cell.mCellX));
        put32(out, static_cast<std::uint32_t>(cell.mCellY));
        put32(out, cell.mSide);
        putFloat(out, cell.mSpacing);
        putFloat(out, cell.mHeightBase);
        put32(out, cell.mFlags);
        put32(out, static_cast<std::uint32_t>(cell.mBasins.size()));
        put32(out, 0);
        out.insert(out.end(), cell.mParameterDigest.begin(), cell.mParameterDigest.end());
        out.insert(out.end(), cell.mInputDigest.begin(), cell.mInputDigest.end());
        if (!cell.noPits())
        {
            out.insert(out.end(), cell.mCapacity.begin(), cell.mCapacity.end());
            out.insert(out.end(), cell.mBasinMax.begin(), cell.mBasinMax.end());
            out.insert(out.end(), cell.mCatchment.begin(), cell.mCatchment.end());
        }
        out.insert(out.end(), cell.mTexelFlags.begin(), cell.mTexelFlags.end());
        for (const std::uint16_t top : cell.mTop)
            put16(out, top);
        if (!cell.noPits())
        {
            for (const std::uint16_t id : cell.mBasinId)
                put16(out, id);
            for (const BasinInfo& basin : cell.mBasins)
            {
                put32(out, basin.mArea);
                out.push_back(basin.mMaxCapacityCode);
                out.push_back(basin.mCatchmentCode);
                put16(out, 0);
            }
        }
        return {};
    }

    Error parseCell(const std::uint8_t* data, std::size_t size, CellMap& cell)
    {
        cell = CellMap{};
        if (size < headerSize)
            return fail("header", 0, "the payload is shorter than its header");
        if (std::memcmp(data, magic, 8) != 0)
            return fail("magic", 0, "not a puddle map payload");
        if (get32(data + 8) != layoutVersion)
            return fail("layoutVersion", 8, "another layout version");
        CellMap parsed;
        parsed.mCellX = static_cast<std::int32_t>(get32(data + 12));
        parsed.mCellY = static_cast<std::int32_t>(get32(data + 16));
        parsed.mSide = get32(data + 20);
        parsed.mSpacing = getFloat(data + 24);
        parsed.mHeightBase = getFloat(data + 28);
        parsed.mFlags = get32(data + 32);
        const std::uint32_t basinCount = get32(data + 36);
        if (get32(data + 40) != 0)
            return fail("reserved", 40, "reserved bytes must be zero");
        if (parsed.mSide < minSide || parsed.mSide > maxSide)
            return fail("side", 20, "the side must be from 8 to 1024 texels");
        if ((parsed.mFlags & ~knownFlags) != 0)
            return fail("flags", 32, "unknown flag bits");
        if (basinCount > maxBasins)
            return fail("basinCount", 36, "too many basins");
        if ((parsed.mFlags & flagNoPits) != 0 && basinCount != 0)
            return fail("basinCount", 36, "a cell without pits has no basins");
        if (static_cast<std::uint64_t>(basinCount) > static_cast<std::uint64_t>(parsed.mSide) * parsed.mSide)
            return fail("basinCount", 36, "more basins than texels");
        if (payloadSize(parsed.mSide, parsed.mFlags, basinCount) != size)
            return fail("size", size, "the payload size does not match its header");
        std::copy(data + 44, data + 76, parsed.mParameterDigest.begin());
        std::copy(data + 76, data + 108, parsed.mInputDigest.begin());

        const std::size_t n = static_cast<std::size_t>(parsed.mSide) * parsed.mSide;
        const std::uint8_t* p = data + headerSize;
        if ((parsed.mFlags & flagNoPits) == 0)
        {
            parsed.mCapacity.assign(p, p + n);
            p += n;
            parsed.mBasinMax.assign(p, p + n);
            p += n;
            parsed.mCatchment.assign(p, p + n);
            p += n;
        }
        parsed.mTexelFlags.assign(p, p + n);
        p += n;
        parsed.mTop.resize(n);
        for (std::size_t i = 0; i < n; ++i, p += 2)
            parsed.mTop[i] = get16(p);
        if ((parsed.mFlags & flagNoPits) == 0)
        {
            parsed.mBasinId.resize(n);
            for (std::size_t i = 0; i < n; ++i, p += 2)
                parsed.mBasinId[i] = get16(p);
            parsed.mBasins.resize(basinCount);
            for (std::uint32_t b = 0; b < basinCount; ++b, p += 8)
            {
                parsed.mBasins[b].mArea = get32(p);
                parsed.mBasins[b].mMaxCapacityCode = p[4];
                parsed.mBasins[b].mCatchmentCode = p[5];
                if (get16(p + 6) != 0)
                    return fail("reserved", static_cast<std::uint64_t>(p - data) + 6, "reserved bytes must be zero");
            }
        }
        if (Error error = validate(parsed))
            return error;
        cell = std::move(parsed);
        return {};
    }

    bool packCellKey(std::int32_t cellX, std::int32_t cellY, std::uint64_t& key) noexcept
    {
        constexpr std::int64_t bias = std::int64_t{ 1 } << 21;
        const std::int64_t x = static_cast<std::int64_t>(cellX) + bias;
        const std::int64_t y = static_cast<std::int64_t>(cellY) + bias;
        if (x < 0 || y < 0 || x >= (bias << 1) || y >= (bias << 1))
            return false;
        key = (static_cast<std::uint64_t>(tagPuddleCell) << 56) | (static_cast<std::uint64_t>(x) << 22)
            | static_cast<std::uint64_t>(y);
        return true;
    }

    bool unpackCellKey(std::uint64_t key, std::int32_t& cellX, std::int32_t& cellY) noexcept
    {
        constexpr std::int64_t bias = std::int64_t{ 1 } << 21;
        if ((key >> 56) != tagPuddleCell || ((key >> 44) & 0xFFF) != 0)
            return false;
        cellX = static_cast<std::int32_t>(static_cast<std::int64_t>((key >> 22) & 0x3FFFFF) - bias);
        cellY = static_cast<std::int32_t>(static_cast<std::int64_t>(key & 0x3FFFFF) - bias);
        return true;
    }
}
