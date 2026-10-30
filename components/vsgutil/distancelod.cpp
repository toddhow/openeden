#include "distancelod.hpp"

#include <vsg/app/RecordTraversal.h>
#include <vsg/vk/State.h>

namespace vsgUtil
{
    void DistanceLOD::accept(vsg::RecordTraversal& visitor) const
    {
        const vsg::dmat4& modelView = visitor.getState()->modelviewMatrixStack.top();
        // A point (w = 1); the modelview has no projective part, so w stays 1.
        const vsg::dvec4 eye = modelView * vsg::dvec4(center.x, center.y, center.z, 1.0);
        // In the node's own units, as osg::CullVisitor::getDistanceToViewPoint: divided by the (uniform) scale above it.
        const double scale = vsg::length(vsg::dvec3(modelView[0][0], modelView[0][1], modelView[0][2]));
        const double distance = vsg::length(vsg::dvec3(eye.x, eye.y, eye.z)) / (scale > 0.0 ? scale : 1.0);
        for (std::size_t i = 0; i < children.size(); ++i)
            if (isVisible(i, distance))
                children[i]->accept(visitor);
    }
}
