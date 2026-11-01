#ifndef OPENEDEN_RENDER_PROBES_H
#define OPENEDEN_RENDER_PROBES_H

// OpenEden (task 0028): baked light probes. This header is the data: the probe volumes, the `openeden.probes` file
// format (a parser with located errors and a canonical writer) and the naming of a cell's file. The maths that reads
// the probes is in probesamplermath.hpp. No game types.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vsg/maths/vec3.h>

namespace Render::Gi
{
    constexpr int shCoefficients = 9;
    // Game units; also the baker's default.
    constexpr float defaultSpacing = 128.f;
    // The GPU buffer's capacity and the parser's limit.
    constexpr std::size_t maxVolumeProbes = 8192;
    constexpr std::size_t maxVolumes = 64;
    // The sample position is moved this fraction of the spacing along the surface normal, so a wall does not read the
    // probes behind it.
    constexpr float normalBiasFraction = 0.3f;

    // Radiance projected on the real spherical harmonics basis of probesamplermath.hpp.
    struct Sh9
    {
        std::array<vsg::vec3, shCoefficients> c{};
    };

    // A regular grid of probes. probes[x + size[0] * (y + size[1] * z)] sits at origin + spacing * (x, y, z), in world
    // units, Z up.
    struct Volume
    {
        vsg::vec3 origin{ 0.f, 0.f, 0.f };
        float spacing = defaultSpacing;
        std::array<std::uint32_t, 3> size{ 1, 1, 1 };
        std::vector<Sh9> probes;
    };

    // An interior is identified by its name, an exterior cell by its grid coordinates.
    struct CellId
    {
        bool interior = true;
        std::string name;
        int x = 0;
        int y = 0;
    };

    struct ProbeSet
    {
        CellId cell;
        std::uint32_t rays = 0;
        std::uint32_t bounces = 0;
        std::uint32_t seed = 0;
        // The flat ambient colour (linear) that the bake assumed.
        vsg::vec3 ambient{ 0.f, 0.f, 0.f };
        std::vector<Volume> volumes;
    };

    struct ProbeError
    {
        std::string file;
        int line = 0;
        std::string field;
        std::string message;
        // "file:line: field: message"
        std::string describe() const;
    };

    std::optional<ProbeSet> parseProbes(std::string_view json, std::string_view fileName, ProbeError& error);
    // Precondition: validate(set) is empty.
    std::string writeCanonical(const ProbeSet& set);
    // "" when valid, else the first problem found.
    std::string validate(const ProbeSet& set);

    // "interior/<sanitised name>-<hash>" or "exterior/<x>_<y>".
    std::string cellKey(const CellId& cell);
    // "openeden/probes/<cellKey>.json"
    std::string probeFilePath(const CellId& cell);
}

#endif
