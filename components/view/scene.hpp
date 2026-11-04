#ifndef VSGOPENMW_VIEW_SCENE_H
#define VSGOPENMW_VIEW_SCENE_H

#include <components/pipeline/descriptorvalue.hpp>
#include <components/pipeline/viewdata.hpp>
#include <components/render/aerialmath.hpp>

namespace View
{
    /*
     * Conveniently manages scene data assigned to VIEW_SET.
     */
    struct Scene : public Pipeline::DynamicDescriptorValue<Pipeline::Data::Scene>
    {
        Scene();
        // `aerial` (task 0066, stage c) stores the scale with a negative sign, which tells the shaders to use the
        // aerial perspective instead of the linear fog (Render::Aerial::sceneFogRange); false is exactly as before.
        void setFogRange(float start, float end, bool aerial = false)
        {
            const auto [fogStart, fogScale] = Render::Aerial::sceneFogRange(start, end, aerial);
            value().fogStart = fogStart;
            value().fogScale = fogScale;
            dirty();
        }
        void setDepthRange(float near, float far)
        {
            value().zNear = near;
            value().zFar = far;
            dirty();
        }
        void setLightPosition(const vsg::vec3& pos, const vsg::mat4& invView);
    };
}

#endif
