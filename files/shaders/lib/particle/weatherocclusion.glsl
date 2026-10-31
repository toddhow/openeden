#ifndef LIB_PARTICLE_WEATHEROCCLUSION
#define LIB_PARTICLE_WEATHEROCCLUSION

// OpenEden: weather particles under a roof (task 0012, V4l); OpenMW's applyOcclusionDiscard at 442eec6e2f, with the
// coordinates of MWRender::occlusionCoord. The depth map is rendered from above with reversed depth; its image rows run
// from the box's high world Y. The box follows the camera; its size is scene.lightDiffuse.w and its height
// scene.lightSpecular.w (see lib/view/data.glsl on why not a buffer or field of its own).
layout(set=TEXTURE_SET, binding=OCCLUSION_UNIT) uniform sampler2D occlusionMap;

bool isOccludedParticle(vec3 worldPos)
{
    vec3 camera = scene.invView[3].xyz;
    vec2 coord = (worldPos.xy - camera.xy) / scene.lightDiffuse.w + 0.5;
    float particleDepth = 0.5 + (worldPos.z - camera.z) / scene.lightSpecular.w;
    return particleDepth < texture(occlusionMap, vec2(coord.x, 1.0 - coord.y)).r;
}

#endif
