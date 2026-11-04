// The Slang twin of lib/util/quickstep.glsl (task 0072). Change both together.
#ifndef LIB_UTIL_QUICKSTEP_SLANG
#define LIB_UTIL_QUICKSTEP_SLANG

float quickstep(float x)
{
    x = clamp(x, 0.0, 1.0);
    x = 1.0 - x * x;
    x = 1.0 - x * x;
    return x;
}

#endif
