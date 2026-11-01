#include "lib/pushconstants.glsl"

layout(location = 0) in vec3 inVertex;

layout(location=0) out Outputs
{
#include "inout.glsl"
} vert_out;

out gl_PerVertex{ vec4 gl_Position; };

void main()
{
    gl_Position = pc.projection * (pc.modelview * vec4(inVertex, 1.0));
    // OpenEden (task 0040): the dome is centred on the camera, so a vertex's direction from the eye is its position
    // turned into the view's frame (for the sun's glow).
    vert_out.viewDir = (pc.modelview * vec4(inVertex, 1.0)).xyz;

    // this is a cylinder, so every second vertex belongs to the bottom-most row
    if (gl_VertexIndex%2 == 1)
        vert_out.fade = 0;
    else
        vert_out.fade = 1;
}
