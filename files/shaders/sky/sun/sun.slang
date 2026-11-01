// The sun, in Slang (task 0023, F1b). This is the proof that the Slang pipeline works: it must draw exactly what
// sky/sun/main.vert and sky/sun/main.frag draw, and it must use the same descriptor sets, bindings, push constants and
// interface locations, because the C++ side builds the pipeline layout by hand (research note 0005, section 5).
//
// Loaded in place of the GLSL pair only when [Rendering] slang shaders is on. The sun is always drawn with the
// MATERIAL and DIFFUSE_MAP modes (MWRender weather.cpp, CelestialBody::pipelineOptions), so both are unconditional here.
//
// Compiled with: -profile spirv_1_0 -matrix-layout-column-major, so matrices lie in memory as GLSL's mat4 does.

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
    float align_dummy1;
    float align_dummy2;
};
// lib/material/bindings.glsl: MATERIAL_BINDING = 1, DIFFUSE_UNIT = 0, both in TEXTURE_SET
[[vk::binding(1, 2)]] ConstantBuffer<Material> material;
[[vk::binding(0, 2)]] Sampler2D diffuseMap;

// lib/view/data.glsl (std140). The whole struct is declared so that the block has the size the C++ side uploads,
// although only skyColor is read here.
struct Scene
{
    float4x4 invProjection;
    float4x4 projection;
    float4x4 invView;
    float4 cascadeSplits;
    float4x4 viewToCascadeProj[4];
    float4 cascadeFrustumWidths;
    float4 lightDiffuse;
    float4 lightSpecular;
    float4 lightViewPos;
    float4 ambient;
    float4 fogColor;
    float4 skyColor; // .a = glareView
    float2 resolution;
    float fogStart;
    float fogScale;
    float time;
    float zNear;
    float zFar;
    float rainIntensity;
    float4 groundcoverPlayer;
    float4 groundcoverParams;
};
// lib/view/bindings.glsl: VIEW_SCENE_BINDING = 0, in VIEW_SET
[[vk::binding(0, 0)]] ConstantBuffer<Scene> scene;

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

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target
{
    float4 outColor = diffuseMap.Sample(input.texCoord);
    outColor.xyz *= material.emissive.xyz;
    outColor.a *= material.diffuse.a * scene.skyColor.a;
    if (outColor.a == 0)
        discard;
    return outColor;
}
