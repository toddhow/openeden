#include "skyvisbake.hpp"

#include <algorithm>
#include <cmath>

#include "skyvismath.hpp"

namespace Render::Gi
{
    namespace
    {
        // SplitMix64 (Steele, Lea and Flood): a good 64-bit mixer, to turn lattice coordinates into a stream number.
        std::uint64_t mix(std::uint64_t x)
        {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }

        float length(const vsg::vec3& v)
        {
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }
    }

    std::uint64_t skyProbeKey(std::int64_t gx, std::int64_t gy, std::uint32_t level)
    {
        return mix(mix(mix(static_cast<std::uint64_t>(gx)) ^ static_cast<std::uint64_t>(gy)) ^ level);
    }

    SkyProbeResult bakeSkyProbe(
        const SkyBakeSettings& settings, const RayWorld& world, const vsg::vec3& position, std::uint64_t key)
    {
        Pcg32 rng(settings.seed, key);
        const float offset = rng.nextFloat();
        const float weight = skySphere / static_cast<float>(settings.rays);
        SkyProbeResult result;
        std::uint32_t close = 0;
        for (std::uint32_t i = 0; i < settings.rays; ++i)
        {
            const vsg::vec3 dir = spiralDirection(i, settings.rays, offset);
            vsg::vec3 hitPos, hitNormal;
            if (world.cast(position, position + dir * settings.skyDistance, hitPos, hitNormal))
            {
                if (length(hitPos - position) <= settings.invalidDistance)
                    ++close;
            }
            else
                visAdd(result.sh, dir, weight);
        }
        for (float& v : result.sh)
            v = skyVisQuantize(v);
        result.invalid = static_cast<float>(close) >= settings.invalidFraction * static_cast<float>(settings.rays);
        return result;
    }

    SkyVisBaker::SkyVisBaker(const SkyBakeSettings& settings, const SkyRayWorld& world, int cellX, int cellY)
        : mSettings(settings)
        , mWorld(world)
    {
        mField.cellX = cellX;
        mField.cellY = cellY;
        mField.rays = settings.rays;
        mField.seed = settings.seed;
        mField.spacing = settings.spacing;
        mField.levelSpacing = settings.levelSpacing;
        mField.levels = settings.levels;
        const std::uint32_t side = skyVisColumnsPerSide(settings.spacing);
        const float originX = static_cast<float>(cellX) * exteriorCellSize;
        const float originY = static_cast<float>(cellY) * exteriorCellSize;
        mField.base.reserve(static_cast<std::size_t>(side) * side);
        for (std::uint32_t iy = 0; iy < side; ++iy)
            for (std::uint32_t ix = 0; ix < side; ++ix)
            {
                float z = settings.fallbackHeight;
                if (!world.groundHeight(originX + static_cast<float>(ix) * settings.spacing,
                        originY + static_cast<float>(iy) * settings.spacing, z))
                    z = settings.fallbackHeight;
                mField.base.push_back(z);
            }
        mField.probes.assign(mField.base.size() * settings.levels, VisSh{});
        mInvalid.assign(mField.probes.size(), false);
    }

    bool SkyVisBaker::step(std::size_t count)
    {
        const std::uint32_t side = skyVisColumnsPerSide(mSettings.spacing);
        const std::int64_t divisions = static_cast<std::int64_t>(side) - 1;
        const float originX = static_cast<float>(mField.cellX) * exteriorCellSize;
        const float originY = static_cast<float>(mField.cellY) * exteriorCellSize;
        const std::size_t end = std::min(mField.probes.size(), mNext + count);
        for (; mNext < end; ++mNext)
        {
            const std::size_t column = mNext / mField.levels;
            const std::uint32_t level = static_cast<std::uint32_t>(mNext % mField.levels);
            const std::uint32_t ix = static_cast<std::uint32_t>(column % side);
            const std::uint32_t iy = static_cast<std::uint32_t>(column / side);
            const vsg::vec3 position(originX + static_cast<float>(ix) * mSettings.spacing,
                originY + static_cast<float>(iy) * mSettings.spacing,
                skyVisLevelHeight(mField.base[column], level, mSettings.levelSpacing));
            const std::int64_t gx = static_cast<std::int64_t>(mField.cellX) * divisions + ix;
            const std::int64_t gy = static_cast<std::int64_t>(mField.cellY) * divisions + iy;
            const SkyProbeResult r = bakeSkyProbe(mSettings, mWorld, position, skyProbeKey(gx, gy, level));
            mField.probes[mNext] = r.sh;
            mInvalid[mNext] = r.invalid;
        }
        return done();
    }

    bool SkyVisBaker::resume(const SkyVisField& partial, std::size_t next, const std::vector<bool>& invalid)
    {
        if (partial.cellX != mField.cellX || partial.cellY != mField.cellY || partial.rays != mField.rays
            || partial.seed != mField.seed || partial.spacing != mField.spacing
            || partial.levelSpacing != mField.levelSpacing || partial.levels != mField.levels
            || partial.base != mField.base || partial.probes.size() != mField.probes.size()
            || invalid.size() != mInvalid.size() || next > mField.probes.size())
            return false;
        for (std::size_t i = 0; i < next; ++i)
        {
            mField.probes[i] = partial.probes[i];
            mInvalid[i] = invalid[i];
        }
        mNext = next;
        return true;
    }

    SkyVisField SkyVisBaker::finish() const
    {
        SkyVisField out = mField;
        const std::uint32_t side = skyVisColumnsPerSide(mSettings.spacing);
        const std::uint32_t levels = mField.levels;
        std::vector<bool> valid(out.probes.size());
        for (std::size_t i = 0; i < valid.size(); ++i)
            valid[i] = !mInvalid[i];
        const auto indexOf = [&](std::uint32_t ix, std::uint32_t iy, std::uint32_t l) {
            return (static_cast<std::size_t>(ix) + static_cast<std::size_t>(side) * iy) * levels + l;
        };
        for (int pass = 0; pass < fillPasses; ++pass)
        {
            // What this pass fills only counts as valid in the next one, so the order inside a pass cannot matter.
            std::vector<bool> filled = valid;
            for (std::uint32_t iy = 0; iy < side; ++iy)
                for (std::uint32_t ix = 0; ix < side; ++ix)
                    for (std::uint32_t l = 0; l < levels; ++l)
                    {
                        const std::size_t i = indexOf(ix, iy, l);
                        if (valid[i])
                            continue;
                        VisSh sum{};
                        int count = 0;
                        const auto add = [&](std::size_t j) {
                            if (!valid[j])
                                return;
                            for (int k = 0; k < shCoefficients; ++k)
                                sum[k] += out.probes[j][k];
                            ++count;
                        };
                        if (ix > 0)
                            add(indexOf(ix - 1, iy, l));
                        if (ix + 1 < side)
                            add(indexOf(ix + 1, iy, l));
                        if (iy > 0)
                            add(indexOf(ix, iy - 1, l));
                        if (iy + 1 < side)
                            add(indexOf(ix, iy + 1, l));
                        if (l > 0)
                            add(indexOf(ix, iy, l - 1));
                        if (l + 1 < levels)
                            add(indexOf(ix, iy, l + 1));
                        if (count == 0)
                            continue;
                        for (int k = 0; k < shCoefficients; ++k)
                            out.probes[i][k] = skyVisQuantize(sum[k] / static_cast<float>(count));
                        filled[i] = true;
                    }
            valid = filled;
        }
        // Any still empty takes the visibility of open flat ground, which leaves the flat ambient as it is.
        VisSh neutral = hemisphereVisibility();
        for (float& v : neutral)
            v = skyVisQuantize(v);
        for (std::size_t i = 0; i < valid.size(); ++i)
            if (!valid[i])
                out.probes[i] = neutral;
        return out;
    }
}
