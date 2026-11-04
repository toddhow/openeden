// The Slang twin of lib/math/billboard.glsl (task 0072, stage f): the model-view matrix with its rotation and scale
// replaced by the identity, so that the geometry faces the camera. GLSL's m[c][r] is m[r][c] here. Change both together.
#ifndef LIB_MATH_BILLBOARD_SLANG
#define LIB_MATH_BILLBOARD_SLANG

float4x4 billboardModelView(float4x4 m)
{
    float4x4 modelView = m;
    modelView[0][0] = 1.0;
    modelView[1][0] = 0.0;
    modelView[2][0] = 0.0;
    modelView[0][1] = 0.0;
    modelView[1][1] = 1.0;
    modelView[2][1] = 0.0;
    modelView[0][2] = 0.0;
    modelView[1][2] = 0.0;
    modelView[2][2] = 1.0;
    return modelView;
}

#endif
