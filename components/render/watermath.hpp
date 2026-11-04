#ifndef OPENEDEN_RENDER_WATERMATH_H
#define OPENEDEN_RENDER_WATERMATH_H

#include <algorithm>

/*
 * OpenEden (task 0068): the pure maths of the water's far fade, no VSG or game types.
 *
 * The water's ripples are a 128 by 128 normal map, tiled many times and read at several scales. The picture has no
 * mip levels (the GPU image has one level), so far away, where one screen pixel covers many texels, the normal flips
 * from pixel to pixel and the sea speckles. A mip level would average those texels into a flat normal. The shader
 * does the same by hand: it scales each ripple octave by `octaveFade` of the texels one pixel covers, which is 1 for
 * a pixel that covers at most one texel (so near water is exactly as before) and falls as 1 / texels beyond that.
 *
 * `files/shaders/water/main.frag` repeats `waterOctaveFade`: change both together.
 */
namespace Render::Water
{
    /// The share of a ripple octave that is kept when one pixel covers `texelsPerPixel` texels of the normal map
    /// (the larger of the two screen directions). In (0, 1]; 1 up to one texel per pixel.
    inline float octaveFade(float texelsPerPixel)
    {
        return 1.f / std::max(1.f, texelsPerPixel);
    }
}

#endif
