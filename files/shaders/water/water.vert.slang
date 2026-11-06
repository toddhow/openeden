// The water's vertex stage, in Slang (task 0072, water family): the twin of water/main.vert (no defines). It must
// declare what the GLSL declares (checked by SlangPermutations). The four vertices of the water plane are made here from
// the view frustum's corners, as in the GLSL; the statements follow it one for one. GLSL's m[c][r] is m[r][c] here.
import glsl;

import lib.inout;
#include "lib/pushconstants.slang"
import lib.view.depthrange;
import lib.view.scene;

struct VertexOutput
{
    CommonInout io;
    float4 position : SV_Position;
};

float3 slide(float3 from, float3 to, float targetZ)
{
    float zDiff = to.z - from.z;
    return lerp(from, to, clamp(-(from.z - targetZ) / zDiff, 0.0, 1.0));
}

[shader("vertex")]
VertexOutput vertMain(uint vertexIndexU : SV_VulkanVertexID)
{
    int vertexIndex = int(vertexIndexU);
    VertexOutput output;
    float2 edge = float2(1.0) + float2(8.0) / scene.resolution;
    float3 frustumCorners[8] = { float3(edge.x, edge.y, nearDepth), float3(-edge.x, edge.y, nearDepth),
        float3(edge.x, -edge.y, nearDepth), float3(-edge.x, -edge.y, nearDepth), float3(edge.x, edge.y, farDepth),
        float3(-edge.x, edge.y, farDepth), float3(edge.x, -edge.y, farDepth), float3(-edge.x, -edge.y, farDepth) };

    // Project frustum corners into local space
    float4x4 invModelView = inverse(pc.modelview);
    float4x4 inv = mul(invModelView, scene.invProjection);
    for (int i = 0; i < 8; ++i)
    {
        float4 invCorner = mul(inv, float4(frustumCorners[i], 1.0f));
        frustumCorners[i] = invCorner.xyz / invCorner.w;
    }

    float relativeCameraZ = invModelView[2][3];
    bool eyeUnderwater = relativeCameraZ < 0;
    int side = vertexIndex % 2;
    float3 farTop = frustumCorners[6 + side];
    float3 farBottom = frustumCorners[4 + side];
    int underwaterSide = 2 * int(eyeUnderwater);
    int underwaterSign = 1 - underwaterSide;

    // Prevent collisions with the near plane
    float bumpZ = scene.zNear * (1 - clamp(abs(relativeCameraZ) / scene.zNear, 0.0, 1.0));
    float targetZ = bumpZ * -underwaterSign;

    float3 vertex;
    if (underwaterSign * farTop.z >= 0 && underwaterSign * farBottom.z >= 0)
        vertex = farTop;
    else if (vertexIndex > 1 && farTop.z > 0 && farBottom.z < 0)
    {
        // Horizon vertices
        vertex = slide(farTop, farBottom, targetZ);
    }
    else
    {
        // Screen edge vertices
        int index = (vertexIndex + underwaterSide) % 4;
        float3 nearCorner = frustumCorners[index];
        float3 farCorner = frustumCorners[4 + index];
        vertex = slide(nearCorner, farCorner, targetZ);
    }

    float4 viewPos = mul(pc.modelview, float4(vertex, 1.0));
    output.io.viewPos = viewPos.xyz;
    // The GLSL leaves pointLightDiffuse unwritten; the water's fragment stage never reads it.
    output.io.pointLightDiffuse = float3(0.0);
    output.position = mul(pc.projection, viewPos);
    return output;
}
