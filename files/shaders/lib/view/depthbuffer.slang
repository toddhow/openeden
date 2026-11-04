// The Slang twin of lib/view/depthbuffer.glsl (task 0072): the view-space depth of a depth-buffer value (perspective
// projection). GLSL's m[c][r] is m[r][c] here. Change both together.
#ifndef LIB_VIEW_DEPTHBUFFER_SLANG
#define LIB_VIEW_DEPTHBUFFER_SLANG

// The camera-space (negative) z of a depth-buffer value, for a perspective projection's inverse.
float depthBufferValueToViewZ(float depth, float4x4 invProjection)
{
    return invProjection[2][3] / (invProjection[3][2] * depth + invProjection[3][3]);
}

float reconstructViewZ(Sampler2D zBuffer, int2 texel, float2 zBufferSize, float4x4 invProjection)
{
    float sampledDepth = zBuffer.Load(int3(texel, 0)).r;
    return depthBufferValueToViewZ(sampledDepth, invProjection);
}

#endif
