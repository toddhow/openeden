// OpenEden (task 0028, stage d): baked reflection probes. The C++ twins are components/render/reflectionmath.hpp (the
// maths) and reflectiongpu.hpp (the layout of the buffer); change them together. Include after lib/view/bindings.glsl
// and lib/sets.glsl.
//   reflData[0] = (probe count, resolution, levels, enabled); [1] = (strength, baked ambient.xyz);
//   [3 + p] = (position.xyz, 0); probe p's chain starts at [19 + p * chainTexels]: the octahedral map at full size,
//   then half size and so on down to 4 by 4, one after the other.
#ifndef LIB_VIEW_REFLECTION
#define LIB_VIEW_REFLECTION

layout(std430, set=VIEW_SET, binding=VIEW_REFLECTION_BINDING) readonly buffer ReflectionData {
    vec4 reflData[];
};

const int reflTexelsBase = 19;

bool giReflectionEnabled()
{
    return reflData[0].w > 0.5;
}

// True when the buffer holds the exterior's sky environment image (reflData[2].x = 1, set by updateSkyEnvironment) and
// not an interior's baked probes.
bool giReflectionIsSkyEnvironment()
{
    return reflData[2].x > 0.5;
}

// The texels before copy `level` in a chain whose full-size map is `res` wide.
int reflLevelOffset(int res, int level)
{
    int offset = 0;
    for (int l = 0; l < level; ++l)
    {
        int size = res >> l;
        offset += size * size;
    }
    return offset;
}

// Octahedral mapping (Cigolle et al., JCGT 2014); the sign of zero counts as positive (octEncode).
vec2 reflOctEncode(vec3 dir)
{
    float sum = abs(dir.x) + abs(dir.y) + abs(dir.z);
    if (!(sum > 0.0))
        return vec2(0.5);
    vec2 e = dir.xy / sum;
    if (dir.z < 0.0)
        e = vec2((1.0 - abs(e.y)) * (e.x >= 0.0 ? 1.0 : -1.0), (1.0 - abs(e.x)) * (e.y >= 0.0 ? 1.0 : -1.0));
    return e * 0.5 + 0.5;
}

// Bilinear read of one copy; reads outside the square are clamped to its edge (sampleReflectionLevel).
vec3 reflReadLevel(int base, int size, vec2 uv)
{
    float maxIndex = float(size - 1);
    vec2 f = clamp(uv * float(size) - 0.5, vec2(0.0), vec2(maxIndex));
    ivec2 i0 = ivec2(f);
    ivec2 i1 = min(i0 + 1, ivec2(size - 1));
    vec2 t = f - vec2(i0);
    vec3 top = mix(reflData[base + i0.y * size + i0.x].xyz, reflData[base + i0.y * size + i1.x].xyz, t.x);
    vec3 bottom = mix(reflData[base + i1.y * size + i0.x].xyz, reflData[base + i1.y * size + i1.x].xyz, t.x);
    return mix(top, bottom, t.y);
}

// The light a surface of this roughness sees in `dir` in probe `probe` (sampleReflectionChain).
vec3 reflSampleProbe(int probe, int res, int levels, vec3 dir, float roughness)
{
    int chain = reflLevelOffset(res, levels);
    int base = reflTexelsBase + probe * chain;
    float lod = clamp(roughness, 0.0, 1.0) * float(levels - 1);
    int l0 = min(int(lod), levels - 1);
    int l1 = min(l0 + 1, levels - 1);
    float t = lod - float(l0);
    vec2 uv = reflOctEncode(dir);
    vec3 a = reflReadLevel(base + reflLevelOffset(res, l0), res >> l0, uv);
    if (l1 == l0 || t <= 0.0)
        return a;
    vec3 b = reflReadLevel(base + reflLevelOffset(res, l1), res >> l1, uv);
    return mix(a, b, t);
}

// The environment BRDF of Karis, "Physically Based Shading on Mobile" (2014): a surface of specular colour f0 reflects an
// environment by f0 * x + y (envBrdfScaleBias).
vec2 reflEnvBrdf(float roughness, float nDotV)
{
    vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return max(ab, vec2(0.0));
}

// The light reflected towards `dir` at `worldPos`: the two nearest probes, weighted so that one leaving the pair has
// weight zero (reflectionBlend). The baked ambient is replaced by the current one, as the diffuse term does
// (giAmbient); the result is scaled by the strength.
vec3 giReflection(vec3 worldPos, vec3 dir, float roughness, vec3 flatAmbient)
{
    int count = int(reflData[0].x + 0.5);
    int res = int(reflData[0].y + 0.5);
    int levels = int(reflData[0].z + 0.5);
    float d0 = 1e30, d1 = 1e30, d2 = 1e30;
    int i0 = 0, i1 = 0;
    for (int i = 0; i < count; ++i)
    {
        float dist = length(reflData[3 + i].xyz - worldPos);
        if (dist < d0)
        {
            d2 = d1;
            d1 = d0;
            i1 = i0;
            d0 = dist;
            i0 = i;
        }
        else if (dist < d1)
        {
            d2 = d1;
            d1 = dist;
            i1 = i;
        }
        else if (dist < d2)
            d2 = dist;
    }
    vec3 light;
    if (count < 2)
        light = reflSampleProbe(i0, res, levels, dir, roughness);
    else
    {
        float w0, w1;
        if (count == 2)
        {
            w0 = d1;
            w1 = d0;
        }
        else
        {
            w0 = max(0.0, 1.0 - d0 / d2);
            w1 = max(0.0, 1.0 - d1 / d2);
        }
        float total = w0 + w1;
        if (total > 1e-6)
        {
            w0 /= total;
            w1 /= total;
        }
        else
        {
            w0 = 1.0;
            w1 = 0.0;
        }
        light = reflSampleProbe(i0, res, levels, dir, roughness) * w0;
        if (w1 > 0.0)
            light += reflSampleProbe(i1, res, levels, dir, roughness) * w1;
    }
    vec3 offset = flatAmbient - reflData[1].yzw;
    return max(light + offset, vec3(0.0)) * reflData[1].x;
}

#endif
