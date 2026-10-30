#ifndef VSGOPENMW_VSGUTIL_READIMAGE_H
#define VSGOPENMW_VSGUTIL_READIMAGE_H

#include <vsg/io/Options.h>

namespace vsgUtil
{
    /*
     * Reads an image or the default image on failure.
     */
    vsg::ref_ptr<vsg::Data> readImage(const std::string& path, vsg::ref_ptr<const vsg::Options> options);

    vsg::ref_ptr<vsg::Data> readOptionalImage(const std::string& path, vsg::ref_ptr<const vsg::Options> options);

    /// openeden-vsg: replaces an 8-bit sRGB format with its UNORM equivalent (same bytes, no conversion on sampling).
    void removeSrgb(vsg::Data& data);
}

#endif
