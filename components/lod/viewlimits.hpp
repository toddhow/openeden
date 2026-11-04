#ifndef OPENMW_COMPONENTS_LOD_VIEWLIMITS_H
#define OPENMW_COMPONENTS_LOD_VIEWLIMITS_H

// OpenEden world LOD (task 0066, stage b): the view distance used to be one number that set the tile range, the far
// plane and the fog end together. This file takes them apart. Pure code: no GPU, no clock, no allocation.

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace Lod
{
    /// The reach `[WorldLod] view distance = 40000` has had since commit ed53100fd3 (the default of the setting). Used
    /// when the setting is `auto` but `map view` is off or the world's extent is not known yet.
    inline constexpr float legacyViewDistance = 40000.f;

    /// The default of `[WorldLod] max view distance`: the cap of `auto`, a hardware budget, not a property of the map.
    /// Stage a measured the far field at this distance (note 0029): the numbers are in the spec's stage log.
    inline constexpr float defaultMaxViewDistance = 600000.f;

    /// What `[WorldLod] view distance` says: `auto` or a number of world units.
    struct ViewDistanceSetting
    {
        bool mAuto = false;
        float mValue = 0; // only when !mAuto
    };

    /// "auto" in any case, or a finite number greater than 0 (nothing else around it but spaces and tabs). Anything
    /// else gives nothing.
    std::optional<ViewDistanceSetting> parseViewDistanceSetting(std::string_view text) noexcept;

    /// The box of land cells (cell coordinates, inclusive). Empty when no cell was given.
    struct LandExtent
    {
        std::int32_t mMinX = 0;
        std::int32_t mMinY = 0;
        std::int32_t mMaxX = -1;
        std::int32_t mMaxY = -1;

        bool empty() const noexcept { return mMaxX < mMinX || mMaxY < mMinY; }
    };

    LandExtent landExtent(std::span<const std::pair<std::int32_t, std::int32_t>> cells) noexcept;

    /// The diagonal of the extent's box in world units, `cellSize` units per cell: the farthest any camera can be from
    /// land. 0 for an empty extent.
    double landDiagonal(const LandExtent& extent, double cellSize) noexcept;

    struct ViewLimitsInput
    {
        float mCameraViewingDistance = 7168; // [Camera] viewing distance
        bool mLodActive = false; // [WorldLod] enabled, [Terrain] distant terrain, and not a bake run
        bool mMapView = false; // [WorldLod] map view
        ViewDistanceSetting mSetting;
        float mMaxViewDistance = defaultMaxViewDistance; // [WorldLod] max view distance, caps `auto`
        double mLandDiagonal = 0; // landDiagonal of the loaded world; 0 while it is not known
    };

    struct ViewLimits
    {
        float mTileRange = 0; // how far tiles and clusters are chosen
        float mPagingRange = 0; // how far the live terrain and the distant objects are paged
        float mFarPlane = 0; // the projection's far plane
        float mFogRange = 0; // where the land fog ends
    };

    /// With the world LOD inactive all four are `mCameraViewingDistance`. With it active and `map view` off all four
    /// are the larger of that and the setting's number (`auto` counts as legacyViewDistance), as before this task.
    /// With `map view` on the tile range reaches `auto`'s land diagonal (capped by `mMaxViewDistance`) or the number,
    /// and the far plane and the fog follow it (stage c gives the fog its own model); the paging range stays at the
    /// old number, because the live chunks cost per chunk, not per tile.
    ViewLimits computeViewLimits(const ViewLimitsInput& input) noexcept;
}

#endif
