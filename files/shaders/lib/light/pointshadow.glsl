// Shadows of the point lights (task 0077): the GLSL twin of pointshadow.slang, for the shaders that are still GLSL.
// Include after lib/view/scene.glsl. Change both together, and components/render/pointshadowmath.hpp and
// pointshadowgpu.hpp. See the Slang file for what it does.
const int POINT_SHADOW_HEADER_VEC4S = 2;
const int POINT_SHADOW_MAX_SLOTS = 8;

layout(set=VIEW_SET, binding=VIEW_POINT_SHADOW_MAP_BINDING) uniform texture2DArray pointShadowMap;
layout(set=VIEW_SET, binding=VIEW_POINT_SHADOW_SAMPLER_BINDING) uniform samplerShadow pointShadowSampler;
layout(std430, set=VIEW_SET, binding=VIEW_POINT_SHADOW_DATA_BINDING) readonly buffer PointShadowBuffer
{
    vec4 pointShadowData[];
};

// The receiver's geometric normal in view space, set by the caller before the light loop.
vec3 pointShadowNormal = vec3(0.0, 0.0, 1.0);

const vec2 pointShadowPoisson[16] = vec2[16](vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432),
    vec2(-0.81544232, -0.87912464), vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023),
    vec2(0.79197514, 0.19090188), vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790));

// Faces 0..5 = +X, -X, +Y, -Y, +Z, -Z; right = cross(forward, up).
const vec3 pointShadowFaceForward[6]
    = vec3[6](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));
const vec3 pointShadowFaceUp[6]
    = vec3[6](vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, -1, 0), vec3(0, 1, 0));
const vec3 pointShadowFaceRight[6]
    = vec3[6](vec3(0, -1, 0), vec3(0, 1, 0), vec3(1, 0, 0), vec3(-1, 0, 0), vec3(1, 0, 0), vec3(1, 0, 0));

int pointShadowFaceOf(vec3 d)
{
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z)
        return d.x < 0.0 ? 1 : 0;
    if (a.y >= a.z)
        return d.y < 0.0 ? 3 : 2;
    return d.z < 0.0 ? 5 : 4;
}

void pointShadowFrisvad(vec3 n, out vec3 t1, out vec3 t2)
{
    float a = 1.0 / (1.0 + n.z);
    float b = -n.x * n.y * a;
    t1 = vec3(1.0 - n.x * n.x * a, b, -n.x);
    t2 = vec3(b, 1.0 - n.y * n.y * a, -n.y);
}

void pointShadowTangents(vec3 n, out vec3 t1, out vec3 t2)
{
    if (n.z < -0.95)
    {
        pointShadowFrisvad(vec3(n.x, -n.y, -n.z), t1, t2);
        t1 = vec3(t1.x, -t1.y, -t1.z);
        t2 = vec3(t2.x, -t2.y, -t2.z);
        return;
    }
    pointShadowFrisvad(n, t1, t2);
}

float pointShadowTap(vec3 q, uint slot, float far, float tanHalf, float resolution, float near, float depthBias)
{
    int face = pointShadowFaceOf(q);
    float dist = dot(q, pointShadowFaceForward[face]);
    if (dist >= far) // beyond the face's far plane (a light whose reach is larger than its range): lit
        return 1.0;
    float scale = 0.5 / (dist * tanHalf);
    float u = 0.5 + scale * dot(q, pointShadowFaceRight[face]);
    float v = 0.5 + scale * dot(q, pointShadowFaceUp[face]);
    float texel = 2.0 * dist * tanHalf / resolution;
    float d = max(dist - depthBias * texel, near);
    float compare = near * (far - d) / ((far - near) * d);
    return texture(sampler2DArrayShadow(pointShadowMap, pointShadowSampler), vec4(u, v, float(6u * slot + uint(face)), compare));
}

float pointLightShadow(uint lightIndex, vec3 viewPos)
{
    vec4 entry = pointShadowData[POINT_SHADOW_HEADER_VEC4S + lightIndex];
    if (entry.x < 0.0)
        return 1.0;
    uint slot = uint(entry.x);
    uint maxLights = uint(pointShadowData.length()) - uint(POINT_SHADOW_HEADER_VEC4S) - uint(POINT_SHADOW_MAX_SLOTS);
    vec4 slotData = pointShadowData[uint(POINT_SHADOW_HEADER_VEC4S) + maxLights + slot];
    vec4 h0 = pointShadowData[0];
    vec4 h1 = pointShadowData[1];
    float resolution = h0.y;
    float tanHalf = h0.z;
    float near = h0.w;
    float far = slotData.w;

    mat3 toWorld = mat3(scene.invView);
    vec3 toReceiver = toWorld * (viewPos - slotData.xyz);
    vec3 normal = toWorld * pointShadowNormal;
    int face0 = pointShadowFaceOf(toReceiver);
    float d0 = dot(toReceiver, pointShadowFaceForward[face0]);
    if (!(d0 > 0.0))
        return 1.0;
    float texel0 = 2.0 * d0 * tanHalf / resolution;
    vec3 p = toReceiver + normal * (h1.x * texel0);
    float radius = h1.z * texel0;
    vec3 t1, t2;
    pointShadowTangents(normal, t1, t2);
    int taps = clamp(int(h1.w), 1, 16);
    float sum = 0.0;
    for (int i = 0; i < taps; ++i)
    {
        vec2 o = pointShadowPoisson[i];
        vec3 q = p + t1 * (o.x * radius) + t2 * (o.y * radius);
        sum += pointShadowTap(q, slot, far, tanHalf, resolution, near, h1.y);
    }
    return mix(1.0, sum / float(taps), entry.y);
}
