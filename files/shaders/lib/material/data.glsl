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
    // OpenEden (task 0044): the True PBR set's roughness scale for a packed material map (0: none). Was padding.
    float align_dummy2;
    // roughness, metalness, reflectance, occlusion. Roughness and reflectance below zero mean "derive them" (tier 1).
    vec4 pbr;
};
