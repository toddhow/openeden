struct Material
{
    vec4 ambient;
    vec4 diffuse;
    vec4 emissive;
    vec4 specular;
    float shininess;
    float alphaTestCutoff;
    // OpenEden (task 0027): 1 where the surface is marked as skin; else 0.
    float subsurface;
    float align_dummy2;
    // roughness, metalness, reflectance, occlusion. Roughness and reflectance below zero mean "derive them" (tier 1).
    vec4 pbr;
};
