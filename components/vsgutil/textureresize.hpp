#ifndef OPENEDEN_VSGUTIL_TEXTURERESIZE_H
#define OPENEDEN_VSGUTIL_TEXTURERESIZE_H

#include <cstdint>
#include <mutex>
#include <string>

#include <vsg/core/Data.h>

#include "texturecap.hpp"

/*
 * Task 0049: the data operations of the runtime texture downscaler, on a vsg::Data held in memory (no device needed).
 * Pure functions of the input image, the cap and the kind: no randomness, no threads inside, the same bytes every run.
 */
namespace vsgUtil::TextureResize
{
    using TextureCap::Kind;

    /*
     * A new image without the top `levels` mip levels of `source`: the same format and block size, the size of the new
     * top level, the matching MipmapLayout (rebased) or properties.mipLevels, and the retained bytes copied exactly.
     * `levels <= 0` returns `source` itself (the same pointer, which keeps the off path identical). Returns null if the
     * chain is not that long, or the image is not a plain 2D one (3D, array, cube).
     */
    vsg::ref_ptr<vsg::Data> dropMipLevels(const vsg::Data& source, int levels);

    /*
     * A new single-level image, `levels` halvings of the top level of `source` by a box filter of the stored values
     * (what the GPU's mip generation does for a UNORM image, so no gamma is applied). For R8, R8G8, R8G8B8, B8G8R8,
     * R8G8B8A8 and B8G8R8A8 UNORM. Kind::Normal renormalises xyz (the first three channels of an RGB or BGR layout,
     * read by their position in the format) after each halving. `alphaCutoff` in (0, 1] on a format with alpha keeps
     * the share of texels at or above it (coverage preserving); a negative value leaves alpha as averaged.
     * properties.mipLevels of the result is 0, so the GPU makes the chain. `levels <= 0` returns `source`; any other
     * format (compressed, sRGB, float, 16 bit) or a 3D image returns null.
     */
    vsg::ref_ptr<vsg::Data> shrinkUncompressed(const vsg::Data& source, int levels, Kind kind, float alphaCutoff);

    /*
     * Task 0080 (terrain layer arrays): the same for an array of layers as convertToArrayTexture lays it out.
     *
     * dropArrayLevels: a block compressed 2D array (imageViewType 2D_ARRAY, a MipmapLayout, each mip level stored for
     * all layers in turn) without its top `levels` levels: the stored bytes from the first kept level on, the layout
     * rebased, the size of the new top level, the same layer count and format. `levels <= 0` returns `source` itself.
     * Null when the image is not such an array or does not have `levels` + 1 levels.
     *
     * shrinkArray: an uncompressed 4 byte 2D array (one level stored, the GPU makes the chain) with each layer halved
     * `levels` times by the box filter of shrinkUncompressed (Kind::Normal renormalises xyz). `levels <= 0` returns
     * `source`. Null for any other array, or when a layer would shrink below one texel.
     * Both read `source` only, so they may run on a worker thread while the array is in use.
     */
    vsg::ref_ptr<vsg::Data> dropArrayLevels(const vsg::Data& source, int levels);
    vsg::ref_ptr<vsg::Data> shrinkArray(const vsg::Data& source, int levels, Kind kind);

    /*
     * Brings `source` to a longest side of at most `cap` by dropping levels and then shrinking. Returns `source` itself
     * if `cap` <= 0 or nothing is needed or nothing can be done (a block-compressed image with one level, an
     * unsupported format), in which case the last is counted in the report as left big. Counts what it did in report().
     */
    vsg::ref_ptr<vsg::Data> applyCap(vsg::ref_ptr<vsg::Data> source, int cap, Kind kind, float alphaCutoff);

    // What applyCapUncounted did, so a caller that may throw its result away (a loader thread that lost a race to
    // cache it) can leave the report alone.
    struct CapOutcome
    {
        enum class Result
        {
            None, // nothing needed or nothing counted
            Dropped,
            Shrunk,
            LeftBig,
        };
        Result result = Result::None;
        uint64_t before = 0;
        uint64_t after = 0;
    };

    // Adds an outcome to report().
    void record(const CapOutcome& outcome);

    // As applyCap, but writes what it did to `outcome` and leaves report() alone.
    vsg::ref_ptr<vsg::Data> applyCapUncounted(
        vsg::ref_ptr<vsg::Data> source, int cap, Kind kind, float alphaCutoff, CapOutcome& outcome);

    struct Report
    {
        uint32_t dropped = 0; // textures made smaller by dropping levels only
        uint32_t shrunk = 0; // textures made smaller by averaging (after dropping what the chain gave)
        uint32_t leftBig = 0; // textures over the cap that could not be made smaller
        uint64_t bytesBefore = 0; // bytes of every counted texture before
        uint64_t bytesAfter = 0; // and after
    };

    // Loaders run on several threads: every call takes the mutex.
    class ReportAccumulator
    {
    public:
        void addDropped(uint64_t before, uint64_t after);
        void addShrunk(uint64_t before, uint64_t after);
        void addLeftBig(uint64_t bytes);
        Report snapshot() const;
        void reset();

    private:
        mutable std::mutex mMutex;
        Report mReport;
    };

    ReportAccumulator& report();

    // "texture downscale: 12 dropped levels, ..." for the log.
    std::string describe(const Report& report);
}

#endif
