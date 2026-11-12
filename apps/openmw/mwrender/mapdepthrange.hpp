#ifndef OPENMW_MWRENDER_MAPDEPTHRANGE_H
#define OPENMW_MWRENDER_MAPDEPTHRANGE_H

#include <optional>

#include <components/esm/refid.hpp>

#include <vsg/maths/box.h>
#include <vsg/maths/vec2.h>

namespace MWRender
{
    // The heights an exterior local map's camera must see (task 0073, review): the loaded objects, the cell's terrain
    // and its water, as OpenMW's local map took them from the whole scene root (localmap.cpp, mSceneRoot->getBound()).
    struct MapDepthRange
    {
        float mMin = 0.f;
        float mMax = 0.f;
    };

    // `objects` may be invalid (an exterior grid with no references), `terrain` is the cell's (min, max) height and
    // `water` the water level, each when known. Non-finite values are ignored. Nothing known gives no range: the
    // caller then renders nothing (the old code converted an empty box's +-inf to float, which is undefined).
    // The side of one exterior map cell in world units: the cell size of the worldspace (8192 for Morrowind's,
    // 4096 for a neutral game's), so grid x, y times it is where the cell lies.
    float mapWorldSizeFor(const ESM::RefId& worldspace);

    std::optional<MapDepthRange> mapDepthRange(
        const vsg::dbox& objects, const std::optional<vsg::vec2>& terrain, const std::optional<float>& water);
}

#endif
