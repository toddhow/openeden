// The Slang twin of lib/material/data.glsl (task 0072): the material uniform's layout (std140). Change both together;
// the meaning of each field is in the GLSL.
#ifndef LIB_MATERIAL_DATA_SLANG
#define LIB_MATERIAL_DATA_SLANG

struct Material
{
    float4 ambient;
    float4 diffuse;
    float4 emissive;
    float4 specular;
    float shininess;
    float alphaTestCutoff;
    float subsurface;
    float align_dummy2;
    float4 pbr;
};

#endif
