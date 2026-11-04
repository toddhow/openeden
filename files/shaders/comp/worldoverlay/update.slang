// The world overlay's update pass, in Slang (task 0072, stage b): the twin of comp/worldoverlay/update.comp (no
// defines). It must declare what the GLSL declares, the local size included (checked by SlangPermutations). The work
// group size is that of comp/worldoverlay/workgroupsize.glsl; change both together (the C++ side dispatches by it).
#include "lib/sets.slang"

static const int workGroupSize = 9;

[[vk::binding(0, TEXTURE_SET)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> outImage;
[[vk::binding(1, TEXTURE_SET)]] Sampler2D mask;
[[vk::binding(0, COMPUTE_SET)]] Sampler2D colorTexture;

struct PushConstants
{
    float4 dstRect;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[shader("compute")]
[numthreads(workGroupSize, workGroupSize, 1)]
void updateMain(uint3 globalId : SV_DispatchThreadID)
{
    int2 coord = int2(globalId.xy) + int2(pc.dstRect.xy);
    if (coord.x < pc.dstRect.z && coord.y < pc.dstRect.w)
    {
        float2 texCoord = float2(globalId.xy) / (pc.dstRect.zw - pc.dstRect.xy);
        float4 color = colorTexture.SampleLevel(texCoord, 0.0);
        color.a *= mask.Load(int3(coord, 0)).r;
        outImage[coord] = color;
    }
}
