#ifndef VSGOPENMW_VSGUTIL_INTERSECTNORMAL_H
#define VSGOPENMW_VSGUTIL_INTERSECTNORMAL_H

#include <vsg/maths/transform.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/state/ArrayState.h>
#include <vsg/utils/LineSegmentIntersector.h>

#include "id.hpp"

namespace vsgUtil
{
    /// The ArrayState that computes a hit's vertices, if it has one: terrain's from its height map, a software skin's posed.
    /// What such a state returns is a proxy that later draws overwrite, so it is computed again here, for this hit.
    inline vsg::ref_ptr<const vsg::vec3Array> computedHitVertices(const vsg::LineSegmentIntersector::Intersection& hit)
    {
        for (auto it = hit.nodePath.rbegin(); it != hit.nodePath.rend(); ++it)
        {
            const auto* group = dynamic_cast<const vsg::StateGroup*>(*it);
            if (!group || !group->prototypeArrayState)
                continue;
            auto state = group->prototypeArrayState->cloneArrayState();
            for (const auto& command : group->stateCommands)
                command->accept(*state);
            if (!hit.arrays.empty())
                hit.arrays.front()->accept(*state);
            return state->vertexArray(hit.instanceIndex);
        }
        return {};
    }

    /// The vertices that the hit's indexRatios index, as the intersector saw them.
    inline vsg::ref_ptr<const vsg::vec3Array> hitVertices(const vsg::LineSegmentIntersector::Intersection& hit)
    {
        if (auto computed = computedHitVertices(hit))
            return computed;
        if (hit.arrays.empty())
            return {};
        return vsg::ref_ptr<const vsg::vec3Array>(dynamic_cast<const vsg::vec3Array*>(hit.arrays.front().get()));
    }

    /// The hit triangle's normal in the world, as osgUtil's getWorldIntersectNormal gave it, or nothing for a degenerate one.
    inline bool geometricWorldNormal(const vsg::LineSegmentIntersector::Intersection& hit, const vsg::vec3Array& vertices,
        vsg::vec3& normal)
    {
        if (hit.indexRatios.size() < 3)
            return false;
        const vsg::dvec3 v0(vertices[hit.indexRatios[0].index]);
        const vsg::dvec3 v1(vertices[hit.indexRatios[1].index]);
        const vsg::dvec3 v2(vertices[hit.indexRatios[2].index]);
        const vsg::dvec3 local = vsg::cross(v1 - v0, v2 - v0);
        if (vsg::length2(local) == 0.0)
            return false;
        normal = vsg::vec3(vsg::normalize(local * vsg::inverse_3x3(hit.localToWorld)));
        return true;
    }

    /// A vertex's normal array holds a bind pose, so a posed or computed surface takes its triangle's normal.
    inline vsg::vec3 worldNormal(const vsg::LineSegmentIntersector::Intersection& intersection, int arrayID)
    {
        vsg::vec3 normal;
        if (auto computed = computedHitVertices(intersection))
        {
            if (geometricWorldNormal(intersection, *computed, normal))
                return normal;
        }
        for (auto& data : intersection.arrays)
        {
            if (auto id = vsgUtil::ID::get(*data))
            {
                if (id->id == arrayID)
                {
                    auto& array = static_cast<vsg::vec3Array&>(*data);
                    vsg::dvec3 n(0, 0, 0);
                    for (auto& r : intersection.indexRatios)
                        n += vsg::dvec3(array[r.index]) * r.ratio;
                    return vsg::vec3(vsg::normalize(n * vsg::inverse_3x3(intersection.localToWorld)));
                }
            }
        }
        if (auto vertices = hitVertices(intersection))
        {
            if (geometricWorldNormal(intersection, *vertices, normal))
                return normal;
        }
        return { 0, 0, 1 };
    }
}

#endif
