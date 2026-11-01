#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_RENDERING_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_RENDERING_H

#include <string>

#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

namespace Settings
{
    // OpenEden (task 0023): groundwork for later rendering features. Everything is off by default, and off means the
    // renderer is exactly as it was.
    struct RenderingCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<bool> mGpuTiming{ mIndex, "Rendering", "gpu timing" };
        SettingValue<bool> mSlangShaders{ mIndex, "Rendering", "slang shaders" };
        SettingValue<bool> mDeviceReport{ mIndex, "Rendering", "device report" };

        // Task 0025 (F2): the linear HDR path. Read at start.
        SettingValue<bool> mHdr{ mIndex, "Rendering", "hdr" };
        SettingValue<std::string> mHdrTonemap{ mIndex, "Rendering", "hdr tonemap" };
        SettingValue<float> mHdrExposure{ mIndex, "Rendering", "hdr exposure", makeClampSanitizerFloat(-8, 8) };
        SettingValue<bool> mHdrBloom{ mIndex, "Rendering", "hdr bloom" };
        SettingValue<float> mHdrBloomStrength{ mIndex, "Rendering", "hdr bloom strength",
            makeClampSanitizerFloat(0, 1) };

        // Task 0026 (F3): stored normals, depth and velocity. Read at start; needs `hdr`.
        SettingValue<bool> mGBuffer{ mIndex, "Rendering", "gbuffer" };
        SettingValue<std::string> mGBufferDebugView{ mIndex, "Rendering", "gbuffer debug view" };
    };
}

#endif
