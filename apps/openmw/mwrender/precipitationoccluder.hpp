#ifndef OPENEDEN_MWRENDER_PRECIPITATIONOCCLUDER_H
#define OPENEDEN_MWRENDER_PRECIPITATIONOCCLUDER_H

#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/state/Descriptor.h>

#include "occlusionbox.hpp"

namespace vsg
{
    class Context;
    class LookAt;
    class Orthographic;
}

namespace MWRender
{
    /**
     * @brief Renders the scene's depth from above around the camera, so weather particles under a roof can be discarded
     * ([Shaders] weather particle occlusion, task 0012 V4l): OpenMW's PrecipitationOccluder at 442eec6e2f.
     *
     * Owned by RenderManager, which creates it only with the setting on. update() runs on the main thread. Without
     * children the pass only clears, to the far depth, which occludes nothing.
     */
    class PrecipitationOccluder
    {
    public:
        explicit PrecipitationOccluder(vsg::Context& context);
        ~PrecipitationOccluder();

        vsg::ref_ptr<vsg::RenderGraph> renderGraph() const { return mRenderGraph; }
        vsg::ref_ptr<vsg::View> view() const { return mView; }
        vsg::ref_ptr<vsg::Descriptor> mapDescriptor() const { return mMap; }

        /// The box's XY size (rain and snow both wrap in a square) and height, which the particle shader reads from the
        /// scene data (lib/particle/weatherocclusion.glsl).
        float boxSize() const { return mBoxSize; }
        float boxHeight() const { return mBoxHeight; }

        /// Moves the box to the camera, for particles wrapped in this range.
        void update(const vsg::vec3& cameraPosition, const vsg::vec3& range);

    private:
        vsg::ref_ptr<vsg::RenderGraph> mRenderGraph;
        vsg::ref_ptr<vsg::View> mView;
        vsg::ref_ptr<vsg::Orthographic> mOrtho;
        vsg::ref_ptr<vsg::LookAt> mLookAt;
        vsg::ref_ptr<vsg::Descriptor> mMap;
        float mBoxSize = 1.f;
        float mBoxHeight = 1.f;
    };
}

#endif
