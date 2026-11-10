#ifndef OPENMW_COMPONENTS_LOD_CLUSTEREFFECT_H
#define OPENMW_COMPONENTS_LOD_CLUSTEREFFECT_H

// OpenEden world LOD (task 0096, stage b). A time-driven effect of one cluster group: today a texture scroll (the
// NiUVController of lava planes, waterfalls and the Ghostgate fence, reduced to a constant speed). Pure code: the
// record the baker writes into layout version 2, the checks on it, and the maths the renderer evaluates each frame
// (a CPU twin: the cluster draws take the result as a texture matrix, as the live objects' Anim::TexMat does).
//
// Not built: a periodic alpha pulse (it only matters for blended surfaces, which clusters leave out) and texture
// flipping (a group has one texture; vanilla Morrowind has no NiFlipController).

#include <cmath>
#include <cstdint>

namespace Lod
{
    /// The kinds of effect a group can carry (bits of ClusterEffect::mKind; any other bit is refused).
    inline constexpr std::uint32_t clusterEffectUvScroll = 1u << 0;
    inline constexpr std::uint32_t clusterEffectKnownKinds = clusterEffectUvScroll;
    /// The fastest scroll the payload may hold, in texture repeats per second. Vanilla's lava and waterfalls are far
    /// below 1; a mod's NIF is untrusted, so the baker and the reader refuse anything beyond this.
    inline constexpr float maxClusterUvVelocity = 64.f;

    struct ClusterEffect
    {
        std::uint32_t mKind = 0;
        /// Texture repeats per second added to the u and v coordinates, while the kind has clusterEffectUvScroll;
        /// 0 otherwise.
        float mUvVelocity[2] = { 0, 0 };
        bool operator==(const ClusterEffect&) const = default;
        bool none() const noexcept { return mKind == 0; }
    };

    /// Whether the record can be stored: only known kinds, finite velocities within the cap, and nothing set that
    /// the kind does not use (so every effect has exactly one byte form).
    inline bool isValidClusterEffect(const ClusterEffect& effect) noexcept
    {
        if ((effect.mKind & ~clusterEffectKnownKinds) != 0)
            return false;
        for (const float v : effect.mUvVelocity)
            if (!std::isfinite(v) || std::fabs(v) > maxClusterUvVelocity)
                return false;
        if ((effect.mKind & clusterEffectUvScroll) == 0)
            return effect.mUvVelocity[0] == 0 && effect.mUvVelocity[1] == 0 && effect.mKind == 0;
        // A scroll that does not move is not an effect.
        return effect.mUvVelocity[0] != 0 || effect.mUvVelocity[1] != 0;
    }

    /// The texture offset of a scrolling group after `seconds` (game time), in [0, 1) on each axis. The fraction is
    /// taken in double precision, so an hour of play loses no precision a float texture coordinate would keep, and
    /// a repeating texture is unchanged by the whole part. A time that is not finite, or an effect without a scroll,
    /// gives 0.
    inline void clusterUvOffset(const ClusterEffect& effect, double seconds, float (&out)[2]) noexcept
    {
        out[0] = 0;
        out[1] = 0;
        if ((effect.mKind & clusterEffectUvScroll) == 0 || !std::isfinite(seconds))
            return;
        for (int axis = 0; axis < 2; ++axis)
        {
            const double distance = static_cast<double>(effect.mUvVelocity[axis]) * seconds;
            double fraction = distance - std::floor(distance);
            if (!(fraction >= 0.0 && fraction < 1.0))
                fraction = 0.0;
            out[axis] = static_cast<float>(fraction);
            if (out[axis] >= 1.f) // a fraction just under 1 can round up to 1 in float
                out[axis] = 0.f;
        }
    }
}

#endif
