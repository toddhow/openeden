#ifndef OPENEDEN_RENDER_SKYVISMATH_H
#define OPENEDEN_RENDER_SKYVISMATH_H

// OpenEden (task 0036, stage a): the maths of the sky-visibility field. Header-only, no game types. The shader twin
// (files/shaders/lib/view/skyvis.glsl, stage c) repeats the formulas; change both together.
//
// In plain words: a probe stores V(direction), 1 where the sky can be seen and 0 where something blocks it. The sky
// is stored the same way, as a colour for each direction (also nine numbers per channel). Adding up sky times
// visibility over all directions gives the light that reaches the probe from the sky; it is divided by the same sum
// for open flat ground, so open ground keeps the flat ambient exactly and anything that sees less sky gets
// proportionally less, in the colour of the sky it can still see.

#include <algorithm>
#include <cmath>

#include <vsg/maths/vec3.h>

#include "hdrmath.hpp"
#include "probesamplermath.hpp"
#include "skyvisibility.hpp"

namespace Render::Gi
{
    // The sphere's solid angle, 4 pi.
    constexpr float skySphere = 12.5663706f;

    // V = 1 everywhere: only the constant term, sqrt(4 pi).
    inline VisSh openVisibility()
    {
        VisSh v{};
        v[0] = 3.5449077f;
        return v;
    }

    inline VisSh blockedVisibility()
    {
        return VisSh{};
    }

    // V = 1 for z > 0 and 0 below: the visibility of a point on open flat ground. Only the constant (2 pi * 0.282095),
    // the z term (0.488603 * pi) and zero for the rest (the other terms integrate to zero over a hemisphere).
    inline VisSh hemisphereVisibility()
    {
        VisSh v{};
        v[0] = 1.7724539f;
        v[2] = 0.488603f * 3.14159265f;
        return v;
    }

    // Adds a visible direction with the given weight (the weights of a whole sphere of rays sum to 4 pi).
    inline void visAdd(VisSh& sh, const vsg::vec3& dir, float weight)
    {
        const auto basis = shBasis(dir);
        for (int i = 0; i < shCoefficients; ++i)
            sh[i] += basis[i] * weight;
    }

    // The colour of the sky for every direction as SH, from a zenith colour, a horizon colour and a ground colour
    // (linear light). The sky depends on the height of the direction only, so just the terms that do not depend on
    // the direction's compass bearing are non-zero. Above the horizon the colour goes from the horizon's to the
    // zenith's by the height of the direction; below it is the ground colour.
    inline Sh9 projectSky(const vsg::vec3& zenith, const vsg::vec3& horizon, const vsg::vec3& ground)
    {
        constexpr int steps = 256; // even, so the horizon is a boundary between two steps
        constexpr float pi = 3.14159265358979f;
        Sh9 sky;
        const float h = 2.f / static_cast<float>(steps);
        for (int s = 0; s < steps; ++s)
        {
            // Equal steps of z are equal solid angles (Archimedes), each 2 pi * h.
            const float z = -1.f + (static_cast<float>(s) + 0.5f) * h;
            const vsg::vec3 colour = z >= 0.f ? horizon + (zenith - horizon) * z : ground;
            const float w = 2.f * pi * h;
            sky.c[0] += colour * (0.282095f * w);
            sky.c[2] += colour * (0.488603f * z * w);
            sky.c[6] += colour * (0.315392f * (3.f * z * z - 1.f) * w);
        }
        return sky;
    }

    // How bright the lower half of the sky is against the horizon colour (a named choice: the ground reflects some of
    // the light and is darker than the sky).
    constexpr float groundSkyFactor = 0.3f;

    // The sky for the renderer: the weather's sky (zenith) and horizon colours, display-encoded as the weather and
    // Render::Atmosphere hold them, to SH in linear light.
    inline Sh9 skyFromColours(const vsg::vec3& skyEncoded, const vsg::vec3& horizonEncoded)
    {
        const vsg::vec3 zenith = Render::Hdr::toLinear(skyEncoded);
        const vsg::vec3 horizon = Render::Hdr::toLinear(horizonEncoded);
        return projectSky(zenith, horizon, horizon * groundSkyFactor);
    }

    // The light from the sky that reaches a point with this visibility, per channel, divided by 4 pi (so a uniform
    // sky of colour L seen by an open point gives L).
    inline vsg::vec3 skyDot(const Sh9& sky, const VisSh& vis)
    {
        vsg::vec3 sum(0.f, 0.f, 0.f);
        for (int i = 0; i < shCoefficients; ++i)
            sum += sky.c[i] * vis[i];
        return sum * (1.f / skySphere);
    }

    // The share of the flat ambient a point keeps: skyDot against the same for open flat ground, per channel, from 0
    // to 1 (a point in the air sees more than open ground, and is not made brighter than the flat ambient).
    inline vsg::vec3 skyOcclusion(const Sh9& sky, const VisSh& vis)
    {
        const vsg::vec3 here = skyDot(sky, vis);
        const vsg::vec3 ground = skyDot(sky, hemisphereVisibility());
        const auto channel = [](float a, float b) { return b > 1e-6f ? std::clamp(a / b, 0.f, 1.f) : 1.f; };
        return vsg::vec3(channel(here.x, ground.x), channel(here.y, ground.y), channel(here.z, ground.z));
    }

    // The cosine weighted share of the sphere a surface facing `normal` sees: the visibility convolved with the cosine
    // lobe (the factors 1, 2/3 and 1/4 of shAmbient), 1 for a surface that sees everything its lobe covers.
    inline float visCosine(const VisSh& v, const vsg::vec3& normal)
    {
        constexpr std::array<float, 9> band
            = { 1.f, 2.f / 3.f, 2.f / 3.f, 2.f / 3.f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f };
        const auto basis = shBasis(normal);
        float sum = 0.f;
        for (int i = 0; i < shCoefficients; ++i)
            sum += v[i] * basis[i] * band[i];
        return sum;
    }

    // visCosine of hemisphereVisibility() for a unit normal, in closed form: 0.5 + 0.5 z.
    inline float hemisphereCosine(const vsg::vec3& unitNormal)
    {
        return 0.5f + 0.5f * unitNormal.z;
    }

    // The share of the flat ambient a surface keeps, per channel: the geometric share (what the surface can see of what
    // open ground it faces would see, from visCosine) times the shift in colour that comes from seeing only part of the
    // sky (skyOcclusion against the plain share, so it is not counted twice). A zero normal gives skyOcclusion.
    inline vsg::vec3 skyOcclusionNormal(const Sh9& sky, const VisSh& vis, const vsg::vec3& normal)
    {
        const vsg::vec3 ground = skyDot(sky, hemisphereVisibility());
        const vsg::vec3 here = skyDot(sky, vis);
        const float openGround = hemisphereVisibility()[0];
        const float omni = vis[0] / openGround;
        const auto rawChannel = [](float a, float b) { return b > 1e-6f ? a / b : 1.f; };
        const vsg::vec3 skyRatio(
            rawChannel(here.x, ground.x), rawChannel(here.y, ground.y), rawChannel(here.z, ground.z));
        const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        float geometric = std::clamp(omni, 0.f, 1.f);
        if (length > 1e-10f)
        {
            const vsg::vec3 n = normal * (1.f / length);
            const float groundShare = hemisphereCosine(n);
            // A surface facing straight down sees none of open ground: it keeps the plain share.
            const float s = std::clamp(groundShare / 0.2f, 0.f, 1.f);
            const float weight = s * s * (3.f - 2.f * s);
            const float facing = std::clamp(visCosine(vis, n) / std::max(groundShare, 1e-3f), 0.f, 1.f);
            geometric = geometric * (1.f - weight) + facing * weight;
        }
        const vsg::vec3 shift = omni > 1e-3f ? skyRatio * (1.f / omni) : vsg::vec3(1.f, 1.f, 1.f);
        const auto clamp01 = [](float x) { return std::clamp(x, 0.f, 1.f); };
        return vsg::vec3(clamp01(geometric * shift.x), clamp01(geometric * shift.y), clamp01(geometric * shift.z));
    }

    // --- The sky image's reflection on objects (the "glaze" fix) ---------------------------------------------------
    // The sky environment (rendermanager.cpp updateSkyEnvironment) is one reflection probe holding the whole sky. Added
    // unmodified to every PBR pixel it lifted rough dark stone to milky grey, because nothing said the pixel could see
    // that sky. The shader term is scaled by skyReflectionFactor. Twins: skyReflectionFactor, skyDirectionVisibility and
    // skyReflectionRoughnessWeight in files/shaders/lib/view/skyvis.glsl and skyvis.slang; change them together.

    // What a point with no baked sky-visibility data is assumed to see in the mirror direction. Unbaked cells must not
    // glaze either, so this is conservative (a quarter of the sky image) rather than the old 1.
    constexpr float skyReflectionUnbakedVisibility = 0.25f;

    // How much of the sky image a surface of this roughness reflects. A dielectric's reflection is a faint rim at
    // glancing angles and a rough one scatters the sky into the ambient term (which the sky-visibility ambient already
    // carries), so the weight is (1 - roughness)^2, the square the specular lobe's narrowing follows (a choice, not a
    // measured value). A metal keeps its full reflection at any roughness: the weight blends to 1 with metalness.
    inline float skyReflectionRoughnessWeight(float roughness, float metalness)
    {
        const float smooth = 1.f - std::clamp(roughness, 0.f, 1.f);
        const float m = std::clamp(metalness, 0.f, 1.f);
        return smooth * smooth * (1.f - m) + m;
    }

    // The share of the sky a point sees in direction `dir` (need not be unit length): the geometric part of
    // skyOcclusionNormal without its colour shift. 1 on open ground in any direction above the horizon, 0 under a roof.
    inline float skyDirectionVisibility(const VisSh& vis, const vsg::vec3& dir)
    {
        const float omni = std::clamp(vis[0] / hemisphereVisibility()[0], 0.f, 1.f);
        const float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (!(length > 1e-10f))
            return omni;
        const vsg::vec3 n = dir * (1.f / length);
        const float groundShare = hemisphereCosine(n);
        const float s = std::clamp(groundShare / 0.2f, 0.f, 1.f);
        const float weight = s * s * (3.f - 2.f * s);
        const float facing = std::clamp(visCosine(vis, n) / std::max(groundShare, 1e-3f), 0.f, 1.f);
        return omni * (1.f - weight) + facing * weight;
    }

    // The factor the sky image's reflection is multiplied by: the visibility toward the mirror direction (where
    // `dataWeight`, 0 to 1, is the share of the sampled columns that have baked data; the rest falls back to
    // skyReflectionUnbakedVisibility) times the roughness weight.
    inline float skyReflectionFactor(float visibility, float dataWeight, float roughness, float metalness)
    {
        const float w = std::clamp(dataWeight, 0.f, 1.f);
        const float vis = skyReflectionUnbakedVisibility * (1.f - w) + std::clamp(visibility, 0.f, 1.f) * w;
        return vis * skyReflectionRoughnessWeight(roughness, metalness);
    }
}

#endif
