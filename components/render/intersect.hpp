#ifndef OPENEDEN_RENDER_INTERSECT_H
#define OPENEDEN_RENDER_INTERSECT_H

#include <algorithm>

#include <vsg/maths/vec3.h>
#include <vsg/utils/LineSegmentIntersector.h>

#include <components/animation/transform.hpp>
#include <components/pipeline/mode.hpp>
#include <components/vsgutil/intersectnormal.hpp>

namespace Render
{
    /// The nearest hit of a line segment intersector. What mTransform stands for (a game object, an editor instance)
    /// is for the caller to look up.
    struct NearestIntersection
    {
        bool mHit = false;
        vsg::vec3 mPointWorld;
        vsg::vec3 mNormalWorld;
        float mRatio = 0;
        /// The first Anim::Transform on the path from the root to the hit, or null. Owned by the scene graph.
        const Anim::Transform* mTransform = nullptr;
    };

    /// Sorts the intersector's results by distance and describes the nearest one.
    inline NearestIntersection nearestIntersection(vsg::LineSegmentIntersector& intersector)
    {
        auto& intersections = intersector.intersections;
        if (intersections.empty())
            return {};
        std::sort(
            intersections.begin(), intersections.end(),
            [](auto& lhs, auto& rhs) -> auto{ return lhs->ratio < rhs->ratio; });

        const auto& intersection = intersections.front();
        vsg::vec3 nWorld = vsgUtil::worldNormal(*intersection, static_cast<int>(Pipeline::Mode::NORMAL));
        /*
        auto dir = vsg::normalize(eye - vsg::dvec3(result.mHitPointWorld));
        if (vsg::length2(nWorld) > 0 && vsg::dot(dir, vsg::dvec3(nWorld)) < 0)
            continue;
            */

        NearestIntersection result;
        result.mHit = true;
        result.mNormalWorld = nWorld;
        result.mPointWorld = vsg::vec3(intersection->worldIntersection);
        result.mRatio = static_cast<float>(intersection->ratio);
        for (auto node : intersection->nodePath)
        {
            if (auto trans = dynamic_cast<const Anim::Transform*>(node))
            {
                result.mTransform = trans;
                break;
            }
        }
        return result;
    }
}

#endif
