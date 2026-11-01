#ifndef VSGOPENMW_MWRENDER_INTERSECT_H
#define VSGOPENMW_MWRENDER_INTERSECT_H

#include "mask.hpp"
#include "rendermanager.hpp"
#include "scene.hpp"

#include <vsg/core/visit.h>

#include <components/render/intersect.hpp>
#include <components/vsgutil/cameradistanceintersector.hpp>

/*
 * Intersects game objects.
 */
namespace MWRender
{
    RenderManager::RayResult getIntersectionResult(vsg::LineSegmentIntersector& intersector, Scene& scene)
    {
        const Render::NearestIntersection nearest = Render::nearestIntersection(intersector);
        if (!nearest.mHit)
            return {};

        RenderManager::RayResult result;
        result.mHit = true;
        result.mHitNormalWorld = nearest.mNormalWorld;
        result.mHitPointWorld = nearest.mPointWorld;
        result.mRatio = nearest.mRatio;
        if (nearest.mTransform)
            result.mHitObject = scene.getPtr(nearest.mTransform);
        return result;
    }

    void configure(vsg::Intersector& visitor, bool ignorePlayer, bool ignoreActors)
    {
        visitor.traversalMask = Mask_Terrain | Mask_Object;
        if (!ignoreActors)
            visitor.traversalMask |= Mask_Actor;
        if (!ignorePlayer)
            visitor.traversalMask |= Mask_Player;
    }

    RenderManager::RayResult castRay(const vsg::dvec3& origin, const vsg::dvec3& dest, bool ignorePlayer,
        bool ignoreActors, Scene& scene, vsg::ref_ptr<vsg::Node> node)
    {
        vsg::LineSegmentIntersector intersector(origin, dest);
        configure(intersector, ignorePlayer, ignoreActors);
        return getIntersectionResult(vsg::visit(intersector, node), scene);
    }

    RenderManager::RayResult castCameraToViewportRay(float nX, float nY, float maxDistance, bool ignorePlayer,
        bool ignoreActors, const vsg::Camera& camera, Scene& scene, vsg::ref_ptr<vsg::Node> node)
    {
        vsgUtil::CameraDistanceIntersector intersector(camera, nX, nY, static_cast<double>(maxDistance));
        configure(intersector, ignorePlayer, ignoreActors);
        return getIntersectionResult(vsg::visit(intersector, node), scene);
    }
}

#endif
