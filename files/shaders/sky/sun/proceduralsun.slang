// The procedural sun (task 0047), in Slang (decision 0009: new shaders are Slang). It draws a computed disc and glow in
// place of the texture tx_sun_05.dds, on the same quad, with the same descriptor sets, bindings, push constants and
// interface locations as sun.slang (the C++ side builds the pipeline layout by hand). The picture binding is declared
// by nobody here: this shader does not read it, though it stays bound.
//
// The maths is Render::Sun in components/render/sunmath.hpp; the constants below repeat it (a test reads them back),
// change both together. r is the pixel's distance from the quad's centre in units of the quad's half width.
//
// Parameters travel in the sun's own material (nothing new is bound): emissive.xyz the sun colour (display encoded, as
// the textured sun's), diffuse.a the weather's alpha, pbr.x the HDR intensity, pbr.y the glow scale, pbr.z the disc
// radius. Loaded only when [Rendering] procedural sun is on (and hdr).
//
// Compiled with: -profile spirv_1_0 -matrix-layout-column-major, so matrices lie in memory as GLSL's mat4 does.

static const float kLimbCoefficient = 0.6;
static const float kTightAmount = 0.5;
static const float kTightWidth = 1.2;
static const float kWideAmount = 0.15;
static const float kWideWidth = 6.0;
static const float kGlowGain = 0.12;
static const float kWindowStart = 0.5;
static const float kEdgeSoftness = 0.04;

// lib/sets.glsl
static const int VIEW_SET = 0;
static const int TEXTURE_SET = 2;

// lib/pushconstants.glsl
struct PushConstants
{
    float4x4 projection;
    float4x4 modelview;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// lib/material/data.glsl (std140)
struct Material
{
    float4 ambient;
    float4 diffuse;
    float4 emissive;
    float4 specular;
    float shininess;
    float alphaTestCutoff;
    float subsurface;
    float align_dummy2;
    float4 pbr;
};
// lib/material/bindings.glsl: MATERIAL_BINDING = 1 in TEXTURE_SET (DIFFUSE_UNIT = 0 is not read here)
[[vk::binding(1, 2)]] ConstantBuffer<Material> material;

// The scene uniform: one declaration for every shader (lib/view/data.slang, lib/view/scene.slang).
import lib.view.scene;

struct VertexOutput
{
    // lib/inout_texcoord.glsl, location 0
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(uint vertexIndex : SV_VulkanVertexID)
{
    VertexOutput output;
    output.texCoord = float2((vertexIndex << 1) & 2, vertexIndex & 2) * 0.5;
    float3 vertex = float3(output.texCoord - 0.5, 0.0);
    float4 position = mul(pc.projection, mul(pc.modelview, float4(vertex, 1.0)));
    // vsgopenmw-reverse-depth: farDepth + epsilon, as in the GLSL
    position.z = 0.001;
    output.position = position;
    return output;
}

struct FragmentInput
{
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

float smoothStep01(float e0, float e1, float x)
{
    float t = saturate((x - e0) / max(e1 - e0, 1e-6));
    return t * t * (3.0 - 2.0 * t);
}

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target
{
    float2 p = (input.texCoord - 0.5) * 2.0;
    float r = length(p);
    float radius = max(material.pbr.z, 0.001);
    float x = r / radius;

    // The disc: limb darkening (a darker rim) and a soft edge one pixel wider than its own softness.
    float softness = kEdgeSoftness * radius + fwidth(r);
    float coverage = 1.0 - smoothStep01(radius - softness, radius + softness, r);
    // The rim's value is kept out to the end of the soft edge (the coverage ramp reaches past the radius), so that the
    // edge is a ramp and not a step.
    float xc = min(x, 1.0);
    float limb = 1.0 - kLimbCoefficient * (1.0 - sqrt(max(0.0, 1.0 - xc * xc)));

    // The glow: a tight bright falloff and a wide faint one, faded to nothing before the quad's edge.
    float shape = kTightAmount / (1.0 + (x / kTightWidth) * (x / kTightWidth)) + kWideAmount * exp(-x / kWideWidth);
    float glow = max(0.0, material.pbr.y * kGlowGain * shape * (1.0 - smoothStep01(kWindowStart, 1.0, r)));

    float brightness = coverage * limb + glow;
    float alpha = saturate(brightness);
    float scale = brightness > 0.0 ? material.pbr.x * max(brightness, 1.0) : 0.0;

    float3 colour = material.emissive.xyz;
    // OpenEden (task 0025): the linear view, marked by scene.fogColor.w, as lib/view/linear.glsl (sign preserving).
    if (scene.fogColor.w > 1.5)
        colour = sign(colour) * pow(abs(colour), float3(2.2));
    float outAlpha = alpha * material.diffuse.a * scene.skyColor.a;
    if (outAlpha == 0)
        discard;
    return float4(colour * scale, outAlpha);
}
