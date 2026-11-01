#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_RENDERING_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_RENDERING_H

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
    };
}

#endif
