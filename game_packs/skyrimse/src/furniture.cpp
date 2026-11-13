#include "game_skyrimse/furniture.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace game_skyrimse
{
    namespace
    {
        constexpr std::uint32_t kVersion20207 = 0x14020007u;
        constexpr std::size_t kMaxHeaderLine = 128;

        class Cursor
        {
        public:
            explicit Cursor(std::span<const std::byte> data)
                : mData(data)
            {
            }
            [[nodiscard]] std::size_t remaining() const { return mData.size() - mPos; }
            [[nodiscard]] std::size_t position() const { return mPos; }
            bool take(void* out, std::size_t n)
            {
                if (remaining() < n)
                    return false;
                std::memcpy(out, mData.data() + mPos, n);
                mPos += n;
                return true;
            }
            bool skip(std::size_t n)
            {
                if (remaining() < n)
                    return false;
                mPos += n;
                return true;
            }
            template <class T>
            bool get(T& out)
            {
                return take(&out, sizeof(T));
            }
            bool seek(std::size_t pos)
            {
                if (pos > mData.size())
                    return false;
                mPos = pos;
                return true;
            }
            [[nodiscard]] const std::byte* here() const { return mData.data() + mPos; }

        private:
            std::span<const std::byte> mData;
            std::size_t mPos = 0;
        };
    }

    bool readFurnitureMarkers(std::span<const std::byte> nif, std::vector<FurnitureMarker>& out, std::string& error,
        const FurnitureLimits& limits)
    {
        out.clear();
        error.clear();
        Cursor c(nif);
        const std::size_t scan = std::min(nif.size(), kMaxHeaderLine);
        std::size_t lineEnd = 0;
        while (lineEnd < scan && nif[lineEnd] != std::byte{ '\n' })
            ++lineEnd;
        if (lineEnd == scan)
        {
            error = "no NIF header line";
            return false;
        }
        const std::string line(reinterpret_cast<const char*>(nif.data()), lineEnd);
        if (line.find("File Format, Version ") == std::string::npos)
        {
            error = "not a NIF";
            return false;
        }
        c.seek(lineEnd + 1);

        std::uint32_t version = 0, userVersion = 0, blockCount = 0, userVersion2 = 0;
        std::uint8_t endian = 1;
        if (!c.get(version) || !c.get(endian) || !c.get(userVersion) || !c.get(blockCount) || !c.get(userVersion2))
        {
            error = "the NIF header is cut short";
            return false;
        }
        if (version != kVersion20207 || endian != 1)
        {
            error = "not a little-endian NIF 20.2.0.7";
            return false;
        }
        if (blockCount > limits.mMaxBlocks)
        {
            error = "too many blocks";
            return false;
        }
        if (userVersion2 >= 130)
        {
            error = "a newer Bethesda NIF (user version 2 " + std::to_string(userVersion2) + ")";
            return false;
        }
        for (int i = 0; i < 3; ++i) // export info: three short strings
        {
            std::uint8_t n = 0;
            if (!c.get(n) || !c.skip(n))
            {
                error = "the export info is cut short";
                return false;
            }
        }
        std::uint16_t typeCount = 0;
        if (!c.get(typeCount) || typeCount > limits.mMaxBlockTypes)
        {
            error = "bad block type count";
            return false;
        }
        std::vector<std::uint8_t> isMarker(typeCount, 0);
        for (std::uint16_t i = 0; i < typeCount; ++i)
        {
            std::uint32_t n = 0;
            if (!c.get(n) || n > limits.mMaxTypeNameBytes || c.remaining() < n)
            {
                error = "bad block type name";
                return false;
            }
            const std::string_view view(reinterpret_cast<const char*>(c.here()), n);
            isMarker[i] = view == "BSFurnitureMarker" || view == "BSFurnitureMarkerNode";
            c.skip(n);
        }
        std::vector<std::uint16_t> typeOf(blockCount);
        for (std::uint32_t i = 0; i < blockCount; ++i)
        {
            if (!c.get(typeOf[i]) || typeOf[i] >= typeCount)
            {
                error = "bad block type index";
                return false;
            }
        }
        std::vector<std::uint32_t> sizeOf(blockCount);
        for (std::uint32_t i = 0; i < blockCount; ++i)
        {
            if (!c.get(sizeOf[i]))
            {
                error = "the block sizes are cut short";
                return false;
            }
        }
        std::uint32_t stringCount = 0, maxString = 0;
        if (!c.get(stringCount) || !c.get(maxString) || stringCount > limits.mMaxStrings)
        {
            error = "bad string table";
            return false;
        }
        for (std::uint32_t i = 0; i < stringCount; ++i)
        {
            std::uint32_t n = 0;
            if (!c.get(n) || !c.skip(n))
            {
                error = "the string table is cut short";
                return false;
            }
        }
        std::uint32_t groups = 0;
        if (!c.get(groups) || !c.skip(static_cast<std::size_t>(groups) * 4))
        {
            error = "the groups are cut short";
            return false;
        }
        for (std::uint32_t i = 0; i < blockCount; ++i)
        {
            const std::size_t start = c.position();
            if (c.remaining() < sizeOf[i])
            {
                error = "a block runs past the end of the file";
                out.clear();
                return false;
            }
            if (isMarker[typeOf[i]])
            {
                Cursor b(nif);
                b.seek(start);
                std::int32_t nameIndex = 0;
                std::uint32_t count = 0;
                if (!b.get(nameIndex) || !b.get(count))
                {
                    error = "a furniture marker block is cut short";
                    out.clear();
                    return false;
                }
                if (count > limits.mMaxMarkers || static_cast<std::size_t>(count) * 20 + 8 > sizeOf[i])
                {
                    error = "a furniture marker block holds an absurd count";
                    out.clear();
                    return false;
                }
                for (std::uint32_t k = 0; k < count; ++k)
                {
                    FurnitureMarker m;
                    if (!b.get(m.mOx) || !b.get(m.mOy) || !b.get(m.mOz) || !b.get(m.mHeading) || !b.get(m.mType)
                        || !b.get(m.mEntry))
                    {
                        error = "a furniture marker is cut short";
                        out.clear();
                        return false;
                    }
                    if (!std::isfinite(m.mOx) || !std::isfinite(m.mOy) || !std::isfinite(m.mOz)
                        || !std::isfinite(m.mHeading))
                        continue; // a damaged marker is skipped, the rest are kept
                    out.push_back(m);
                }
            }
            c.skip(sizeOf[i]);
        }
        return true;
    }

    SeatPoint markerInWorld(const FurniturePlacement& placement, const FurnitureMarker& marker)
    {
        const double s = std::isfinite(placement.mScale) && placement.mScale > 0.0 ? placement.mScale : 1.0;
        const double sinT = std::sin(placement.mRotZ);
        const double cosT = std::cos(placement.mRotZ);
        const double ox = static_cast<double>(marker.mOx) * s;
        const double oy = static_cast<double>(marker.mOy) * s;
        SeatPoint seat;
        // the furniture's right is (cos t, -sin t), its forward (sin t, cos t)
        seat.mX = placement.mX + ox * cosT + oy * sinT;
        seat.mY = placement.mY - ox * sinT + oy * cosT;
        seat.mZ = placement.mZ;
        seat.mSurfaceZ = placement.mZ + static_cast<double>(marker.mOz) * s;
        seat.mHeading = static_cast<float>(placement.mRotZ + static_cast<double>(marker.mHeading));
        return seat;
    }

    std::vector<EntryPoint> entryPoints(
        const FurniturePlacement& placement, const SeatPoint& seat, std::uint16_t entryFlags, double distance)
    {
        const double h = static_cast<double>(seat.mHeading);
        const double fx = std::sin(h), fy = std::cos(h); // the sitter's forward
        const double rx = std::cos(h), ry = -std::sin(h); // the sitter's right
        const bool any = (entryFlags & (EntryBit::Front | EntryBit::Behind | EntryBit::Right | EntryBit::Left)) != 0;
        std::vector<EntryPoint> out;
        auto add = [&](bool allowed, double dx, double dy, Side side) {
            if (allowed || !any)
                out.push_back({ seat.mX + dx * distance, seat.mY + dy * distance, placement.mZ, side });
        };
        add((entryFlags & EntryBit::Front) != 0, fx, fy, Side::Front);
        add((entryFlags & EntryBit::Left) != 0, -rx, -ry, Side::Left);
        add((entryFlags & EntryBit::Right) != 0, rx, ry, Side::Right);
        add((entryFlags & EntryBit::Behind) != 0, -fx, -fy, Side::Behind);
        return out;
    }
}
