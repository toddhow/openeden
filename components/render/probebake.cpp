#include "probebake.hpp"

#include <algorithm>
#include <cmath>

#include "probesamplermath.hpp"

namespace Render::Gi
{
    namespace
    {
        constexpr float pi = 3.14159265358979f;
        // The angle between neighbours of a spiral that fills a sphere evenly: pi * (3 - sqrt(5)).
        constexpr float goldenAngle = 2.39996323f;
        // The constant part of a probe of uniform light: the uniform radiance L projects to L * 4 pi * Y00.
        constexpr float uniformScale = 4.f * pi * 0.282095f;
        // The distance at which the classic falloff stops lighting (the shader's epsilon).
        constexpr float classicEpsilon = 0.03f;

        float quickstep(float x)
        {
            x = std::clamp(x, 0.f, 1.f);
            x = 1.f - x * x;
            return 1.f - x * x;
        }

        float length(const vsg::vec3& v)
        {
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }

        float dot(const vsg::vec3& a, const vsg::vec3& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        vsg::vec3 cross(const vsg::vec3& a, const vsg::vec3& b)
        {
            return vsg::vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
        }

        // Everything one probe's rays need.
        struct Context
        {
            const BakeSettings& settings;
            const RayWorld& world;
            const std::vector<BakeLight>& lights;
        };

        // The light of the cell's point lights that reaches a surface point, as the engine's lit diffuse would give
        // (colour times N.L times the falloff), with a shadow ray each.
        vsg::vec3 directLight(const Context& c, const vsg::vec3& point, const vsg::vec3& normal)
        {
            vsg::vec3 sum(0.f, 0.f, 0.f);
            const vsg::vec3 from = point + normal * surfaceOffset;
            for (const BakeLight& light : c.lights)
            {
                const vsg::vec3 toLight = light.position - from;
                const float distance = length(toLight);
                if (distance <= 0.f || distance > lightRange(c.settings.falloff, light.radius))
                    continue;
                const vsg::vec3 dir = toLight * (1.f / distance);
                const float facing = dot(normal, dir);
                if (facing <= 0.f)
                    continue;
                if (distance > lightClearance)
                {
                    vsg::vec3 hitPos, hitNormal;
                    if (c.world.cast(from, light.position - dir * lightClearance, hitPos, hitNormal))
                        continue;
                }
                sum += light.color * (facing * attenuate(c.settings.falloff, light.radius, distance));
            }
            return sum;
        }

        // The light arriving at `origin` from `dir`: the ambient light where the ray escapes, otherwise the ambient
        // light weighted by how far the surface is (a stand-in for how open the space is), plus the light the surface
        // reflects: its direct light and, while bounces remain, what reaches it from one random direction.
        vsg::vec3 radiance(const Context& c, Pcg32& rng, const vsg::vec3& origin, const vsg::vec3& dir, std::uint32_t depth)
        {
            const BakeSettings& s = c.settings;
            vsg::vec3 hitPos, hitNormal;
            if (!c.world.cast(origin, origin + dir * s.openDistance, hitPos, hitNormal))
                return s.ambient;
            hitNormal = facingNormal(hitNormal, dir);
            const float distance = length(hitPos - origin);
            vsg::vec3 result = s.ambient * std::min(1.f, distance / s.openDistance);
            if (depth < s.bounces)
            {
                vsg::vec3 incoming = directLight(c, hitPos, hitNormal);
                if (depth + 1 < s.bounces)
                {
                    const float u1 = rng.nextFloat();
                    const float u2 = rng.nextFloat();
                    incoming += radiance(c, rng, hitPos + hitNormal * surfaceOffset, cosineDirection(hitNormal, u1, u2),
                        depth + 1);
                }
                result += incoming * s.albedo;
            }
            return result;
        }
    }

    float lightRange(const LightFalloff& f, float radius)
    {
        if (!f.classic)
            return radius * 2.f;
        const float r = radius * f.linearRadiusMult;
        const float attenuation = f.linearValue / r;
        return 1.f / (classicEpsilon * attenuation);
    }

    float attenuate(const LightFalloff& f, float radius, float distance)
    {
        float value = 0.f;
        if (f.useConstant)
            value += f.constantValue;
        if (f.useLinear)
        {
            const float r = radius * f.linearRadiusMult;
            float attenuation = f.linearValue;
            if (f.linearMethod == 1)
                attenuation /= r;
            if (f.linearMethod == 2)
                attenuation /= r * r;
            value += attenuation * distance;
        }
        if (f.useQuadratic)
        {
            const float r = radius * f.quadraticRadiusMult;
            float attenuation = f.quadraticValue;
            if (f.quadraticMethod == 1)
                attenuation /= r;
            if (f.quadraticMethod == 2)
                attenuation /= r * r;
            value += attenuation * distance * distance;
        }
        float illumination = value > 0.f ? std::clamp(1.f / value, 0.f, 1.f) : 1.f;
        if (!f.classic)
            illumination *= 1.f - quickstep(distance / radius - 1.f);
        return illumination;
    }

    Pcg32::Pcg32(std::uint64_t seed, std::uint64_t stream)
    {
        mIncrement = (stream << 1u) | 1u;
        next();
        mState += seed;
        next();
    }

    std::uint32_t Pcg32::next()
    {
        const std::uint64_t old = mState;
        mState = old * 6364136223846793005ull + mIncrement;
        const auto xorShifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rotation = static_cast<std::uint32_t>(old >> 59u);
        return (xorShifted >> rotation) | (xorShifted << ((~rotation + 1u) & 31u));
    }

    float Pcg32::nextFloat()
    {
        // 24 bits fit a float exactly.
        return static_cast<float>(next() >> 8u) * (1.f / 16777216.f);
    }

    vsg::vec3 spiralDirection(std::uint32_t index, std::uint32_t count, float offset)
    {
        const float t = (static_cast<float>(index) + offset) / static_cast<float>(count);
        const float z = 1.f - 2.f * t;
        const float r = std::sqrt(std::max(0.f, 1.f - z * z));
        const float phi = goldenAngle * (static_cast<float>(index) + offset);
        return vsg::vec3(r * std::cos(phi), r * std::sin(phi), z);
    }

    vsg::vec3 facingNormal(const vsg::vec3& normal, const vsg::vec3& rayDir)
    {
        const float n = length(normal);
        const float r = length(rayDir);
        const vsg::vec3 reverse = r > 0.f ? rayDir * (-1.f / r) : vsg::vec3(0.f, 0.f, 1.f);
        constexpr float minLength = 1e-4f;
        if (!(n > minLength))
            return reverse;
        const vsg::vec3 unit = normal * (1.f / n);
        return dot(unit, rayDir) > 0.f ? unit * -1.f : unit;
    }

    vsg::vec3 cosineDirection(const vsg::vec3& normal, float u1, float u2)
    {
        const float r = std::sqrt(u1);
        const float phi = 2.f * pi * u2;
        const float x = r * std::cos(phi);
        const float y = r * std::sin(phi);
        const float z = std::sqrt(std::max(0.f, 1.f - u1));
        const vsg::vec3 helper = std::abs(normal.z) < 0.999f ? vsg::vec3(0.f, 0.f, 1.f) : vsg::vec3(1.f, 0.f, 0.f);
        vsg::vec3 tangent = cross(helper, normal);
        tangent = tangent * (1.f / length(tangent));
        const vsg::vec3 bitangent = cross(normal, tangent);
        return tangent * x + bitangent * y + normal * z;
    }

    ProbeResult bakeProbe(const BakeSettings& settings, const RayWorld& world, const std::vector<BakeLight>& lights,
        const vsg::vec3& position, std::uint32_t probeIndex)
    {
        const Context context{ settings, world, lights };
        Pcg32 rng(settings.seed, probeIndex);
        const float offset = rng.nextFloat();
        const float weight = 4.f * pi / static_cast<float>(settings.rays);
        ProbeResult result;
        std::uint32_t close = 0;
        for (std::uint32_t i = 0; i < settings.rays; ++i)
        {
            const vsg::vec3 dir = spiralDirection(i, settings.rays, offset);
            vsg::vec3 hitPos, hitNormal;
            if (world.cast(position, position + dir * settings.invalidDistance, hitPos, hitNormal))
                ++close;
            shAdd(result.sh, dir, radiance(context, rng, position, dir, 0), weight);
        }
        result.invalid = static_cast<float>(close) >= settings.invalidFraction * static_cast<float>(settings.rays);
        return result;
    }

    Volume layoutVolume(const vsg::vec3& low, const vsg::vec3& high, float spacing, std::size_t maxProbes)
    {
        constexpr float growth = 1.05f;
        Volume v;
        float s = spacing;
        while (true)
        {
            const float half = s * 0.5f;
            const vsg::vec3 lo = low - vsg::vec3(half, half, half);
            const vsg::vec3 hi = high + vsg::vec3(half, half, half);
            // A box that is not finite (an empty world) is one probe.
            float extent[3] = { hi.x - lo.x, hi.y - lo.y, hi.z - lo.z };
            for (float& e : extent)
                if (!std::isfinite(e) || e < 0.f)
                    e = 0.f;
            std::size_t product = 1;
            for (int a = 0; a < 3; ++a)
            {
                v.size[a] = static_cast<std::uint32_t>(std::max(0.f, std::ceil(extent[a] / s)) + 1.f);
                product *= v.size[a];
            }
            if (product <= maxProbes)
            {
                v.origin = lo;
                v.spacing = s;
                v.probes.assign(product, Sh9{});
                return v;
            }
            s *= growth;
        }
    }

    vsg::vec3 probePosition(const Volume& volume, std::size_t index)
    {
        const std::size_t sx = volume.size[0];
        const std::size_t sy = volume.size[1];
        const std::size_t x = index % sx;
        const std::size_t y = (index / sx) % sy;
        const std::size_t z = index / (sx * sy);
        return volume.origin
            + vsg::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) * volume.spacing;
    }

    Sh9 uniformProbe(const vsg::vec3& color)
    {
        Sh9 sh;
        sh.c[0] = color * uniformScale;
        return sh;
    }

    void fillInvalidProbes(Volume& volume, std::vector<bool> invalid, const vsg::vec3& ambient)
    {
        const std::int64_t sx = volume.size[0];
        const std::int64_t sy = volume.size[1];
        const std::int64_t sz = volume.size[2];
        static constexpr int steps[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 },
            { 0, 0, -1 } };
        for (int pass = 0; pass < fillPasses; ++pass)
        {
            std::vector<bool> filled = invalid;
            bool any = false;
            for (std::int64_t z = 0; z < sz; ++z)
                for (std::int64_t y = 0; y < sy; ++y)
                    for (std::int64_t x = 0; x < sx; ++x)
                    {
                        const std::size_t index = static_cast<std::size_t>(x + sx * (y + sy * z));
                        if (!invalid[index])
                            continue;
                        Sh9 sum;
                        int count = 0;
                        for (const auto& step : steps)
                        {
                            const std::int64_t nx = x + step[0], ny = y + step[1], nz = z + step[2];
                            if (nx < 0 || ny < 0 || nz < 0 || nx >= sx || ny >= sy || nz >= sz)
                                continue;
                            const std::size_t other = static_cast<std::size_t>(nx + sx * (ny + sy * nz));
                            if (invalid[other])
                                continue;
                            for (int k = 0; k < shCoefficients; ++k)
                                sum.c[k] += volume.probes[other].c[k];
                            ++count;
                        }
                        if (count == 0)
                            continue;
                        for (int k = 0; k < shCoefficients; ++k)
                            volume.probes[index].c[k] = sum.c[k] * (1.f / static_cast<float>(count));
                        filled[index] = false;
                        any = true;
                    }
            invalid = std::move(filled);
            if (!any)
                break;
        }
        for (std::size_t i = 0; i < invalid.size(); ++i)
            if (invalid[i])
                volume.probes[i] = uniformProbe(ambient);
    }

    VolumeBaker::VolumeBaker(
        const BakeSettings& settings, const RayWorld& world, std::vector<BakeLight> lights, Volume volume)
        : mSettings(settings)
        , mWorld(world)
        , mLights(std::move(lights))
        , mVolume(std::move(volume))
        , mInvalid(mVolume.probes.size(), false)
    {
    }

    bool VolumeBaker::step(std::size_t count)
    {
        const std::size_t end = std::min(mNext + count, mVolume.probes.size());
        for (; mNext < end; ++mNext)
        {
            const ProbeResult r = bakeProbe(
                mSettings, mWorld, mLights, probePosition(mVolume, mNext), static_cast<std::uint32_t>(mNext));
            mVolume.probes[mNext] = r.sh;
            mInvalid[mNext] = r.invalid;
        }
        return done();
    }

    void VolumeBaker::resume(const Volume& volume, std::size_t next, const std::vector<bool>& invalid)
    {
        if (volume.probes.size() != mVolume.probes.size() || invalid.size() != mInvalid.size()
            || next > mVolume.probes.size())
            return;
        std::copy(volume.probes.begin(), volume.probes.begin() + static_cast<std::ptrdiff_t>(next),
            mVolume.probes.begin());
        mInvalid = invalid;
        mNext = next;
    }

    Volume VolumeBaker::finish() const
    {
        Volume v = mVolume;
        fillInvalidProbes(v, mInvalid, mSettings.ambient);
        return v;
    }
}
