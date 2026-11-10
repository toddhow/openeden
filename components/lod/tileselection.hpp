#ifndef OPENMW_COMPONENTS_LOD_TILESELECTION_H
#define OPENMW_COMPONENTS_LOD_TILESELECTION_H

// OpenEden world LOD (task 0055, stage S1a). Choosing, for each baked tile, whether to draw the tile or the live
// terrain, from the tile's error in screen pixels. Pure code: no GPU, no clock, no allocation (the caller owns the
// array of states). Thresholds are starting values (see the stage log of the spec).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "tilekey.hpp"

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

    /// The camera, in world units. Preconditions (not checked): finite numbers, a unit forward vector, 0 < fovY < pi.
    struct SelectionCamera
    {
        double mX = 0, mY = 0, mZ = 0;
        double mForwardX = 0, mForwardY = 1, mForwardZ = 0;
        double mViewportHeight = 720; // pixels
        double mFovY = 1.0; // radians, vertical
        double mAspect = 16.0 / 9.0; // width / height
        double mViewDistance = 30000; // tiles whose nearest point is farther than this are skipped
        /// How far the live terrain draws, in world units on the ground plane (0 or less: not known, off). A tile
        /// that has no finer tile under it (TileState::mHasChildren false) and whose square is not wholly inside
        /// this range cannot be left to the live terrain, which does not draw all of it: it is chosen as Tile
        /// whatever its pixel error says (the far-terrain gap of 2026-11-09: a rough tile's error keeps it Live out
        /// to several times the live range, and nothing drew it there).
        double mLiveRange = 0;
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
        Covered, // (selectTileTree only) an ancestor tile is drawn, so this one is not
    };

    /// One tile's state. The caller keeps one per tile, in key order, and passes the same array every frame.
    struct TileState
    {
        TileBounds mBounds;
        TileChoice mChoice = TileChoice::Unset;
        bool mVisible = false; // inside view distance and the view cone this frame
        bool mHasChildren = false; // a finer baked tile lies under this one (read with SelectionCamera::mLiveRange)
        float mPixels = 0; // the error on screen this frame (0 when not visible)
    };

    struct SelectionResult
    {
        std::size_t mTiles = 0; // visible, drawn as tile
        std::size_t mLive = 0; // visible, drawn as live terrain
        std::size_t mSkipped = 0; // not visible
        std::size_t mChanged = 0; // visible tiles whose choice changed this call
        std::size_t mCovered = 0; // (selectTileTree only) tiles under a drawn ancestor
    };

    /// Updates every state. A visible tile with no choice yet takes Tile when pixels <= threshold, else Live. A tile
    /// that is Tile becomes Live when pixels > threshold; a tile that is Live becomes Tile only when pixels <
    /// hysteresisShare * threshold. A tile that is not visible keeps its choice (so hysteresis survives looking away)
    /// and counts as skipped. Deterministic: the result depends only on the arguments.
    SelectionResult selectTiles(std::span<TileState> states, const SelectionCamera& camera, float threshold) noexcept;

    /// The tiles ordered for selectTileTree: level descending (the biggest first), then key ascending. A parent is
    /// always before its children.
    std::vector<TileId> orderTilesForTree(std::vector<TileId> ids);

    /// For each tile of `ordered` (from orderTilesForTree) the index of its nearest ancestor that is in the list (the
    /// parent at the next level, or the grandparent if that tile was not baked, and so on), or -1. The result is
    /// always smaller than the tile's own index.
    std::vector<std::int32_t> nearestAncestors(std::span<const TileId> ordered);

    /// selectTiles for tiles of several levels. `parents` is parallel to `states` and holds nearestAncestors; every
    /// parent index is below the child's. Per tile, in order:
    ///  - if its parent is Tile or Covered, the tile is Covered (not drawn, not visible, mPixels 0, counted in
    ///    mCovered) whatever the camera says; its earlier choice is forgotten (a Covered tile that is reached again
    ///    starts as Unset);
    ///  - otherwise (no parent, or the parent is Live, Unset or skipped) it is chosen exactly as selectTiles chooses,
    ///    with the same visibility rules, hysteresis and counts.
    /// A tile that is Covered is not in mTiles, mLive or mSkipped.
    SelectionResult selectTileTree(std::span<TileState> states, std::span<const std::int32_t> parents,
        const SelectionCamera& camera, float threshold) noexcept;
}

#endif
