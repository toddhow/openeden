#pragma import_defines(GLOW_MAP, DIFFUSE_MAP, MATERIAL)

#include "lib/material/descriptors_frag.glsl"
#include "lib/view/scene.glsl"
#include "lib/view/linear.glsl"
#include "lib/view/linear.glsl"

layout(location=0) in Inputs
{
#include "lib/inout_texcoord.glsl"
} frag_in;

layout(location=0) out vec4 outColor;

void main()
{
    vec4 phase = texture(glowMap, frag_in.texCoord);
    vec4 mask = texture(diffuseMap, frag_in.texCoord);
    vec4 blendedLayer = phase * material.emissive;
    // OpenEden (task 0025): the sum is of encoded colours, as it was chosen, and converted.
    outColor = vec4(toLinearIfView(blendedLayer.xyz + toEncodedIfView(scene.skyColor.xyz)),
        scene.skyColor.a * material.diffuse.a * mask.a);
}

