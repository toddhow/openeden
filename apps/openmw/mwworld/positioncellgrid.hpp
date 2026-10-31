#ifndef OPENMW_APPS_OPENMW_MWWORLD_POSITIONCELLGRID_H
#define OPENMW_APPS_OPENMW_MWWORLD_POSITIONCELLGRID_H

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

namespace MWWorld
{
    struct PositionCellGrid
    {
        vsg::vec3 mPosition;
        vsg::ivec4 mCellBounds;
    };
}

#endif
