#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_CELLGRIDBOUNDS_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_CELLGRIDBOUNDS_H

#include <components/misc/vsgmath.hpp>

namespace DetourNavigator
{
    struct CellGridBounds
    {
        vsg::ivec2 mCenter;
        int mHalfSize;
    };
}

#endif
