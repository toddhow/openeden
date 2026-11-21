struct FrameArgs
{
    mat4 worldOffset;
    mat4 emitMatrix;
    ivec4 emitCount;
    vec4 time;
    // OpenEden: wrap-around for weather particles (OpenMW's WrapAroundOperator). wrapMatrix takes particle space to
    // world axes centred on the camera, wrapInverse back; wrapRange.xyz is the box size, wrapRange.w > 0 turns it on.
    mat4 wrapMatrix;
    mat4 wrapInverse;
    vec4 wrapRange;
};

struct EmitArgs
{
    vec4 offsetRandom;
    vec4 direction;
    vec4 velocityLifetime;
};

struct SizeArgs
{
    float growTime;
    float fadeTime;
    float defaultSize;
    float dummy;
};

struct GravityArgs
{
    vec4 positionDecay;
    vec4 directionForce;
};

struct CollideArgs
{
    vec4 positionBounce;
    vec4 xVectorRadius;
    vec4 yVectorRadius;
    vec4 plane;
};

struct SimulateArgs
{
    EmitArgs emit;
    SizeArgs size;
    GravityArgs gravity;
    CollideArgs collide;
};
