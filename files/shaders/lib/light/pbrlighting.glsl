// OpenEden (task 0027, R1): point lights with the microfacet specular. Include after lib/material/pbr.glsl and
// lib/light/pixellighting.glsl (which bring the light grid and the falloff).
// Sums the point lights at a pixel: the vanilla diffuse light (colour * N.L * the game's falloff, over the same light
// grid) and the microfacet specular. Negative lights add no specular, as OpenMW gave them none.
void pbrPointLights(vec2 screenCoord, vec3 normal, vec3 viewPos, float roughness, vec3 f0, out vec3 diffuse,
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
            diffuse += lightColor.xyz * lambert(lightDir, normal) * illumination;
            specular += max(lightColor.xyz, vec3(0)) * illumination
                * pbrSpecularLobe(normal, viewDir, lightDir, roughness, f0);
        }
    }
}
