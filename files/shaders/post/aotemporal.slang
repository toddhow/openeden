// Temporal accumulation of ambient occlusion, in Slang (task 0076; decision 0009: new shaders are written in Slang).
// Three compute passes over the half-resolution occlusion image (Render::AoTemporalPass, components/render/aotemporal.cpp):
//
//   genMain      per pixel: the ground truth ambient occlusion of the fragment pass (post/ao.slang, the same maths) with the
//                pixel's noise moved by a different offset every frame (Roberts' R2 sequence), so that the frames sample
//                other slice directions and step positions. Writes the raw value.
//   resolveMain  per pixel: the history (the previous frame's result) is read at the four texels round the position the
//                velocity gives; each is weighted by bilinear weight, by whether its stored depth and normal are the
//                surface that is here now; the history value is limited to the variance range of the raw values round
//                the pixel and blended in with a weight that starts as a cumulative average and settles at the setting.
//   copyMain     the result becomes the next frame's history (a separate image, so no thread reads what another writes).
//
// The result image (value, encoded depth, count, packed normal) is what the tonemap reads (its first channel); its
// filter (aoFiltered) stays the last spatial step. The CPU twin of every formula is components/render/
// aotemporalmath.hpp (Render::Ao::Temporal) and, for the occlusion itself, aomath.hpp (Render::Ao): change them together.
// The constants below are read back by a test (RenderAoTemporalShader).
//
// Compiled with: -profile spirv_1_0 -matrix-layout-column-major.

// ---- aotemporalmath.hpp constants ----
static const float maxHistoryWeight = 0.98;
static const float maxCount = 255.0;
static const float depthRejectLow = 0.02;
static const float depthRejectHigh = 0.1;
static const float normalRejectLow = 0.8;
static const float normalRejectHigh = 0.95;

// ---- the settings of the fragment pass (post/aosettings.slang): ids 1100 to 1102 ----
[vk::constant_id(1100)] const float AO_RADIUS = 100.0;
[vk::constant_id(1101)] const int AO_SLICES = 2;
[vk::constant_id(1102)] const int AO_STEPS = 6;

// The bindings of the pass's descriptor set (components/render/aotemporal.cpp).
[[vk::binding(0, 0)]] Sampler2D linearDepth; // the stored linear depth (0: nothing opaque), nearest
[[vk::binding(1, 0)]] Sampler2D viewNormal; // the stored view-space normal, nearest
[[vk::binding(2, 0)]] Sampler2D velocityTex; // currentUV - previousUV, from the velocity pass, nearest
[[vk::binding(3, 0)]] [format("r32f")] RWTexture2D<float> rawStore; // this frame's unaccumulated value
[[vk::binding(4, 0)]] [format("rgba32f")] RWTexture2D<float4> resolveStore; // value, encoded depth, count, packed normal
[[vk::binding(5, 0)]] [format("rgba32f")] RWTexture2D<float4> historyStore; // the previous frame's resolveStore

struct Push
{
    float4 depthRow; // row 3 of this frame's view space to the previous frame's clip space: its w is the previous depth
    float4 rot0; // xyz: row 0 of the rotation from this view space to the previous one; w: the projection's [0][0]
    float4 rot1; // xyz: row 1; w: the projection's [1][1]
    float4 rot2; // xyz: row 2; w: the history weight (0: take nothing from the history)
    float4 noise; // xy: the noise offset of the frame; zw: the history's uv shift by the TAA jitter
    float4 misc; // x: debug view (1 raw, 2 weight, 3 reject), y: the clamp's gamma (0: none)
};
[[vk::push_constant]] ConstantBuffer<Push> push;

static const float PI = 3.14159265358979;

float saturate1(float x)
{
    return clamp(x, 0.0, 1.0);
}

float smoothStep(float lo, float hi, float x)
{
    float t = saturate1((x - lo) / (hi - lo));
    return t * t * (3.0 - 2.0 * t);
}

// ---- aotemporalmath.hpp: encodeDepth, depthMismatch, normalMatch ----
float encodeDepth(float depth)
{
    return depth > 0.0 ? log2(1.0 + depth) : -1.0;
}

float depthMismatch(float expectedPreviousDepth, float storedEncoded)
{
    if (storedEncoded < 0.0)
        return expectedPreviousDepth > 0.0 ? 1.0 : 0.0;
    if (!(expectedPreviousDepth > 0.0))
        return 1.0;
    return smoothStep(depthRejectLow, depthRejectHigh, abs(encodeDepth(expectedPreviousDepth) - storedEncoded));
}

float normalMatch(float d)
{
    return smoothStep(normalRejectLow, normalRejectHigh, d);
}

// ---- aotemporalmath.hpp: packNormal, unpackNormal (octahedral, 12 bits per axis) ----
float packNormal(float3 n)
{
    float l1 = abs(n.x) + abs(n.y) + abs(n.z);
    float2 o = n.xy / l1;
    if (n.z < 0.0)
        o = (1.0 - abs(float2(o.y, o.x))) * float2(o.x >= 0.0 ? 1.0 : -1.0, o.y >= 0.0 ? 1.0 : -1.0);
    float2 q = floor(clamp(o, float2(-1.0, -1.0), float2(1.0, 1.0)) * 0.5 * 4095.0 + 0.5 * 4095.0 + 0.5);
    return q.x * 4096.0 + q.y;
}

float3 unpackNormal(float packedValue)
{
    float qx = floor(packedValue / 4096.0);
    float qy = packedValue - qx * 4096.0;
    float2 o = float2(qx, qy) / 4095.0 * 2.0 - 1.0;
    float3 n = float3(o.x, o.y, 1.0 - abs(o.x) - abs(o.y));
    if (n.z < 0.0)
    {
        float2 f = (1.0 - abs(float2(o.y, o.x))) * float2(o.x >= 0.0 ? 1.0 : -1.0, o.y >= 0.0 ? 1.0 : -1.0);
        n = float3(f.x, f.y, n.z);
    }
    return normalize(n);
}

// ---- aomath.hpp: bilateralWeight (the tonemap's weights, here for the same-surface neighbourhood) ----
float bilateralWeight(float depthDifference, float depth, float normalDot)
{
    float relative = depth > 0.0 ? abs(depthDifference) / depth : 1.0;
    return (1.0 - smoothStep(0.02, 0.05, relative)) * smoothStep(0.707, 0.9, normalDot);
}

// ---- post/ao.slang: the fragment pass's maths, unchanged except for the noise ----
float gradientNoise(float2 p)
{
    // Interleaved gradient noise (Jimenez 2014): a fixed pattern by pixel position.
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

float2 projectionXY()
{
    return float2(push.rot0.w, push.rot1.w);
}

float3 viewPosAt(float2 uv, float d)
{
    return float3((uv * 2.0 - 1.0) * d / projectionXY(), -d);
}

float horizonCosine(float3 delta, float3 toCamera)
{
    float len = length(delta);
    if (!(len > 0.0) || len >= AO_RADIUS)
        return -1.0;
    float weight = clamp(1.0 - len / AO_RADIUS, 0.0, 1.0);
    float cosine = dot(delta, toCamera) / len;
    // The falloff of Render::Ao::horizonCosine at 1.
    return -1.0 + (cosine + 1.0) * weight;
}

float sliceVisibility(float h1, float h2, float n)
{
    h1 = n + max(h1 - n, -0.5 * PI);
    h2 = n + min(h2 - n, 0.5 * PI);
    float sinN = sin(n);
    float cosN = cos(n);
    return 0.25 * (-cos(2.0 * h1 - n) + cosN + 2.0 * h1 * sinN) + 0.25 * (-cos(2.0 * h2 - n) + cosN + 2.0 * h2 * sinN);
}

int2 texelAt(float2 uv)
{
    uint fw, fh;
    linearDepth.GetDimensions(fw, fh);
    // A small bias so that a coordinate on a texel corner (the centre of a half-resolution pixel, 2p + 1 up to the
    // rounding of a float) is always the same texel; without it the tie went either way pixel by pixel and the centre and
    // the horizon samples disagreed about which texel is "here".
    return clamp(int2(floor(uv * float2(float(fw), float(fh)) + 0.001)), int2(0, 0), int2(int(fw) - 1, int(fh) - 1));
}

// The full-resolution texel that stands for a half-resolution pixel. The pixel's centre lies exactly on the corner of
// four full-resolution texels (an even size): the fragment pass (post/ao.slang) lets the hardware's nearest filter pick
// one by the rounding of its interpolated coordinate, and takes the pixel's centre as the surface's position even though
// the depth is a texel's, half a texel away. That is a fixed pattern in its picture (dots on a plane, checks on a wall,
// which no averaging over frames can remove) and a compute pass cannot repeat it. This pass takes the lower right texel
// (always the same one) and its centre as the position, so that the depth, the normal, the velocity and the position
// agree and the samples of the horizon search are measured from it.
int2 fullTexel(int2 p, float2 halfSize)
{
    return texelAt((float2(p) + 0.5) / halfSize);
}

float2 texelCentre(int2 t)
{
    uint fw, fh;
    linearDepth.GetDimensions(fw, fh);
    return (float2(t) + 0.5) / float2(float(fw), float(fh));
}

float3 loadNormal(int2 t)
{
    float3 n = viewNormal.Load(int3(t, 0)).xyz;
    return dot(n, n) > 0.0 ? normalize(n) : float3(0.0, 0.0, 1.0);
}

bool hasSurface(int2 t)
{
    float3 n = viewNormal.Load(int3(t, 0)).xyz;
    return linearDepth.Load(int3(t, 0)).r > 0.0 && dot(n, n) > 0.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void genMain(uint3 id : SV_DispatchThreadID)
{
    uint hw, hh;
    rawStore.GetDimensions(hw, hh);
    if (id.x >= hw || id.y >= hh)
        return;
    int2 p = int2(id.xy);
    float2 halfSize = float2(float(hw), float(hh));
    float2 texCoord = (float2(p) + 0.5) / halfSize;
    int2 centreTexel = texelAt(texCoord);
    float d = linearDepth.Load(int3(centreTexel, 0)).r;
    float3 n = viewNormal.Load(int3(centreTexel, 0)).xyz;
    if (!(d > 0.0) || !(dot(n, n) > 0.0))
    {
        rawStore[p] = 1.0;
        return;
    }
    n = normalize(n);
    float2 centreUv = texelCentre(centreTexel);
    float3 position = viewPosAt(centreUv, d);
    float3 toCamera = normalize(-position);
    float2 fragCoord = float2(p) + 0.5;
    float2 noise = float2(gradientNoise(fragCoord), gradientNoise(fragCoord + float2(5.588238, 5.588238)));
    // aotemporalmath.hpp: rotateNoise
    noise = frac(noise + push.noise.xy);
    float2 projXY = projectionXY();
    float2 uvRadius = min(AO_RADIUS * abs(projXY) * 0.5 / d, float2(0.25, 0.25));
    float visibility = 0.0;
    for (int s = 0; s < AO_SLICES; ++s)
    {
        float phi = (float(s) + noise.x) / float(AO_SLICES) * PI;
        float2 uvDir = float2(cos(phi), sin(phi));
        float3 dirView = normalize(float3(uvDir * float2(sign(projXY)), 0.0));
        float3 orthoDir = dirView - dot(dirView, toCamera) * toCamera;
        float3 axis = normalize(cross(dirView, toCamera));
        float3 projected = n - axis * dot(n, axis);
        float projectedLength = length(projected);
        if (projectedLength < 1e-4)
        {
            visibility += 1.0;
            continue;
        }
        float cosN = clamp(dot(projected, toCamera) / projectedLength, 0.0, 1.0);
        float nAngle = (dot(orthoDir, projected) >= 0.0 ? 1.0 : -1.0) * acos(cosN);
        float horizon[2] = { -1.0, -1.0 };
        for (int side = 0; side < 2; ++side)
        {
            float direction = side == 0 ? 1.0 : -1.0;
            for (int k = 1; k <= AO_STEPS; ++k)
            {
                float t = (float(k) - noise.y) / float(AO_STEPS);
                float2 sampleUv = centreUv + direction * uvDir * uvRadius * t;
                float sampleDepth = linearDepth.Load(int3(texelAt(sampleUv), 0)).r;
                if (!(sampleDepth > 0.0))
                    continue;
                horizon[side] = max(horizon[side], horizonCosine(viewPosAt(sampleUv, sampleDepth) - position, toCamera));
            }
        }
        float h1 = -acos(clamp(horizon[1], -1.0, 1.0));
        float h2 = acos(clamp(horizon[0], -1.0, 1.0));
        visibility += projectedLength * sliceVisibility(h1, h2, nAngle);
    }
    rawStore[p] = clamp(visibility / float(AO_SLICES), 0.0, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void resolveMain(uint3 id : SV_DispatchThreadID)
{
    uint hw, hh;
    resolveStore.GetDimensions(hw, hh);
    if (id.x >= hw || id.y >= hh)
        return;
    int2 p = int2(id.xy);
    int2 maxP = int2(int(hw) - 1, int(hh) - 1);
    float2 halfSize = float2(float(hw), float(hh));
    float2 uv = (float2(p) + 0.5) / halfSize;
    int debugView = int(push.misc.x + 0.5);

    int2 t = fullTexel(p, halfSize);
    float raw = rawStore[p];
    if (!hasSurface(t))
    {
        float shown = debugView == 2 || debugView == 3 ? 0.0 : 1.0;
        resolveStore[p] = float4(shown, -1.0, 1.0, packNormal(float3(0.0, 0.0, 1.0)));
        return;
    }
    float d = linearDepth.Load(int3(t, 0)).r;
    float3 n = loadNormal(t);

    // Where this pixel was in the previous frame, and what the previous camera saw of it.
    float baseWeight = clamp(push.rot2.w, 0.0, maxHistoryWeight);
    float3 viewPos = viewPosAt(texelCentre(t), d);
    float expectedDepth = dot(push.depthRow, float4(viewPos, 1.0));
    float2 velocity = velocityTex.Load(int3(t, 0)).rg;
    float2 historyPos = (uv - velocity + push.noise.zw) * halfSize - 0.5;
    float2 base = floor(historyPos);
    float2 f = historyPos - base;
    float3 rotated = float3(dot(push.rot0.xyz, n), dot(push.rot1.xyz, n), dot(push.rot2.xyz, n));
    float sumW = 0.0;
    float sumValue = 0.0;
    float sumCount = 0.0;
    if (baseWeight > 0.0)
    {
        for (int i = 0; i < 4; ++i)
        {
            int2 offset = int2(i & 1, i >> 1);
            int2 tap = int2(base) + offset;
            if (any(tap < int2(0, 0)) || any(tap > maxP))
                continue;
            float bx = offset.x == 0 ? 1.0 - f.x : f.x;
            float by = offset.y == 0 ? 1.0 - f.y : f.y;
            float4 stored = historyStore[tap];
            // A history that was never written (a frame that was not recorded after a build) may hold anything: a tap
            // that is not a finite number takes no part, and 0 * NaN cannot get into the sums.
            if (any(isnan(stored)) || any(isinf(stored)))
                continue;
            float w = bx * by * (1.0 - depthMismatch(expectedDepth, stored.y))
                * normalMatch(dot(rotated, unpackNormal(stored.w)));
            if (!(w > 0.0))
                continue;
            sumW += w;
            sumValue += w * stored.x;
            sumCount += w * stored.z;
        }
    }
    float confidence = sumW > 1e-4 ? saturate1(sumW) : 0.0;
    float historyValue = confidence > 0.0 ? sumValue / sumW : raw;
    float historyCount = confidence > 0.0 ? sumCount / sumW : 0.0;

    // The same-surface 3 by 3 neighbourhood of the raw values: the history is limited to its variance range.
    float gamma = push.misc.y;
    if (gamma > 0.0 && confidence > 0.0)
    {
        float m1 = raw;
        float m2 = raw * raw;
        float total = 1.0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0)
                    continue;
                int2 q = clamp(p + int2(dx, dy), int2(0, 0), maxP);
                int2 qt = fullTexel(q, halfSize);
                if (!hasSurface(qt))
                    continue;
                float w = bilateralWeight(linearDepth.Load(int3(qt, 0)).r - d, d, dot(loadNormal(qt), n));
                float v = rawStore[q];
                total += w;
                m1 += w * v;
                m2 += w * v * v;
            }
        m1 /= total;
        m2 /= total;
        float deviation = sqrt(max(m2 - m1 * m1, 0.0));
        historyValue = clamp(historyValue, m1 - gamma * deviation, m1 + gamma * deviation);
    }

    // aotemporalmath.hpp: historyBlend, accumulate, nextCount
    float blend = (historyCount > 0.0 && confidence > 0.0)
        ? confidence * min(baseWeight, historyCount / (historyCount + 1.0))
        : 0.0;
    float value = raw + (historyValue - raw) * saturate1(blend);
    float count = min(1.0 + max(historyCount, 0.0) * saturate1(confidence), maxCount);
    if (baseWeight <= 0.0)
        count = 1.0;

    float shown = value;
    if (debugView == 1)
        shown = raw;
    else if (debugView == 2)
        shown = blend;
    else if (debugView == 3)
        shown = 1.0 - confidence;
    resolveStore[p] = float4(shown, encodeDepth(d), count, packNormal(n));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void copyMain(uint3 id : SV_DispatchThreadID)
{
    uint hw, hh;
    resolveStore.GetDimensions(hw, hh);
    if (id.x >= hw || id.y >= hh)
        return;
    historyStore[int2(id.xy)] = resolveStore[int2(id.xy)];
}
