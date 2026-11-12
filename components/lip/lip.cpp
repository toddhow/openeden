#include "lip.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Lip
{
    namespace
    {
        constexpr std::size_t kHeaderBytes = 24;
        constexpr float kMaxValue = 2.f; // every float of the data is in [-kMaxValue, kMaxValue]
        constexpr std::int32_t kMinFirst = -120;
        constexpr std::uint32_t kTimingMarker = 16; // the u16 at offset 20 when the timing field is in place

        std::uint32_t readLe16(std::string_view b, std::size_t at)
        {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(b[at]))
                | (static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 1])) << 8);
        }

        std::uint32_t readLe32(std::string_view b, std::size_t at)
        {
            return readLe16(b, at) | (readLe16(b, at + 2) << 16);
        }

        Result fail(std::string_view name, const std::string& what)
        {
            Result r;
            r.mError = (name.empty() ? std::string("lip") : std::string(name)) + ": " + what;
            return r;
        }

        // The zero-run decoding of the payload; false on a truncated or empty run token or over the cap.
        bool runDecode(std::string_view payload, std::uint64_t cap, std::vector<float>& out)
        {
            std::vector<unsigned char> bytes;
            std::size_t i = 0;
            while (i < payload.size())
            {
                if (payload[i] == 0)
                {
                    if (i + 2 >= payload.size())
                        return false;
                    const std::uint32_t count = readLe16(payload, i + 1);
                    if (count == 0 || bytes.size() + count > cap)
                        return false;
                    bytes.insert(bytes.end(), count, 0);
                    i += 3;
                }
                else
                {
                    if (bytes.size() + 1 > cap)
                        return false;
                    bytes.push_back(static_cast<unsigned char>(payload[i]));
                    ++i;
                }
            }
            if (bytes.empty() || bytes.size() % 4 != 0)
                return false;
            out.resize(bytes.size() / 4);
            for (std::size_t k = 0; k < out.size(); ++k)
            {
                const std::uint32_t bits = static_cast<std::uint32_t>(bytes[4 * k])
                    | (static_cast<std::uint32_t>(bytes[4 * k + 1]) << 8)
                    | (static_cast<std::uint32_t>(bytes[4 * k + 2]) << 16)
                    | (static_cast<std::uint32_t>(bytes[4 * k + 3]) << 24);
                float f;
                std::memcpy(&f, &bits, 4);
                if (!std::isfinite(f) || f < -kMaxValue || f > kMaxValue)
                    return false;
                out[k] = f;
            }
            return true;
        }

        bool timingFields(std::string_view b, std::int32_t& first)
        {
            if (b.size() < kHeaderBytes || readLe16(b, 20) != kTimingMarker)
                return false;
            first = static_cast<std::int32_t>(readLe32(b, 16));
            return first >= kMinFirst && first <= 0;
        }
    }

    Result read(std::string_view bytes, std::string_view name, const Limits& limits)
    {
        if (bytes.size() > limits.mMaxBytes)
            return fail(name, "the file is " + std::to_string(bytes.size()) + " bytes, over the limit");
        if (bytes.size() < kHeaderBytes)
            return fail(name, "shorter than the 24 byte header");
        if (readLe32(bytes, 0) != 1)
            return fail(name, "the version is not 1");
        const std::uint32_t frames = readLe16(bytes, 12);

        Track track;
        std::vector<float> floats;
        bool found = false;
        std::size_t rows = 0;
        std::size_t p0 = 0;
        for (std::uint32_t extra = 0; extra <= limits.mMaxExtraHeaderBytes && kHeaderBytes + extra < bytes.size();
             ++extra)
        {
            if (!runDecode(bytes.substr(kHeaderBytes + extra), limits.mMaxExpandedBytes, floats))
                continue;
            const std::size_t count = floats.size();
            p0 = (kVisemeBase + kSlots - count % kSlots) % kSlots;
            rows = (p0 + count + kSlots - 1) / kSlots;
            if (rows != frames && rows != frames + 1ull)
                continue;
            track.mExtraHeaderBytes = extra;
            found = true;
            break;
        }
        if (!found)
            return fail(name, "no payload offset in the first " + std::to_string(limits.mMaxExtraHeaderBytes + 1)
                    + " gives a track of " + std::to_string(frames) + " frames");

        track.mFrames = static_cast<std::uint32_t>(rows);
        track.mGrid.assign(rows * kSlots, 0.f);
        std::copy(floats.begin(), floats.end(), track.mGrid.begin() + static_cast<std::ptrdiff_t>(p0));

        // The timing: the fields where the 24 byte header puts them, else with the one extra byte at 14 removed.
        if (timingFields(bytes, track.mFirst))
            track.mTimingKnown = true;
        else if (bytes.size() > kHeaderBytes)
        {
            std::string repaired(bytes.substr(0, 14));
            repaired.append(bytes.substr(15, kHeaderBytes - 14));
            std::int32_t first = 0;
            if (readLe32(repaired, 0) == readLe32(bytes, 0) && readLe32(repaired, 4) == readLe32(bytes, 4)
                && readLe32(repaired, 8) == readLe32(bytes, 8) && readLe16(repaired, 12) == frames
                && timingFields(repaired, first))
            {
                track.mFirst = first;
                track.mTimingKnown = true;
            }
        }
        if (!track.mTimingKnown)
            track.mFirst = 0;
        Result r;
        r.mOk = true;
        r.mTrack = std::move(track);
        return r;
    }

    float frameTime(const Track& track, std::uint32_t frame)
    {
        if (!track.mTimingKnown)
            return std::numeric_limits<float>::quiet_NaN();
        return (static_cast<float>(frame) + static_cast<float>(track.mFirst)) / kFramesPerSecond;
    }

    bool sampleVisemes(const Track& track, float seconds, std::array<float, kVisemes>& out)
    {
        out.fill(0.f);
        if (!track.mTimingKnown || track.mFrames == 0 || !std::isfinite(seconds)
            || track.mGrid.size() < static_cast<std::size_t>(track.mFrames) * kSlots)
            return false;
        const float position = seconds * kFramesPerSecond - static_cast<float>(track.mFirst); // in rows
        if (position < 0.f || position > static_cast<float>(track.mFrames - 1))
            return true; // before the first row and after the last: a closed mouth
        const auto low = static_cast<std::uint32_t>(position);
        const std::uint32_t high = std::min(low + 1, track.mFrames - 1);
        const float t = position - static_cast<float>(low);
        for (std::size_t k = 0; k < kVisemes; ++k)
        {
            const float a = track.mGrid[low * kSlots + kVisemeBase + k];
            const float b = track.mGrid[high * kSlots + kVisemeBase + k];
            out[k] = a + (b - a) * t;
        }
        return true;
    }
}
