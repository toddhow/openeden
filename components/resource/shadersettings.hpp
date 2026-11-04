#ifndef VSGOPENMW_RESOURCE_SHADERSETTINGS_H
#define VSGOPENMW_RESOURCE_SHADERSETTINGS_H

#include <cstdint>
#include <span>
#include <string>
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

    /// Task 0072 (review of stages b and c): every name a settings file can write as a "#define <name> 1" line
    /// (shadow.settings, terrain.settings). A Slang twin's define set may contain them besides the imported names.
    std::span<const std::string_view> settingsDefineNames();

    /// Task 0072 (review of stages b and c): how a float setting is written into the GLSL settings files: a literal
    /// that parses back to exactly `value`, so that the GLSL and the Slang twin (which gets the float itself as a
    /// specialization constant) compute with the same number. Finite values only.
    std::string shaderFloatLiteral(float value);

    class ShaderSettings : public vsgUtil::VirtualFiles
    {
    public:
        ShaderSettings();

        // The values of every constant of settingsConstantTable(), for the Slang twins (put on the shader options).
        vsg::ref_ptr<vsgUtil::SettingsConstants> constants;
    };
}

#endif
