// OpenEden (task 0028): baked light probes. The C++ twin is components/render/probesamplermath.hpp and the buffer's
// layout is components/render/probegpu.hpp; change them together. Include after lib/view/bindings.glsl and
// lib/sets.glsl.
//   giData[0] = (origin.xyz, spacing); [1] = (size.xyz, enabled); [2] = (normal bias, strength, 0, 0);
//   [3] = (baked ambient.xyz, 0); probe i, coefficient k at [4 + 9 * i + k].
layout(std430, set=VIEW_SET, binding=VIEW_PROBE_BINDING) readonly buffer ProbeData {
    vec4 giData[];
};

// The ambient light of probe `index` for `n`: cosine-convolved irradiance divided by pi (see shAmbient).
vec3 giProbeAmbient(int index, vec3 n)
{
    int base = 4 + 9 * index;
    float basis[9] = float[9](0.282095, 0.488603 * n.y, 0.488603 * n.z, 0.488603 * n.x, 1.092548 * n.x * n.y,
        1.092548 * n.y * n.z, 0.315392 * (3.0 * n.z * n.z - 1.0), 1.092548 * n.x * n.z, 0.546274 * (n.x * n.x - n.y * n.y));
    float band[9] = float[9](1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0, 0.25, 0.25, 0.25, 0.25, 0.25);
    vec3 sum = vec3(0.0);
    for (int k = 0; k < 9; ++k)
        sum += giData[base + k].xyz * (basis[k] * band[k]);
    return max(sum, vec3(0.0));
}

// Trilinear in the lattice after the normal bias (sampleAmbient).
vec3 giSampleAmbient(vec3 worldPos, vec3 worldNormal)
{
    vec3 n = normalize(worldNormal);
    float spacing = giData[0].w;
    vec3 p = worldPos + n * (giData[2].x * spacing);
    vec3 size = giData[1].xyz;
    vec3 g = clamp((p - giData[0].xyz) / spacing, vec3(0.0), size - 1.0);
    vec3 f = floor(g);
    vec3 t = g - f;
    ivec3 i0 = ivec3(f);
    ivec3 i1 = min(i0 + 1, ivec3(size) - 1);
    int sx = int(size.x);
    int sy = int(size.y);
    vec3 x00 = mix(giProbeAmbient(i0.x + sx * (i0.y + sy * i0.z), n), giProbeAmbient(i1.x + sx * (i0.y + sy * i0.z), n), t.x);
    vec3 x10 = mix(giProbeAmbient(i0.x + sx * (i1.y + sy * i0.z), n), giProbeAmbient(i1.x + sx * (i1.y + sy * i0.z), n), t.x);
    vec3 x01 = mix(giProbeAmbient(i0.x + sx * (i0.y + sy * i1.z), n), giProbeAmbient(i1.x + sx * (i0.y + sy * i1.z), n), t.x);
    vec3 x11 = mix(giProbeAmbient(i0.x + sx * (i1.y + sy * i1.z), n), giProbeAmbient(i1.x + sx * (i1.y + sy * i1.z), n), t.x);
    return mix(mix(x00, x10, t.y), mix(x01, x11, t.y), t.z);
}

// 1 inside the lattice box, 0 at one spacing outside it (volumeWeight).
float giVolumeWeight(vec3 worldPos)
{
    vec3 lo = giData[0].xyz;
    vec3 hi = lo + giData[0].w * (giData[1].xyz - 1.0);
    vec3 outside = max(lo - worldPos, worldPos - hi);
    float d = max(max(outside.x, outside.y), max(outside.z, 0.0));
    return clamp(1.0 - d / giData[0].w, 0.0, 1.0);
}

// The ambient at a point (giAmbient): the baked value plus what was added to the flat ambient since the bake, faded
// into the flat ambient outside the volume. No volume loaded gives the flat ambient.
vec3 giAmbient(vec3 worldPos, vec3 worldNormal, vec3 flatAmbient)
{
    if (giData[1].w < 0.5)
        return flatAmbient;
    float w = giVolumeWeight(worldPos);
    vec3 offset = flatAmbient - giData[3].xyz;
    vec3 gi = max(giSampleAmbient(worldPos, worldNormal) * giData[2].y + offset, vec3(0.0));
    return mix(flatAmbient, gi, w);
}
