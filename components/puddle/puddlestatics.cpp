#include "puddlestatics.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace Puddle
{
    namespace
    {
        constexpr std::uint8_t magic[8] = { 'E', 'D', 'E', 'N', 'P', 'S', 'T', 0 };
        constexpr std::uint32_t canonicalNanBits = 0x7FC00000u;

        void putU32(std::vector<std::uint8_t>& out, std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }

        std::uint32_t getU32(const std::uint8_t* p)
        {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
                | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        std::uint32_t floatBits(float value)
        {
            std::uint32_t bits;
            std::memcpy(&bits, &value, 4);
            return bits;
        }

        float bitsFloat(std::uint32_t bits)
        {
            float value;
            std::memcpy(&value, &bits, 4);
            return value;
        }

        Error fail(const char* field, std::uint64_t offset, std::string message)
        {
            return Error{ field, offset, std::move(message) };
        }

        bool topAllowed(float value)
        {
            return std::isfinite(value) && std::abs(value) <= staticMaxAbsHeight;
        }
    }

    bool StaticCapture::operator==(const StaticCapture& other) const
    {
        if (mCellX != other.mCellX || mCellY != other.mCellY || mSide != other.mSide
            || floatBits(mSpacing) != floatBits(other.mSpacing) || mRefsDigest != other.mRefsDigest
            || mTops.size() != other.mTops.size())
            return false;
        for (std::size_t i = 0; i < mTops.size(); ++i)
        {
            // NaN is one value here ("nothing"): compare as the file would store it.
            const bool a = std::isnan(mTops[i]);
            const bool b = std::isnan(other.mTops[i]);
            if (a != b || (!a && floatBits(mTops[i]) != floatBits(other.mTops[i])))
                return false;
        }
        return true;
    }

    std::uint64_t capturePayloadSize(std::uint32_t side) noexcept
    {
        return captureHeaderSize + 4ull * side * side;
    }

    Error writeCapture(const StaticCapture& capture, std::vector<std::uint8_t>& out)
    {
        if (capture.mSide < minSide || capture.mSide > maxSide)
            return fail("side", 0, "the side must be from 8 to 1024 texels");
        if (!std::isfinite(capture.mSpacing) || !(capture.mSpacing > 0.f))
            return fail("spacing", 0, "the spacing must be a positive number");
        const std::size_t count = static_cast<std::size_t>(capture.mSide) * capture.mSide;
        if (capture.mTops.size() != count)
            return fail("tops", 0, "a capture must hold side * side values");
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!std::isnan(capture.mTops[i]) && !topAllowed(capture.mTops[i]))
                return fail("tops", captureHeaderSize + 4 * i,
                    "a top is infinite or beyond +-" + std::to_string(static_cast<long long>(staticMaxAbsHeight)));
        }
        out.clear();
        out.reserve(capturePayloadSize(capture.mSide));
        out.insert(out.end(), magic, magic + 8);
        putU32(out, captureLayoutVersion);
        putU32(out, static_cast<std::uint32_t>(capture.mCellX));
        putU32(out, static_cast<std::uint32_t>(capture.mCellY));
        putU32(out, capture.mSide);
        putU32(out, floatBits(capture.mSpacing));
        putU32(out, 0);
        out.insert(out.end(), capture.mRefsDigest.begin(), capture.mRefsDigest.end());
        for (const float top : capture.mTops)
            putU32(out, std::isnan(top) ? canonicalNanBits : floatBits(top));
        return {};
    }

    Error parseCapture(const std::uint8_t* data, std::size_t size, StaticCapture& capture)
    {
        if (size < captureHeaderSize)
            return fail("header", 0, "the capture is shorter than its header");
        if (std::memcmp(data, magic, 8) != 0)
            return fail("magic", 0, "not a static capture");
        if (getU32(data + 8) != captureLayoutVersion)
            return fail("layout", 8, "unknown layout version");
        const std::uint32_t side = getU32(data + 20);
        if (side < minSide || side > maxSide)
            return fail("side", 20, "the side must be from 8 to 1024 texels");
        if (size != capturePayloadSize(side))
            return fail("size", size, "the size does not match the side");
        if (getU32(data + 28) != 0)
            return fail("reserved", 28, "reserved word is not 0");
        const float spacing = bitsFloat(getU32(data + 24));
        if (!std::isfinite(spacing) || !(spacing > 0.f))
            return fail("spacing", 24, "the spacing must be a positive number");
        StaticCapture parsed;
        parsed.mCellX = static_cast<std::int32_t>(getU32(data + 12));
        parsed.mCellY = static_cast<std::int32_t>(getU32(data + 16));
        parsed.mSide = side;
        parsed.mSpacing = spacing;
        std::memcpy(parsed.mRefsDigest.data(), data + 32, 32);
        const std::size_t count = static_cast<std::size_t>(side) * side;
        parsed.mTops.resize(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint32_t bits = getU32(data + captureHeaderSize + 4 * i);
            const float value = bitsFloat(bits);
            if (std::isnan(value))
            {
                if (bits != canonicalNanBits)
                    return fail("tops", captureHeaderSize + 4 * i, "a NaN other than the canonical one");
            }
            else if (!topAllowed(value))
                return fail("tops", captureHeaderSize + 4 * i, "a top is infinite or out of range");
            parsed.mTops[i] = value;
        }
        capture = std::move(parsed);
        return {};
    }

    CaptureStore::CaptureStore(std::filesystem::path folder)
        : mFolder(std::move(folder))
    {
    }

    std::filesystem::path CaptureStore::pathOf(std::int32_t cellX, std::int32_t cellY) const
    {
        return mFolder / (std::to_string(cellX) + "_" + std::to_string(cellY) + ".static");
    }

    std::optional<std::vector<float>> CaptureStore::load(
        std::int32_t cellX, std::int32_t cellY, std::uint32_t side, float spacing, const Digest& refsDigest) const
    {
        std::error_code ec;
        const std::uintmax_t fileSize = std::filesystem::file_size(pathOf(cellX, cellY), ec);
        // A file of the wrong size is refused before it is read (a hostile or damaged file in the folder).
        if (ec || side < minSide || side > maxSide || fileSize != capturePayloadSize(side))
            return std::nullopt;
        std::ifstream file(pathOf(cellX, cellY), std::ios::binary);
        if (!file)
            return std::nullopt;
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        StaticCapture capture;
        if (parseCapture(bytes.data(), bytes.size(), capture))
            return std::nullopt;
        if (capture.mCellX != cellX || capture.mCellY != cellY || capture.mSide != side
            || floatBits(capture.mSpacing) != floatBits(spacing) || capture.mRefsDigest != refsDigest)
            return std::nullopt;
        return std::move(capture.mTops);
    }

    Error CaptureStore::store(const StaticCapture& capture) const
    {
        std::vector<std::uint8_t> bytes;
        if (Error error = writeCapture(capture, bytes))
            return error;
        std::error_code ec;
        std::filesystem::create_directories(mFolder, ec);
        const std::filesystem::path target = pathOf(capture.mCellX, capture.mCellY);
        std::filesystem::path temporary = target;
        temporary += ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file)
                return fail("capture", 0, "cannot write " + temporary.string());
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file)
                return fail("capture", 0, "cannot write " + temporary.string());
        }
        std::filesystem::rename(temporary, target, ec);
        if (ec)
            return fail("capture", 0, "cannot rename " + temporary.string() + ": " + ec.message());
        return {};
    }

    StaticFilterResult applyStatics(
        const std::vector<float>& terrain, const std::vector<float>& raw, std::uint32_t side, float spacing)
    {
        StaticFilterResult result;
        const std::size_t count = static_cast<std::size_t>(side) * side;
        result.mHeights = terrain;
        result.mIsStatic.assign(count, 0);
        if (terrain.size() != count || raw.size() != count || side == 0)
            return result;

        // 1. Candidates: a finite top clearly above the ground.
        std::vector<std::uint8_t> candidate(count, 0);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (topAllowed(raw[i]) && raw[i] >= terrain[i] + staticMinRise)
            {
                candidate[i] = 1;
                ++result.mCandidates;
            }
        }

        // 2. Flat enough: no candidate neighbour (4-neighbourhood) differs by more than the slope limit.
        const float stepLimit = staticSlopeLimit * spacing;
        std::vector<std::uint8_t> flat(count, 0);
        const auto tooSteep
            = [&](std::size_t a, std::size_t b) { return candidate[b] && std::abs(raw[a] - raw[b]) > stepLimit; };
        for (std::uint32_t y = 0; y < side; ++y)
        {
            for (std::uint32_t x = 0; x < side; ++x)
            {
                const std::size_t i = static_cast<std::size_t>(y) * side + x;
                if (!candidate[i])
                    continue;
                bool ok = true;
                if (x > 0 && tooSteep(i, i - 1))
                    ok = false;
                if (x + 1 < side && tooSteep(i, i + 1))
                    ok = false;
                if (y > 0 && tooSteep(i, i - side))
                    ok = false;
                if (y + 1 < side && tooSteep(i, i + side))
                    ok = false;
                flat[i] = ok ? 1 : 0;
            }
        }

        // 3. Width: keep a texel only if it lies in a full square of flat texels (a morphological opening).
        std::vector<std::uint8_t> wide(count, 0);
        const std::uint32_t w = std::max<std::uint32_t>(staticMinWidthTexels, 1);
        if (side >= w)
        {
            for (std::uint32_t y = 0; y + w <= side; ++y)
            {
                for (std::uint32_t x = 0; x + w <= side; ++x)
                {
                    bool full = true;
                    for (std::uint32_t dy = 0; dy < w && full; ++dy)
                    {
                        for (std::uint32_t dx = 0; dx < w; ++dx)
                        {
                            if (!flat[static_cast<std::size_t>(y + dy) * side + x + dx])
                            {
                                full = false;
                                break;
                            }
                        }
                    }
                    if (!full)
                        continue;
                    for (std::uint32_t dy = 0; dy < w; ++dy)
                        for (std::uint32_t dx = 0; dx < w; ++dx)
                            wide[static_cast<std::size_t>(y + dy) * side + x + dx] = 1;
                }
            }
        }

        // 4. Area: 8-connected groups of kept texels smaller than the minimum are clutter. Scan in index order with an
        // explicit stack, so the answer does not depend on anything but the arrays.
        std::vector<std::uint8_t> visited(count, 0);
        std::vector<std::size_t> stack;
        std::vector<std::size_t> group;
        for (std::size_t start = 0; start < count; ++start)
        {
            if (!wide[start] || visited[start])
                continue;
            group.clear();
            stack.clear();
            stack.push_back(start);
            visited[start] = 1;
            while (!stack.empty())
            {
                const std::size_t i = stack.back();
                stack.pop_back();
                group.push_back(i);
                const std::int64_t x = static_cast<std::int64_t>(i % side);
                const std::int64_t y = static_cast<std::int64_t>(i / side);
                for (std::int64_t dy = -1; dy <= 1; ++dy)
                {
                    for (std::int64_t dx = -1; dx <= 1; ++dx)
                    {
                        if (dx == 0 && dy == 0)
                            continue;
                        const std::int64_t nx = x + dx;
                        const std::int64_t ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<std::int64_t>(side)
                            || ny >= static_cast<std::int64_t>(side))
                            continue;
                        const std::size_t n = static_cast<std::size_t>(ny) * side + static_cast<std::size_t>(nx);
                        if (wide[n] && !visited[n])
                        {
                            visited[n] = 1;
                            stack.push_back(n);
                        }
                    }
                }
            }
            if (group.size() < staticMinAreaTexels)
                continue;
            for (const std::size_t i : group)
            {
                result.mIsStatic[i] = 1;
                result.mHeights[i] = raw[i];
            }
            result.mAccepted += static_cast<std::uint32_t>(group.size());
        }
        return result;
    }
}
