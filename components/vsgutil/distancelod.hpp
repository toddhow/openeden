#ifndef OPENEDEN_VSGUTIL_DISTANCELOD_H
#define OPENEDEN_VSGUTIL_DISTANCELOD_H

#include <vector>

#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

namespace vsgUtil
{
    /**
     * @brief Level of detail chosen by distance from the eye, as osg::LOD in DISTANCE_FROM_EYE_POINT mode (task 0012):
     * child i is drawn when the distance from the eye to center, in the node's own units (eye-space distance divided by
     * the scale above the node), lies in [ranges[i].min, ranges[i].max). Every matching
     * child is drawn; children without a range are never drawn.
     *
     * vsg::LOD chooses by projected screen size and draws only the first match, which cannot express NIF's
     * NiLODNode ranges. Only recording selects; other visitors (compilation, intersection, bounds) see every child.
     */
    class DistanceLOD : public vsg::Inherit<vsg::Group, DistanceLOD>
    {
    public:
        struct Range
        {
            float min = 0.f;
            float max = 0.f;
        };

        vsg::vec3 center;
        std::vector<Range> ranges;

        /// Indices of the children drawn at this eye-space distance.
        bool isVisible(std::size_t child, double distance) const
        {
            return child < ranges.size() && distance >= ranges[child].min && distance < ranges[child].max;
        }

        using vsg::Inherit<vsg::Group, DistanceLOD>::accept;
        void accept(vsg::RecordTraversal& visitor) const override;
    };
}

#endif
