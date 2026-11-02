#ifndef OPENMW_COMPONENTS_LOD_TILESELECTION_H
#define OPENMW_COMPONENTS_LOD_TILESELECTION_H

// OpenEden world LOD (task 0055, stage S1a). Choosing, for each baked tile, whether to draw the tile or the live
// terrain, from the tile's error in screen pixels. Pure code: no GPU, no clock, no allocation (the caller owns the
// array of states). DRAFT: thresholds are starting values (see the stage log of the spec).

#include <cstddef>
#include <cstdint>
#include <span>

namespace Lod
{
    /// Starting thresholds in pixels (design doc 3.3; S0's numbers in docs/research/0025-lod-baseline.md say the
    /// first cost to avoid is draw commands, so tiles may be generous). `setting` default is the Medium value.
    inline constexpr float thresholdHigh = 2.f;
    inline constexpr float thresholdMedium = 4.f;
    inline constexpr float thresholdLow = 8.f;
    /// A tile replaced by the live terrain returns only when its error is below this share of the threshold.
    inline constexpr float hysteresisShare = 0.7f;

    /// A tile's size and error in world units. Z is up. (originX, originY) is the south-west corner.
    struct TileBounds
    {
        float mOriginX = 0;
        float mOriginY = 0;
        float mSide = 0;
        float mMinHeight = 0;
        float mMaxHeight = 0;
        float mGeometricError = 0;
    };

    /// The camera, in world units, with a unit forward vector.
    struct SelectionCamera
    {
        double mX = 0, mY = 0, mZ = 0;
        double mForwardX = 0, mForwardY = 1, mForwardZ = 0;
        double mViewportHeight = 720; // pixels
        double mFovY = 1.0; // radians, vertical
        double mAspect = 16.0 / 9.0; // width / height
        double mViewDistance = 30000; // tiles whose nearest point is farther than this are skipped
    };

    /// Distance from the camera to the nearest point of the tile's bounding box (0 inside it).
    double distanceToTile(const SelectionCamera& camera, const TileBounds& bounds) noexcept;

    /// geometricError * viewportHeight / (2 * max(d, 1) * tan(fovY / 2)).
    double pixelError(const SelectionCamera& camera, const TileBounds& bounds) noexcept;

    enum class TileChoice : std::uint8_t
    {
        Unset, // never chosen yet
        Tile, // draw the baked tile
        Live, // draw the live terrain for this square
    };

    /// One tile's state. The caller keeps one per tile, in key order, and passes the same array every frame.
    struct TileState
    {
        TileBounds mBounds;
        TileChoice mChoice = TileChoice::Unset;
        bool mVisible = false; // inside view distance and the view cone this frame
        float mPixels = 0; // the error on screen this frame (0 when not visible)
    };

    struct SelectionResult
    {
        std::size_t mTiles = 0; // visible, drawn as tile
        std::size_t mLive = 0; // visible, drawn as live terrain
        std::size_t mSkipped = 0; // not visible
        std::size_t mChanged = 0; // visible tiles whose choice changed this call
    };

    /// Updates every state. A visible tile with no choice yet takes Tile when pixels <= threshold, else Live. A tile
    /// that is Tile becomes Live when pixels > threshold; a tile that is Live becomes Tile only when pixels <
    /// hysteresisShare * threshold. A tile that is not visible keeps its choice (so hysteresis survives looking away)
    /// and counts as skipped. Deterministic: the result depends only on the arguments.
    SelectionResult selectTiles(std::span<TileState> states, const SelectionCamera& camera, float threshold) noexcept;
}

#endif
