// OpenEden (task 0038): the shadow of the sky's clouds on the ground. The C++ twins are components/render/cloudmath.hpp
// (domePoint, domeUv, shadowDirection, sunFade, shadowFactor, sampleGrid) and components/render/cloudgpu.hpp (the layout
// of the data); change them together. The data rides at the end of the sky-visibility header buffer (binding
// VIEW_SKYVIS_SKY_BINDING). Included by lib/view/shadow.glsl when `shadow.settings` defines CLOUD_SHADOW; it declares
// the header, which lib/view/skyvis.glsl then leaves out. Include after lib/view/bindings.glsl, lib/sets.glsl and
// lib/view/scene.glsl.
layout(std430, set=VIEW_SET, binding=VIEW_SKYVIS_SKY_BINDING) readonly buffer SkyVisHeader {
    vec4 skyHeader[];
};

// The first entry after the sky's own: skyHeaderFixed (12) + skyWindow squared (49), as skyvisgpu.hpp.
const int CLOUD_BASE = 61;
const int CLOUD_GRID = 64;
const int CLOUD_GRID_OFFSET = 8;
const int CLOUD_GRID_VEC4S = 1024;
const float CLOUD_UV_CENTER_U = 2.5;
const float CLOUD_UV_CENTER_V = -1.5;
const float CLOUD_UV_PER_UNIT = 0.0024975;
const float CLOUD_LAYER_HEIGHT = 15000.0;
const float CLOUD_SUN_UP_FROM = 0.08;
const float CLOUD_SUN_UP_TO = 0.3;
const float CLOUD_SHADOW_EXTINCTION = 2.0;
const float CLOUD_DOME_RADIUS[5] = float[5](0.0, 275.0, 462.0, 688.0, 1000.0);
const float CLOUD_DOME_HEIGHT[5] = float[5](306.733, 288.429, 254.262, 200.076, 100.0);

// Where a ray from the camera (unit, Z up, in the dome's frame) meets the dome, as its X and Y (domePoint).
vec2 cloudDomePoint(vec3 direction)
{
    float horizontal = sqrt(direction.x * direction.x + direction.y * direction.y);
    if (horizontal < 1e-6)
        return vec2(0.0);
    float slope = max(direction.z, 0.0) / horizontal;
    float radius = CLOUD_DOME_RADIUS[4];
    for (int i = 1; i < 5; ++i)
    {
        float farSlope = CLOUD_DOME_HEIGHT[i] / CLOUD_DOME_RADIUS[i];
        if (farSlope > slope)
            continue;
        float s = (CLOUD_DOME_HEIGHT[i] - CLOUD_DOME_HEIGHT[i - 1]) / (CLOUD_DOME_RADIUS[i] - CLOUD_DOME_RADIUS[i - 1]);
        radius = (CLOUD_DOME_HEIGHT[i - 1] - s * CLOUD_DOME_RADIUS[i - 1]) / (slope - s);
        radius = clamp(radius, CLOUD_DOME_RADIUS[i - 1], CLOUD_DOME_RADIUS[i]);
        break;
    }
    return direction.xy / horizontal * radius;
}

float cloudGridCell(int layer, int x, int y)
{
    int index = y * CLOUD_GRID + x;
    return skyHeader[CLOUD_BASE + CLOUD_GRID_OFFSET + layer * CLOUD_GRID_VEC4S + (index >> 2)][index & 3];
}

// Bilinear, wrapping: sampleGrid.
float cloudSampleGrid(int layer, vec2 uv)
{
    vec2 p = uv * float(CLOUD_GRID) - 0.5;
    vec2 f = floor(p);
    vec2 t = p - f;
    int x0 = int(mod(f.x, float(CLOUD_GRID)));
    int y0 = int(mod(f.y, float(CLOUD_GRID)));
    int x1 = (x0 + 1) % CLOUD_GRID;
    int y1 = (y0 + 1) % CLOUD_GRID;
    float a = mix(cloudGridCell(layer, x0, y0), cloudGridCell(layer, x1, y0), t.x);
    float b = mix(cloudGridCell(layer, x0, y1), cloudGridCell(layer, x1, y1), t.x);
    return mix(a, b, t.y);
}

// The thickness of one layer's cloud in the direction `worldDirection` from the camera (domeUv, then the grid).
float cloudLayerThickness(int layer, vec3 worldDirection, float scroll)
{
    int at = CLOUD_BASE + 1 + layer * 3;
    mat3 rotation = mat3(skyHeader[at].xyz, skyHeader[at + 1].xyz, skyHeader[at + 2].xyz);
    // The rotation takes the dome's frame to the world, so its transpose, a row vector times it, takes a direction back.
    vec3 local = worldDirection * rotation;
    vec2 point = cloudDomePoint(local);
    vec2 uv = vec2(CLOUD_UV_CENTER_U + CLOUD_UV_PER_UNIT * point.x, CLOUD_UV_CENTER_V - CLOUD_UV_PER_UNIT * point.y - scroll);
    return cloudSampleGrid(layer, uv);
}

// The share of the direct sun that reaches the ground at `viewPos` through the clouds (1 when there is no shadow).
float cloudShadow(vec3 viewPos)
{
    vec4 params = skyHeader[CLOUD_BASE];
    float amount = params.x;
    if (amount <= 0.0)
        return 1.0;
    mat3 invRotation = mat3(scene.invView);
    vec3 toSun = normalize(invRotation * scene.lightViewPos.xyz);
    amount *= smoothstep(CLOUD_SUN_UP_FROM, CLOUD_SUN_UP_TO, toSun.z);
    if (amount <= 0.0)
        return 1.0;
    vec3 camera = scene.invView[3].xyz;
    vec3 ground = (scene.invView * vec4(viewPos, 1.0)).xyz;
    // shadowDirection: from the camera towards the cloud the sun's ray from the ground meets.
    float up = max(toSun.z, CLOUD_SUN_UP_FROM * 0.5);
    float travel = max(CLOUD_LAYER_HEIGHT - (ground.z - camera.z), 0.0) / up;
    vec3 target = (ground - camera) + toSun * travel;
    float len = sqrt(dot(target, target));
    vec3 direction = len < 1e-6 ? vec3(0.0, 0.0, 1.0) : target / len;
    float blend = params.y;
    vec4 flags = skyHeader[CLOUD_BASE + 7];
    float thick = 0.0;
    if (flags.x > 0.5)
        thick += (1.0 - blend) * cloudLayerThickness(0, direction, params.z);
    if (flags.y > 0.5 && blend > 0.0)
        thick += blend * cloudLayerThickness(1, direction, params.z);
    return 1.0 - clamp(amount, 0.0, 1.0) * (1.0 - exp(-CLOUD_SHADOW_EXTINCTION * clamp(thick, 0.0, 1.0)));
}
