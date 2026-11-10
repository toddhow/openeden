#ifndef OPENMW_COMPONENTS_LOD_CLUSTERSWAY_H
#define OPENMW_COMPONENTS_LOD_CLUSTERSWAY_H

// OpenEden world LOD (task 0096, stage c, first slice: the maths only). Wind sway of far foliage, flags and banners.
// Vanilla Morrowind has no sway controllers (stage a survey, finding 4), so this is a procedural effect that a name
// list selects. This header is the pure part: which models may sway, a per-object phase from its position, and the
// displacement of a point at a given height above the object's base. The shader and the cluster layout that would use
// it are NOT built yet (see the stage log); nothing in the game calls this header.
//
// All numbers below are first guesses for a look, not measurements.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace Lod
{
    /// The most an object's top may lean, as a fraction of its height (a cap, so a bad setting cannot tear a tree).
    inline constexpr float maxSwayLean = 0.1f;
    /// The base period of the sway in seconds of game time.
    inline constexpr double swayPeriodSeconds = 3.0;
    /// The sideways motion across the wind is at most this share of the motion along it.
    inline constexpr float swayCrossShare = 0.25f;

    namespace Detail
    {
        inline char swayLower(char c) noexcept
        {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        inline bool swayContainsNoCase(std::string_view text, std::string_view token) noexcept
        {
            if (token.empty() || token.size() > text.size())
                return false;
            for (std::size_t start = 0; start + token.size() <= text.size(); ++start)
            {
                std::size_t i = 0;
                while (i < token.size() && swayLower(text[start + i]) == swayLower(token[i]))
                    ++i;
                if (i == token.size())
                    return true;
            }
            return false;
        }
    }

    /// Whether a model path contains any word of `list` (words separated by commas, semicolons or newlines; spaces
    /// at the ends of a word are ignored; case does not matter). An empty list, or only empty words, matches nothing.
    inline bool swayCapableModel(std::string_view modelPath, std::string_view list) noexcept
    {
        std::size_t begin = 0;
        while (begin <= list.size())
        {
            std::size_t end = list.find_first_of(",;\n", begin);
            if (end == std::string_view::npos)
                end = list.size();
            std::string_view word = list.substr(begin, end - begin);
            while (!word.empty() && (word.front() == ' ' || word.front() == '\t' || word.front() == '\r'))
                word.remove_prefix(1);
            while (!word.empty() && (word.back() == ' ' || word.back() == '\t' || word.back() == '\r'))
                word.remove_suffix(1);
            if (Detail::swayContainsNoCase(modelPath, word))
                return true;
            begin = end + 1;
        }
        return false;
    }

    /// A phase in [0, 2 pi) from an object's position (whole units), so neighbours do not move in step. The same
    /// position always gives the same phase. A position that is not finite gives 0.
    inline float swayPhase(float x, float y) noexcept
    {
        constexpr float twoPi = 6.28318530718f;
        constexpr float limit = 1e9f;
        if (!std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > limit || std::fabs(y) > limit)
            return 0.f;
        std::uint32_t h = static_cast<std::uint32_t>(static_cast<std::int32_t>(std::floor(x)))
            ^ (static_cast<std::uint32_t>(static_cast<std::int32_t>(std::floor(y))) * 0x9E3779B1u);
        h ^= h >> 16;
        h *= 0x85EBCA6Bu;
        h ^= h >> 13;
        h *= 0xC2B2AE35u;
        h ^= h >> 16;
        // 24 bits are exact in a float; the result is below 2 pi.
        return static_cast<float>(h >> 8) / 16777216.f * twoPi * 0.999999f;
    }

    /// The displacement of a point `height` units above an object's base, for an object `objectHeight` units tall:
    /// out[0] along the wind, out[1] across it, in world units. The base is fixed and the bend grows with the square
    /// of the height (a bending stalk), so pieces joined at the base stay joined. `lean` (clamped to 0..maxSwayLean)
    /// is the top's lean as a share of the height at full wind, `wind` is clamped to 0..1, `phase` shifts this object
    /// against others and `seconds` is game time (reduced to one period in double precision first). The wind pushes
    /// the object downwind and the gusts make it swing about that lean. Any input that is not finite, an object
    /// height of 0 or less, or a point at or below the base gives 0. A point above the top moves as the top does.
    inline void swayDisplacement(
        float lean, float wind, float height, float objectHeight, float phase, double seconds, float (&out)[2]) noexcept
    {
        out[0] = 0;
        out[1] = 0;
        if (!std::isfinite(lean) || !std::isfinite(wind) || !std::isfinite(height) || !std::isfinite(objectHeight)
            || !std::isfinite(phase) || !std::isfinite(seconds) || objectHeight <= 0.f || height <= 0.f)
            return;
        lean = std::clamp(lean, 0.f, maxSwayLean);
        wind = std::clamp(wind, 0.f, 1.f);
        const float t = std::min(height / objectHeight, 1.f);
        const float amplitude = lean * wind * objectHeight * t * t;
        const double cycles = seconds / swayPeriodSeconds;
        const double angle = (cycles - std::floor(cycles)) * 6.283185307179586 + static_cast<double>(phase);
        const float main = static_cast<float>(std::sin(angle));
        const float gust = static_cast<float>(std::sin(3.0 * angle + 1.3));
        // |wave| <= 1: the downwind lean plus a swing of the same size, and a smaller gust.
        const float wave = 0.5f + 0.35f * main + 0.15f * gust;
        out[0] = amplitude * wave;
        out[1] = amplitude * swayCrossShare * static_cast<float>(std::sin(angle + 1.5707963267948966));
    }
}

#endif
