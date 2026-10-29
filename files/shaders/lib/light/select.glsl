#include "grid.glsl"

layout (std430, set=VIEW_SET, binding=VIEW_LIGHT_INDICES_BINDING) readonly buffer LightIndexBuffer{
    uint lightIndexList[];
};

layout (std430, set=VIEW_SET, binding=VIEW_LIGHT_GRID_BINDING) readonly buffer LightGridBuffer{
    LightGrid lightGrid[];
};

uint selectTile(vec2 screenCoord, float viewZ)
{
    // openeden-vsg: per-vertex lighting passes screen coordinates of off-screen vertices (outside 0..1, or from
    // behind the camera); unclamped they index lightGrid out of bounds, which faults the device on Windows drivers.
    vec3 gridCoord = screenToGridCoord(screenCoord, viewZ, scene.zNear, scene.zFar);
    // NaN (log of a negative depth term, or clip w of 0) would make the clamp and conversion below undefined.
    gridCoord = mix(gridCoord, vec3(0), isnan(gridCoord));
    uvec3 coord = uvec3(clamp(gridCoord, vec3(0), vec3(gridSize.x, gridSize.y, gridSize.z) - vec3(1)));
    return gridCoordToIndex(coord);
}
