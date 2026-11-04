// The Slang twin of lib/material/terms.glsl (task 0072): the material's ambient, diffuse and emissive terms times the
// light. Include after lib/view/scene.slang; the includer declares `colorMode` (and `material` with MATERIAL) first.
// Change both together.
#ifndef LIB_MATERIAL_TERMS_SLANG
#define LIB_MATERIAL_TERMS_SLANG

#include "lib/material/materialsettings.slang"
#include "lib/view/linear.slang"

float4 getDiffuse(float4 vertexColor)
{
    if (colorMode == 2)
        return float4(toLinearIfView(vertexColor.xyz), vertexColor.w);
#ifdef MATERIAL
    return float4(toLinearIfView(material.diffuse.xyz), material.diffuse.w);
#else
    return float4(1);
#endif
}
float3 getAmbient(float4 vertexColor)
{
    if (colorMode == 2)
        return toLinearIfView(vertexColor.xyz);
#ifdef MATERIAL
    return toLinearIfView(material.ambient.xyz);
#else
    return float3(1);
#endif
}
float3 getEmission(float4 vertexColor)
{
    if (colorMode == 1)
        return toLinearIfView(vertexColor.xyz);
#ifdef MATERIAL
    return toLinearIfView(material.emissive.xyz);
#else
    return float3(0);
#endif
}

float4 materialTermsAmbient(float3 ambientLight, float3 lightDiffuse, float4 vertexColor)
{
    if (colorMode == 3)
        return float4(toLinearIfView(vertexColor.xyz), vertexColor.w);
    float3 ambient = ambientLight * getAmbient(vertexColor);
    float4 materialDiffuse = getDiffuse(vertexColor);
    float3 diffuse = lightDiffuse * materialDiffuse.xyz;
    float4 total = float4(ambient + diffuse + getEmission(vertexColor), materialDiffuse.a);
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

float4 materialTerms(float3 lightDiffuse, float4 vertexColor)
{
    if (colorMode == 3)
        return float4(toLinearIfView(vertexColor.xyz), vertexColor.w);
    float3 ambient = scene.ambient.xyz * getAmbient(vertexColor);
    float4 materialDiffuse = getDiffuse(vertexColor);
    float3 diffuse = lightDiffuse * materialDiffuse.xyz;
    float4 total = float4(ambient + diffuse + getEmission(vertexColor), materialDiffuse.a);
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

float4 materialTermsScaled(
    float3 ambientLight, float3 lightDiffuse, float4 vertexColor, float ambientScale, float diffuseScale)
{
    if (colorMode == 3)
        return float4(toLinearIfView(vertexColor.xyz), vertexColor.w);
    float3 ambient = ambientLight * getAmbient(vertexColor) * ambientScale;
    float4 materialDiffuse = getDiffuse(vertexColor);
    float3 diffuse = lightDiffuse * materialDiffuse.xyz * diffuseScale;
    float4 total = float4(ambient + diffuse + getEmission(vertexColor), materialDiffuse.a);
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

#endif
