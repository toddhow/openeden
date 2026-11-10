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
        // Task 0090: the ranges below are cut silently to what the code was tried with (0.01 to 1,000,000 pixels).
        SettingValue<float> mTerrainTileThreshold{ mIndex, "WorldLod", "terrain tile threshold",
            makeClampSanitizerFloat(0.01f, 1000000.f) };
        // Task 0087: seconds a terrain tile takes to dither in or out; 0 is the instant swap of before.
        SettingValue<float> mTerrainFadeTime{ mIndex, "WorldLod", "terrain fade time",
            makeClampSanitizerFloat(0, 60.f) };
        // Task 0059: baked object clusters (DRAFT layout), off by default; they need `enabled` too.
        SettingValue<bool> mObjectClusters{ mIndex, "WorldLod", "object clusters" };
        // Task 0096 stage b (layout version 2 of the cluster pack): bake a constant texture scroll of lava planes,
        // waterfalls and the Ghostgate fence into the clusters (`openmw --bake-lod-objects`) and play it on the far
        // draws. Off by default; a pack baked with it on holds version 2 payloads, which this setting off draws still.
        SettingValue<bool> mObjectClusterEffects{ mIndex, "WorldLod", "object cluster effects" };
        // Task 0096 stage b: let the bake take activators that have no name and no script (lava planes, waterfalls,
        // fences) into the clusters as statics are. Off by default; read only by the bake.
        SettingValue<bool> mObjectClusterScenery{ mIndex, "WorldLod", "object cluster scenery" };
        // Task 0096 stage c: far foliage sways in the wind. The bake marks the models whose path holds a word of
        // `object cluster sway models` (comma separated) and the renderer moves them in the vertex shader. Off by
        // default; both are read by the bake, the renderer plays any sway group of a pack only when `sway` is true.
        SettingValue<bool> mObjectClusterSway{ mIndex, "WorldLod", "object cluster sway" };
        SettingValue<std::string> mObjectClusterSwayModels{ mIndex, "WorldLod", "object cluster sway models" };
        // The top's lean at full wind as a share of the object's height (0 to 0.1), and a wind strength used when the
        // weather gives none; `sway wind` (0 to 4) multiplies the weather's wind, and the result is at most 1.
        // The displacement grows with the distance, 1 to 6 times (`Lod::swayDistanceBoost`), so far trees visibly
        // move; not physical. Off by default; read by the renderer only.
        SettingValue<bool> mObjectClusterSwayBoost{ mIndex, "WorldLod", "object cluster sway distance boost" };
        SettingValue<float> mObjectClusterSwayLean{ mIndex, "WorldLod", "object cluster sway lean",
            makeClampSanitizerFloat(0.f, 0.1f) };
        SettingValue<float> mObjectClusterSwayWind{ mIndex, "WorldLod", "object cluster sway wind",
            makeClampSanitizerFloat(0.f, 4.f) };
        SettingValue<float> mObjectClusterThreshold{ mIndex, "WorldLod", "object cluster threshold",
            makeClampSanitizerFloat(0.01f, 1000000.f) };
        SettingValue<float> mObjectClusterFadeTime{ mIndex, "WorldLod", "object cluster fade time",
            makeClampSanitizerFloat(0, 60.f) };
        // The viewing distance used while the world LOD is on (distant terrain on too): the larger of this and
        // [Camera] viewing distance, so far land is drawn out to the horizon without editing [Camera]. A number of
        // units or `auto` (task 0066: with `map view` on, the reach of the loaded world's land); it is text so that
        // both fit, parsed by Lod::parseViewDistanceSetting.
        SettingValue<std::string> mViewDistance{ mIndex, "WorldLod", "view distance" };
        // Task 0066: whole-map view. Off by default: with it off the tile range, the far plane, the fog end and the
        // paging range are one number as before.
        SettingValue<bool> mMapView{ mIndex, "WorldLod", "map view" };
        // The most `view distance = auto` may reach, in units (a hardware budget).
        SettingValue<float> mMaxViewDistance{ mIndex, "WorldLod", "max view distance",
            makeClampSanitizerFloat(1.f, 10000000.f) };
        // Task 0066 stage c: the aerial haze of the whole-map view (with `map view` on): the light from far land fades
        // into the air's colour. `haze` turns it on; the distance, scale height and weather gain shape it
        // (aerialmath.hpp).
        SettingValue<bool> mHaze{ mIndex, "WorldLod", "haze" };
        SettingValue<float> mHazeVisibility{ mIndex, "WorldLod", "haze visibility",
            makeClampSanitizerFloat(1.f, 10000000.f) };
        SettingValue<float> mHazeHeight{ mIndex, "WorldLod", "haze height", makeClampSanitizerFloat(1.f, 1000000.f) };
        SettingValue<float> mHazeWeatherGain{ mIndex, "WorldLod", "haze weather gain",
            makeClampSanitizerFloat(0, 100.f) };
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
