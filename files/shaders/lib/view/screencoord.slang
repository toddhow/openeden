// The Slang twin of lib/view/screencoord.glsl (task 0072): perspective projections. Change both together.
#ifndef LIB_VIEW_SCREENCOORD_SLANG
#define LIB_VIEW_SCREENCOORD_SLANG

import lib.math.projection;

float2 clipPosToScreenCoord(float4 clipPos)
{
    float3 ndc = clipPos.xyz / clipPos.w;
    return ndc.xy * 0.5 + 0.5;
}

float2 viewPosToScreenCoord(float3 viewPos, float4x4 projection)
{
    float4 clipPos = mulPerspective(projection, float4(viewPos, 1.0));
    return clipPosToScreenCoord(clipPos);
}

float4 screenCoordToViewPos(float2 screenCoord, float depth, float4x4 invProjection)
{
    float4 clipPos = float4(screenCoord * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mulInvPerspective(invProjection, clipPos);
    return viewPos / viewPos.w;
}

#endif
