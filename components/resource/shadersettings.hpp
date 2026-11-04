#ifndef VSGOPENMW_RESOURCE_SHADERSETTINGS_H
#define VSGOPENMW_RESOURCE_SHADERSETTINGS_H

#include <cstdint>
#include <span>
#include <string_view>

#include <components/vsgutil/virtualfile.hpp>

namespace vsgUtil
{
    class SettingsConstants;
}

namespace Resource
{
    // OpenEden (task 0072, stage b): a value of a `*.settings` include that the Slang twins of the GLSL shaders take
    // as a specialization constant. The GLSL gets "const <type> <name> = <value>;" written into its source; a twin is
    // compiled at build time, so it declares "[vk::constant_id(<id>)] const <type> <name> = <default>;" and the
    // pipeline code gives the value (vsgUtil::addSettingsConstants). The ids are fixed: change a `.slang` file and this
    // table together. Type: 'b' bool (VkBool32), 'i' int, 'f' float.
    struct SettingsConstantInfo
    {
        std::string_view mName;
        uint32_t mId;
        char mType;
    };

    // Far above every constant_id the GLSL uses (0 to START_UVSET_CONSTANTS + 17, lib/material/bindings.glsl).
    inline constexpr uint32_t settingsConstantBase = 1000;

    std::span<const SettingsConstantInfo> settingsConstantTable();

    class ShaderSettings : public vsgUtil::VirtualFiles
    {
    public:
        ShaderSettings();

        // The values of every constant of settingsConstantTable(), for the Slang twins (put on the shader options).
        vsg::ref_ptr<vsgUtil::SettingsConstants> constants;
    };
}

#endif
