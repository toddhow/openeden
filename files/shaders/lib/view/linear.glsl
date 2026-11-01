// OpenEden (task 0025, F2): the linear HDR path. Include after lib/view/scene.glsl.
//
// With [Rendering] hdr on, the main view lights in linear space and draws to a float target; every other view (preview,
// local map, the editor, shadows) keeps the display-encoded maths it always had. The main view is marked by
// scene.fogColor.w being 2: it is 1 in the main view without HDR and 0 or an editor colour's alpha elsewhere, so the
// test is > 1.5, never != 0. A field of its own would grow the scene uniform past 640 bytes, which brightened
// interiors once (apps/openmw/mwrender/CLAUDE.md).
//
// The transfer function is a pure power law, as Render::Hdr (components/render/hdrmath.hpp) has it: change both
// together. It is sign preserving, because Morrowind has negative (darkness) point lights and pow of a negative number
// is NaN.
#ifndef LIB_VIEW_LINEAR
#define LIB_VIEW_LINEAR

const float displayGamma = 2.2;

bool linearView()
{
    return scene.fogColor.w > 1.5;
}

vec3 toLinear(vec3 encoded)
{
    return sign(encoded) * pow(abs(encoded), vec3(displayGamma));
}

vec3 toEncoded(vec3 linear)
{
    return sign(linear) * pow(abs(linear), vec3(1.0 / displayGamma));
}

// For values the CPU cannot convert (per-material, per-texel, per-vertex): the same colour in the view's space.
vec3 toLinearIfView(vec3 encoded)
{
    return linearView() ? toLinear(encoded) : encoded;
}

// For values the CPU converted already (scene colours), when the shader needs the display-encoded number back, such as
// to add a constant that was chosen for encoded colours.
vec3 toEncodedIfView(vec3 viewSpace)
{
    return linearView() ? toEncoded(viewSpace) : viewSpace;
}

#endif
