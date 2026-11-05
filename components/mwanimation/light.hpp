#ifndef VSGOPENMW_MWANIMATION_LIGHT_H
#define VSGOPENMW_MWANIMATION_LIGHT_H

#include <vsg/nodes/Group.h>

#include <components/animation/controllers.hpp>
#include <components/animation/transform.hpp>

namespace ESM
{
    struct Light;
}
namespace MWAnim
{
    vsg::Node* addLight(
        vsg::Group* attachLight, vsg::Group* fallback, const ESM::Light& light, Anim::Controllers& controllers);

    /// Task 0077: makes `carrier`, the transform above the model that carries `light` (the node addLight returned),
    /// leave out the model from that light's own point light shadow pass. Does nothing for a null argument.
    void markPointShadowCarrier(Anim::Transform* carrier, vsg::Node* light);
}

#endif
