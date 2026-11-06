// The Slang twin of lib/view/volume.glsl (task 0072): the reader of the volumetric fog. Include after
// lib/view/scene.slang, lib/view/linear.slang and lib/view/fogsettings.slang. The meaning of every value is in the
// GLSL; change both together (and files/shaders/comp/volume/volume.slang, components/render/volumemath.hpp).
//
// One difference: GLSL reads gl_FragCoord inside applyVolumeFog; Slang has no such global in a function, so the
// caller passes the fragment's window position.
#ifndef LIB_VIEW_VOLUME_SLANG
#define LIB_VIEW_VOLUME_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"

import lib.view.fogsettings;
import lib.view.linear;
import lib.view.scene;

[[vk::binding(VIEW_VOLUME_BINDING, VIEW_SET)]] Sampler3D volumeFog;

struct VolumeParams
{
    float4 grid;
    float4 range;
    float4 fog;
    float4 sun;
    float4 lamps;
    float4 jitter;
    float4x4 toPreviousClip;
};
[[vk::binding(VIEW_VOLUME_PARAMS_BINDING, VIEW_SET)]] ConstantBuffer<VolumeParams> volumeParams;

bool volumeActive()
{
    return cVolumetrics && volumeParams.grid.w > 0.5 && linearView();
}

float volumeDensity()
{
    if (scene.fogScale <= 0.0)
        return 0.0;
    float fogEnd = scene.fogStart + 1.0 / scene.fogScale;
    return volumeParams.fog.x * 2.0 / max(fogEnd - scene.fogStart * 0.5, 1.0);
}

float3 applyVolumeFog(float3 src, float3 viewPos, float2 fragCoord)
{
    float depth = max(-viewPos.z, 1e-3);
    float2 uv = fragCoord / scene.resolution;
    float slices = volumeParams.grid.z;
    float volumeFar = volumeParams.range.y;
    float coordinate = clamp(log(depth / volumeParams.range.x) / volumeParams.range.z, 0.0, 1.0) * slices;
    float4 v = volumeFog.SampleLevel(float3(uv, (max(coordinate, 1.0) - 0.5) / slices), 0.0);
    v = lerp(float4(0.0, 0.0, 0.0, 1.0), v, min(coordinate, 1.0));
    float transmittance = v.a;
    float scattering = 1.0 - transmittance;
    float3 tint = scattering > 1e-4 ? v.rgb / scattering : scene.fogColor.xyz;
    if (cVolumetricsDebug)
        return tint;
    transmittance *= exp(-volumeDensity() * max(depth - volumeFar, 0.0));
    return toLinear(lerp(toEncoded(src), toEncoded(tint), 1.0 - transmittance));
}

#endif
