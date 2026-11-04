// The Slang twin of lib/math/distance.glsl (task 0072). Change both together.
#ifndef LIB_MATH_DISTANCE_SLANG
#define LIB_MATH_DISTANCE_SLANG

float distanceSquared(float2 a, float2 b)
{
    a -= b;
    return dot(a, a);
}

#endif
