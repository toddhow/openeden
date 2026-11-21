// Automatic exposure, in Slang (task 0364a, stage A1 of the Community Shaders parity plan; decision 0009: new shaders are
// written in Slang; ideas only, no code of another project). Three compute passes (Render::AutoExposurePass,
// components/render/autoexposure.cpp) over a sample of the HDR scene:
//
//   clearMain    one group: zeroes the histogram (64 bins, the luminance sum, the sample count).
//   histMain     a grid of threads, one sample every `stride` pixels (a 2 by 2 bilinear average at the sample's corner):
//                the sample's luminance goes into a log2 histogram (64 bins: bin 0 for what is too dark to count, 63 bins
//                over 16 stops) and into a clamped fixed-point sum (the mean for the colour grade's contrast pivot).
//                Each group counts in group-shared memory first and adds to the global histogram once.
//   resolveMain  one thread: the trimmed mean of log2 luminance between two percentiles, written with the arithmetic
//                mean, the weight and the frame's serial into one slot of a small ring in host-visible memory. The CPU
//                reads a slot that is at least four frames old, adapts the exposure by the frame's dt (never a wall
//                clock) and applies it as an exposure change in the tonemap (the tonemap's interface does not change).
//
// The CPU twin of every formula is components/render/autoexposuremath.hpp (Render::AutoExposure); change them together.
// The constants below are read back by a test (RenderAutoExposureShader).
//
// Compiled with: -profile spirv_1_0 -matrix-layout-column-major.

// ---- autoexposuremath.hpp constants ----
static const int BINS = 64;
static const int FIRST_BIN = 1;
static const int MEASURED_BINS = 63;
static const float MIN_LOG2 = -8.0;
static const float MAX_LOG2 = 8.0;
static const int LUMA_SUM_INDEX = 64;
static const int SAMPLE_COUNT_INDEX = 65;
static const float MEAN_LUMA_CLAMP = 16.0;
static const float MEAN_LUMA_SCALE = 256.0;
static const int SLOTS = 8;
static const uint SERIAL_MASK = 16777215;

// The bindings of the pass's descriptor set (components/render/autoexposure.cpp).
[[vk::binding(0, 0)]] Sampler2D sceneColor; // the HDR scene, linear filtering
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> histogram; // 66 words, device local
[[vk::binding(2, 0)]] RWStructuredBuffer<uint4> results; // 2 entries a slot, host visible: (mean log2, mean luma, weight, serial), (highlight log2, 0, 0, 0)

struct Push
{
    float4 params; // x: pixels between samples, y: the frame's serial (1 .. 2^24), z: low percentile, w: high percentile
    float4 params2; // x: the highlight percentile (0: not measured)
};
[[vk::push_constant]] ConstantBuffer<Push> push;

[shader("compute")]
[numthreads(64, 1, 1)]
void clearMain(uint3 id : SV_DispatchThreadID)
{
    histogram[id.x] = 0;
    if (id.x < 2)
        histogram[BINS + id.x] = 0;
}

groupshared uint localBins[BINS];
groupshared uint localLumaSum;
groupshared uint localCount;

int binOf(float luma)
{
    if (!(luma >= exp2(MIN_LOG2)))
        return 0;
    float position = (log2(luma) - MIN_LOG2) / ((MAX_LOG2 - MIN_LOG2) / float(MEASURED_BINS));
    return FIRST_BIN + clamp(int(floor(position)), 0, MEASURED_BINS - 1);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void histMain(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)
{
    if (index < BINS)
        localBins[index] = 0;
    if (index == 0)
    {
        localLumaSum = 0;
        localCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint sizeX;
    uint sizeY;
    sceneColor.GetDimensions(sizeX, sizeY);
    float stride = push.params.x;
    float2 corner = float2(id.xy) * stride + float2(stride * 0.5);
    if (corner.x < float(sizeX) && corner.y < float(sizeY))
    {
        // The corner between four texels: linear filtering gives their average.
        float3 c = sceneColor.SampleLevel((floor(corner) + float2(1.0)) / float2(float(sizeX), float(sizeY)), 0.0).rgb;
        bool usable = !(any(isnan(c)) || any(isinf(c)));
        if (usable)
        {
            float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
            InterlockedAdd(localBins[binOf(luma)], 1);
            uint fixedLuma = uint(clamp(luma, 0.0, MEAN_LUMA_CLAMP) * MEAN_LUMA_SCALE + 0.5);
            InterlockedAdd(localLumaSum, fixedLuma);
            InterlockedAdd(localCount, 1);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (index < BINS && localBins[index] > 0)
        InterlockedAdd(histogram[index], localBins[index]);
    if (index == 0)
    {
        InterlockedAdd(histogram[LUMA_SUM_INDEX], localLumaSum);
        InterlockedAdd(histogram[SAMPLE_COUNT_INDEX], localCount);
    }
}

float binCentreLog2(int bin)
{
    return MIN_LOG2 + (float(bin - FIRST_BIN) + 0.5) * ((MAX_LOG2 - MIN_LOG2) / float(MEASURED_BINS));
}

[shader("compute")]
[numthreads(1, 1, 1)]
void resolveMain(uint3 id : SV_DispatchThreadID)
{
    float total = 0.0;
    for (int i = FIRST_BIN; i < BINS; ++i)
        total += float(histogram[i]);
    float low = clamp(push.params.z, 0.0, 1.0);
    float high = clamp(push.params.w, low, 1.0);
    float cutLow = total * low;
    float cutHigh = total * high;
    float passed = 0.0;
    float sum = 0.0;
    float weight = 0.0;
    for (int i = FIRST_BIN; i < BINS; ++i)
    {
        float count = float(histogram[i]);
        float begin = passed;
        float end = passed + count;
        passed = end;
        float kept = max(0.0, min(end, cutHigh) - max(begin, cutLow));
        sum += kept * binCentreLog2(i);
        weight += kept;
    }
    float meanLog2 = weight > 0.0 ? sum / weight : binCentreLog2(FIRST_BIN);
    float count = float(histogram[SAMPLE_COUNT_INDEX]);
    float meanLuma = count > 0.0 ? float(histogram[LUMA_SUM_INDEX]) / MEAN_LUMA_SCALE / count : 0.0;
    // The level below which the highlight percentile of the counted samples lie (interpolated inside its bin).
    float highLog2 = MIN_LOG2;
    float cut = total * clamp(push.params2.x, 0.0, 1.0);
    float cumulative = 0.0;
    bool found = false;
    for (int i = FIRST_BIN; i < BINS && !found; ++i)
    {
        float count = float(histogram[i]);
        if (cumulative + count >= cut && count > 0.0)
        {
            highLog2 = MIN_LOG2 + (float(i - FIRST_BIN) + (cut - cumulative) / count) * ((MAX_LOG2 - MIN_LOG2) / float(MEASURED_BINS));
            found = true;
        }
        cumulative += count;
    }
    if (!found && total >= 1.0)
        highLog2 = MAX_LOG2;
    uint serial = uint(push.params.y);
    uint slot = serial % uint(SLOTS);
    results[slot * 2] = uint4(asuint(meanLog2), asuint(meanLuma), asuint(weight), serial & SERIAL_MASK);
    results[slot * 2 + 1] = uint4(asuint(highLog2), 0, 0, 0);
}
