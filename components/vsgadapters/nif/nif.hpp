#ifndef VSGOPENMW_VSGADAPTERS_NIF_NIF_H
#define VSGOPENMW_VSGADAPTERS_NIF_NIF_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <vsg/io/ReaderWriter.h>
#include <vsg/state/Descriptor.h>

#include "automaps.hpp"

namespace Render::Pbr
{
    class OverrideTable;
}

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
        nif(const Pipeline::Builder& builder, vsg::ref_ptr<const vsg::Options> imageOptions, int computeBin,
            int depthSortedBin)
            : mBuilder(builder)
            , mImageOptions(imageOptions)
            , mComputeBin(computeBin)
            , mDepthSortedBin(depthSortedBin)
        {
        }
        vsg::ref_ptr<vsg::Object> read(std::istream&, vsg::ref_ptr<const vsg::Options> = {}) const override;

        bool getFeatures(Features& features) const override;

        /// Task 0110 (neutral world, stage N3a-4): a reader for files of another game that the Morrowind reader
        /// refuses. `accepts` is given the first bytes of the file; when it says yes the whole file goes to `read`
        /// (which owns the format: this class includes no format header). Null (the default): every file reaches the
        /// Morrowind reader, as before. Set by the host (apps/openmw/mwrender/skyrimmodel) for Skyrim SE files when the
        /// neutral world is on; must be set before the first model is read.
        struct SceneReader
        {
            std::function<bool(std::string_view fileStart)> accepts;
            std::function<vsg::ref_ptr<vsg::Object>(
                std::string data, const std::string& filename, vsg::ref_ptr<const vsg::Options> options)>
                read;
        };
        std::shared_ptr<const SceneReader> sceneReader;

        /// Whether or not nodes marked as "MRK" should be shown.
        /// These should be hidden ingame, but visible in the editor.
        bool showMarkers = false;

        // Mask to use for nodes that ignore the crosshair intersection.
        // This is used for NiCollisionSwitch nodes with NiCollisionSwitch state set to disabled.
        unsigned int intersectionDisabledMask = ~0;

        // openeden-vsg: normal and specular maps found next to diffuse textures (task 0012, V4h), and how to tell
        // whether a texture path exists.
        AutoMapSettings autoMaps;
        std::function<bool(std::string_view)> textureExists;
        // openeden (task 0027, tier 2): material overrides keyed by model and diffuse texture, owned by the renderer;
        // null (the default) means none and no path is looked up.
        std::shared_ptr<const Render::Pbr::OverrideTable> materialTable;
        // openeden-vsg: `[Shaders] soft particles` (task 0012, V4k).
        bool softParticles = false;
        /// OpenEden: the normalized path of the one model whose particles weather occlusion discards under a roof,
        /// empty and its occlusion map, unset (task 0012, V4l).
        std::string occludedParticleModel;
        vsg::ref_ptr<vsg::Descriptor> occludedParticleMap;

        const int mComputeBin;
        const int mDepthSortedBin;
    };
}

#endif
