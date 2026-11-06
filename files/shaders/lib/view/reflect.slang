// The Slang twin of lib/view/reflect.glsl (task 0072): screen-space reflection and refraction against the reflection
// pass's colour and depth. Include after lib/view/scene.slang. One difference: the GLSL reads gl_FragCoord for the
// jitter; here the caller passes the fragment's window position. Change both together.
#ifndef LIB_VIEW_REFLECT_SLANG
#define LIB_VIEW_REFLECT_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"
import lib.view.scene;
import lib.view.raytrace;

[[vk::binding(VIEW_REFLECT_MAP_BINDING, VIEW_SET)]] Sampler2D reflectMap;
[[vk::binding(VIEW_REFLECT_DEPTH_MAP_BINDING, VIEW_SET)]] Sampler2D reflectDepthMap;

float scaleThickness(float thickness, float3 viewPos)
{
    // As in the GLSL, -1/2000 is an integer division: the factor is 0 and the thickness is not scaled.
    static const float factor = -1 / 2000;
    float depthScale = min(1.0f, viewPos.z * factor) * 5;
    return thickness + thickness * depthScale;
}

float scaleStride(float stride, float3 viewPos)
{
    // Shorter strides far away, where perspective makes objects small on screen.
    static const float strideCutoff = -1 / 3000.0;
    stride *= 1.0 - min(1.0, viewPos.z * strideCutoff);
    return 1.0 + float(int(stride));
}

float2 reflectDepthMapSize()
{
    uint width;
    uint height;
    reflectDepthMap.GetDimensions(width, height);
    return float2(int2(width, height));
}

float4 screenSpaceReflection(
    float4x4 projection, float4x4 invProjection, float3 viewPos, float3 viewNormal, float maxDistance, float2 fragCoord)
{
    float thickness = scaleThickness(15, viewPos);
    float stride = scaleStride(10, viewPos);
    static const float maxSteps = 25;
    float jitterFraction = float((int(fragCoord.x + fragCoord.y)) & 1) * 0.5;

    float3 viewVec = normalize(viewPos);
    float3 pivot = normalize(reflect(viewVec, viewNormal));

    float3 hitPointView;
    float2 hitPixel;
    bool hit = traceScreenSpaceRay(viewPos.xyz, pivot, scene.projection, scene.invProjection, reflectDepthMap,
        thickness, -scene.zNear, stride, jitterFraction, maxSteps, maxDistance, hitPixel, hitPointView);
    return float4(hitPixel / reflectDepthMapSize(), 0, float(hit));
}

float4 screenSpaceRefraction(float4x4 projection, float4x4 invProjection, float3 viewPos, float3 viewNormal, float ior,
    float maxDistance, float2 fragCoord)
{
    float thickness = scaleThickness(15, viewPos);
    float stride = scaleStride(10, viewPos);
    static const float maxSteps = 10;
    float jitterFraction = float((int(fragCoord.x + fragCoord.y)) & 1) * 0.5;

    float3 viewVec = normalize(viewPos);
    float3 pivot = normalize(refract(viewVec, viewNormal, ior));

    float3 hitPointView;
    float2 hitPixel;
    bool hit = traceScreenSpaceRay(viewPos.xyz, pivot, scene.projection, scene.invProjection, reflectDepthMap,
        thickness, -scene.zNear, stride, jitterFraction, maxSteps, maxDistance, hitPixel, hitPointView);
    return float4(hitPixel / reflectDepthMapSize(), 0, float(hit));
}

#endif
