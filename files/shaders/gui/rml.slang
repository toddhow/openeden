// The RmlUi overlay in Slang (task 0111, stage U2): the Slang twin of gui/rml.vert and gui/rml.frag. It declares what
// the GLSL declares (the C++ side, components/ui_rml/renderinterface.cpp, builds the pipeline by hand): vertex inputs
// at locations 0 to 2 (position as two floats, premultiplied colour, texture coordinate), colour and texture
// coordinate passed at locations 1 and 2, the push constants of ui_rml/uigeometry.hpp (PushConstants: a clip matrix
// and RmlUi's translation, applied before the matrix) and the texture as a combined image sampler at set 0,
// binding 0. Checked by SlangPermutations and SlangInterfaceRecords in components-tests.
//
// Compiled by openeden_add_slang_permutations (files/shaders/CMakeLists.txt) with -matrix-layout-column-major; it
// has no defines.

struct PushConstants
{
    float4x4 matrix;
    float2 translation;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

struct VertexInput
{
    [[vk::location(0)]] float2 vertex : POSITION;
    [[vk::location(1)]] float4 color : COLOR;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
};

struct VertexOutput
{
    [[vk::location(1)]] float4 color : COLOR;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(pc.matrix, float4(input.vertex + pc.translation, 0.0, 1.0));
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}

[[vk::binding(0, 0)]] Sampler2D diffuseMap;

struct FragmentInput
{
    [[vk::location(1)]] float4 color : COLOR;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
};

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
    return diffuseMap.Sample(input.texCoord) * input.color;
}
