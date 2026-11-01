#include "fog.settings"
#include "lib/view/linear.glsl"

// openeden-vsg: OpenMW's [Fog] radial fog and exponential fog (compatibility/fog.glsl at cf3dbd3aa7; task 0012).
vec3 applyFog(/*Scene scene, */vec3 src, float distance)
{
    float fogValue;
    if (cExponentialFog)
    {
        float fogEnd = scene.fogStart + (scene.fogScale > 0.0 ? 1.0 / scene.fogScale : 0.0);
        fogValue = 1.0 - exp(-2.0 * max(0.0, distance - scene.fogStart / 2.0) / max(fogEnd - scene.fogStart / 2.0, 1e-6));
    }
    else
        fogValue = clamp((distance - scene.fogStart) * scene.fogScale, 0.0, 1.0);
    // OpenEden (task 0025): in the linear view the mix is made on encoded colours and converted, so that fog keeps the
    // density it was tuned for; a mix in linear light hazes much more.
    if (linearView())
        return toLinear(mix(toEncoded(src), toEncoded(scene.fogColor.xyz), fogValue));
    return mix(src, scene.fogColor.xyz, fogValue);
}

// Distance from the eye (radial fog) or along the view direction (OpenMW's default).
vec3 applyFog(vec3 src, vec3 viewPos)
{
    return applyFog(src, cRadialFog ? length(viewPos) : abs(viewPos.z));
}
