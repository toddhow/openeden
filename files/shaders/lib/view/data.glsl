struct Scene
{
    mat4 invProjection;
    mat4 projection;
    mat4 invView;
    vec4 cascadeSplits;
    mat4 viewToCascadeProj[4];
    vec4 cascadeFrustumWidths;
    vec4 lightDiffuse;
    vec4 lightSpecular;
    vec4 lightViewPos;
    vec4 ambient;
    vec4 fogColor;
    vec4 skyColor; //.a=glareView;
    vec2 resolution;
    float fogStart;
    float fogScale;
    float time;
    float zNear;
    float zFar;
    float rainIntensity;
    // OpenEden (task 0012): groundcover's player position (xyz, world) and wind speed (w), and its fade-out distance
    // (x), stomp mode (y) and stomp intensity (z), as OpenMW's groundcover shader took them.
    vec4 groundcoverPlayer;
    vec4 groundcoverParams;
};
