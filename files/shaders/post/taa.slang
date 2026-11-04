// Temporal anti-aliasing, in Slang (task 0075; decision 0009: new shaders are written in Slang). Two compute passes:
//
//   resolveMain  per pixel: this frame's colour (drawn with a sub-pixel jitter) blended with the previous frame's
//                resolved colour, which is found through the velocity (the closest pixel of a 3 by 3 gives it; the sky
//                is moved with the camera's rotation only), read with a 5-tap Catmull-Rom filter, limited to the
//                neighbourhood of the current colour (variance clipping after Salvi 2016, or a min/max box), and
//                dropped where the history's depth says the surface is a new one. Everything that mixes colours works
//                in the luma-compressed colour, so a very bright pixel cannot dominate.
//   outputMain   per pixel: keeps the resolved colour as the next frame's history and writes the picture that bloom and
//                the tonemap read, the same colour sharpened by a plus-shaped unsharp mask limited to its neighbours'
//                range. The history never holds the sharpened colour.
//
// The CPU twin of every formula is components/render/taamath.hpp (Render::Taa): change them together. The constants
// below are read back by a test (RenderTaaShader). The history's alpha holds log2(1 + the closest depth of the 3 by 3)
// (-1: sky), so that a depth that changed between the frames shows a surface that was not there.
//
// Compiled with: -profile spirv_1_0 -matrix-layout-column-major, so matrices lie in memory as GLSL's mat4 does.

// ---- taamath.hpp constants ----
static const float velocityFullPixels = 16.0;
static const float movingWeightShare = 0.5;
static const float depthRejectLow = 0.03;
static const float depthRejectHigh = 0.12;
static const float varianceGamma = 1.0;

// The bindings of the pass's descriptor set (components/render/taa.cpp).
[[vk::binding(0, 0)]] Sampler2D colourTex; // the scene (linear HDR), nearest
[[vk::binding(1, 0)]] Sampler2D depthTex; // the stored linear depth (0: nothing opaque), nearest
[[vk::binding(2, 0)]] Sampler2D velocityTex; // currentUV - previousUV, from the velocity pass, nearest
[[vk::binding(3, 0)]] Sampler2D historyTex; // the previous resolved colour, linear
[[vk::binding(4, 0)]] [format("rgba16f")] RWTexture2D<float4> resolveStore;
[[vk::binding(5, 0)]] [format("rgba16f")] RWTexture2D<float4> historyStore;
[[vk::binding(6, 0)]] [format("rgba16f")] RWTexture2D<float4> outputStore;

struct Push
{
    float4x4 toPrevClip; // this frame's view space to the previous frame's clip space (Render::Velocity::toPreviousClip)
    float4 projScale; // xy: the projection's [0][0] and [1][1]; zw: the image size in texels
    float4 params; // x: the history weight (0: take nothing from it), y: 1 = min/max box, 0 = variance box, z: gamma, w: debug view
    float4 look; // x: sharpness
};
[[vk::push_constant]] ConstantBuffer<Push> push;

// ---- taamath.hpp: luma, compress, expand, YCoCg ----
float luma(float3 c)
{
    return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z;
}

float3 compressColour(float3 c)
{
    c = max(c, float3(0.0, 0.0, 0.0));
    return c / (1.0 + luma(c));
}

float3 expandColour(float3 c)
{
    c = max(c, float3(0.0, 0.0, 0.0));
    return c / (1.0 - min(luma(c), 0.9999));
}

float3 toYCoCg(float3 c)
{
    return float3(0.25 * (c.x + 2.0 * c.y + c.z), 0.5 * (c.x - c.z), 0.25 * (-c.x + 2.0 * c.y - c.z));
}

float3 fromYCoCg(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

float saturate1(float x)
{
    return clamp(x, 0.0, 1.0);
}

float smoothStep(float lo, float hi, float x)
{
    float t = saturate1((x - lo) / (hi - lo));
    return t * t * (3.0 - 2.0 * t);
}

// ---- taamath.hpp: encodeDepth, depthReject ----
float encodeDepth(float linearDepth)
{
    return linearDepth > 0.0 ? log2(1.0 + linearDepth) : -1.0;
}

// The history's depths over the 3 by 3 pixels round the reprojected one: the surface may be any of them, because the
// closest depth of a 3 by 3 changes with the jitter at a silhouette.
float depthRejectRange(float expectedPreviousLinearDepth, float storedMin, float storedMax)
{
    if (storedMax < 0.0 && expectedPreviousLinearDepth > 0.0)
        return 1.0;
    float e = encodeDepth(expectedPreviousLinearDepth);
    float outside = max(max(storedMin - e, e - storedMax), 0.0);
    return smoothStep(depthRejectLow, depthRejectHigh, outside);
}

float historyWeightFor(float baseWeight, float speedPixels)
{
    return baseWeight * (1.0 - (1.0 - movingWeightShare) * saturate1(speedPixels / velocityFullPixels));
}

// ---- taamath.hpp: clipToBox ----
float3 clipToBox(float3 colour, float3 lo, float3 hi, out float clippedShare)
{
    float3 centre = 0.5 * (lo + hi);
    float3 halfExtent = 0.5 * (hi - lo);
    float3 d = colour - centre;
    float t = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        float a = abs(d[i]);
        float r = halfExtent[i] > 1e-8 ? a / halfExtent[i] : (a > 1e-8 ? 1e8 : 0.0);
        t = max(t, r);
    }
    clippedShare = 0.0;
    if (t <= 1.0)
        return colour;
    clippedShare = 1.0 - 1.0 / t;
    return centre + d / t;
}

int2 clampTexel(int2 t, int2 size)
{
    return clamp(t, int2(0, 0), size - int2(1, 1));
}

// The scene's colour, compressed, with NaN and infinity dropped (a lamp can hold a huge value).
float3 loadColour(int2 t)
{
    float3 c = colourTex.Load(int3(t, 0)).rgb;
    // NaN is dropped; an infinite value (a half-float overflow) is a very bright pixel, not a black one.
    c = float3(isnan(c.x) ? 0.0 : c.x, isnan(c.y) ? 0.0 : c.y, isnan(c.z) ? 0.0 : c.z);
    return compressColour(min(c, float3(60000.0, 60000.0, 60000.0)));
}

// ---- taamath.hpp: catmullRom5 (the weights of the four taps at -1, 0, 1, 2 for the fraction t) ----
float4 catmullRomWeights(float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return float4(-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2);
}

float3 historyColour(float2 uv, float2 size)
{
    float2 position = uv * size;
    float2 base = floor(position - 0.5);
    float2 f = position - 0.5 - base;
    float4 wx = catmullRomWeights(f.x);
    float4 wy = catmullRomWeights(f.y);
    float wx12 = wx.y + wx.z;
    float wy12 = wy.y + wy.z;
    float2 o12 = float2(wx.z / wx12, wy.z / wy12);
    float2 centre = base + 0.5;
    float3 sum = historyTex.SampleLevel((centre + o12) / size, 0.0).rgb * (wx12 * wy12);
    sum += historyTex.SampleLevel((centre + float2(-1.0, o12.y)) / size, 0.0).rgb * (wx.x * wy12);
    sum += historyTex.SampleLevel((centre + float2(2.0, o12.y)) / size, 0.0).rgb * (wx.w * wy12);
    sum += historyTex.SampleLevel((centre + float2(o12.x, -1.0)) / size, 0.0).rgb * (wx12 * wy.x);
    sum += historyTex.SampleLevel((centre + float2(o12.x, 2.0)) / size, 0.0).rgb * (wx12 * wy.w);
    float total = wx12 * wy12 + wx.x * wy12 + wx.w * wy12 + wx12 * wy.x + wx12 * wy.w;
    return sum / total;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void resolveMain(uint3 id : SV_DispatchThreadID)
{
    int2 size = int2(push.projScale.zw);
    if (any(int2(id.xy) >= size))
        return;
    float2 sizeF = push.projScale.zw;
    int2 p = int2(id.xy);
    float2 uv = (float2(p) + 0.5) / sizeF;

    // The 3 by 3 neighbourhood of the current colour (YCoCg of the compressed colour): its moments and range, and the
    // pixel nearest to the camera.
    float3 centreColour = float3(0.0, 0.0, 0.0);
    float3 m1 = float3(0.0, 0.0, 0.0);
    float3 m2 = float3(0.0, 0.0, 0.0);
    float3 lo = float3(1e9, 1e9, 1e9);
    float3 hi = float3(-1e9, -1e9, -1e9);
    float nearest = 1e30;
    int2 nearestTexel = p;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 t = clampTexel(p + int2(dx, dy), size);
            float3 c = toYCoCg(loadColour(t));
            if (dx == 0 && dy == 0)
                centreColour = c;
            m1 += c;
            m2 += c * c;
            lo = min(lo, c);
            hi = max(hi, c);
            float d = depthTex.Load(int3(t, 0)).r;
            if (d > 0.0 && d < nearest)
            {
                nearest = d;
                nearestTexel = t;
            }
        }
    m1 /= 9.0;
    m2 /= 9.0;

    // Where this pixel was in the previous frame.
    bool surface = nearest < 1e29;
    float2 velocity = float2(0.0, 0.0);
    float2 previousUv = uv;
    float expectedDepth = 0.0;
    bool usable = push.params.x > 0.0;
    if (surface)
    {
        velocity = velocityTex.Load(int3(nearestTexel, 0)).rg;
        previousUv = uv - velocity;
        float2 nearestUv = (float2(nearestTexel) + 0.5) / sizeF;
        float3 viewPos = float3((nearestUv * 2.0 - 1.0) * nearest / push.projScale.xy, -nearest);
        expectedDepth = mul(push.toPrevClip, float4(viewPos, 1.0)).w;
    }
    else
    {
        // The sky has no depth: its direction through the rotation part of the same matrix (taamath.hpp:
        // skyPreviousUv).
        float3 dir = float3((uv * 2.0 - 1.0) / push.projScale.xy, -1.0);
        float4 clip = mul(push.toPrevClip, float4(dir, 0.0));
        if (clip.w > 0.0)
        {
            previousUv = clip.xy / clip.w * 0.5 + 0.5;
            velocity = uv - previousUv;
        }
        else
            usable = false;
    }
    if (any(previousUv < float2(0.0, 0.0)) || any(previousUv > float2(1.0, 1.0)))
        usable = false;
    float speedPixels = length(velocity * sizeF);

    float storedMin = -1.0;
    float storedMax = -1.0;
    if (usable)
    {
        int2 previousTexel = int2(previousUv * sizeF);
        storedMin = 1e9;
        storedMax = -1e9;
        for (int sy = -1; sy <= 1; ++sy)
            for (int sx = -1; sx <= 1; ++sx)
            {
                float stored = historyStore[clampTexel(previousTexel + int2(sx, sy), size)].a;
                storedMin = min(storedMin, stored);
                storedMax = max(storedMax, stored);
            }
    }
    float rejected = usable ? depthRejectRange(expectedDepth, storedMin, storedMax) : 0.0;

    float3 current = fromYCoCg(centreColour);
    float3 result = current;
    float clippedShare = 0.0;
    float weight = 0.0;
    if (usable)
    {
        float3 history = toYCoCg(compressColour(historyColour(previousUv, sizeF)));
        float3 boxLo = lo;
        float3 boxHi = hi;
        if (push.params.y < 0.5)
        {
            float3 mean = m1;
            float3 sd = sqrt(max(m2 - m1 * m1, float3(0.0, 0.0, 0.0)));
            float3 vlo = mean - push.params.z * sd;
            float3 vhi = mean + push.params.z * sd;
            // Limited to the min/max box; where they do not overlap the midpoint of the facing edges.
            for (int i = 0; i < 3; ++i)
            {
                float a = max(vlo[i], lo[i]);
                float b = min(vhi[i], hi[i]);
                if (a > b)
                {
                    float mid = vhi[i] < lo[i] ? 0.5 * (vhi[i] + lo[i]) : 0.5 * (hi[i] + vlo[i]);
                    a = mid;
                    b = mid;
                }
                boxLo[i] = a;
                boxHi[i] = b;
            }
        }
        float3 clipped = clipToBox(history, boxLo, boxHi, clippedShare);
        weight = historyWeightFor(push.params.x, speedPixels) * (1.0 - rejected);
        float3 mixed = lerp(current, fromYCoCg(clipped), weight);
        result = mixed;
    }

    float3 colour = expandColour(result);
    if (any(isnan(colour)) || any(isinf(colour)))
        colour = expandColour(current);

    int debugView = int(push.params.w + 0.5);
    if (debugView == 1)
        colour = float3(weight, weight, weight);
    else if (debugView == 2)
        colour = float3(rejected, clippedShare, saturate1(speedPixels / velocityFullPixels));
    else if (debugView == 3)
        colour = float3(saturate1(abs(velocity.x * sizeF.x) / 8.0), saturate1(abs(velocity.y * sizeF.y) / 8.0), 0.0);

    resolveStore[p] = float4(colour, surface ? encodeDepth(nearest) : -1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void outputMain(uint3 id : SV_DispatchThreadID)
{
    int2 size = int2(push.projScale.zw);
    if (any(int2(id.xy) >= size))
        return;
    int2 p = int2(id.xy);
    float4 centre = resolveStore[p];
    historyStore[p] = centre;

    float sharpness = int(push.params.w + 0.5) == 0 ? push.look.x : 0.0;
    float3 shown = centre.rgb;
    if (sharpness > 0.0)
    {
        // taamath.hpp: sharpenCross
        float3 c = compressColour(centre.rgb);
        float3 n = compressColour(resolveStore[clampTexel(p + int2(0, -1), size)].rgb);
        float3 s = compressColour(resolveStore[clampTexel(p + int2(0, 1), size)].rgb);
        float3 e = compressColour(resolveStore[clampTexel(p + int2(1, 0), size)].rgb);
        float3 w = compressColour(resolveStore[clampTexel(p + int2(-1, 0), size)].rgb);
        float3 mean = 0.25 * (n + s + e + w);
        float3 rangeLo = min(c, min(min(n, s), min(e, w)));
        float3 rangeHi = max(c, max(max(n, s), max(e, w)));
        shown = expandColour(clamp(c + sharpness * (c - mean), rangeLo, rangeHi));
    }
    outputStore[p] = float4(shown, 1.0);
}
