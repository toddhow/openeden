#ifndef VSGOPENMW_VSGUTIL_READIMAGE_H
#define VSGOPENMW_VSGUTIL_READIMAGE_H

#include <vsg/io/Options.h>

#include "texturecap.hpp"

namespace vsgUtil
{
    /*
     * Task 0049: how a read may be capped by the texture downscaler. A call that gives none is not capped, so the UI,
     * the HUD, water, the sky and every other read that does not opt in stay exactly as they were.
     */
    struct ImageUse
    {
        TextureCap::Category category = TextureCap::Category::None;
        TextureCap::Kind kind = TextureCap::Kind::Colour; // what the texels mean, for a CPU shrink
        int floor = 0; // a cap is never lower than this (a height map keeps at least 256); 0: none
        float alphaCutoff = -1.f; // an alpha tested image: keep the share of texels at or above this (0 to 1); < 0: off
        // A further cap for this read only (far object clusters, task 0066 stage e): the smaller one wins; 0: none.
        int maxSize = 0;
    };

    /*
     * Reads an image or the default image on failure.
     */
    vsg::ref_ptr<vsg::Data> readImage(
        const std::string& path, vsg::ref_ptr<const vsg::Options> options, const ImageUse& use = {});

    vsg::ref_ptr<vsg::Data> readOptionalImage(
        const std::string& path, vsg::ref_ptr<const vsg::Options> options, const ImageUse& use = {});

    /*
     * Task 0049: reads the images of one array (the terrain's layers), each with `use`. If the cap leaves any of them
     * larger than the cap (a block compressed image with a single level cannot be made smaller), they are all read
     * again without a cap, so that capping never turns an array of layers of one size into one of mixed sizes (which
     * the array draws grey). Unreadable images are null, or throw when `required`.
     */
    std::vector<vsg::ref_ptr<vsg::Data>> readArrayImages(const std::vector<std::string>& paths,
        vsg::ref_ptr<const vsg::Options> options, const ImageUse& use, bool required);

    /// openeden-vsg: replaces an 8-bit sRGB format with its UNORM equivalent (same bytes, no conversion on sampling).
    void removeSrgb(vsg::Data& data);
}

#endif
