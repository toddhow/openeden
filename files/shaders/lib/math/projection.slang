// The Slang twin of lib/math/projection.glsl (task 0072): perspective products that skip the identity components.
// GLSL's lhs[c][r] (column c, row r) is m[r][c] here (-matrix-layout-column-major). Change both together.
#ifndef LIB_MATH_PROJECTION_SLANG
#define LIB_MATH_PROJECTION_SLANG

float4 mulInvPerspective(float4x4 m, float4 rhs)
{
    return float4(m[0][0] * rhs[0], m[1][1] * rhs[1], m[2][3], m[3][2] * rhs[2] + m[3][3]);
}

float4 mulPerspective(float4x4 m, float4 rhs)
{
    return float4(m[0][0] * rhs[0], m[1][1] * rhs[1], m[2][2] * rhs[2] + m[2][3], m[3][2] * rhs[2]);
}

#endif
