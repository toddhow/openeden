// The sky's dome (the atmosphere), in Slang (task 0072, stage b): the twin of sky/atmosphere/main.vert and main.frag.
// It must declare what the GLSL declares (checked by SlangPermutations); neither stage imports a define. The
// fog.settings values come as specialization constants (lib/view/fogsettings.slang).
import lib.pushconstants;
import lib.view.fog;
import lib.view.linear;
import lib.view.scene;

struct VertexInput
{
    [[vk::location(0)]] float3 inVertex : A_POSITION;
};

// sky/atmosphere/inout.glsl
struct VertexOutput
{
    [[vk::location(0)]] float fade : FADE;
    [[vk::location(1)]] float3 viewDir : VIEWDIR;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input, uint vertexIndex : SV_VulkanVertexID)
{
    VertexOutput output;
    output.position = mul(pc.projection, mul(pc.modelview, float4(input.inVertex, 1.0)));
    // OpenEden (task 0040): the dome is centred on the camera, so a vertex's direction from the eye is its position
    // turned into the view's frame (for the sun's glow).
    output.viewDir = mul(pc.modelview, float4(input.inVertex, 1.0)).xyz;
    // this is a cylinder, so every second vertex belongs to the bottom-most row
    output.fade = vertexIndex % 2 == 1 ? 0.0 : 1.0;
    return output;
}

struct FragmentInput
{
    [[vk::location(0)]] float fade : FADE;
    [[vk::location(1)]] float3 viewDir : VIEWDIR;
};

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
    // OpenEden (task 0025): the gradient is mixed on encoded colours, as it was drawn, and then converted.
    float3 horizon = toEncodedIfView(scene.fogColor.xyz);
    float3 colour = lerp(horizon, toEncodedIfView(scene.skyColor.xyz), input.fade);
    float4 outColor = float4(toLinearIfView(colour), 1.0);
    // OpenEden (task 0040): the sun's glow in haze, over the whole dome.
    if (cSunGlow)
    {
        float3 glow = sunGlowColour(normalize(input.viewDir), outColor.xyz);
        outColor.xyz = cSunGlowDebug ? glow : outColor.xyz + glow;
    }
    return outColor;
}
