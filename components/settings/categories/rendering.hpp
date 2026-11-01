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

        // Task 0027 (F4, R1): the material model and physically based lighting. Read at start; needs `hdr`.
        SettingValue<bool> mPbr{ mIndex, "Rendering", "pbr" };
        SettingValue<std::string> mPbrLook{ mIndex, "Rendering", "pbr look" };
        SettingValue<bool> mPbrMaterialMaps{ mIndex, "Rendering", "pbr material maps" };

        // Task 0028: ambient light from baked light probes. Read at start; needs hdr and pbr.
        SettingValue<bool> mGi{ mIndex, "Rendering", "gi" };
        SettingValue<float> mGiStrength{ mIndex, "Rendering", "gi strength", makeClampSanitizerFloat(0, 2) };
        // Stage d: scales the reflections of the baked reflection probes (0 turns them off), and draws the probes.
        SettingValue<float> mGiReflectionStrength{ mIndex, "Rendering", "gi reflection strength",
            makeClampSanitizerFloat(0, 2) };
        SettingValue<bool> mGiDebug{ mIndex, "Rendering", "gi debug" };
        // The baker (openmw --bake-gi): rays per probe, bounces, probe spacing in game units, random seed, and how many
        // milliseconds of each frame it may use.
        SettingValue<int> mGiBakeRays{ mIndex, "Rendering", "gi bake rays", makeClampSanitizerInt(1, 65536) };
        SettingValue<int> mGiBakeBounces{ mIndex, "Rendering", "gi bake bounces", makeClampSanitizerInt(0, 8) };
        SettingValue<float> mGiBakeSpacing{ mIndex, "Rendering", "gi bake spacing", makeClampSanitizerFloat(16, 4096) };
        SettingValue<int> mGiBakeSeed{ mIndex, "Rendering", "gi bake seed", makeClampSanitizerInt(0, 2147483647) };
        // Reflection probes: the most to place, rays averaged per texel, and the map's width in texels (8, 16, 32 or 64;
        // other values are rounded down to one of these).
        SettingValue<int> mGiBakeReflectionProbes{ mIndex, "Rendering", "gi bake reflection probes",
            makeClampSanitizerInt(0, 16) };
        SettingValue<int> mGiBakeReflectionSamples{ mIndex, "Rendering", "gi bake reflection samples",
            makeClampSanitizerInt(1, 64) };
        SettingValue<int> mGiBakeReflectionResolution{ mIndex, "Rendering", "gi bake reflection resolution",
            makeClampSanitizerInt(8, 64) };
        SettingValue<int> mGiBakeBudget{ mIndex, "Rendering", "gi bake budget ms", makeClampSanitizerInt(1, 200) };
    };
}

#endif
