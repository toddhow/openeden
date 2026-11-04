// The Slang twin of lib/material/terms.glsl (task 0072): the material's ambient, diffuse and emissive terms times the
// light. Include after lib/view/scene.slang; the includer declares `colorMode` (and `material` with MATERIAL) first.
// PARTICLE (task 0072, stage h, toddhow's decision 2026-11-04, "keep the original per particle color but do it in
// slang"): this twin DIFFERS from the GLSL on purpose. A particle's colour and alpha over its life (the simulation
// pass writes them to each particle, the vertex stage passes them on) are the vertex colour, and colour mode 0, which
// every particle pipeline has today (no NiVertexColorProperty is read for them), takes ambient, diffuse and alpha
// from it, as OpenMW 0.51 did: nifloader.cpp's handleParticleSystem passed hasVertexColors = true to
// applyDrawableProperties, which makes AMBIENT_AND_DIFFUSE the default, and files/shaders/compatibility/objects.frag
// multiplied the fragment's alpha by the vertex alpha (442eec6e2f). A particle whose NiVertexColorProperty says
// "ignore" also reads 0 here and so differs from 0.51; the GLSL ignores the colour of every particle.
// Change both together. Stage f: colorMode is a specialization constant in the object shader, so it is switched on,
// never compared (a comparison with a literal is an OpSpecConstantOp, files/shaders/CLAUDE.md); the terrain's plain
// constant folds the same way.
#ifndef LIB_MATERIAL_TERMS_SLANG
#define LIB_MATERIAL_TERMS_SLANG

#include "lib/material/materialsettings.slang"
#include "lib/view/linear.slang"

float4 getDiffuse(float4 vertexColor)
{
    switch (colorMode)
    {
#ifdef PARTICLE
        case 0: // see the header: a particle's colour is its vertex colour unless the NIF says otherwise
#endif
        case 2:
            return float4(toLinearIfView(vertexColor.xyz), vertexColor.w);
        default:
            break;
    }
#ifdef MATERIAL
    return float4(toLinearIfView(material.diffuse.xyz), material.diffuse.w);
#else
    return float4(1);
#endif
}
float3 getAmbient(float4 vertexColor)
{
    switch (colorMode)
    {
#ifdef PARTICLE
        case 0:
#endif
        case 2:
            return toLinearIfView(vertexColor.xyz);
        default:
            break;
    }
#ifdef MATERIAL
    return toLinearIfView(material.ambient.xyz);
#else
    return float3(1);
#endif
}
float3 getEmission(float4 vertexColor)
{
    switch (colorMode)
    {
        case 1:
            return toLinearIfView(vertexColor.xyz);
        default:
            break;
    }
#ifdef MATERIAL
    return toLinearIfView(material.emissive.xyz);
#else
    return float3(0);
#endif
}

float4 materialTermsAmbient(float3 ambientLight, float3 lightDiffuse, float4 vertexColor)
{
    switch (colorMode)
    {
        case 3:
            return float4(toLinearIfView(vertexColor.xyz), vertexColor.w);
        default:
            break;
    }
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
    switch (colorMode)
    {
        case 3:
            return float4(toLinearIfView(vertexColor.xyz), vertexColor.w);
        default:
            break;
    }
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
    switch (colorMode)
    {
        case 3:
            return float4(toLinearIfView(vertexColor.xyz), vertexColor.w);
        default:
            break;
    }
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
