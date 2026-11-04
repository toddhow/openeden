// The Slang twin of lib/particle/data.glsl and util.glsl (task 0072, stage f): a particle of the simulation buffer
// (std430). Change both together.
#ifndef LIB_PARTICLE_DATA_SLANG
#define LIB_PARTICLE_DATA_SLANG

struct Particle
{
    float4 positionSize;
    float4 color;
    float4 velocityAge;
    float4 maxAge;
};

bool isDead(Particle p)
{
    return p.velocityAge.w >= p.maxAge.x;
}

#endif
