#ifndef OPENEDEN_RENDER_REFLECTIONPROBES_H
#define OPENEDEN_RENDER_REFLECTIONPROBES_H

// OpenEden (task 0028, stage d): reflection probes, baked next to the light probes. A cell's reflection probes are
// one file, `openeden/probes/<cellKey>.reflections.json` (format `openeden.reflections`, version 1), beside the light
// probe file. It is a separate file, and not version 2 of `openeden.probes`, because version 1 readers refuse any other
// version: a cell with light probes and no reflections, or the reverse, works either way. No game types.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vsg/maths/vec3.h>

#include "probes.hpp"
#include "reflectionmath.hpp"

namespace Render::Gi
{
    // One reflection probe: where it is and the room as seen from there, in an octahedral map (see reflectionmath.hpp).
    struct ReflectionProbe
    {
        vsg::vec3 position{ 0.f, 0.f, 0.f };
        // resolution * resolution linear colours, row by row. Texel (x, y) is texels[y * resolution + x].
        std::vector<vsg::vec3> texels;
    };

    struct ReflectionSet
    {
        CellId cell;
        // How the bake was made.
        std::uint32_t samples = 0;
        std::uint32_t bounces = 0;
        std::uint32_t seed = 0;
        // The flat ambient colour (linear) that the bake assumed, as in ProbeSet.
        vsg::vec3 ambient{ 0.f, 0.f, 0.f };
        std::uint32_t resolution = defaultReflectionResolution;
        std::vector<ReflectionProbe> probes;
    };

    // The same kind of located error as the light probe file's.
    std::optional<ReflectionSet> parseReflections(std::string_view json, std::string_view fileName, ProbeError& error);
    // Canonical text: two-space indent, keys in a fixed order, LF, one final newline. Precondition: validate is empty.
    std::string writeCanonical(const ReflectionSet& set);
    // "" when valid, else the first problem found.
    std::string validate(const ReflectionSet& set);

    // "openeden/probes/<cellKey>.reflections.json"
    std::string reflectionFilePath(const CellId& cell);
}

#endif
