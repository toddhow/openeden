#ifndef OPENEDEN_RENDER_SKYVISBAKE_H
#define OPENEDEN_RENDER_SKYVISBAKE_H

// OpenEden (task 0036, stage b): the pure part of the exterior sky-visibility baker. For each probe of a cell's
// lattice it shoots rays through a `SkyRayWorld` (the game's Jolt world in the game, a made-up scene in the tests) and
// keeps one fact per ray: did it escape to the sky? Everything is deterministic: the random numbers of a probe depend
// only on the seed and the probe's place in the world's lattice, never on the order or the time slicing, and the
// probes on the border between two cells are bit-identical in both cells' bakes. No game types.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vsg/maths/vec3.h>

#include "probebake.hpp"
#include "skyvisibility.hpp"

namespace Render::Gi
{
    // What the sky baker can ask of the world, beyond a ray: the height of the terrain itself (not buildings or trees),
    // which the lattice follows.
    class SkyRayWorld : public RayWorld
    {
    public:
        // True when there is terrain at (x, y); then `z` is its height.
        virtual bool groundHeight(float x, float y, float& z) const = 0;
    };

    // A ray counts as seeing the sky when nothing stops it within this distance (game units; a cell is 8192).
    constexpr float defaultSkyDistance = 8192.f;
    // The ground height of a column with no terrain (open water, nothing loaded): sea level, Morrowind's water at 0.
    constexpr float defaultSkyFallbackHeight = 0.f;

    struct SkyBakeSettings
    {
        std::uint32_t rays = 256;
        std::uint32_t seed = 1;
        float spacing = defaultSkySpacing;
        float levelSpacing = defaultSkyLevelSpacing;
        std::uint32_t levels = defaultSkyLevels;
        float skyDistance = defaultSkyDistance;
        float fallbackHeight = defaultSkyFallbackHeight;
        // A probe at least this share of whose rays stop within `invalidDistance` is taken to be inside solid
        // geometry.
        float invalidDistance = defaultInvalidDistance;
        float invalidFraction = defaultInvalidFraction;
    };

    struct SkyProbeResult
    {
        VisSh sh{};
        bool invalid = false;
    };

    // The key of the probe at global lattice place (gx, gy), level: the same for the two cells that share a border.
    std::uint64_t skyProbeKey(std::int64_t gx, std::int64_t gy, std::uint32_t level);

    // Bakes one probe at `position`. `key` makes its random numbers its own (see skyProbeKey).
    SkyProbeResult bakeSkyProbe(
        const SkyBakeSettings& settings, const RayWorld& world, const vsg::vec3& position, std::uint64_t key);

    // Bakes one exterior cell a few probes at a time, so a caller can keep its window alive and save progress.
    class SkyVisBaker
    {
    public:
        // Reads the cell's ground heights at once (one ray per column); the world must outlive the baker.
        SkyVisBaker(const SkyBakeSettings& settings, const SkyRayWorld& world, int cellX, int cellY);

        // Bakes up to `count` more probes; true when every probe is done.
        bool step(std::size_t count);
        bool done() const { return mNext >= mField.probes.size(); }
        std::size_t next() const { return mNext; }
        std::size_t total() const { return mField.probes.size(); }

        // Continues a bake that stopped: the first `next` probes are taken from `partial`, and the invalid ones
        // listed. Returns false and changes nothing when `partial` is not this baker's cell, settings or ground.
        bool resume(const SkyVisField& partial, std::size_t next, const std::vector<bool>& invalid);
        const std::vector<bool>& invalid() const { return mInvalid; }
        const SkyVisField& field() const { return mField; }

        // The finished field, with the invalid probes filled in. Precondition: done().
        SkyVisField finish() const;

    private:
        SkyBakeSettings mSettings;
        const SkyRayWorld& mWorld;
        SkyVisField mField;
        std::vector<bool> mInvalid;
        std::size_t mNext = 0;
    };
}

#endif
