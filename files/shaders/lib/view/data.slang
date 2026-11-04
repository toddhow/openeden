// The Slang twin of lib/view/data.glsl (task 0072): the scene uniform's layout (std140). Change both together; the
// comments on each field are in the GLSL. The struct must not grow past 640 bytes (apps/openmw/mwrender/CLAUDE.md).
#ifndef LIB_VIEW_DATA_SLANG
#define LIB_VIEW_DATA_SLANG

struct Scene
{
    float4x4 invProjection;
    float4x4 projection;
    float4x4 invView;
    float4 cascadeSplits;
    float4x4 viewToCascadeProj[4];
    float4 cascadeFrustumWidths;
    float4 lightDiffuse;
    float4 lightSpecular;
    float4 lightViewPos;
    float4 ambient;
    float4 fogColor;
    float4 skyColor; // .a = glareView
    float2 resolution;
    float fogStart;
    float fogScale;
    float time;
    float zNear;
    float zFar;
    float rainIntensity;
    float4 groundcoverPlayer;
    float4 groundcoverParams;
};

#endif
