#version 450
// A fullscreen triangle with texture coordinates 0 to 1 across the target, in Vulkan's top-left origin: the vertex
// stage of the tonemap, bloom, velocity and ambient occlusion passes (components/render). Task 0072, stage g: it was
// the same string in each of those four files; the Slang twin is post/fullscreen.slang.
layout(location=0) out vec2 texCoord;
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = position * 0.5 + 0.5;
}
