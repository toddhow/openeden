#ifndef VSGOPENMW_VSGADAPTERS_NIF_NIF_H
#define VSGOPENMW_VSGADAPTERS_NIF_NIF_H

#include <functional>
#include <string_view>

#include <vsg/io/ReaderWriter.h>

#include "automaps.hpp"

namespace Pipeline
{
    class Builder;
}

namespace vsgAdapters
{
    /*
     * Reads VER_MW nif file into vsg::Node or throws.
     * Depends on nif, animation, pipeline.
     */
    // template<int NifVersion>
    class nif : public vsg::ReaderWriter
    {
        const Pipeline::Builder& mBuilder;
        vsg::ref_ptr<const vsg::Options> mImageOptions;

    public:
        nif(const Pipeline::Builder& builder, vsg::ref_ptr<const vsg::Options> imageOptions, int computeBin, int depthSortedBin)
            : mBuilder(builder)
            , mImageOptions(imageOptions)
            , mComputeBin(computeBin)
            , mDepthSortedBin(depthSortedBin)
        {
        }
        vsg::ref_ptr<vsg::Object> read(std::istream&, vsg::ref_ptr<const vsg::Options> = {}) const override;

        bool getFeatures(Features& features) const override;

        /// Whether or not nodes marked as "MRK" should be shown.
        /// These should be hidden ingame, but visible in the editor.
        bool showMarkers = false;

        // Mask to use for nodes that ignore the crosshair intersection.
        // This is used for NiCollisionSwitch nodes with NiCollisionSwitch state set to disabled.
        unsigned int intersectionDisabledMask = ~0;

        // openeden-vsg: normal and specular maps found next to diffuse textures (task 0012, V4h), and how to tell whether
        // a texture path exists.
        AutoMapSettings autoMaps;
        std::function<bool(std::string_view)> textureExists;

        const int mComputeBin;
        const int mDepthSortedBin;
    };
}

#endif
