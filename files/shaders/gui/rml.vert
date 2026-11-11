// Task 0104 (decision 0015): the vertex stage of the RmlUi overlay. The matrix takes RmlUi's pixel space to clip
// space (ui_rml/uigeometry.hpp, PushConstants); the translation is RmlUi's, added before the matrix.
layout(push_constant) uniform PushConstants {
    mat4 matrix;
    vec2 translation;
} pc;

layout(location=0) in vec2 inVertex;
layout(location=1) in vec4 inColor;
layout(location=2) in vec2 inTexCoord;

layout(location=1) out vec4 outColor;
layout(location=2) out vec2 outTexCoord;
out gl_PerVertex{ vec4 gl_Position; };

void main()
{
    gl_Position = pc.matrix * vec4(inVertex + pc.translation, 0.0, 1.0);
    outColor = inColor;
    outTexCoord = inTexCoord;
}
