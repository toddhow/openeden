// The Slang twin of sky/clouds/inout.glsl (task 0072): what the clouds' vertex stage passes to the fragment stage, at
// the locations the GLSL block gives its members (one after the other).
#ifndef SKY_CLOUDS_INOUT_SLANG
#define SKY_CLOUDS_INOUT_SLANG

#ifdef TEXCOORD
#define CLOUDS_TEXCOORD [[vk::location(1)]] float2 texCoord : V_TEXCOORD0;
#define CLOUDS_DOME_LOCATION 2
#else
#define CLOUDS_TEXCOORD
#define CLOUDS_DOME_LOCATION 1
#endif

// color; texCoord with TEXCOORD; OpenEden (task 0038): the vertex in the dome's own space, and the direction to the
// sun in it, for the lit look.
#define CLOUDS_INOUT                                                                                                   \
    [[vk::location(0)]] float4 color : V_COLOR;                                                                        \
    CLOUDS_TEXCOORD                                                                                                    \
    [[vk::location(CLOUDS_DOME_LOCATION)]] float3 domeDir : V_DOMEDIR;                                                 \
    [[vk::location(CLOUDS_DOME_LOCATION + 1)]] float3 sunLocal : V_SUNLOCAL;

#endif
