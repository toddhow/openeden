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
        // Task 0054 (S0): extra columns in the frame stats file (memory, loaded cells, scene census).
        SettingValue<bool> mFrameCounters{ mIndex, "Rendering", "frame counters" };

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

        // Task 0045: parallax occlusion mapping from the True PBR height files (`_p`). Read at start.
        SettingValue<bool> mParallax{ mIndex, "Rendering", "parallax" };
        SettingValue<std::string> mParallaxQuality{ mIndex, "Rendering", "parallax quality" };
        SettingValue<float> mParallaxScale{ mIndex, "Rendering", "parallax scale", makeClampSanitizerFloat(0, 4) };
        SettingValue<bool> mParallaxDiscardEdges{ mIndex, "Rendering", "parallax discard edges" };

        // Task 0049: the runtime texture downscaler. Sizes are texels on the longest side, 0 = no cap; read at start.
        SettingValue<int> mTextureMaxSize{ mIndex, "Rendering", "texture max size", makeClampSanitizerInt(0, 8192) };
        SettingValue<int> mTextureMaxSizeTerrain{ mIndex, "Rendering", "texture max size terrain",
            makeClampSanitizerInt(0, 8192) };
        SettingValue<int> mTextureMaxSizeObjects{ mIndex, "Rendering", "texture max size objects",
            makeClampSanitizerInt(0, 8192) };
        SettingValue<std::string> mTextureMemoryBudget{ mIndex, "Rendering", "texture memory budget" };
        SettingValue<bool> mTextureDownscaleReport{ mIndex, "Rendering", "texture downscale report" };

        // Task 0029 (R2): ambient occlusion from the stored depth and normals. Read at start; needs hdr, gbuffer and
        // pbr.
        SettingValue<bool> mAo{ mIndex, "Rendering", "ao" };
        SettingValue<float> mAoRadius{ mIndex, "Rendering", "ao radius", makeClampSanitizerFloat(8, 1000) };
        SettingValue<float> mAoStrength{ mIndex, "Rendering", "ao strength", makeClampSanitizerFloat(0, 4) };
        SettingValue<int> mAoSlices{ mIndex, "Rendering", "ao slices", makeClampSanitizerInt(1, 8) };
        SettingValue<int> mAoSteps{ mIndex, "Rendering", "ao steps", makeClampSanitizerInt(1, 16) };
        SettingValue<bool> mAoTerrain{ mIndex, "Rendering", "ao terrain" };
        // Task 0076: temporal accumulation of the occlusion. Read at start; needs ao.
        SettingValue<bool> mAoTemporal{ mIndex, "Rendering", "ao temporal" };
        SettingValue<float> mAoTemporalHistory{ mIndex, "Rendering", "ao temporal history",
            makeClampSanitizerFloat(0.5f, 0.98f) };
        SettingValue<float> mAoTemporalClamp{ mIndex, "Rendering", "ao temporal clamp", makeClampSanitizerFloat(0, 4) };
        SettingValue<std::string> mAoTemporalDebug{ mIndex, "Rendering", "ao temporal debug" };

        // Task 0033 (R7): screen-space subsurface scattering on skin. Read at start; needs hdr, gbuffer and pbr. The
        // width is in game units.
        SettingValue<bool> mSss{ mIndex, "Rendering", "sss" };
        SettingValue<float> mSssStrength{ mIndex, "Rendering", "sss strength", makeClampSanitizerFloat(0, 1) };
        SettingValue<float> mSssWidth{ mIndex, "Rendering", "sss width", makeClampSanitizerFloat(0.1f, 10) };
        // Task 0075: temporal anti-aliasing. Read at start; needs hdr, gbuffer and a single-sampled scene. The history
        // weight is the share of the previous frame in the resolved colour; clipping is "variance" or "minmax"; debug
        // is "none", "weight", "reject" or "velocity".
        SettingValue<bool> mTaa{ mIndex, "Rendering", "taa" };
        SettingValue<float> mTaaSharpness{ mIndex, "Rendering", "taa sharpness", makeClampSanitizerFloat(0, 1) };
        SettingValue<float> mTaaJitterScale{ mIndex, "Rendering", "taa jitter scale", makeClampSanitizerFloat(0, 1) };
        SettingValue<float> mTaaHistory{ mIndex, "Rendering", "taa history", makeClampSanitizerFloat(0.5f, 0.98f) };
        SettingValue<std::string> mTaaClipping{ mIndex, "Rendering", "taa clipping" };
        SettingValue<float> mTaaClipGamma{ mIndex, "Rendering", "taa clip gamma", makeClampSanitizerFloat(0.5f, 4) };
        SettingValue<std::string> mTaaDebug{ mIndex, "Rendering", "taa debug" };
        SettingValue<bool> mAtmosphere{ mIndex, "Rendering", "atmosphere" };
        SettingValue<std::string> mAtmosphereLook{ mIndex, "Rendering", "atmosphere look" };
        // Decision 0010 answer 4: the atmosphere's sun height follows the hour of the colour tables, not the drawn sun.
        SettingValue<bool> mSunFollowsColourTables{ mIndex, "Rendering", "sun follows colour tables" };
        // Task 0047: the sun as a computed disc (read at start; needs hdr and the Slang shader built): its size, its
        // HDR intensity and the strength of its glow.
        SettingValue<bool> mProceduralSun{ mIndex, "Rendering", "procedural sun" };
        SettingValue<float> mProceduralSunSize{ mIndex, "Rendering", "procedural sun size",
            makeClampSanitizerFloat(0.25f, 2.5f) };
        SettingValue<float> mProceduralSunIntensity{ mIndex, "Rendering", "procedural sun intensity",
            makeClampSanitizerFloat(1, 100) };
        SettingValue<float> mProceduralSunGlow{ mIndex, "Rendering", "procedural sun glow",
            makeClampSanitizerFloat(0, 4) };

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
        // Reflection probes: the most to place, rays averaged per texel, and the map's width in texels (8, 16, 32 or
        // 64; other values are rounded down to one of these).
        SettingValue<int> mGiBakeReflectionProbes{ mIndex, "Rendering", "gi bake reflection probes",
            makeClampSanitizerInt(0, 16) };
        SettingValue<int> mGiBakeReflectionSamples{ mIndex, "Rendering", "gi bake reflection samples",
            makeClampSanitizerInt(1, 64) };
        SettingValue<int> mGiBakeReflectionResolution{ mIndex, "Rendering", "gi bake reflection resolution",
            makeClampSanitizerInt(8, 64) };
        SettingValue<int> mGiBakeBudget{ mIndex, "Rendering", "gi bake budget ms", makeClampSanitizerInt(1, 200) };
        // Task 0036: ambient light for exteriors from a baked field of how much sky each place sees, lit by the live
        // sky (read at start; needs hdr, pbr and atmosphere), its strength, and the baker (openmw --bake-gi-exterior):
        // rays per probe and the distance between columns in game units (rounded to a size that divides a cell).
        SettingValue<bool> mGiExterior{ mIndex, "Rendering", "gi exterior" };
        SettingValue<float> mGiExteriorStrength{ mIndex, "Rendering", "gi exterior strength",
            makeClampSanitizerFloat(0, 2) };
        // Task 0037: rain wetness (read at start; needs hdr and pbr): its strength, whether flat ground collects
        // puddles, the seconds of game time a soaked surface takes to dry, and a debug view of wetness and cover.
        SettingValue<bool> mWetness{ mIndex, "Rendering", "wetness" };
        SettingValue<float> mWetnessStrength{ mIndex, "Rendering", "wetness strength", makeClampSanitizerFloat(0, 1) };
        SettingValue<bool> mWetnessPuddles{ mIndex, "Rendering", "wetness puddles" };
        SettingValue<float> mWetnessDryTime{ mIndex, "Rendering", "wetness dry time",
            makeClampSanitizerFloat(1, 86400) };
        SettingValue<bool> mWetnessDebug{ mIndex, "Rendering", "wetness debug" };
        // Task 0039: snow and ash cover (read at start; needs hdr and pbr): its strength, the seconds of game time it
        // takes to build up while it snows or ash falls and to melt after, and a debug view of cover and upwardness.
        SettingValue<bool> mCover{ mIndex, "Rendering", "cover" };
        SettingValue<float> mCoverStrength{ mIndex, "Rendering", "cover strength", makeClampSanitizerFloat(0, 1) };
        SettingValue<float> mCoverBuildTime{ mIndex, "Rendering", "cover build time",
            makeClampSanitizerFloat(1, 86400) };
        SettingValue<float> mCoverMeltTime{ mIndex, "Rendering", "cover melt time",
            makeClampSanitizerFloat(1, 864000) };
        SettingValue<bool> mCoverDebug{ mIndex, "Rendering", "cover debug" };
        // Task 0038: lit clouds (read at start; needs hdr), their look (`lit` or `flat`), and cloud shadows on the
        // ground (read at start; needs hdr and atmosphere), their strength and a debug view of the shadow mask.
        SettingValue<bool> mClouds{ mIndex, "Rendering", "clouds" };
        SettingValue<std::string> mCloudsLook{ mIndex, "Rendering", "clouds look" };
        SettingValue<bool> mCloudShadows{ mIndex, "Rendering", "cloud shadows" };
        SettingValue<float> mCloudShadowStrength{ mIndex, "Rendering", "cloud shadow strength",
            makeClampSanitizerFloat(0, 1) };
        SettingValue<bool> mCloudShadowDebug{ mIndex, "Rendering", "cloud shadow debug" };
        // Task 0040: lit rain and snow (read at start; needs hdr), lightning as light (needs hdr), its strength (the
        // brightness of a flash can never pass the cap in components/render/lightningmath.hpp) and a test aid that
        // holds one flash at a given age in seconds (-1: off), and the sun's glow in haze (needs hdr and atmosphere),
        // its strength and a debug view.
        SettingValue<bool> mPrecipitationLighting{ mIndex, "Rendering", "precipitation lighting" };
        SettingValue<bool> mLightning{ mIndex, "Rendering", "lightning" };
        SettingValue<float> mLightningStrength{ mIndex, "Rendering", "lightning strength",
            makeClampSanitizerFloat(0, 2) };
        SettingValue<float> mLightningDebugAge{ mIndex, "Rendering", "lightning debug age",
            makeClampSanitizerFloat(-1, 10) };
        SettingValue<bool> mSunGlow{ mIndex, "Rendering", "sun glow" };
        SettingValue<float> mSunGlowStrength{ mIndex, "Rendering", "sun glow strength", makeClampSanitizerFloat(0, 2) };
        SettingValue<bool> mSunGlowDebug{ mIndex, "Rendering", "sun glow debug" };
        // OpenEden (task 0041): the volumetric fog.
        SettingValue<bool> mVolumetrics{ mIndex, "Rendering", "volumetrics" };
        SettingValue<std::string> mVolumetricsQuality{ mIndex, "Rendering", "volumetrics quality" };
        SettingValue<float> mVolumetricsDensity{ mIndex, "Rendering", "volumetrics density",
            makeClampSanitizerFloat(0, 8) };
        SettingValue<float> mVolumetricsAnisotropy{ mIndex, "Rendering", "volumetrics anisotropy",
            makeClampSanitizerFloat(-0.95f, 0.95f) };
        SettingValue<bool> mVolumetricsTemporal{ mIndex, "Rendering", "volumetrics temporal" };
        SettingValue<bool> mVolumetricsDebug{ mIndex, "Rendering", "volumetrics debug" };
        // OpenEden (task 0042): ray-marched volumetric clouds (read at start; need hdr and atmosphere).
        SettingValue<bool> mVolumetricClouds{ mIndex, "Rendering", "volumetric clouds" };
        SettingValue<std::string> mVolumetricCloudsQuality{ mIndex, "Rendering", "volumetric clouds quality" };
        SettingValue<float> mVolumetricCloudsCoverageScale{ mIndex, "Rendering", "volumetric clouds coverage scale",
            makeClampSanitizerFloat(0, 2) };
        SettingValue<float> mVolumetricCloudsDensity{ mIndex, "Rendering", "volumetric clouds density",
            makeClampSanitizerFloat(0, 4) };
        SettingValue<bool> mVolumetricCloudsShadows{ mIndex, "Rendering", "volumetric clouds shadows" };
        SettingValue<std::string> mVolumetricCloudsDebug{ mIndex, "Rendering", "volumetric clouds debug" };
        SettingValue<int> mGiBakeSkyRays{ mIndex, "Rendering", "gi bake sky rays", makeClampSanitizerInt(1, 65536) };
        SettingValue<float> mGiBakeSkySpacing{ mIndex, "Rendering", "gi bake sky spacing",
            makeClampSanitizerFloat(128, 2048) };
    };
}

#endif
