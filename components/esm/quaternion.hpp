#ifndef OPENMW_COMPONENTS_ESM_QUATERNION_H
#define OPENMW_COMPONENTS_ESM_QUATERNION_H

#include <vsg/maths/quat.h>

namespace ESM
{
    // format 0, savegames only
    struct Quaternion
    {
        float mValues[4];

        Quaternion() = default;

        Quaternion(const vsg::dquat& q)
            : mValues{
                static_cast<float>(q.w),
                static_cast<float>(q.x),
                static_cast<float>(q.y),
                static_cast<float>(q.z),
            }
        {
        }

        operator vsg::dquat() const { return vsg::dquat(mValues[1], mValues[2], mValues[3], mValues[0]); }
    };
}

#endif
