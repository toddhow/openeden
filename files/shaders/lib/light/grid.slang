// The Slang twin of lib/light/grid.glsl and lib/light/gridsize.glsl (task 0072): the clustered light grid. Change
// them together.
#ifndef LIB_LIGHT_GRID_SLANG
#define LIB_LIGHT_GRID_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"

struct LightGrid
{
    uint offset;
    uint count;
};

struct GridSize
{
    uint x;
    uint y;
    uint z;
    uint unused;
};

[[vk::binding(VIEW_LIGHT_GRID_SIZE_BINDING, VIEW_SET)]] ConstantBuffer<GridSize> gridSize;

uint gridCoordToIndex(uint3 coord)
{
    return coord.x + gridSize.x * coord.y + (gridSize.x * gridSize.y) * coord.z;
}

float3 screenToGridCoord(float2 screenCoord, float viewZ, float near, float far)
{
    return float3(screenCoord * float2(gridSize.x, gridSize.y),
        min(float(gridSize.z - 1), max(0.f, float(gridSize.z) * log((-viewZ - near) / (far - near) + 1.f))));
}

#endif
