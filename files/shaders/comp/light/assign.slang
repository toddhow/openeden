// The clustered light assignment pass, in Slang (task 0072, stage c): the twin of comp/light/assign.comp (no
// defines). It must declare what the GLSL declares, the local size included (checked by SlangPermutations). The work
// group size is that of comp/light/workgroupsize.glsl, which components/view/lightgrid.cpp includes to dispatch;
// change both together. STORAGE_ARGS_BINDING is that of comp/bindings.glsl.
import lib.light.equations;
import lib.light.grid;
import lib.light.maxlights;
import lib.light.pointlights;
#include "lib/sets.slang"
#include "lib/view/bindings.slang"
import lib.view.scene;
import lib.view.screencoord;

static const int workGroupSizeX = 8;
static const int workGroupSizeY = 4;
static const int workGroupSizeZ = 4;
static const int STORAGE_ARGS_BINDING = 1;

[[vk::binding(VIEW_LIGHT_INDICES_BINDING, VIEW_SET)]] RWStructuredBuffer<uint> lightIndexList;
[[vk::binding(VIEW_LIGHT_GRID_BINDING, VIEW_SET)]] RWStructuredBuffer<LightGrid> lightGrid;
// The GLSL's block { uint indexCount; }: a storage block that is not an array (GLSLShaderStorageBuffer).
struct IndexCountBlock
{
    uint indexCount;
};
[[vk::binding(STORAGE_ARGS_BINDING, COMPUTE_SET)]] GLSLShaderStorageBuffer<IndexCountBlock> indexCountBuffer;

bool testLight(uint lightIndex, uint3 gridCoord)
{
    PointLight l = pointLights[lightIndex];
    if (l.colorIntensity.w == 0)
        return false;
    float3 vp = l.position.xyz;
    float range = lightRange(l.colorIntensity.w);
    float3 vp_min, vp_max;
    vp_min.yz = vp.yz + range;
    vp_max.yz = vp.yz - range;

    // restrict view_z
    if ((vp_max.z >= -scene.zNear) || (vp_min.z <= -scene.zFar))
        return false;

    vp_min.z = min(-scene.zNear, vp_min.z);
    vp_max.z = max(-scene.zFar, vp_max.z);
    vp_min.x = vp.x - range;
    vp_max.x = vp.x + range;

    float2 fp_a = viewPosToScreenCoord(float3(vp_min.xy, vp_min.z), scene.projection);
    float2 fp_b = viewPosToScreenCoord(float3(vp_min.xy, vp_max.z), scene.projection);
    float2 fp_c = viewPosToScreenCoord(float3(vp_max.xy, vp_max.z), scene.projection);
    float2 fp_d = viewPosToScreenCoord(float3(vp_max.xy, vp_min.z), scene.projection);
    float2 fp_min = min(fp_a, fp_b);
    float2 fp_max = max(fp_c, fp_d);

    // restrict to the frustum
    bool exit = false;
    exit = exit || (fp_min.x < 0.f && fp_max.x < 0.f) || (fp_min.y < 0.f && fp_max.y < 0.f);
    exit = exit || (fp_min.x >= 1.f && fp_max.x >= 1.f) || (fp_min.y >= 1.f && fp_max.y >= 1.f);
    exit = exit || fp_min.x > fp_max.x || fp_min.y > fp_max.y;
    if (exit)
        return false;

    fp_min.xy = max(float2(0.f), fp_min);
    fp_max.xy = min(float2(1.f), fp_max);

    float3 grid_coord_min = screenToGridCoord(fp_min, vp_min.z, scene.zNear, scene.zFar);
    float3 grid_coord_max = screenToGridCoord(fp_max, vp_max.z, scene.zNear, scene.zFar);

    return !((grid_coord_min.x < gridCoord.x && grid_coord_max.x < gridCoord.x)
        || (grid_coord_min.x > gridCoord.x + 1 && grid_coord_max.x > gridCoord.x + 1)
        || (grid_coord_min.y < gridCoord.y && grid_coord_max.y < gridCoord.y)
        || (grid_coord_min.y > gridCoord.y + 1 && grid_coord_max.y > gridCoord.y + 1));
}

[shader("compute")]
[numthreads(workGroupSizeX, workGroupSizeY, workGroupSizeZ)]
void assignMain(uint3 globalId : SV_DispatchThreadID)
{
    uint lightCount = uint(pointLights[0].position.w);
    uint3 gridCoord = globalId;
    uint gridIndex = gridCoordToIndex(gridCoord);

    uint visibleLightCount = 0;
    uint visibleLightIndices[maxLightsPerCluster];
    for (uint i = 0; i < lightCount; ++i)
    {
        if (!testLight(i, gridCoord))
            continue;
        uint visibleIndex = min(visibleLightCount++, maxLightsPerCluster - 1);
        visibleLightIndices[visibleIndex] = i;
    }

    uint offset;
    InterlockedAdd(indexCountBuffer.indexCount, visibleLightCount, offset);
    for (uint i = 0; i < visibleLightCount; ++i)
        lightIndexList[offset + i] = visibleLightIndices[i];

    lightGrid[gridIndex].offset = offset;
    lightGrid[gridIndex].count = visibleLightCount;
}
