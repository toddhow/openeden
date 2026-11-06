// The fog of war's update pass, in Slang (task 0072, stage b): the twin of comp/fogofwar/update.comp (no defines). It
// must declare what the GLSL declares, the local size included (checked by SlangPermutations). The work group size and
// the constants are those of comp/fogofwar/workgroupsize.glsl and constants.glsl; change them together (the C++ side
// dispatches by the work group size).
import lib.math.distance;
#include "lib/sets.slang"

static const int workGroupSize = 8;
static const int resolution = 32;
static const float exploreRadius = 0.17f * (resolution - 1); // explore radius from 0 to resolution-1
static const float sqrExploreRadius = exploreRadius * exploreRadius;

[[vk::binding(0, COMPUTE_SET)]] [[vk::image_format("r8")]] RWTexture2D<float4> fogImage;

struct PushConstants
{
    float4 rect;
    float4 center; // xy
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[shader("compute")]
[numthreads(workGroupSize, workGroupSize, 1)]
void updateMain(uint3 globalId : SV_DispatchThreadID)
{
    int2 coord = int2(globalId.xy) + int2(pc.rect.xy);
    if (coord.x < pc.rect.z && coord.y < pc.rect.w)
    {
        float2 texelCenter = float2(coord) + 0.5;
        float sqrDist = distanceSquared(texelCenter, pc.center.xy);
        float v = sqrDist / sqrExploreRadius;
        if (v < 1)
        {
            float current = fogImage[coord].r;
            fogImage[coord] = float4(min(current, v));
        }
    }
}
