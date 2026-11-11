// Task 0104 (decision 0015): the fragment stage of the RmlUi overlay. Texture and vertex colour are both
// premultiplied by alpha, so the product is premultiplied too (blend: one, one minus source alpha).
layout(binding = 0) uniform sampler2D diffuseMap;
layout(location = 1) in vec4 vertColor;
layout(location = 2) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(diffuseMap, texCoord) * vertColor;
}
