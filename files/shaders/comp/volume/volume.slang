// The volumetric fog's compute passes, in Slang (task 0041; decision 0009: new shaders are written in Slang).
//
// The fog is a grid of small boxes that follow the camera's view (froxels): x and y across the screen, z along the view
// axis in slices that are thin near the camera and thick far away (components/render/volumemath.hpp, sliceDepth).
//   fillMain      works out, for every froxel, the light the fog scatters towards the eye (rgb, per unit of
//                 scattering) and how dense the fog is (a), and writes them to the fill image.
//   integrateMain walks each view ray front to back through the fill image and writes, for each slice, the light
//                 scattered towards the eye up to the slice's far side (rgb) and the share of the picture behind it
//                 that still gets through (a). The forward shaders read that image (lib/view/volume.glsl).
//
// The twins of the maths are components/render/volumemath.hpp (sliceDepth, henyeyGreenstein, integrateSlice, fog
// extinction, jitter) and files/shaders/lib/view/volume.glsl (the reader). Change them together; a test reads the
// constants back from this file.
//
// Compiled with: -profile spirv_1_0 -matrix-layout-column-major, so matrices lie in memory as GLSL's mat4 does.

// lib/sets.glsl
static const int VIEW_SET = 0;
static const int COMPUTE_SET = 3;

// The scene uniform: one declaration for every shader (lib/view/data.slang, lib/view/scene.slang).
import lib.view.scene;

// components/render/volumegpu.hpp (std140)
struct VolumeParams
{
    float4 grid; // x, y, z: the froxel counts; w: 1 when the volume is on
    float4 range; // x: nearest slice depth, y: farthest, z: log(far / near)
    float4 fog; // x: density scale, y: 1 / height scale (0: none), z: unused, w: anisotropy
    float4 sun; // x: sun gain, y: shadow maximum distance, z: shadow fade start, w: shadow cascade count
    float4 lamps; // x: lamp gain, y: shadow bias, z: 1 for the debug view (the fog shows the sun's shadow)
    float4 jitter; // x, y, z: this frame's sample offsets (0 to 1), w: history weight (0: no history)
    float4x4 toPreviousClip;
};
// lib/view/bindings.glsl: VIEW_VOLUME_PARAMS_BINDING = 15
[[vk::binding(15, 0)]] ConstantBuffer<VolumeParams> volume;

// lib/view/bindings.glsl: VIEW_SHADOW_MAP_BINDING = 6, VIEW_SHADOW_SAMPLER_BINDING = 7 (the sun's shadow cascades)
[[vk::binding(6, 0)]] Texture2DArray<float> shadowMap;
[[vk::binding(7, 0)]] SamplerComparisonState shadowSampler;

// The point lights and the light grid, as lib/light/pointlights.glsl and lib/light/grid.glsl have them (VIEW_LIGHTS_BINDING
// = 2, VIEW_LIGHT_INDICES_BINDING = 3, VIEW_LIGHT_GRID_BINDING = 4, VIEW_LIGHT_GRID_SIZE_BINDING = 5). The first light's
// position.w holds the number of lights.
struct PointLight
{
    float4 colorIntensity; // rgb: colour, a: radius
    float4 position; // view space
};
struct LightGridCell
{
    uint offset;
    uint count;
};
struct LightGridSize
{
    uint x;
    uint y;
    uint z;
    uint unused;
};
[[vk::binding(2, 0)]] StructuredBuffer<PointLight> pointLights;
[[vk::binding(3, 0)]] StructuredBuffer<uint> lightIndexList;
[[vk::binding(4, 0)]] StructuredBuffer<LightGridCell> lightGrid;
[[vk::binding(5, 0)]] ConstantBuffer<LightGridSize> lightGridSize;

// The compute set (lib/sets.glsl: COMPUTE_SET = 3): this frame's samples, the integrated result the forward shaders
// read, the blend of this frame's samples with the history (the samples themselves when there is no temporal blend), and
// the previous frame's blend.
[[vk::binding(0, 3)]] [format("rgba16f")] RWTexture3D<float4> fillImage;
[[vk::binding(1, 3)]] [format("rgba16f")] RWTexture3D<float4> resultImage;
[[vk::binding(2, 3)]] [format("rgba16f")] RWTexture3D<float4> blendedImage;
[[vk::binding(3, 3)]] Sampler3D historyImage;

static const float pi = 3.14159265358979;

// volumemath.hpp: sliceDepth
float sliceDepth(float slice)
{
    return volume.range.x * exp(volume.range.z * slice / volume.grid.z);
}

// volumemath.hpp: fogExtinction, with the density setting. The engine's exponential fog 1 - exp(-2 (d - start / 2) /
// (end - start / 2)), as a density that is the same everywhere. No fog scale means no fog.
float fogDensity()
{
    if (scene.fogScale <= 0.0)
        return 0.0;
    float fogEnd = scene.fogStart + 1.0 / scene.fogScale;
    return volume.fog.x * 2.0 / max(fogEnd - scene.fogStart * 0.5, 1.0);
}

// The point in view space of froxel centre `coord` (x, y in froxels, z in slices) and the camera's view axis depth
// of it. The direction does not depend on the depth, so any depth in the frustum gives it.
float3 viewPosition(float3 coord)
{
    float2 uv = coord.xy / volume.grid.xy;
    float2 ndc = uv * 2.0 - 1.0;
    float4 p = mul(scene.invProjection, float4(ndc, 1.0, 1.0));
    float3 ray = p.xyz / p.w;
    return ray * (sliceDepth(coord.z) / max(-ray.z, 1e-6));
}

// volumemath.hpp: henyeyGreenstein (the same formula as sunGlowHenyeyGreenstein in lib/view/fog.glsl)
float henyeyGreenstein(float cosTheta, float g)
{
    float c = clamp(cosTheta, -1.0, 1.0);
    float k = clamp(g, -0.99, 0.99);
    float denominator = 1.0 + k * k - 2.0 * k * c;
    return (1.0 - k * k) / (4.0 * pi * denominator * sqrt(denominator));
}

// The sun's shadow at a point of view space: 1 lit, 0 shadowed. The same lookup as shadowMaps() in lib/view/shadow.glsl
// (cascade by view depth, fade out towards the maximum distance), with one hardware comparison, no soft shadows and a
// fixed bias (a fog point has no surface normal). A view with no cascades (shadows off) has none: 1.
float sunShadow(float3 viewPos)
{
    int count = int(volume.sun.w);
    if (count <= 0)
        return 1.0;
    if (scene.cascadeSplits[count - 1] == 0.0)
        return 1.0;
    float fade = 0.0;
    float maxDistance = volume.sun.y;
    if (maxDistance != 0.0)
    {
        float fadeStart = -volume.sun.z * maxDistance;
        fade = max(0.0, fadeStart - viewPos.z);
        if (volume.sun.z != 1.0)
            fade /= maxDistance + fadeStart;
        if (fade >= 1.0)
            return 1.0;
    }
    uint cascade = 0;
    for (int i = 0; i < count - 1; ++i)
    {
        if (viewPos.z < scene.cascadeSplits[i])
            cascade = i + 1;
    }
    float4 coord = mul(scene.viewToCascadeProj[cascade], float4(viewPos, 1.0));
    float lit = shadowMap.SampleCmpLevelZero(shadowSampler, float3(coord.xy, float(cascade)), coord.z + volume.lamps.y);
    return lerp(lit, 1.0, fade);
}

// The light of the point lights near `viewPos` (view space), added to the fog's: the lamps' own colour, falling off to
// nothing at twice their radius (the range the surfaces' light ends at), scattering the same in every direction. The
// falloff is a smooth curve of its own, not the surfaces' Morrowind falloff, so that this shader needs none of the
// fallback values the GLSL reads. `uv` is the screen position of the froxel (0 to 1), which picks the light grid's cell.
float3 lampLight(float2 uv, float3 viewPos)
{
    float3 sum = float3(0.0, 0.0, 0.0);
    uint total = uint(pointLights[0].position.w);
    if (volume.lamps.x <= 0.0 || total == 0)
        return sum;
    uint3 size = uint3(lightGridSize.x, lightGridSize.y, lightGridSize.z);
    // lib/light/grid.glsl: screenToGridCoord and gridCoordToIndex, clamped as selectTile does.
    float3 gridCoord = float3(uv * float2(size.xy),
        min(float(size.z - 1), max(0.0, float(size.z) * log((-viewPos.z - scene.zNear) / (scene.zFar - scene.zNear) + 1.0))));
    if (any(isnan(gridCoord)))
        gridCoord = float3(0.0, 0.0, 0.0);
    uint3 cell = uint3(clamp(gridCoord, float3(0.0, 0.0, 0.0), float3(size) - float3(1.0, 1.0, 1.0)));
    uint index = cell.x + size.x * cell.y + size.x * size.y * cell.z;
    LightGridCell entry = lightGrid[index];
    uint count = min(total, entry.count);
    for (uint i = 0; i < count; ++i)
    {
        PointLight lamp = pointLights[lightIndexList[entry.offset + i]];
        float range = lamp.colorIntensity.w * 2.0;
        float d = length(lamp.position.xyz - viewPos);
        if (range > 0.0 && d < range)
        {
            float fall = 1.0 - d / range;
            sum += lamp.colorIntensity.xyz * (fall * fall);
        }
    }
    return sum * volume.lamps.x;
}

// The light the fog scatters towards the eye at `viewPos`, per unit of scattering: the air's own colour (the weather's
// fog colour, which already holds the sky's light), plus the sun where its light reaches. The sun's term is shaped like
// the sun glow of task 0040: the Henyey-Greenstein lobe, scaled by the fog colour's brightness so a dim fog (night) has
// a dim sun term, and fading out when the light is too dim to be the sun.
float3 fogLight(float2 uv, float3 viewPos)
{
    float3 light = scene.fogColor.xyz + lampLight(uv, viewPos);
    float3 sun = scene.lightDiffuse.xyz;
    float brightest = max(sun.x, max(sun.y, sun.z));
    float presence = smoothstep(0.02, 0.2, brightest);
    if (volume.sun.x <= 0.0 || presence <= 0.0)
        return light;
    float cosTheta = dot(normalize(viewPos), normalize(scene.lightViewPos.xyz));
    float phase = 4.0 * pi * henyeyGreenstein(cosTheta, volume.fog.w);
    float luma = dot(scene.fogColor.xyz, float3(0.2126, 0.7152, 0.0722));
    // Clear weather keeps half of the effect: shafts need some haze, but a clear day still has a little.
    float haze = 1.0 - clamp(scene.skyColor.w, 0.0, 1.0);
    float weight = volume.sun.x * phase * (0.5 + 0.5 * haze) * presence * luma;
    return light + (sun / max(brightest, 1e-4)) * weight * sunShadow(viewPos);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void fillMain(uint3 id : SV_DispatchThreadID)
{
    uint3 grid = uint3(volume.grid.xyz);
    if (id.x >= grid.x || id.y >= grid.y || id.z >= grid.z)
        return;
    // The sample sits at this frame's offset inside the froxel (the middle when there is no temporal blend).
    float3 coord = float3(id) + volume.jitter.xyz;
    float3 viewPos = viewPosition(coord);
    float density = fogDensity();
    if (volume.fog.y > 0.0)
    {
        // Thinner higher up, from the camera's own height.
        float worldZ = mul(scene.invView, float4(viewPos, 1.0)).z;
        float baseZ = mul(scene.invView, float4(0.0, 0.0, 0.0, 1.0)).z;
        density *= exp(-max(worldZ - baseZ, 0.0) * volume.fog.y);
    }
    float2 uv = coord.xy / volume.grid.xy;
    // The debug view: red, the sun's shadow (1 lit); green, 1 when shadow cascades are set up; blue, 1 when the last
    // cascade has a split (the shadow maps cover this view).
    int cascades = int(volume.sun.w);
    float3 debugColour = float3(sunShadow(viewPos), cascades > 0 ? 1.0 : 0.0,
        (cascades > 0 && scene.cascadeSplits[max(cascades - 1, 0)] != 0.0) ? 1.0 : 0.0);
    float3 light = volume.lamps.z > 0.5 ? debugColour : fogLight(uv, viewPos);
    // The light and the density are kept apart: a dim fog's product would be a tiny number in a half float.
    fillImage[id] = float4(light, density);
}

// The temporal blend: this frame's samples (fillImage) with the previous frame's blend, found where the froxel's centre
// was in the previous frame's view (volume.toPreviousClip; its clip w is the view axis depth). Nothing is blended when
// the history weight is 0 (first frame, a jump of the camera) or the froxel was outside the previous volume.
[shader("compute")]
[numthreads(8, 8, 1)]
void temporalMain(uint3 id : SV_DispatchThreadID)
{
    uint3 grid = uint3(volume.grid.xyz);
    if (id.x >= grid.x || id.y >= grid.y || id.z >= grid.z)
        return;
    float4 current = fillImage[id];
    float4 result = current;
    float weight = volume.jitter.w;
    if (weight > 0.0)
    {
        float3 viewPos = viewPosition(float3(id) + float3(0.5, 0.5, 0.5));
        float4 clip = mul(volume.toPreviousClip, float4(viewPos, 1.0));
        if (clip.w > 1e-3)
        {
            float2 uv = clip.xy / clip.w * 0.5 + 0.5;
            float slice = log(clip.w / volume.range.x) / volume.range.z * volume.grid.z;
            if (all(uv >= float2(0.0, 0.0)) && all(uv <= float2(1.0, 1.0)) && slice >= 0.0 && slice <= volume.grid.z)
            {
                float4 history = historyImage.SampleLevel(float3(uv, slice / volume.grid.z), 0.0);
                if (!any(isnan(history)) && !any(isinf(history)))
                    result = lerp(current, history, weight);
            }
        }
    }
    blendedImage[id] = result;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void integrateMain(uint3 id : SV_DispatchThreadID)
{
    uint3 grid = uint3(volume.grid.xyz);
    if (id.x >= grid.x || id.y >= grid.y)
        return;
    float3 scattered = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;
    float previous = sliceDepth(0.0);
    for (uint z = 0; z < grid.z; ++z)
    {
        float next = sliceDepth(float(z + 1));
        float dz = next - previous;
        previous = next;
        float4 cell = blendedImage[uint3(id.xy, z)];
        float sigma = cell.a;
        // volumemath.hpp: integrateSlice. (1 - exp(-sigma dz)) / sigma tends to dz as sigma tends to 0.
        float step = exp(-sigma * dz);
        float weight = sigma < 1e-6 ? dz : (1.0 - step) / sigma;
        scattered += transmittance * cell.rgb * sigma * weight;
        transmittance *= step;
        resultImage[uint3(id.xy, z)] = float4(scattered, transmittance);
    }
}
