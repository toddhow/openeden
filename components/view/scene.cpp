#include "scene.hpp"

#include <vsg/maths/transform.h>

#include <components/pipeline/viewbindings.hpp>

namespace View
{
    Scene::Scene()
        : DynamicDescriptorValue<Pipeline::Data::Scene>(Pipeline::Descriptors::VIEW_SCENE_BINDING)
    {
        auto& v = value();
        v.time = 0;
        v.fogStart = v.fogScale = 0;
    }

    void Scene::setLightPosition(const vsg::vec3& pos, const vsg::mat4& invView)
    {
        // openeden-vsg: VSG 1.1.x's vec3 * mat4 treats the vector as a point (w = 1) and divides by the projective term,
        // which shrank the sun direction by the camera's distance from the origin. A vec4 with w = 0 is a direction.
        auto viewDir = vsg::vec4(vsg::normalize(pos), 0.f) * invView;
        value().lightViewPos = vsg::vec4(viewDir.x, viewDir.y, viewDir.z, 0.f);
        dirty();
    }
}
