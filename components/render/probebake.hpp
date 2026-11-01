#ifndef OPENEDEN_RENDER_PROBEBAKE_H
#define OPENEDEN_RENDER_PROBEBAKE_H

// OpenEden (task 0028, stage c): the pure part of the light probe baker. It shoots rays from each probe through a
// `RayWorld` (the game's Jolt world in the game, a made-up room in the tests), adds up the light that arrives, and
// stores it as spherical harmonics. No game types. Everything is deterministic: the same settings, world and lights
// give the same probes, whatever the order or the time slicing.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vsg/maths/vec3.h>

#include "probes.hpp"

namespace Render::Gi
{
    // The game's point light falloff (the LightAttenuation_* fallback values and `classic falloff`); the C++ twin of
    // `lightRange` and `attenuate` in files/shaders/lib/light/equations.glsl. Change both together.
    struct LightFalloff
    {
        bool useConstant = false;
        float constantValue = 0.f;
        bool useLinear = true;
        int linearMethod = 1;
        float linearValue = 3.f;
        float linearRadiusMult = 1.f;
        bool useQuadratic = false;
        int quadraticMethod = 1;
        float quadraticValue = 16.f;
        float quadraticRadiusMult = 1.f;
        bool classic = false;
    };

    // The distance at which a light of this radius ends.
    float lightRange(const LightFalloff& falloff, float radius);
    // The share of its colour a light of this radius gives at this distance.
    float attenuate(const LightFalloff& falloff, float radius, float distance);

    // A point light of the cell: world position, linear colour (negative for a darkness light) and radius.
    struct BakeLight
    {
        vsg::vec3 position;
        vsg::vec3 color;
        float radius = 0.f;
    };

    // What the baker can ask of the world: the first surface along a segment.
    class RayWorld
    {
    public:
        virtual ~RayWorld() = default;
        // True when something is hit between `from` and `to`; then `hitPos` and `hitNormal` (unit length, facing the
        // side the ray came from) are set.
        virtual bool cast(const vsg::vec3& from, const vsg::vec3& to, vsg::vec3& hitPos, vsg::vec3& hitNormal) const
            = 0;
    };

    // How far a ray looks for a surface, and the distance at which the ambient light counts as fully open (game units).
    constexpr float defaultOpenDistance = 512.f;
    // The one reflectance of every surface: the physics world carries no textures (task 0028, D5).
    constexpr float defaultAlbedo = 0.5f;
    // A probe at least this share of whose rays hit within `invalidDistance` is taken to be inside solid geometry.
    constexpr float defaultInvalidFraction = 0.9f;
    constexpr float defaultInvalidDistance = 16.f;
    // Shadow and bounce rays start this far off the surface, along its normal.
    constexpr float surfaceOffset = 1.f;
    // A shadow ray stops this far short of the light, so the light's own lamp model does not shade it.
    constexpr float lightClearance = 24.f;
    // Passes of filling invalid probes from their valid neighbours.
    constexpr int fillPasses = 3;

    struct BakeSettings
    {
        std::uint32_t rays = 256;
        std::uint32_t bounces = 2;
        std::uint32_t seed = 1;
        float openDistance = defaultOpenDistance;
        float albedo = defaultAlbedo;
        float invalidDistance = defaultInvalidDistance;
        float invalidFraction = defaultInvalidFraction;
        // The cell's flat ambient colour (linear): what a ray that escapes carries.
        vsg::vec3 ambient{ 0.f, 0.f, 0.f };
        LightFalloff falloff;
    };

    struct ProbeResult
    {
        Sh9 sh;
        bool invalid = false;
    };

    // The 32-bit PCG generator (O'Neill), used so a probe's random numbers depend only on the seed and its index.
    class Pcg32
    {
    public:
        Pcg32(std::uint64_t seed, std::uint64_t stream);
        std::uint32_t next();
        // In [0, 1).
        float nextFloat();

    private:
        std::uint64_t mState = 0;
        std::uint64_t mIncrement = 1;
    };

    // The `index`-th of `count` directions of a spiral over the sphere, shifted by `offset` in [0, 1).
    vsg::vec3 spiralDirection(std::uint32_t index, std::uint32_t count, float offset);
    // `normal` turned to face against `rayDir` (a surface normal may point either way), as a unit vector; a zero normal
    // becomes the reverse of the ray.
    vsg::vec3 facingNormal(const vsg::vec3& normal, const vsg::vec3& rayDir);
    // A direction around `normal`, cosine weighted, from two numbers in [0, 1).
    vsg::vec3 cosineDirection(const vsg::vec3& normal, float u1, float u2);

    // Bakes one probe at `position`. `lights` are visited in the given order.
    ProbeResult bakeProbe(const BakeSettings& settings, const RayWorld& world, const std::vector<BakeLight>& lights,
        const vsg::vec3& position, std::uint32_t probeIndex);

    // A volume covering the box from `low` to `high` (grown by half a spacing on every side) with probes at least
    // `spacing` apart, and as many probes as fit in `maxProbes`; the probes are not filled in.
    Volume layoutVolume(const vsg::vec3& low, const vsg::vec3& high, float spacing, std::size_t maxProbes);

    // The position of probe `index` of a volume.
    vsg::vec3 probePosition(const Volume& volume, std::size_t index);

    // A probe of a uniform light of this colour.
    Sh9 uniformProbe(const vsg::vec3& color);

    // Replaces each invalid probe by the average of its valid face neighbours, a few passes; any still invalid becomes
    // a uniform probe of `ambient`.
    void fillInvalidProbes(Volume& volume, std::vector<bool> invalid, const vsg::vec3& ambient);

    // Bakes a volume a few probes at a time, so a caller can keep its window alive and save progress.
    class VolumeBaker
    {
    public:
        // `volume` has its layout (origin, spacing, size) and no probes yet; the baker owns the lights and borrows the
        // world, which must outlive it.
        VolumeBaker(const BakeSettings& settings, const RayWorld& world, std::vector<BakeLight> lights, Volume volume);

        // Bakes up to `count` more probes; true when every probe is done.
        bool step(std::size_t count);
        bool done() const { return mNext >= mVolume.probes.size(); }
        std::size_t next() const { return mNext; }
        std::size_t total() const { return mVolume.probes.size(); }

        // Continues a bake that stopped: the probes before `next` are taken from `volume`, and the invalid ones listed.
        void resume(const Volume& volume, std::size_t next, const std::vector<bool>& invalid);
        const std::vector<bool>& invalid() const { return mInvalid; }
        const Volume& volume() const { return mVolume; }

        // The finished volume, with the invalid probes filled in. Precondition: done().
        Volume finish() const;

    private:
        BakeSettings mSettings;
        const RayWorld& mWorld;
        std::vector<BakeLight> mLights;
        Volume mVolume;
        std::vector<bool> mInvalid;
        std::size_t mNext = 0;
    };
}

#endif
