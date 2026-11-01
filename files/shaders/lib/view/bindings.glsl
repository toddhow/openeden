const int VIEW_SCENE_BINDING=0;
const int VIEW_ENV_BINDING=1;
const int VIEW_LIGHTS_BINDING=2;
const int VIEW_LIGHT_INDICES_BINDING=3;
const int VIEW_LIGHT_GRID_BINDING=4;
const int VIEW_LIGHT_GRID_SIZE_BINDING=5;
const int VIEW_SHADOW_MAP_BINDING=6;
const int VIEW_SHADOW_SAMPLER_BINDING=7;
const int VIEW_REFLECT_MAP_BINDING=8;
const int VIEW_REFLECT_DEPTH_MAP_BINDING=9;
// OpenEden (task 0028): the baked light probes (lib/view/gi.glsl).
const int VIEW_PROBE_BINDING=10;
// OpenEden (task 0028, stage d): the baked reflection probes (lib/view/reflection.glsl).
const int VIEW_REFLECTION_BINDING=11;
// OpenEden (task 0036): the exterior sky-visibility field: the cells' data (lib/view/skyvis.glsl), and its small header.
const int VIEW_SKYVIS_BINDING=12;
const int VIEW_SKYVIS_SKY_BINDING=13;

const int VIEW_ENV_COUNT=32;
