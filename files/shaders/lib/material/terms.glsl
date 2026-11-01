#include "material.settings"
// Needs lib/view/scene.glsl included first (linearView() reads scene).
#include "lib/view/linear.glsl"

vec4 getDiffuse(vec4 vertexColor)
{
    if (colorMode == 2)
        return vec4(toLinearIfView(vertexColor.xyz), vertexColor.w);
#ifdef MATERIAL
    return vec4(toLinearIfView(material.diffuse.xyz), material.diffuse.w);
#else
    return vec4(1);
#endif
}
vec3 getAmbient(vec4 vertexColor)
{
    if (colorMode == 2)
        return toLinearIfView(vertexColor.xyz);
#ifdef MATERIAL
    return toLinearIfView(material.ambient.xyz);
#else
    return vec3(1);
#endif
}
vec3 getEmission(vec4 vertexColor)
{
    if (colorMode == 1)
        return toLinearIfView(vertexColor.xyz);
#ifdef MATERIAL
    return toLinearIfView(material.emissive.xyz);
#else
    return vec3(0);
#endif
}

vec4 materialTerms(vec3 lightDiffuse, vec4 vertexColor /*, Material material, Scene scene, constexpr int colorMode*/)
{
    // OpenEden: colour mode 3 draws the vertex colour unlit, alpha included (debug geometry, mwrender/debuggeometry).
    if (colorMode == 3)
        return vec4(toLinearIfView(vertexColor.xyz), vertexColor.w);
    vec3 ambient = scene.ambient.xyz * getAmbient(vertexColor);
    vec4 materialDiffuse = getDiffuse(vertexColor);
    vec3 diffuse = lightDiffuse * materialDiffuse.xyz;
    vec4 total = vec4(ambient + diffuse + getEmission(vertexColor), materialDiffuse.a);
    // In the linear view the float target keeps light above 1 for the tonemap, so it is not clamped.
    if (cClampLighting)
    {
        if (linearView())
            total = max(total, 0.0);
        else
            total = clamp(total, 0.0, 1.0);
    }
    else
        total = max(total, 0.0);
    return total;
}

// OpenEden (task 0027, R1): materialTerms with the ambient and the diffuse light scaled, for the metal tint, the energy
// conserving diffuse and the baked occlusion. Emission is not scaled. With both scales at 1 it is materialTerms.
vec4 materialTermsScaled(vec3 lightDiffuse, vec4 vertexColor, float ambientScale, float diffuseScale)
{
    if (colorMode == 3)
        return vec4(toLinearIfView(vertexColor.xyz), vertexColor.w);
    vec3 ambient = scene.ambient.xyz * getAmbient(vertexColor) * ambientScale;
    vec4 materialDiffuse = getDiffuse(vertexColor);
    vec3 diffuse = lightDiffuse * materialDiffuse.xyz * diffuseScale;
    vec4 total = vec4(ambient + diffuse + getEmission(vertexColor), materialDiffuse.a);
    if (cClampLighting)
    {
        if (linearView())
            total = max(total, 0.0);
        else
            total = clamp(total, 0.0, 1.0);
    }
    else
        total = max(total, 0.0);
    return total;
}
