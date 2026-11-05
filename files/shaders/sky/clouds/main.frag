#pragma import_defines (COLOR, TEXCOORD, MATERIAL, DIFFUSE_MAP)

#include "lib/material/descriptors_frag.glsl"
#include "lib/view/scene.glsl"
#include "lib/view/linear.glsl"
#include "cloud.settings"
#include "lit.glsl"

layout(location=0) in Inputs
{
#include "inout.glsl"
} frag_in;

layout(location=0) out vec4 outColor;

void main()
{
    if (cVolumetricClouds && material.ambient.a > 1.5)
    {
        // Task 0042: the ray-marched clouds. The compute passes wrote the light scattered towards the eye (premultiplied)
        // and the transmittance for each pixel; the blend wants a colour and an opacity.
        vec4 volume = texture(diffuseMap, gl_FragCoord.xy / scene.resolution);
        float opacity = 1.0 - clamp(volume.a, 0.0, 1.0);
        outColor = vec4(volume.rgb / max(opacity, 1e-4), opacity);
        // The vertex alpha fades the dome's lowest rows into the horizon; the material alpha is the volume's share.
        // In horizon mode (ambient alpha 3, `volumetric clouds horizon`) the fade is not applied: the dome has been
        // lowered below the horizon and the volume's own haze does the fading.
        outColor.a *= (material.ambient.a > 2.5 ? 1.0 : frag_in.color.a) * material.diffuse.a;
        return;
    }
    vec4 cloudPicture = texture(diffuseMap, frag_in.texCoord);
    outColor = cloudPicture;
    outColor.a *= frag_in.color.a * material.diffuse.a;
    // OpenEden (task 0025): the tint was chosen for encoded colours, so it is added to the encoded fog colour and the
    // result converted.
    // OpenEden (task 0034): times the cloud material's colour, white (so no change) unless the atmosphere sets it.
    outColor.xyz = toLinearIfView(outColor.xyz * (toEncodedIfView(scene.fogColor.xyz) + vec3(0.13, 0.13, 0.13)) * material.diffuse.xyz);
    if (cCloudsLit)
    {
        // Task 0038: thick parts darker underneath, thin edges towards the sun glow. The light is linear, so this
        // comes after the conversion. material.specular.a is the weather's share of the effect (0 when off).
        vec3 sunWorld = mat3(scene.invView) * scene.lightViewPos.xyz;
        float lightLuminance = dot(scene.lightDiffuse.xyz, vec3(0.2126, 0.7152, 0.0722));
        float amount = cloudStrength(lightLuminance, material.specular.a);
        vec3 sunTint = scene.lightDiffuse.xyz / max(max(scene.lightDiffuse.x, max(scene.lightDiffuse.y, scene.lightDiffuse.z)), 1e-3);
        // A zero sun direction (no sun set yet) must not make a NaN that the amount of 0 cannot hide.
        float sunLength = length(frag_in.sunLocal);
        float cosTheta = sunLength > 1e-4 ? dot(normalize(frag_in.domeDir), frag_in.sunLocal / sunLength) : 0.0;
        outColor.xyz *= cloudLitFactor(cloudThickness(cloudPicture.xyz, cloudPicture.a), cosTheta, sunWorld.z, amount, sunTint);
    }
    // ease transition between clear color and atmosphere/clouds
    //outColor = mix(vec4(scene.fogColor.xyz, outColor.a), outColor, frag_in.color.a);
}
