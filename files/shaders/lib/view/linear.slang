// The Slang twin of lib/view/linear.glsl (task 0072): the linear HDR path's conversions. Include after
// lib/view/scene.slang. The reasons are in the GLSL; change both together (and components/render/hdrmath.hpp).
#ifndef LIB_VIEW_LINEAR_SLANG
#define LIB_VIEW_LINEAR_SLANG

import lib.view.scene;

static const float displayGamma = 2.2;

bool linearView()
{
    return scene.fogColor.w > 1.5;
}

float3 toLinear(float3 encoded)
{
    return sign(encoded) * pow(abs(encoded), float3(displayGamma));
}

float3 toEncoded(float3 linear)
{
    return sign(linear) * pow(abs(linear), float3(1.0 / displayGamma));
}

float3 toLinearIfView(float3 encoded)
{
    return linearView() ? toLinear(encoded) : encoded;
}

float3 toEncodedIfView(float3 viewSpace)
{
    return linearView() ? toEncoded(viewSpace) : viewSpace;
}

#endif
