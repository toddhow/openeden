// The particle simulation pass, in Slang (task 0072, stage c): the twin of comp/particle/simulate.comp (no defines;
// six specialization constants, set by Pipeline::Particle::create). It must declare what the GLSL declares, the local
// size included (checked by SlangPermutations). The work group size is that of comp/particle/workgroupsize.glsl and
// the structs are those of comp/particle/data.glsl and lib/particle/data.glsl, which the C++ side uses; change them
// together. GLSL's functions (inverse, mod, the bit casts) come from Slang's glsl module, so they compile to the
// same instructions; GLSLShaderStorageBuffer is a storage block that is not an array, as the GLSL's.
import glsl;

#include "lib/material/bindings.slang"
#include "lib/sets.slang"

static const int workGroupSizeX = 32;
static const int SIMULATE_ARGS_BINDING = PARTICLE_BINDING + 1;
static const int FRAME_ARGS_BINDING = PARTICLE_BINDING + 2;
static const int COLOR_CURVE_BINDING = PARTICLE_BINDING + 3;

[vk::constant_id(0)] const bool growFade = false;
[vk::constant_id(1)] const bool colorCurve = false;
[vk::constant_id(2)] const bool gravityPlane = false;
[vk::constant_id(3)] const bool gravityPoint = false;
[vk::constant_id(4)] const bool collidePlane = false;
[vk::constant_id(5)] const bool worldSpace = false;

struct FrameArgs
{
    float4x4 worldOffset;
    float4x4 emitMatrix;
    int4 emitCount;
    float4 time;
    // OpenEden: wrap-around for weather particles (OpenMW's WrapAroundOperator); see comp/particle/data.glsl.
    float4x4 wrapMatrix;
    float4x4 wrapInverse;
    float4 wrapRange;
};

struct EmitArgs
{
    float4 offsetRandom;
    float4 direction;
    float4 velocityLifetime;
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
    float4 positionDecay;
    float4 directionForce;
};

struct CollideArgs
{
    float4 positionBounce;
    float4 xVectorRadius;
    float4 yVectorRadius;
    float4 plane;
};

struct SimulateArgs
{
    EmitArgs emit;
    SizeArgs size;
    GravityArgs gravity;
    CollideArgs collide;
};

// lib/particle/data.glsl
struct Particle
{
    float4 positionSize;
    float4 color;
    float4 velocityAge;
    float4 maxAge; // x
};

struct FrameBlock
{
    FrameArgs frameArgs;
};

[[vk::binding(PARTICLE_BINDING, TEXTURE_SET)]] RWStructuredBuffer<Particle> particles;
[[vk::binding(SIMULATE_ARGS_BINDING, TEXTURE_SET)]] ConstantBuffer<SimulateArgs> args;
[[vk::binding(FRAME_ARGS_BINDING, TEXTURE_SET)]] GLSLShaderStorageBuffer<FrameBlock> frame;
[[vk::binding(COLOR_CURVE_BINDING, TEXTURE_SET)]] Sampler1D colorLUT;

// lib/particle/util.glsl
bool isDead(Particle p)
{
    return p.velocityAge.w >= p.maxAge.x;
}

// lib/math/rotate.glsl: GLSL's mat3 takes columns, so the rows given here are transposed.
float3x3 rotate(float angle, float3 vec)
{
    float c = cos(angle);
    float s = sin(angle);
    float3 v_one_minus_c = vec * (1 - c);
    float3 vs = vec * s;
    return transpose(float3x3(v_one_minus_c * vec.xxx + float3(c, vs.z, -vs.y),
        v_one_minus_c * vec.yyy + float3(-vs.z, c, vs.x), v_one_minus_c * vec.zzz + float3(vs.y, -vs.x, c)));
}

// lib/math/plane.glsl
float4 makePlane(float3 norm, float3 point)
{
    return float4(norm, -dot(norm, point));
}

float distanceToPlane(float4 plane, float3 point)
{
    return dot(plane, float4(point, 1));
}

// lib/math/random.glsl: Bob Jenkins' One-At-A-Time hashing algorithm.
uint hashOne(uint x)
{
    x += (x << 10u);
    x ^= (x >> 6u);
    x += (x << 3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

uint hashTwo(uint2 v)
{
    return hashOne(v.x ^ hashOne(v.y));
}

// A float in [0:1) from the low 23 bits.
float floatConstruct(uint m)
{
    const uint ieeeMantissa = 0x007FFFFFu;
    const uint ieeeOne = 0x3F800000u;
    m &= ieeeMantissa;
    m |= ieeeOne;
    float f = uintBitsToFloat(m);
    return f - 1.0;
}

float random(float x)
{
    return floatConstruct(hashOne(floatBitsToUint(x)));
}

float random(float2 v)
{
    return floatConstruct(hashTwo(floatBitsToUint(v)));
}

float signedRandom(float2 v)
{
    return random(v) * 2 - 1;
}

float signedRandom(float x)
{
    return random(x) * 2 - 1;
}

[shader("compute")]
[numthreads(workGroupSizeX, 1, 1)]
void simulateMain(uint3 globalId : SV_DispatchThreadID)
{
    uint gid = globalId.x;
    float dt = frame.frameArgs.time.x;
    uint count;
    uint stride;
    particles.GetDimensions(count, stride);
    if (gid < count)
    {
        Particle p = particles[gid];
        p.velocityAge.w += dt;
        // Wrapped particles stay fixed in the world like world-space ones. Kept as two statements: conditions
        // combining specialization constants have miscompiled on this NVIDIA driver (apps/openmw/mwrender/CLAUDE.md).
        bool wrap = frame.frameArgs.wrapRange.w > 0.0;
        bool fixInWorld = wrap;
        if (worldSpace)
            fixInWorld = true;
        if (fixInWorld)
        {
            p.positionSize.xyz = mul(frame.frameArgs.worldOffset, float4(p.positionSize.xyz, 1)).xyz;
            p.velocityAge.xyz = mul(float4(p.velocityAge.xyz, 0), inverse(frame.frameArgs.worldOffset)).xyz;
        }
        if (!isDead(p))
        {
            p.positionSize.xyz += p.velocityAge.xyz * dt;
            if (wrap)
            {
                // OpenMW's WrapAroundOperator: keep each world axis within half the range of the camera.
                float3 range = frame.frameArgs.wrapRange.xyz;
                float3 halfRange = range * 0.5;
                float3 pos = mul(frame.frameArgs.wrapMatrix, float4(p.positionSize.xyz, 1)).xyz;
                for (int j = 0; j < 3; ++j)
                {
                    if (pos[j] < -halfRange[j])
                        pos[j] = halfRange[j] + mod(pos[j] - halfRange[j], range[j]) - range[j];
                    else if (pos[j] > halfRange[j])
                        pos[j] = mod(pos[j] + halfRange[j], range[j]) - halfRange[j];
                }
                p.positionSize.xyz = mul(frame.frameArgs.wrapInverse, float4(pos, 1)).xyz;
            }
        }
        else
        {
            // OpenEden: < rather than <=, which emitted one particle too many each frame, even with a count of 0.
            int emitted;
            InterlockedAdd(frame.frameArgs.emitCount.y, 1, emitted);
            if (emitted < frame.frameArgs.emitCount.x)
            {
                float rand1 = signedRandom(float2(frame.frameArgs.time.y, gid));
                float rand2 = signedRandom(rand1);
                float rand3 = signedRandom(rand2);
                p.positionSize.xyz = mul(frame.frameArgs.emitMatrix,
                    float4(args.emit.offsetRandom.x * rand1, args.emit.offsetRandom.y * rand2,
                        args.emit.offsetRandom.z * rand3, 1))
                                         .xyz;

                rand1 = signedRandom(rand3);
                rand2 = signedRandom(rand1);
                rand3 = signedRandom(rand2);
                float speed = args.emit.velocityLifetime.x + args.emit.velocityLifetime.y * rand1;
                float hAngle = args.emit.direction.x + args.emit.direction.y * rand2;
                float vAngle = args.emit.direction.z + args.emit.direction.w * rand3;
                float3x3 rotateH = rotate(hAngle, float3(0, 0, 1));
                float3x3 rotateV = rotate(vAngle, float3(0, 1, 0));
                float4x4 emitNormalMatrix = inverse(transpose(frame.frameArgs.emitMatrix));
                float3 dir = mul(mul(rotateV, rotateH), float3(0, 0, 1));
                dir = mul(emitNormalMatrix, float4(dir, 0)).xyz;
                p.velocityAge = float4(dir * speed, 0);
                p.maxAge.x = args.emit.velocityLifetime.z + args.emit.velocityLifetime.w * random(rand3);
            }
            else
                return;
        }
        if (growFade)
        {
            float age = p.velocityAge.w;
            if (age < args.size.growTime)
                p.positionSize.w = args.size.defaultSize * age / args.size.growTime;
            float timeLeft = max(p.maxAge.x - age, 0.0001);
            if (timeLeft < args.size.fadeTime)
                p.positionSize.w = args.size.defaultSize * timeLeft / args.size.fadeTime;
        }
        if (colorCurve)
        {
            p.color = colorLUT.SampleLevel(p.velocityAge.w / p.maxAge.x, 0.0);
        }
        if (gravityPlane)
        {
            float4 plane = makePlane(args.gravity.directionForce.xyz, args.gravity.positionDecay.xyz);
            float dist = abs(distanceToPlane(plane, p.positionSize.xyz));
            float decayFactor = exp(args.gravity.positionDecay.w * dist);
            p.velocityAge.xyz += args.gravity.directionForce.xyz * args.gravity.directionForce.w * dt * decayFactor;
        }
        else if (gravityPoint)
        {
            float3 diff = args.gravity.positionDecay.xyz - p.positionSize.xyz;
            float dist = length(diff);
            float decayFactor = exp(args.gravity.positionDecay.w * dist);
            float3 dir = diff / dist;
            p.velocityAge.xyz += dir * args.gravity.directionForce.w * dt * decayFactor;
        }
        if (collidePlane)
        {
            float velDotProduct = dot(p.velocityAge.xyz, args.collide.plane.xyz);
            if (velDotProduct > 0 && distanceToPlane(args.collide.plane, p.positionSize.xyz) > 0)
            {
                float3 relativePos = p.positionSize.xyz - args.collide.positionBounce.xyz;
                float xDotProduct = dot(relativePos, args.collide.xVectorRadius.xyz);
                float yDotProduct = dot(relativePos, args.collide.yVectorRadius.xyz);
                if (-args.collide.xVectorRadius.w <= xDotProduct && args.collide.xVectorRadius.w >= xDotProduct
                    && -args.collide.yVectorRadius.w <= yDotProduct && args.collide.yVectorRadius.w >= yDotProduct)
                {
                    p.velocityAge.xyz = args.collide.positionBounce.w
                        * (p.velocityAge.xyz - args.collide.plane.xyz * (2 * velDotProduct));
                }
            }
        }
        particles[gid] = p;
    }
}
