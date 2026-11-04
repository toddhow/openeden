#version 450
// The vertex stage of every post-processing pass (apps/openmw/mwrender/postprocess.cpp, MWRender::PostProcess): a
// fullscreen triangle with OpenMW's omw_TexCoord, 0 to 1 across the screen. The images have OpenGL's origin (see
// PostProcess), so this is not flipped. Task 0072, stage g: it was a string in postprocess.cpp. It has no Slang twin: its
// only partner is a pass's fragment stage translated from an .omwfx file, which stays GLSL (decision 0009), and a
// pipeline takes the twins of all its stages or none.
layout(location=0) out vec2 omw_TexCoord;
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(position, 0.0, 1.0);
    omw_TexCoord = position * 0.5 + 0.5;
}
