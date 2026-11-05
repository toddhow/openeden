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
        // Task 0087: seconds a terrain tile takes to dither in or out; 0 is the instant swap of before.
        SettingValue<float> mTerrainFadeTime{ mIndex, "WorldLod", "terrain fade time", makeMaxSanitizerFloat(0) };
        // Task 0059: baked object clusters (DRAFT layout), off by default; they need `enabled` too.
        SettingValue<bool> mObjectClusters{ mIndex, "WorldLod", "object clusters" };
        SettingValue<float> mObjectClusterThreshold{ mIndex, "WorldLod", "object cluster threshold",
            makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mObjectClusterFadeTime{ mIndex, "WorldLod", "object cluster fade time",
            makeMaxSanitizerFloat(0) };
        // The viewing distance used while the world LOD is on (distant terrain on too): the larger of this and
        // [Camera] viewing distance, so far land is drawn out to the horizon without editing [Camera]. A number of
        // units or `auto` (task 0066: with `map view` on, the reach of the loaded world's land); it is text so that
        // both fit, parsed by Lod::parseViewDistanceSetting.
        SettingValue<std::string> mViewDistance{ mIndex, "WorldLod", "view distance" };
        // Task 0066: whole-map view. Off by default: with it off the tile range, the far plane, the fog end and the
        // paging range are one number as before.
        SettingValue<bool> mMapView{ mIndex, "WorldLod", "map view" };
        // The most `view distance = auto` may reach, in units (a hardware budget).
        SettingValue<float> mMaxViewDistance{ mIndex, "WorldLod", "max view distance", makeMaxStrictSanitizerFloat(0) };
        // Task 0066 stage c: the aerial haze of the whole-map view (with `map view` on): the light from far land fades
        // into the air's colour. `haze` turns it on; the distance, scale height and weather gain shape it
        // (aerialmath.hpp).
        SettingValue<bool> mHaze{ mIndex, "WorldLod", "haze" };
        SettingValue<float> mHazeVisibility{ mIndex, "WorldLod", "haze visibility", makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mHazeHeight{ mIndex, "WorldLod", "haze height", makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mHazeWeatherGain{ mIndex, "WorldLod", "haze weather gain", makeMaxSanitizerFloat(0) };
        // Task 0066 stage e: the hardware tier of the whole-map view (with `map view` on). `tier` is auto or one of
        // minimal, low, medium, high (to try a weaker machine); `far levels` is the highest tile level kept resident, 0
        // meaning the tier's; `far texture max size` caps the pictures of the far object clusters, 0 meaning the
        // tier's.
        SettingValue<std::string> mTier{ mIndex, "WorldLod", "tier" };
        SettingValue<int> mFarLevels{ mIndex, "WorldLod", "far levels", makeClampSanitizerInt(0, 6) };
        SettingValue<int> mFarTextureMaxSize{ mIndex, "WorldLod", "far texture max size",
            makeClampSanitizerInt(0, 8192) };
        // What the built far object clusters may hold in memory, in MiB of what Lod::clusterResidentBytes counts; 0
        // means the tier's. A number above 65536 counts as that.
        SettingValue<int> mFarMemoryBudget{ mIndex, "WorldLod", "far memory budget", makeClampSanitizerInt(0, 65536) };
        SettingValue<int> mTerrainTileLevel{ mIndex, "WorldLod", "terrain tile level", makeClampSanitizerInt(1, 1) };
    };
}

#endif
