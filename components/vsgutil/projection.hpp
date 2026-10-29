#ifndef VSGOPENMW_VSGUTIL_PROJECTION_H
#define VSGOPENMW_VSGUTIL_PROJECTION_H

#include <vsg/vk/State.h>
#include <vsg/app/ProjectionMatrix.h>

namespace vsgUtil
{
    /*
     * Overrides projection for subgraph.
     */
    class SetProjection : public vsg::Group
    {
    public:
        vsg::ref_ptr<vsg::ProjectionMatrix> projection;
        void accept(vsg::RecordTraversal& record) const
        {
            auto state = record.getState();
            auto prev_projection = state->projectionMatrixStack.top();
            auto modelviewMatrix = state->modelviewMatrixStack.top();
            state->setProjectionAndViewMatrix(projection->transform(), modelviewMatrix);
            traverse(record);
            state->setProjectionAndViewMatrix(prev_projection, modelviewMatrix);
        }
    };

    class FieldOfView : public vsg::ProjectionMatrix
    {
    public:
        vsg::ref_ptr<vsg::Perspective> perspective;
        float fieldOfViewY = 0;

        vsg::dmat4 transform() const override
        {
            if (perspective)
                return vsg::perspective(vsg::radians(static_cast<double>(fieldOfViewY)), perspective->aspectRatio, perspective->nearDistance, perspective->farDistance);
            return {};
        }
    };
}

#endif
