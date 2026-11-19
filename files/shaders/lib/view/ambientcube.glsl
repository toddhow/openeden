// OpenEden (task 0355 stage e): the ambient cube of an interior (Skyrim SE's XCLL and LGTM DALC): six colours, one per
// axis direction, and a surface takes the faces it looks at by the squares of its world normal. The C++ twin is
// components/render/ambientcube.hpp, which also documents the layout; change them together. The six colours ride at the
// end of the sky-visibility header buffer (binding VIEW_SKYVIS_SKY_BINDING), after the cloud shadow data. Include after
// lib/view/bindings.glsl, lib/sets.glsl and lib/view/shadow.glsl (and lib/view/skyvis.glsl): the header is declared by
// cloudshadow.glsl with CLOUD_SHADOW, by skyvis.glsl with GI and GI_SKY, else here.
#if !defined(CLOUD_SHADOW) && !defined(SKY_HEADER_DECLARED)
layout(std430, set=VIEW_SET, binding=VIEW_SKYVIS_SKY_BINDING) readonly buffer SkyVisHeader {
    vec4 skyHeader[];
};
#endif

const int AMBIENT_CUBE_BASE = 2117; // Render::AmbientCube::base = Render::Cloud::headerTotalVec4s

// The flat ambient where no cube is loaded; else the cube's colour for `worldNormal` plus what the flat ambient has
// gained since the cube's baseline (Night Eye), never below zero (Render::AmbientCube::ambientFor).
vec3 ambientCubeAmbient(vec3 worldNormal, vec3 flatAmbient)
{
    if (skyHeader[AMBIENT_CUBE_BASE].x < 0.5)
        return flatAmbient;
    float length2 = dot(worldNormal, worldNormal);
    vec3 n = length2 > 1e-20 ? worldNormal * inversesqrt(length2) : vec3(0.0, 0.0, 1.0);
    vec3 sq = n * n;
    vec3 cube = skyHeader[AMBIENT_CUBE_BASE + (n.x >= 0.0 ? 2 : 3)].xyz * sq.x
        + skyHeader[AMBIENT_CUBE_BASE + (n.y >= 0.0 ? 4 : 5)].xyz * sq.y
        + skyHeader[AMBIENT_CUBE_BASE + (n.z >= 0.0 ? 6 : 7)].xyz * sq.z;
    return max(cube + (flatAmbient - skyHeader[AMBIENT_CUBE_BASE + 1].xyz), vec3(0.0));
}
