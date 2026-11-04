// OpenEden: per-pixel lighting for normal and specular maps (task 0012, V4h), as OpenMW's doLighting with
// PER_PIXEL_LIGHTING at 442eec6e2f (files/shaders/lib/light/lighting.glsl), over vsgopenmw's light grid.
#include "equations.glsl"
#include "select.glsl"
#include "pointlights.glsl"

// OpenMW's calcSpecIntensity.
float specularIntensity(vec3 viewNormal, vec3 viewDir, float shininess, vec3 lightDir)
{
    if (dot(viewNormal, lightDir) > 0.0)
    {
        vec3 halfVec = normalize(lightDir - viewDir);
        float NdotH = max(dot(viewNormal, halfVec), 0.0);
        return pow(NdotH, shininess);
    }
    return 0.0;
}

// Sums the point lights' diffuse and specular light at a pixel. Negative lights add no specular, as OpenMW gave them
// none (SceneUtil::configureLight).
void pointLightsPerPixel(vec2 screenCoord, vec3 viewNormal, vec3 viewPos, float shininess, out vec3 diffuse,
    out vec3 specular)
{
    diffuse = vec3(0);
    specular = vec3(0);
    vec3 viewDir = normalize(viewPos);
    uint tileIndex = selectTile(screenCoord, viewPos.z);
    uint count = min(uint(pointLights[0].position.w), lightGrid[tileIndex].count);
    uint offset = lightGrid[tileIndex].offset;
    for (uint l=0; l<count; ++l)
    {
        PointLight light = pointLights[lightIndexList[offset+l]];
        vec4 lightColor = light.colorIntensity;
        vec3 lightVec = light.position.xyz - viewPos;
        float d = length(lightVec);
        if (d <= lightRange(lightColor.w))
        {
            vec3 lightDir = lightVec / d;
            float illumination = attenuate(lightColor.w, d);
#ifdef POINT_SHADOW
            // Task 0077: the shadow of this light, if it has a slot (lib/light/pointshadow.*).
            illumination *= pointLightShadow(lightIndexList[offset+l], viewPos);
#endif
            diffuse += lightColor.xyz * lambert(lightDir, viewNormal) * illumination;
            specular += max(lightColor.xyz, vec3(0)) * specularIntensity(viewNormal, viewDir, shininess, lightDir)
                * illumination;
        }
    }
}
