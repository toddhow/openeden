#ifndef OPENMW_COMPONENTS_LOD_FARTIER_H
#define OPENMW_COMPONENTS_LOD_FARTIER_H

// OpenEden world LOD (task 0066, stage e): how much of the whole-map view a machine can hold. The view is made of the
// baked terrain tiles of levels 1 to 6 (a level doubles the side of a tile) and the object clusters of levels 1 to 4.
// A weak machine gets fewer levels, a larger tile threshold and a smaller reach; a strong one gets all of them. Pure
// code: no GPU, no clock, no allocation. The numbers of the tier table are starting values measured on one machine
// (the stage log of the spec), not properties of the hardware.

#include <cstdint>
#include <span>
#include <string_view>

namespace Lod
{
    /// The highest terrain tile level the baker makes by default (stage d): `far levels` never exceeds it.
    inline constexpr std::uint32_t maxFarLevels = 6;

    /// What the machine reports, from the device report (components/render/devicereport) and the system memory
    /// counters. A field of 0 is unknown.
    struct HardwareReport
    {
        std::uint64_t mDeviceMemoryBytes = 0; // the largest device-local heap
        std::uint64_t mSystemMemoryBytes = 0; // physical memory
        bool mSoftwareDevice = false; // a CPU Vulkan implementation such as llvmpipe
    };

    enum class FarTier : std::uint8_t
    {
        Minimal,
        Low,
        Medium,
        High,
    };

    struct FarTierSettings
    {
        std::uint32_t mFarLevels = 3; // the highest tile level drawn and kept resident
        float mTileThreshold = 8; // pixels of error that a tile may show before the live terrain replaces it
        float mMaxViewDistance = 80000; // the cap of `view distance = auto`, in world units
        std::uint32_t mFarTextureMaxSize = 256; // longest side of the pictures of far object clusters, in pixels
        std::uint64_t mResidentBudgetBytes = 0; // what the far field may hold in system memory
        // What the drawn terrain tiles may hold (task 0060; 0: no budget). `far memory budget = N` overrides both
        // budgets: the clusters get N MiB and the tiles a quarter of it, so the far field may hold 1.25 N MiB in all.
        std::uint64_t mTerrainBudgetBytes = 0;
        // May the planner evict object clusters that lie past the live paging range, under the cluster budget, when the
        // ground beneath them is drawn (task 0060, decision of toddhow 2026-11-04, option b)? Minimal and low tiers
        // only: the far objects vanish and come back as the camera moves, in exchange for a bounded memory. Medium and
        // high keep every cluster the selection wants (an overrun is reported, as before).
        bool mEvictFarClusters = false;
    };

    /// Software devices and unknown or small machines get the smallest tier; the others by device memory and system
    /// memory (the lower of the two tiers they qualify for). Deterministic.
    FarTier chooseFarTier(const HardwareReport& report) noexcept;

    FarTierSettings farTierSettings(FarTier tier) noexcept;

    /// Does the cluster planner give up far clusters under the budget (task 0133 stage 3, toddhow 2026-11-12)? The
    /// tier says so for minimal and low. A neutral (Skyrim) world always does: its clusters have no live fallback and
    /// a Skyrim exterior holds about 1.5 GiB of them, so the budget must be able to bind on every tier.
    bool evictFarClusters(const FarTierSettings& tier, bool neutralWorld) noexcept;

    /// The cluster budget a neutral world's planner works to, from the tier's or the user's (task 0133 stage 3). The
    /// planner admits loads ahead of the evictions that pay for them, so the held bytes overshoot the budget by up to
    /// about 10 percent for a moment (1,126 MiB against 1,024 measured); working to 7/8 keeps the peak under the
    /// number the user set. 0 (no budget) stays 0.
    std::uint64_t neutralClusterBudgetBytes(std::uint64_t budgetBytes) noexcept;

    /// `[WorldLod] tier`: `auto` (also empty; anything not understood is auto with mValid false) or one of minimal,
    /// low, medium, high in any case, with spaces and tabs around.
    struct FarTierSetting
    {
        bool mAuto = true;
        FarTier mTier = FarTier::High; // only when !mAuto
        bool mValid = true;
    };
    FarTierSetting parseFarTierSetting(std::string_view text) noexcept;

    /// The named tier, or for `auto` the one chooseFarTier gives the report.
    FarTier resolveFarTier(const FarTierSetting& setting, const HardwareReport& report) noexcept;

    /// The tile threshold in force: a tier may only make tiles coarser, never finer than the user's setting.
    float effectiveTileThreshold(float setting, const FarTierSettings& tier) noexcept;

    /// The cap of `view distance = auto` in force: the smaller of the setting (when positive) and the tier's.
    float effectiveMaxViewDistance(float setting, const FarTierSettings& tier) noexcept;

    /// `[WorldLod] far texture max size`: 0 or less means the tier's; otherwise the number. Either is rounded down to a
    /// power of two between 16 and 8192.
    std::uint32_t resolveFarTextureSize(std::int32_t setting, std::uint32_t tierSize) noexcept;

    /// What one built cluster holds in system memory, in bytes: the decoded cluster (18 bytes a vertex, 4 an index) and
    /// the draws' arrays that the viewer keeps (position, normal, colour and texture coordinates: 12 + 12 + 16 + 8 = 48
    /// bytes a vertex, 4 an index). Task 0066 stage e measured the working set growing by about 100 bytes a vertex.
    inline constexpr std::uint64_t clusterBytesPerVertex = 18 + 48;
    inline constexpr std::uint64_t clusterBytesPerIndex = 4 + 4;
    std::uint64_t clusterResidentBytes(std::uint64_t vertexCount, std::uint64_t indexCount) noexcept;

    /// May a cluster of `addBytes` be built when `residentBytes` are held, under `budgetBytes` (0: no budget)? The
    /// first cluster is always admitted (an empty far field would be worse than one over budget).
    bool admitCluster(std::uint64_t residentBytes, std::uint64_t addBytes, std::uint64_t budgetBytes) noexcept;

    /// `[WorldLod] far levels`: 0 means the tier's value; otherwise the number, cut to 1 .. maxFarLevels.
    std::uint32_t resolveFarLevels(std::int32_t setting, std::uint32_t tierLevels) noexcept;

    /// The most levels, up to `wanted` (and at least 1), whose cumulative cost fits `budgetBytes`. `levelBytes[i]` is
    /// what level i + 1 costs; level 1 is always kept (it is the base of the far field). A budget of 0 means no
    /// budget.
    std::uint32_t farLevelsWithinBudget(
        std::span<const std::uint64_t> levelBytes, std::uint64_t budgetBytes, std::uint32_t wanted) noexcept;
}

#endif
