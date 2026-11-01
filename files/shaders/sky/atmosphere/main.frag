#include "lib/view/scene.glsl"
#include "lib/view/linear.glsl"
#include "lib/view/fog.glsl"

layout(location=0) in Inputs
{
#include "inout.glsl"
} frag_in;

layout(location=0) out vec4 outColor;

void main()
{
    // OpenEden (task 0025): the sky's gradient is mixed on encoded colours, as it was drawn, and then converted: a mix in
    // linear light is much brighter in the middle of the gradient.
    vec3 horizon = toEncodedIfView(scene.fogColor.xyz);
    vec3 colour = mix(horizon, toEncodedIfView(scene.skyColor.xyz), frag_in.fade);
    outColor = vec4(toLinearIfView(colour), 1.0);
    // OpenEden (task 0040): the sun's glow in haze, over the whole dome (the fog of objects and terrain has it too,
    // lib/view/fog.glsl): the haze round the sun is lit most where the dome is lit most, so the glow is added to the
    // dome's own colour, in linear light, and brightness follows the dome's. The direction comes from the vertices.
    if (cSunGlow)
    {
        vec3 glow = sunGlowColour(normalize(frag_in.viewDir), outColor.xyz);
        outColor.xyz = cSunGlowDebug ? glow : outColor.xyz + glow;
    }
}
