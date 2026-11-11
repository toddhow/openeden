#ifndef OPENMW_PUDDLESTATICS_H
#define OPENMW_PUDDLESTATICS_H

// OpenEden (task 0095, stage W-c): static surfaces for the puddle bake, on the engine side. components/puddle has the
// pure parts (the capture file, the filter, the baker); this file has what needs the game:
//  * `StaticRefs`: the digest of the references (and the models' contents) of a cell, from the content files and the
//    VFS, with no cell loaded. The bake's input digest covers it, and the run-time staleness check recomputes it.
//  * `CaptureSource`: the `StaticSurfaceSource` the baker reads, over the capture folder and the digests.
//  * `StaticCapturer`: runs inside the game, one step per frame, moves the player to each cell that needs a capture,
//    waits for the cell's collision, casts a ray straight down at every texel and writes the capture file. Like
//    SkyVisBaker (apps/openmw/skyvisbaker.cpp), whose way of loading a cell it follows.
// Never commit the output: it is derived from the game files of the user.

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <components/puddle/puddlestatics.hpp>

namespace MWWorld
{
    class ESMStore;
}

namespace VFS
{
    class Manager;
}

namespace OMW
{
    /// What the baker needs to know about the references of one cell.
    class RefsProvider
    {
    public:
        struct Entry
        {
            Puddle::Digest mDigest{};
            /// References that can make a static surface (a cell with none needs no capture).
            std::size_t mCount = 0;
        };
        virtual ~RefsProvider() = default;
        /// False when the references of the cell could not be read.
        virtual bool entry(std::int32_t cellX, std::int32_t cellY, Entry& out) = 0;
    };

    /// The references of a cell as MWRender::collectDistantReferences gives them (the statics, activators and doors the
    /// distant objects draw; the same list the LOD cluster bake uses), each with its model's content hash. One thread
    /// at a time (it keeps caches).
    class StaticRefs final : public RefsProvider
    {
    public:
        StaticRefs(const MWWorld::ESMStore& store, const VFS::Manager& vfs);
        bool entry(std::int32_t cellX, std::int32_t cellY, Entry& out) override;

    private:
        Puddle::Digest modelHash(const std::string& path);

        const MWWorld::ESMStore& mStore;
        const VFS::Manager& mVfs;
        std::map<std::pair<std::int32_t, std::int32_t>, std::optional<Entry>> mCache;
        std::map<std::string, Puddle::Digest> mModelHashes;
    };

    /// The baker's source of static surfaces: digests from the references, tops from the capture folder.
    class CaptureSource final : public Puddle::StaticSurfaceSource
    {
    public:
        CaptureSource(RefsProvider& refs, Puddle::CaptureStore store, std::uint32_t side, float spacing,
            std::size_t cacheCells = 96);

        bool refsDigest(std::int32_t cellX, std::int32_t cellY, Puddle::Digest& digest) override;
        /// A cell with no references that can make a surface has an empty capture (all NaN) without a file; any other
        /// needs a stored capture made for exactly these references.
        bool cellTops(std::int32_t cellX, std::int32_t cellY, std::vector<float>& tops) override;

    private:
        RefsProvider& mRefs;
        Puddle::CaptureStore mStore;
        std::uint32_t mSide;
        float mSpacing;
        std::size_t mCacheCells;
        std::map<std::pair<std::int32_t, std::int32_t>, std::vector<float>> mCache;
        std::deque<std::pair<std::int32_t, std::int32_t>> mOrder;
    };

    /// Captures the cells that need it. Call `update` once per running frame until it returns true.
    class StaticCapturer
    {
    public:
        struct Config
        {
            std::vector<std::pair<std::int32_t, std::int32_t>> mCells;
            std::uint32_t mSide = 0;
            float mSpacing = 0.f;
            float mCellSize = 0.f;
        };

        StaticCapturer(RefsProvider& refs, Puddle::CaptureStore store, Config config);

        bool update();

        std::size_t surveyed() const { return mSurveyNext; }
        std::size_t total() const { return mConfig.mCells.size(); }
        std::size_t toCapture() const { return mToCapture.size(); }
        std::size_t captured() const { return mCaptured; }
        std::size_t reused() const { return mReused; }
        std::size_t empty() const { return mEmpty; }
        std::size_t failed() const { return mFailed; }
        bool surveyDone() const { return mSurveyNext >= mConfig.mCells.size(); }

    private:
        void nextCell();
        bool castSome();

        RefsProvider& mRefs;
        Puddle::CaptureStore mStore;
        Config mConfig;
        std::size_t mSurveyNext = 0;
        std::vector<std::pair<std::int32_t, std::int32_t>> mToCapture;
        std::vector<Puddle::Digest> mToCaptureDigest;
        std::size_t mNext = 0;
        bool mEntered = false;
        unsigned int mWaited = 0;
        unsigned int mSettled = 0;
        unsigned int mAttempts = 0;
        bool mCasting = false;
        std::size_t mRay = 0;
        std::vector<float> mTops;
        std::size_t mCaptured = 0;
        std::size_t mReused = 0;
        std::size_t mEmpty = 0;
        std::size_t mFailed = 0;
    };
}

#endif
