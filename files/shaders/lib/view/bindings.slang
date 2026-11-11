// The Slang twin of lib/view/bindings.glsl (task 0072): the bindings of the view set. Change both together.
#ifndef LIB_VIEW_BINDINGS_SLANG
#define LIB_VIEW_BINDINGS_SLANG

#define VIEW_SCENE_BINDING 0
#define VIEW_ENV_BINDING 1
#define VIEW_LIGHTS_BINDING 2
#define VIEW_LIGHT_INDICES_BINDING 3
#define VIEW_LIGHT_GRID_BINDING 4
#define VIEW_LIGHT_GRID_SIZE_BINDING 5
#define VIEW_SHADOW_MAP_BINDING 6
#define VIEW_SHADOW_SAMPLER_BINDING 7
#define VIEW_REFLECT_MAP_BINDING 8
#define VIEW_REFLECT_DEPTH_MAP_BINDING 9
#define VIEW_PROBE_BINDING 10
#define VIEW_REFLECTION_BINDING 11
#define VIEW_SKYVIS_BINDING 12
#define VIEW_SKYVIS_SKY_BINDING 13
#define VIEW_VOLUME_BINDING 14
#define VIEW_VOLUME_PARAMS_BINDING 15
#define VIEW_CLOUD_SHADOW_BINDING 16
// OpenEden (task 0077): the point lights' shadow maps (lib/light/pointshadow.slang).
#define VIEW_POINT_SHADOW_MAP_BINDING 17
#define VIEW_POINT_SHADOW_SAMPLER_BINDING 18
#define VIEW_POINT_SHADOW_DATA_BINDING 19
// OpenEden (task 0095): the baked puddle map's window of cells (lib/material/puddlemap.slang).
#define VIEW_PUDDLE_BINDING 20

#define VIEW_ENV_COUNT 32

#endif
