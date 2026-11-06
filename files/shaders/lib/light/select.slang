// The Slang twin of lib/light/select.glsl (task 0072): the clustered light grid's index list and the tile a fragment
// falls in. Include after lib/view/scene.slang. GLSL's mix(a, b, isnan(c)) picks per component, as select does here.
// Change both together.
#ifndef LIB_LIGHT_SELECT_SLANG
#define LIB_LIGHT_SELECT_SLANG

import lib.light.grid;
#include "lib/sets.slang"
#include "lib/view/bindings.slang"

[[vk::binding(VIEW_LIGHT_INDICES_BINDING, VIEW_SET)]] StructuredBuffer<uint> lightIndexList;
[[vk::binding(VIEW_LIGHT_GRID_BINDING, VIEW_SET)]] StructuredBuffer<LightGrid> lightGrid;

uint selectTile(float2 screenCoord, float viewZ)
{
    // openeden-vsg: off-screen coordinates would index lightGrid out of bounds (see the GLSL).
    float3 gridCoord = screenToGridCoord(screenCoord, viewZ, scene.zNear, scene.zFar);
    // NaN would make the clamp and conversion below undefined.
    gridCoord = select(isnan(gridCoord), float3(0), gridCoord);
    uint3 coord = uint3(clamp(gridCoord, float3(0), float3(gridSize.x, gridSize.y, gridSize.z) - float3(1)));
    return gridCoordToIndex(coord);
}

#endif
