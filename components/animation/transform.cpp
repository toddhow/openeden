#include "transform.hpp"

#include <vsg/app/RecordTraversal.h>
#include <vsg/io/ObjectFactory.h>
#include <vsg/maths/transform.h>

#include "pointshadowcarrier.hpp"

namespace Anim
{
    static const vsg::RegisterWithObjectFactoryProxy<Transform> registerWithObjectFactory = {};

    void Transform::setAttitude(const vsg::quat& q)
    {
        setRotation(vsg::rotate(q));
    }

    void Transform::traverse(vsg::RecordTraversal& visitor) const
    {
        // Task 0077: the model that carries a point light does not cast in that light's own shadow pass.
        if (mPointShadowCarrier != 0
            && pointShadowCarrierSkipped(visitor.traversalMask, mPointShadowCarrier, pointShadowOwner))
            return;
        vsg::Transform::traverse(visitor);
    }

    void Transform::read(vsg::Input& input)
    {
        input.read("translation", translation);
        input.read("rotation", rotation);
        input.read("scale", scale);
        vsg::Transform::read(input);
    }

    void Transform::write(vsg::Output& output) const
    {
        output.write("translation", translation);
        output.write("rotation", rotation);
        output.write("scale", scale);
        vsg::Transform::write(output);
    }
}
