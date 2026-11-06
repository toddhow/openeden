// The sky's clouds, fragment stage, in Slang (task 0072, stage b): the twin of sky/clouds/main.frag. It must declare
// what the GLSL declares (checked by SlangPermutations). The cloud.settings values come as specialization constants.
#include "lib/material/bindings.slang"
#include "lib/material/descriptors_frag.slang"
import lib.view.linear;
import lib.view.scene;
import sky.clouds.cloudsettings;
#include "sky/clouds/inout.slang"
import sky.clouds.lit;

struct FragmentInput
{
    CLOUDS_INOUT
    float4 fragCoord : SV_Position;
};

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
    float4 outColor;
    if (cVolumetricClouds && material.ambient.a > 1.5)
    {
        // Task 0042: the ray-marched clouds' scattered light (premultiplied) and transmittance for this pixel.
        float4 volume = diffuseMap.Sample(input.fragCoord.xy / scene.resolution);
        float opacity = 1.0 - clamp(volume.a, 0.0, 1.0);
        outColor = float4(volume.rgb / max(opacity, 1e-4), opacity);
        // Horizon mode (ambient alpha 3): the dome's vertex fade is not applied (see main.frag).
        outColor.a *= (material.ambient.a > 2.5 ? 1.0 : input.color.a) * material.diffuse.a;
        return outColor;
    }
    float4 cloudPicture = diffuseMap.Sample(input.texCoord);
    outColor = cloudPicture;
    outColor.a *= input.color.a * material.diffuse.a;
    // OpenEden (task 0025, 0034): the tint is added to the encoded fog colour, times the cloud material's colour.
    outColor.xyz = toLinearIfView(
        outColor.xyz * (toEncodedIfView(scene.fogColor.xyz) + float3(0.13, 0.13, 0.13)) * material.diffuse.xyz);
    if (cCloudsLit)
    {
        // Task 0038: thick parts darker underneath, thin edges towards the sun glow.
        float3 sunWorld = mul((float3x3)scene.invView, scene.lightViewPos.xyz);
        float lightLuminance = dot(scene.lightDiffuse.xyz, float3(0.2126, 0.7152, 0.0722));
        float amount = cloudStrength(lightLuminance, material.specular.a);
        float3 sunTint = scene.lightDiffuse.xyz
            / max(max(scene.lightDiffuse.x, max(scene.lightDiffuse.y, scene.lightDiffuse.z)), 1e-3);
        float sunLength = length(input.sunLocal);
        float cosTheta = sunLength > 1e-4 ? dot(normalize(input.domeDir), input.sunLocal / sunLength) : 0.0;
        outColor.xyz
            *= cloudLitFactor(cloudThickness(cloudPicture.xyz, cloudPicture.a), cosTheta, sunWorld.z, amount, sunTint);
    }
    return outColor;
}
