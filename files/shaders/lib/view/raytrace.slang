// The Slang twin of lib/view/raytrace.glsl (task 0072): screen-space ray tracing against a depth buffer. The
// statements follow the GLSL one for one; change both together. GLSL's mix(a, b, t) is lerp here.
//
// Copyright (c) 2014, Morgan McGuire and Michael Mara
// All rights reserved.
//
// From McGuire and Mara, Efficient GPU Screen-Space Ray Tracing,
// Journal of Computer Graphics Techniques, 2014
//
// This software is open source under the "BSD 2-clause license":
//
//    Redistribution and use in source and binary forms, with or
//    without modification, are permitted provided that the following
//    conditions are met:
//
//    1. Redistributions of source code must retain the above
//    copyright notice, this list of conditions and the following
//    disclaimer.
//
//    2. Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//
//    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
//    CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
//    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
//    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
//    CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
//    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
//    USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
//    AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
//    IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
//    THE POSSIBILITY OF SUCH DAMAGE.
#ifndef LIB_VIEW_RAYTRACE_SLANG
#define LIB_VIEW_RAYTRACE_SLANG

import lib.math.distance;
import lib.math.projection;
import lib.view.depthbuffer;

void swapFloats(inout float a, inout float b)
{
    float temp = a;
    a = b;
    b = temp;
}

float4 projectToScreen(float3 viewPos, float4x4 projection)
{
    float4 position = mulPerspective(projection, float4(viewPos, 1.0));
    position.xy = 0.5 * (position.xy + position.w);
    return position;
}

// The parameters are described in lib/view/raytrace.glsl.
bool traceScreenSpaceRay(float3 csOrigin, float3 csDirection, float4x4 projection, float4x4 invProjection,
    Sampler2D zBuffer, float csZThickness, float nearPlaneZ, float stride, float jitterFraction, float maxSteps,
    float maxRayTraceDistance, out float2 hitPixel, out float3 csHitPoint)
{
    uint zBufferWidth;
    uint zBufferHeight;
    zBuffer.GetDimensions(zBufferWidth, zBufferHeight);
    float2 zBufferSize = float2(int2(zBufferWidth, zBufferHeight));

    // Clip ray to a near plane in 3D (doesn't have to be *the* near plane, although that would be a good idea)
    float rayLength = ((csOrigin.z + csDirection.z * maxRayTraceDistance) > nearPlaneZ)
        ? (nearPlaneZ - csOrigin.z) / csDirection.z
        : maxRayTraceDistance;
    float3 csEndPoint = csDirection * rayLength + csOrigin;

    // Project into screen space
    float4 H0 = projectToScreen(csOrigin, projection);
    H0.xy *= zBufferSize;
    float4 H1 = projectToScreen(csEndPoint, projection);
    H1.xy *= zBufferSize;

    float k0 = 1.0 / H0.w;
    float k1 = 1.0 / H1.w;

    // Switch the original points to values that interpolate linearly in 2D
    float3 Q0 = csOrigin * k0;
    float3 Q1 = csEndPoint * k1;

    // Screen-space endpoints
    float2 P0 = H0.xy * k0;
    float2 P1 = H1.xy * k1;

    float xMax = zBufferSize.x - 0.5, xMin = 0.5, yMax = zBufferSize.y - 0.5, yMin = 0.5;
    float alpha = 0.0;
    // Assume P0 is in the viewport (P1 - P0 is never zero when clipping)
    if ((P1.y > yMax) || (P1.y < yMin))
        alpha = (P1.y - ((P1.y > yMax) ? yMax : yMin)) / (P1.y - P0.y);
    if ((P1.x > xMax) || (P1.x < xMin))
        alpha = max(alpha, (P1.x - ((P1.x > xMax) ? xMax : xMin)) / (P1.x - P0.x));
    P1 = lerp(P1, P0, alpha);
    k1 = lerp(k1, k0, alpha);
    Q1 = lerp(Q1, Q0, alpha);

    // Initialize to off screen
    hitPixel = float2(-1.0, -1.0);

    // If the line is degenerate, make it cover at least one pixel
    P1 += float2((distanceSquared(P0, P1) < 0.0001) ? 0.01 : 0.0);

    float2 delta = P1 - P0;

    // Permute so that the primary iteration is in x to reduce large branches later
    bool permute = false;
    if (abs(delta.x) < abs(delta.y))
    {
        permute = true;
        delta = delta.yx;
        P1 = P1.yx;
        P0 = P0.yx;
    }

    float stepDirection = sign(delta.x);
    float invdx = stepDirection / delta.x;
    float2 dP = float2(stepDirection, invdx * delta.y);

    // Track the derivatives of Q and k
    float3 dQ = (Q1 - Q0) * invdx;
    float dk = (k1 - k0) * invdx;

    // Scale derivatives by the desired pixel stride
    dP *= stride;
    dQ *= stride;
    dk *= stride;

    // Offset the starting values by the jitter fraction
    P0 += dP * jitterFraction;
    Q0 += dQ * jitterFraction;
    k0 += dk * jitterFraction;

    float3 Q = Q0;
    float k = k0;

    float prevZMaxEstimate = csOrigin.z;
    float stepCount = 0.0;
    float rayZMax = prevZMaxEstimate, rayZMin = prevZMaxEstimate;
    float sceneZMax = rayZMax + 1e4;

    // P1.x is never modified after this point, so pre-scale it by the step direction for a signed comparison
    float end = P1.x * stepDirection;

    for (float2 P = P0; (P.x * stepDirection) <= end && (stepCount < maxSteps)
         && ((rayZMax < sceneZMax - csZThickness) || (rayZMin > sceneZMax)) && (sceneZMax != 0.0);
         P += dP, Q.z += dQ.z, k += dk, stepCount += 1.0)
    {
        hitPixel = permute ? P.yx : P;

        rayZMin = prevZMaxEstimate;

        // Compute the value at 1/2 pixel into the future
        rayZMax = (dQ.z * 0.5 + Q.z) / (dk * 0.5 + k);
        prevZMaxEstimate = rayZMax;
        if (rayZMin > rayZMax)
            swapFloats(rayZMin, rayZMax);

        // Camera-space z of the background
        sceneZMax = reconstructViewZ(zBuffer, int2(hitPixel), zBufferSize, invProjection);
    }

    Q.xy += dQ.xy * stepCount;
    csHitPoint = Q * (1.0 / k);

    return (rayZMax >= sceneZMax - csZThickness) && (rayZMin <= sceneZMax);
}

#endif
