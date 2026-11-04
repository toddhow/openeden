// The GUI (MyGUI) in Slang (task 0072, stage a): the Slang twin of gui/gui.vert and gui/gui.frag. It must declare
// what the GLSL declares (the C++ side, components/vsgadapters/mygui/render.cpp, builds the pipeline by hand):
// vertex inputs at locations 0 to 2, the colour and texture coordinate passed at locations 1 and 2, and the texture
// as a combined image sampler at set 0, binding 0. Checked by SlangPermutations in components-tests.
//
// Compiled by openeden_add_slang_permutations (files/shaders/CMakeLists.txt) with -matrix-layout-column-major; it has
// no matrices and no defines.

struct VertexInput
{
    [[vk::location(0)]] float3 vertex : POSITION;
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
    output.position = float4(input.vertex.x, -input.vertex.y, 0.0, 1.0);
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
