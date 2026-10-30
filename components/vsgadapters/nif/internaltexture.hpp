#ifndef OPENEDEN_VSGADAPTERS_NIF_INTERNALTEXTURE_H
#define OPENEDEN_VSGADAPTERS_NIF_INTERNALTEXTURE_H

#include <string>

#include <vsg/core/Data.h>

namespace Nif
{
    struct NiPixelData;
}

namespace vsgAdapters
{
    /**
     * @brief Converts a NIF's internal texture (NiPixelData) to an image (task 0012, V4; OpenMW's
     * NifOsg::Loader::handleInternalTexture at OpenEden cf3dbd3aa7).
     *
     * RGBA and BGRA keep their bytes; RGB and BGR gain an opaque alpha (Vulkan devices rarely sample 3-byte formats);
     * 8-bit palettes expand through the palette (with its alpha for PaletteAlpha, opaque otherwise); DXT1, DXT3 and DXT5
     * become BC1, BC2 and BC3 blocks with every NIF mipmap level VSG can lay out (the renderer cannot generate levels of
     * compressed formats); uncompressed formats keep only the first level and the renderer generates the others.
     *
     * Returns null, with error describing why, for an unsupported format, a palette that is missing or not 8-bit, a
     * zero size, or a level whose data runs past the pixel data.
     */
    vsg::ref_ptr<vsg::Data> readInternalTexture(const Nif::NiPixelData& pixelData, std::string& error);
}

#endif
