#version 450
// Draws a post-processing result, in OpenGL's origin, to the screen (apps/openmw/mwrender/postprocess.cpp,
// PostProcess::createPresent, with multisampling). Task 0072, stage g: it was a string in postprocess.cpp; the Slang
// twin is post/present.slang.
layout(location=0) out vec2 texCoord;
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = vec2(position.x, -position.y) * 0.5 + 0.5;
}
