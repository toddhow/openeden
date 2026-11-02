#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_WORLDLOD_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_WORLDLOD_H

#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace Settings
{
    // OpenEden world LOD (task 0055). All off by default: with `enabled = false` no code of the system runs.
    struct WorldLodCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<bool> mEnabled{ mIndex, "WorldLod", "enabled" };
        SettingValue<float> mTerrainTileThreshold{ mIndex, "WorldLod", "terrain tile threshold",
            makeMaxStrictSanitizerFloat(0) };
        // Task 0059: baked object clusters (DRAFT layout), off by default; they need `enabled` too.
        SettingValue<bool> mObjectClusters{ mIndex, "WorldLod", "object clusters" };
        SettingValue<float> mObjectClusterThreshold{ mIndex, "WorldLod", "object cluster threshold",
            makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mObjectClusterFadeTime{ mIndex, "WorldLod", "object cluster fade time",
            makeMaxSanitizerFloat(0) };
        SettingValue<int> mTerrainTileLevel{ mIndex, "WorldLod", "terrain tile level", makeClampSanitizerInt(1, 1) };
    };
}

#endif
