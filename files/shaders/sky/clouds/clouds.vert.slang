// The sky's clouds, vertex stage, in Slang (task 0072, stage b): the twin of sky/clouds/main.vert. It must declare what
// the GLSL declares (checked by SlangPermutations), apart from the texture coordinate array of lib/attributes.slang
// (see sky/clouds/main.vert.interface-exceptions).
#include "lib/attributes.slang"
#include "lib/material/descriptors_vert.slang"
#include "lib/pushconstants.slang"
import lib.view.scene;
#include "sky/clouds/inout.slang"

struct VertexOutput
{
    CLOUDS_INOUT
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexAttributes input, uint vertexIndex : SV_VulkanVertexID)
{
    VertexOutput output;
    output.position = mul(pc.projection, mul(pc.modelview, float4(input.inVertex, 1.0)));
    // The GLSL writes only the alpha of the colour; the rest is never read.
    output.color = float4(0.0);
    if (vertexIndex >= 49 && vertexIndex <= 64)
        output.color.a = 0.0; // bottom-most row
    else if (vertexIndex >= 33 && vertexIndex <= 48)
        output.color.a = 0.25098; // second row
    else
        output.color.a = 1.0;
#ifdef TEXCOORD
    output.texCoord = mul(texMat.texmat, float4(input.inTexCoord0, 0.0, 1.0)).xy;
#endif
    // OpenEden (task 0038): the dome has a rotation and no scale, so its inverse is its transpose.
    output.domeDir = input.inVertex;
    output.sunLocal = mul(transpose((float3x3)pc.modelview), scene.lightViewPos.xyz);
    return output;
}
