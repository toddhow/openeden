#include "lib/view/scene.glsl"
#include "lib/view/linear.glsl"

layout(location=0) in Inputs
{
#include "inout.glsl"
} frag_in;

layout(location=0) out vec4 outColor;

void main()
{
    // OpenEden (task 0025): the sky's gradient is mixed on encoded colours, as it was drawn, and then converted: a mix in
    // linear light is much brighter in the middle of the gradient.
    outColor = vec4(toLinearIfView(mix(toEncodedIfView(scene.fogColor.xyz), toEncodedIfView(scene.skyColor.xyz), frag_in.fade)), 1.0);
}

