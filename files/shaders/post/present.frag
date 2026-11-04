#version 450
// Draws a post-processing result to the screen (PostProcess::createPresent). Task 0072, stage g: it was a string in
// postprocess.cpp; the Slang twin is post/present.slang.
layout(set=0, binding=0) uniform sampler2D result;
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 color;
void main()
{
    color = vec4(texture(result, texCoord).rgb, 1.0);
}
