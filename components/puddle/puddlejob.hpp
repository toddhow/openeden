#ifndef OPENMW_COMPONENTS_PUDDLE_PUDDLEJOB_H
#define OPENMW_COMPONENTS_PUDDLE_PUDDLEJOB_H

// OpenEden puddles (task 0095, stage W-b). The whole bake as a job that can be run in slices: `openmw --bake-puddles`
// calls `step` once per frame (apps/openmw/puddlebaker.*) and the tests call it in a loop. Steps:
//  1. the input digest of every land cell (a digest-only pass, no baking);
//  2. a cell is taken from the pack on disk when its digests are those of now, else from the work folder (a bake that
//     was killed), else baked, if it is wanted (inside the asked range);
//  3. when the set equals the pack on disk (same manifest, same content digest), nothing is written; otherwise the
//     new pack is written (atomic rename), read back and checked, and the work folder is removed.
// The result never depends on the order cells are baked in or on whether the job was resumed.

#include <array>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "puddlebake.hpp"
#include "puddlepack.hpp"

namespace Puddle
{
    /// Keeps the vertex heights of the last cells read, so the window of the next cell does not read them again.
    class CachingLandSource final : public LandHeightSource
    {
    public:
        CachingLandSource(LandHeightSource& inner, std::size_t capacity = 4096);
        bool cellHeights(std::int32_t cellX, std::int32_t cellY, std::vector<float>& heights) override;

    private:
        LandHeightSource& mInner;
        std::size_t mCapacity;
        std::map<std::pair<std::int32_t, std::int32_t>, std::optional<std::vector<float>>> mCache;
        std::vector<std::pair<std::int32_t, std::int32_t>> mOrder; // oldest first
    };

    struct JobConfig
    {
        BakeSettings mSettings;
        /// <user data>/openeden/puddles
        std::filesystem::path mFolder;
        /// Every cell with land.
        std::vector<std::pair<std::int32_t, std::int32_t>> mLandCells;
        /// The cells to (re)bake, inclusive corners; none means all of them.
        std::optional<std::array<std::int32_t, 4>> mRange; // x0, y0, x1, y1
        bool mStoreOnly = false;
        /// Also measure the unbounded catchment ratios (a report; costs a flow accumulation per cell).
        bool mStatistics = false;
    };

    struct JobResult
    {
        Error mError;
        bool mFinished = false;
        bool mUpToDate = false;
        std::uint32_t mCellsInPack = 0;
        std::uint32_t mBaked = 0;
        std::uint32_t mFromPack = 0;
        std::uint32_t mFromPartial = 0;
        std::uint32_t mSkipped = 0; // outside the range and not valid in the pack on disk
        std::uint64_t mGeneration = 0;
        std::uint64_t mPackBytes = 0;
        std::uint32_t mReadBack = 0; // cells read back fine from the written pack
        // Totals over the cells baked in this run.
        std::uint64_t mPitsInCells = 0;
        std::uint64_t mPitsDroppedByArea = 0;
        std::uint64_t mWetTexels = 0;
        std::uint64_t mClippedUnbounded = 0;
        std::uint64_t mClippedBounded = 0;
        std::uint32_t mHeightsClamped = 0; // cells
        std::uint64_t mPondTexelsDroppedClamped = 0; // pond texels dropped under a clamped height
        /// Cells skipped because a height of their own was not finite or beyond +-1e7 (the first one is described).
        std::uint32_t mBadCells = 0;
        std::string mFirstBadCell;
        /// The pack on disk was made with other settings or another layout (so none of its cells was reused), or
        /// could not be opened although the file exists.
        bool mOldPackOtherSettings = false;
        bool mOldPackUnreadable = false;
        std::vector<float> mKappasBounded;
        std::vector<float> mKappasUnbounded;
    };

    class BakeJob
    {
    public:
        BakeJob(LandHeightSource& source, JobConfig config);

        /// Does work for about `budget` (at least one unit). True when the job has finished (check result().mError).
        bool step(std::chrono::milliseconds budget);
        const JobResult& result() const { return mResult; }
        std::size_t unitsDone() const { return mNext; }
        std::size_t unitsTotal() const { return mConfig.mLandCells.size(); }

    private:
        enum class Phase
        {
            Digests,
            Cells,
            Finish,
            Done
        };
        bool wanted(const std::pair<std::int32_t, std::int32_t>& cell) const;
        void finish();
        void stopWith(Error error);

        CachingLandSource mSource;
        JobConfig mConfig;
        JobResult mResult;
        Phase mPhase = Phase::Digests;
        std::size_t mNext = 0;
        std::unique_ptr<PuddlePack> mOldPack;
        Digest mParameters{};
        std::vector<Digest> mDigests;
        // One record a cell of the pack: its payload is a file of the work folder (or, for a cell taken from the old
        // pack, still in the old pack until the new one is written), never a map in memory.
        struct CellRecord
        {
            std::int32_t mCellX = 0;
            std::int32_t mCellY = 0;
            Digest mInput{};
            bool mInOldPack = false;
        };
        std::vector<CellRecord> mCells;
    };
}

#endif
