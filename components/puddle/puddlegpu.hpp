#ifndef OPENMW_COMPONENTS_PUDDLE_PUDDLEGPU_H
#define OPENMW_COMPONENTS_PUDDLE_PUDDLEGPU_H

// OpenEden puddles (task 0095, stage W-d). Packs the window of loaded puddle cells (PuddleWindow::find) into the
// storage buffer the shader reads. The buffer's layout, and the maths that reads it, are
// components/render/puddlemapgpu.hpp (its comment is the format); this file only fills it. Pure: no Vulkan, no files,
// no clock, no allocation (the caller owns the words), so it runs on a worker thread and its result is copied to the
// GPU buffer on the render thread.
//
// A slot's texels sit at a fixed place (dataBase + slot * side * side), so a cell that stays loaded in the next window
// is not moved to another place in the buffer by the cells that come and go round it.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "puddlemap.hpp"

namespace Puddle
{
    struct GpuWindowParams
    {
        /// The cell the window is centred on; it holds the cells within Render::PuddleMap::windowRadius of it.
        std::int32_t mCentreX = 0;
        std::int32_t mCentreY = 0;
        /// What the pack's cells were baked with (they must all have this many texels a side).
        std::uint32_t mTexelsPerCell = 0;
        std::uint32_t mSpacingUnits = 0;
        std::uint32_t mCellSizeUnits = 0;
        /// Water supplied at wetness 1, and what the ground absorbs, in units of depth (settings).
        float mSupply = 0.f;
        float mAbsorption = 0.f;
        /// A Render::PuddleMap::DebugMode.
        std::uint32_t mDebugMode = 0;
        /// Render::PuddleMap::optionKeepStaticNoise (stage W-c): objects keep the noise puddles on flat static
        /// surfaces the bake found no pit on.
        bool mKeepStaticNoise = false;
    };

    struct GpuPackResult
    {
        std::uint32_t mPacked = 0;
        /// Cells that were found but cannot be used: another texel count, or channels of the wrong size.
        std::uint32_t mSkipped = 0;
    };

    using CellFinder = std::function<std::shared_ptr<const CellMap>(std::int32_t, std::int32_t)>;

    /// True when a cell of this many texels a side fits the game's fixed buffer.
    bool gpuTexelsFit(std::uint32_t texelsPerCell);

    /// Clears `words` words at `out` and fills them for the window centred on the params' cell. If the buffer is
    /// smaller than Render::PuddleMap::bufferWords for these parameters, or the texels do not fit, the buffer is left
    /// cleared and off (nothing is packed). With room for the tops (Render::PuddleMap::bufferWordsWithTops) the cells'
    /// height bases and `top` channels are packed after the texels (stage W-c); a buffer with room for the texels only
    /// says so in its header (no tops), and the shader then cannot test heights.
    GpuPackResult packWindow(
        std::uint32_t* out, std::size_t words, const GpuWindowParams& params, const CellFinder& find);

    /// Clears the header (at most `words` words of it): the shader then draws the old noise puddles.
    void packOff(std::uint32_t* out, std::size_t words);
}

#endif
