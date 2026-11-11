#include "puddlejob.hpp"

#include <algorithm>

#include "puddlepartial.hpp"

namespace Puddle
{
    CachingLandSource::CachingLandSource(LandHeightSource& inner, std::size_t capacity)
        : mInner(inner)
        , mCapacity(std::max<std::size_t>(capacity, 1))
    {
    }

    bool CachingLandSource::cellHeights(std::int32_t cellX, std::int32_t cellY, std::vector<float>& heights)
    {
        const std::pair<std::int32_t, std::int32_t> key{ cellX, cellY };
        auto found = mCache.find(key);
        if (found == mCache.end())
        {
            if (mCache.size() >= mCapacity)
            {
                // Drop the oldest quarter at once so that this is not done for every cell.
                const std::size_t drop = std::max<std::size_t>(mCapacity / 4, 1);
                for (std::size_t i = 0; i < drop && i < mOrder.size(); ++i)
                    mCache.erase(mOrder[i]);
                mOrder.erase(mOrder.begin(), mOrder.begin() + std::min(drop, mOrder.size()));
            }
            std::vector<float> data;
            std::optional<std::vector<float>> entry;
            if (mInner.cellHeights(cellX, cellY, data))
                entry = std::move(data);
            found = mCache.emplace(key, std::move(entry)).first;
            mOrder.push_back(key);
        }
        if (!found->second)
            return false;
        heights = *found->second;
        return true;
    }

    BakeJob::BakeJob(LandHeightSource& source, JobConfig config)
        : mSource(source)
        , mConfig(std::move(config))
    {
        auto& cells = mConfig.mLandCells;
        // South row first, then west to east: neighbours stay in the cache.
        std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second < b.second : a.first < b.first;
        });
        cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
        mResult.mError = validateSettings(mConfig.mSettings);
        if (mResult.mError)
        {
            mPhase = Phase::Done;
            mResult.mFinished = true;
            return;
        }
        if (cells.empty())
        {
            stopWith(Error{ "cells", 0, "there are no land cells to bake" });
            return;
        }
        mParameters = parameterDigest(mConfig.mSettings);
        mDigests.resize(cells.size());
        Manifest manifest;
        manifest.mSettings = mConfig.mSettings;
        Error error;
        const std::filesystem::path packPath = mConfig.mFolder / "puddles.edenpak";
        mOldPack = PuddlePack::open(packPath, error);
        std::error_code ec;
        if (!mOldPack && std::filesystem::exists(packPath, ec))
            mResult.mOldPackUnreadable = true;
        // A pack made with other settings (or another layout) gives nothing to reuse.
        if (mOldPack && !(mOldPack->manifest() == manifest))
        {
            mResult.mOldPackOtherSettings = true;
            mOldPack.reset();
        }
    }

    bool BakeJob::wanted(const std::pair<std::int32_t, std::int32_t>& cell) const
    {
        if (!mConfig.mRange)
            return true;
        const auto& r = *mConfig.mRange;
        return cell.first >= r[0] && cell.first <= r[2] && cell.second >= r[1] && cell.second <= r[3];
    }

    void BakeJob::stopWith(Error error)
    {
        mResult.mError = std::move(error);
        mResult.mFinished = true;
        mPhase = Phase::Done;
    }

    bool BakeJob::step(std::chrono::milliseconds budget)
    {
        const auto end = std::chrono::steady_clock::now() + budget;
        const PartialStore partial(mConfig.mFolder / "work");
        const auto& cells = mConfig.mLandCells;
        BakeOptions options;
        options.mStatistics = mConfig.mStatistics;
        while (mPhase != Phase::Done)
        {
            if (mPhase == Phase::Digests)
            {
                if (mNext < cells.size())
                {
                    mDigests[mNext] = inputDigest(mSource, cells[mNext].first, cells[mNext].second, mConfig.mSettings);
                    ++mNext;
                }
                if (mNext >= cells.size())
                {
                    mPhase = Phase::Cells;
                    mNext = 0;
                }
            }
            else if (mPhase == Phase::Cells)
            {
                if (mNext < cells.size())
                {
                    const auto& cell = cells[mNext];
                    const Digest& digest = mDigests[mNext];
                    ++mNext;
                    CellRecord record;
                    record.mCellX = cell.first;
                    record.mCellY = cell.second;
                    record.mInput = digest;
                    bool have = false;
                    // From the pack on disk, when its cell is intact and is for this land and these settings.
                    if (mOldPack && mOldPack->contains(cell.first, cell.second)
                        && mOldPack->inputDigest(cell.first, cell.second) == std::optional<Digest>(digest))
                    {
                        CellMap loaded;
                        if (!mOldPack->readCell(cell.first, cell.second, loaded) && loaded.mInputDigest == digest
                            && loaded.mParameterDigest == mParameters)
                        {
                            record.mInOldPack = true;
                            have = true;
                            ++mResult.mFromPack;
                        }
                    }
                    // From the work folder (a bake that was killed).
                    if (!have && partial.load(cell.first, cell.second, mParameters, digest))
                    {
                        have = true;
                        ++mResult.mFromPartial;
                    }
                    if (!have && wanted(cell))
                    {
                        BakeResult baked = bakeCell(mSource, cell.first, cell.second, mConfig.mSettings, options);
                        if (baked.mError && baked.mError.mField == "heights")
                        {
                            // One cell with an impossible height is skipped; the others go on.
                            if (mResult.mBadCells++ == 0)
                                mResult.mFirstBadCell = baked.mError.mMessage;
                            continue;
                        }
                        if (baked.mError)
                        {
                            stopWith(Error{ baked.mError.mField, baked.mError.mOffset,
                                "cell (" + std::to_string(cell.first) + ", " + std::to_string(cell.second)
                                    + "): " + baked.mError.mMessage });
                            break;
                        }
                        if (Error error = partial.store(baked.mCell))
                        {
                            stopWith(std::move(error));
                            break;
                        }
                        const BakeStats& st = baked.mStats;
                        ++mResult.mBaked;
                        mResult.mPitsInCells += st.mPitsInCell;
                        mResult.mPitsDroppedByArea += st.mPitsDroppedByArea;
                        mResult.mWetTexels += st.mWetTexels;
                        mResult.mClippedUnbounded += st.mClippedUnbounded;
                        mResult.mClippedBounded += st.mClippedBounded;
                        mResult.mHeightsClamped += st.mHeightsClamped ? 1 : 0;
                        mResult.mPondTexelsDroppedClamped += st.mPondTexelsDroppedClamped;
                        mResult.mKappasBounded.insert(
                            mResult.mKappasBounded.end(), st.mKappasBounded.begin(), st.mKappasBounded.end());
                        mResult.mKappasUnbounded.insert(
                            mResult.mKappasUnbounded.end(), st.mKappasUnbounded.begin(), st.mKappasUnbounded.end());
                        have = true;
                    }
                    if (have)
                        mCells.push_back(record);
                    else
                        ++mResult.mSkipped;
                }
                if (mNext >= cells.size())
                    mPhase = Phase::Finish;
            }
            else if (mPhase == Phase::Finish)
            {
                finish();
                break;
            }
            if (std::chrono::steady_clock::now() >= end && mPhase != Phase::Finish)
                return mPhase == Phase::Done;
        }
        return mPhase == Phase::Done;
    }

    void BakeJob::finish()
    {
        const PartialStore partial(mConfig.mFolder / "work");
        Manifest manifest;
        manifest.mSettings = mConfig.mSettings;
        std::vector<std::pair<std::uint64_t, Digest>> inputs;
        for (const CellRecord& cell : mCells)
        {
            std::uint64_t key = 0;
            if (!packCellKey(cell.mCellX, cell.mCellY, key))
            {
                stopWith(Error{ "cell", 0, "a cell does not pack into a key" });
                return;
            }
            inputs.emplace_back(key, cell.mInput);
        }
        mResult.mCellsInPack = static_cast<std::uint32_t>(mCells.size());
        PackOptions options;
        options.mSourceDigest = contentDigest(manifest, inputs);
        options.mStoreOnly = mConfig.mStoreOnly;
        options.mThreads = 2;
        // Up to date means: every cell came, intact, from the pack on disk, and it is the same set. A pack with a
        // damaged cell is not up to date even when its digest says so: the digest is of the inputs, not of the bytes.
        const bool allFromPack
            = std::all_of(mCells.begin(), mCells.end(), [](const CellRecord& c) { return c.mInOldPack; });
        if (mOldPack && allFromPack && mResult.mBaked == 0 && mOldPack->manifest() == manifest
            && mOldPack->sourceDigest() == options.mSourceDigest && mOldPack->cells().size() == mCells.size())
        {
            mResult.mUpToDate = true;
            mResult.mGeneration = mOldPack->generation();
            partial.clear();
            mResult.mFinished = true;
            mPhase = Phase::Done;
            return;
        }
        options.mGeneration = mOldPack ? mOldPack->generation() + 1 : 1;
        mResult.mGeneration = options.mGeneration;
        std::error_code ec;
        std::filesystem::create_directories(mConfig.mFolder, ec);
        const std::filesystem::path path = mConfig.mFolder / "puddles.edenpak";

        // Every cell becomes a file of the work folder, one at a time (a cell kept from the old pack is copied out
        // before the old pack is released), and the pack is written from the files: no map is held in memory.
        std::vector<CellFile> files;
        files.reserve(mCells.size());
        for (const CellRecord& cell : mCells)
        {
            if (cell.mInOldPack)
            {
                CellMap map;
                if (Error error = mOldPack->readCell(cell.mCellX, cell.mCellY, map))
                {
                    stopWith(std::move(error));
                    return;
                }
                if (Error error = partial.store(map))
                {
                    stopWith(std::move(error));
                    return;
                }
            }
            CellFile file;
            file.mCellX = cell.mCellX;
            file.mCellY = cell.mCellY;
            file.mPath = partial.pathOf(cell.mCellX, cell.mCellY);
            file.mInputDigest = cell.mInput;
            file.mParameterDigest = mParameters;
            std::error_code sizeError;
            file.mSize = std::filesystem::file_size(file.mPath, sizeError);
            if (sizeError)
            {
                stopWith(Error{ "partial", 0, "the file of a cell is missing: " + file.mPath.string() });
                return;
            }
            file.mSide = mConfig.mSettings.texelsPerCell();
            files.push_back(std::move(file));
        }
        mOldPack.reset(); // release the file before it is replaced
        if (Error error = writePuddlePackFromFiles(manifest, files, options, path))
        {
            stopWith(std::move(error));
            return;
        }
        Error openError;
        const std::unique_ptr<PuddlePack> written = PuddlePack::open(path, openError);
        if (!written)
        {
            stopWith(std::move(openError));
            return;
        }
        // Read every cell back and compare it with the file it came from, one at a time.
        std::uint32_t good = 0;
        for (const CellFile& file : files)
        {
            const std::optional<CellMap> original
                = partial.load(file.mCellX, file.mCellY, file.mParameterDigest, file.mInputDigest);
            CellMap back;
            if (original && !written->readCell(file.mCellX, file.mCellY, back) && back == *original)
                ++good;
        }
        mResult.mReadBack = good;
        std::error_code sizeError;
        mResult.mPackBytes = std::filesystem::file_size(path, sizeError);
        if (good != mCells.size())
        {
            stopWith(Error{ "readBack", 0, "the pack that was written does not read back the same" });
            return;
        }
        partial.clear();
        mResult.mFinished = true;
        mPhase = Phase::Done;
    }
}
