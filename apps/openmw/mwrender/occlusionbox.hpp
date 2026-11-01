#ifndef OPENEDEN_MWRENDER_OCCLUSIONBOX_H
#define OPENEDEN_MWRENDER_OCCLUSIONBOX_H

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

namespace MWRender
{
    /// The box above and below the camera that weather particle occlusion renders from above: OpenMW's
    /// PrecipitationOccluder::update and updateRange (precipitationocclusion.cpp at 442eec6e2f), with no renderer.
    struct OcclusionBox
    {
        vsg::vec2 centre; // world XY of the box's centre
        vsg::vec2 size; // world XY size of the box
        float zMax = 0.f; // world Z of the top, where the depth is 1
        float zRange = 0.f; // zMax minus the world Z of the bottom, where the depth is 0
    };

    /// The box for a camera position and a particle wrap range. XY grows by a margin of 50 units past the range; Z
    /// reaches the range plus one cell above and below the camera.
    OcclusionBox makeOcclusionBox(const vsg::vec3& cameraPosition, const vsg::vec3& range);

    /// A world position in the box's depth map: x and y in [0, 1] across the box (0 at the box's low world X and Y),
    /// z the reversed depth, 1 at the top and 0 at the bottom. files/shaders/lib/particle/weatherocclusion.glsl does
    /// the same.
    vsg::vec3 occlusionCoord(const OcclusionBox& box, const vsg::vec3& worldPosition);

    /// True if a particle at this reversed depth is under the surface the depth map holds. Equal depths are not
    /// occluded.
    inline bool isOccluded(float particleDepth, float sceneDepth)
    {
        return particleDepth < sceneDepth;
    }
}

#endif
