#ifndef OPENEDEN_RENDER_SKYVISIBILITY_H
#define OPENEDEN_RENDER_SKYVISIBILITY_H

// OpenEden (task 0036, stage a): the exterior sky-visibility field. For a lattice of points in an exterior cell the
// baker records how much of the sky each point can see, in which directions, as nine spherical harmonics numbers (a
// visibility, 0 blocked to 1 open; not a colour). This header is the data and the `openeden.skyvis` v1 file format
// (a parser with located errors and a canonical writer). The maths that uses it is in skyvismath.hpp. No game types.
//
// The lattice follows the ground: a grid of columns (one every `spacing` units, 33 by 33 for a cell of 8192 units,
// the columns on a cell's border being shared with its neighbour) and, in each column, `levels` probes
// `levelSpacing` units apart starting half a spacing above the column's ground height. So a cell is about
// 33 x 33 x 8 probes without any pruning, whatever the terrain does.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "probes.hpp"

namespace Render::Gi
{
    // The side of an exterior cell in game units (ESM::Land::REAL_SIZE, components/esm3/loadland.hpp).
    constexpr float exteriorCellSize = 8192.f;
    constexpr float defaultSkySpacing = 256.f;
    constexpr float defaultSkyLevelSpacing = 128.f;
    constexpr std::uint32_t defaultSkyLevels = 8;
    constexpr std::uint32_t maxSkyLevels = 16;
    // The spacings a file may use: the cell divided into 4 to 64 parts.
    constexpr std::uint32_t minSkyDivisions = 4;
    constexpr std::uint32_t maxSkyDivisions = 64;
    // Stored numbers are whole multiples of this (16-bit integers in the file).
    constexpr float skyVisQuantum = 1.f / 4096.f;

    // A visibility function in the SH basis of probesamplermath.hpp (shBasis): coefficient i of V.
    using VisSh = std::array<float, shCoefficients>;

    struct SkyVisField
    {
        int cellX = 0;
        int cellY = 0;
        std::uint32_t rays = 0;
        std::uint32_t seed = 0;
        // The distance between columns, and between the levels of a column (game units).
        float spacing = defaultSkySpacing;
        float levelSpacing = defaultSkyLevelSpacing;
        std::uint32_t levels = defaultSkyLevels;
        // columnsPerSide^2 ground heights, column (ix, iy) at [ix + columnsPerSide * iy]; ix counts east from the
        // cell's western edge, iy north from its southern edge (the world's x and y).
        std::vector<float> base;
        // probes[column * levels + level]; level k sits at base + (k + 0.5) * levelSpacing.
        std::vector<VisSh> probes;
    };

    // Columns along one side: the cell divided by the spacing, plus one.
    std::uint32_t skyVisColumnsPerSide(float spacing);
    // The height of a level of a column.
    inline float skyVisLevelHeight(float base, std::uint32_t level, float levelSpacing)
    {
        return base + (static_cast<float>(level) + 0.5f) * levelSpacing;
    }

    // Rounds to the nearest multiple of skyVisQuantum, within the 16-bit range of the file.
    float skyVisQuantize(float value);

    std::optional<SkyVisField> parseSkyVis(std::string_view json, std::string_view fileName, ProbeError& error);
    // Precondition: validate(field) is empty.
    std::string writeCanonical(const SkyVisField& field);
    // "" when valid, else the first problem found.
    std::string validate(const SkyVisField& field);

    // "openeden/probes/exterior/<x>_<y>.skyvis.json"
    std::string skyVisFilePath(int cellX, int cellY);
}

#endif
