#ifndef VSGOPENMW_PIPELINE_MATERIAL_H
#define VSGOPENMW_PIPELINE_MATERIAL_H

#include <vsg/maths/vec4.h>

#include "bindings.hpp"
#include "descriptorvalue.hpp"

namespace Pipeline
{
    namespace Data
    {
        using vsg::vec4;
#include <files/shaders/lib/material/data.glsl>
    }

    /*
     * Provides convenience functions.
     */
    class Material : public DynamicDescriptorValue<Data::Material>
    {
    public:
        Material()
            : DynamicDescriptorValue(Descriptors::MATERIAL_BINDING)
        {
            value() = createDefault();
        }
        void setAlpha(float a) { update(value().diffuse.a, a); }
        // OpenEden (task 0034): the colour part of the diffuse colour (the cloud shader multiplies by it).
        void setTint(const vsg::vec3& tint)
        {
            update(value().diffuse.r, tint.x);
            update(value().diffuse.g, tint.y);
            update(value().diffuse.b, tint.z);
        }
        // OpenEden (task 0038): the cloud shader's share of the lit look (the specular alpha, which it does not use).
        void setCloudShare(float share) { update(value().specular.a, share); }
        // OpenEden (task 0042): makes the cloud shader draw the volumetric clouds' picture instead of the cloud
        // texture's. The flag is the ambient alpha (which the cloud shader does not use), 1 by default, so a material
        // that never calls this stays vanilla; the shader tests for more than 1.5.
        // `horizon` (volumetric clouds horizon) makes it 3: the dome's vertex fade is not applied.
        void setVolumeClouds(bool on, bool horizon = false)
        {
            update(value().ambient.a, on ? (horizon ? 3.f : 2.f) : 1.f);
        }
        static Data::Material createDefault()
        {
            return { vsg::vec4(1, 1, 1, 1), vsg::vec4(1, 1, 1, 1), vsg::vec4(0, 0, 0, 1), vsg::vec4(0, 0, 0, 0), 0.f,
                1.f, 0.f, 0.f, vsg::vec4(-1, 0, -1, 1) };
        }
        static vsg::ref_ptr<value_type> createDefaultValue() { return value_type::create(createDefault()); }

        static bool isDefault(const Data::Material& mat)
        {
            static const auto defaultMat = createDefault();
            return std::memcmp(&mat, &defaultMat, sizeof(Data::Material)) == 0;
        }
    };
}

#endif
