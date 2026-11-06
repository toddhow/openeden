// The sun's glare, fragment stage, in Slang (task 0072, stage b): the twin of sky/sunglare/main.frag, drawn with
// MATERIAL. It must declare what the GLSL declares (checked by SlangPermutations).
#include "lib/material/descriptors_frag.slang"
import lib.view.linear;
import lib.view.scene;

[shader("fragment")]
float4 fragMain() : SV_Target0
{
    float3 forward = float3(0, 0, -1);
    float3 sun = scene.lightViewPos.xyz;
    float angleRadians = acos(dot(forward, sun));
    float angleMaxRadians = material.emissive.a;
    float fade = 1.0 - min(1.0, angleRadians / angleMaxRadians);
    if (fade <= 0)
        discard;
    return float4(toLinearIfView(material.emissive.xyz), material.diffuse.a * fade);
}
