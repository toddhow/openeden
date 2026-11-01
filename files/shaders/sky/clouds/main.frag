#pragma import_defines (COLOR, TEXCOORD, MATERIAL, DIFFUSE_MAP)

#include "lib/material/descriptors_frag.glsl"
#include "lib/view/scene.glsl"
#include "lib/view/linear.glsl"

layout(location=0) in Inputs
{
#include "inout.glsl"
} frag_in;

layout(location=0) out vec4 outColor;

void main()
{
    outColor = texture(diffuseMap, frag_in.texCoord);
    outColor.a *= frag_in.color.a * material.diffuse.a;
    // OpenEden (task 0025): the tint was chosen for encoded colours, so it is added to the encoded fog colour and the
    // result converted.
    outColor.xyz = toLinearIfView(outColor.xyz * (toEncodedIfView(scene.fogColor.xyz) + vec3(0.13, 0.13, 0.13)));
    // ease transition between clear color and atmosphere/clouds
    //outColor = mix(vec4(scene.fogColor.xyz, outColor.a), outColor, frag_in.color.a);
}
