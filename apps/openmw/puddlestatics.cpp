#include "puddlestatics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>

#include <edenarchive/hash.hpp>

#include <components/debug/debuglog.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include "mwbase/environment.hpp"
#include "mwbase/world.hpp"
#include "mwphysics/joltlayers.hpp"
#include "mwphysics/physicssystem.hpp"
#include "mwrender/distantobjects.hpp"
#include "mwworld/cellstore.hpp"
#include "mwworld/esmstore.hpp"
#include "mwworld/ptr.hpp"

namespace OMW
{
    namespace
    {
        // Frames to wait after the player is in the right cell, so that its collision (and its neighbours') exists. The
        // sky-visibility bake waits 90 for the same reason.
        constexpr unsigned int settleFrames = 60;
        // Frames to wait for a cell before giving up on it, and tries before it is counted as failed.
        constexpr unsigned int giveUpFrames = 3000;
        constexpr unsigned int maxAttempts = 3;
        // Work between two frames of a survey or of casting (the window keeps drawing in between).
        constexpr std::chrono::milliseconds sliceTime{ 10 };
        // The rays run from this height to this one (Morrowind's land runs from about -2000 to 8000); what a bake ray
        // can hit: static geometry only, no terrain (it comes from the land records), no doors, actors or water.
        constexpr float rayTop = 20000.f;
        constexpr float rayBottom = -20000.f;
        constexpr int captureMask = MWPhysics::Layers::WORLD;
        // The texels cast between two looks at the clock.
        constexpr std::size_t raysPerLook = 256;

        void hashBytes(EdenArchive::Hasher& hasher, const void* data, std::size_t size)
        {
            hasher.update(data, size);
        }

        template <class T>
        void hashValue(EdenArchive::Hasher& hasher, const T& value)
        {
            hashBytes(hasher, &value, sizeof(T));
        }

        void hashText(EdenArchive::Hasher& hasher, std::string_view text)
        {
            hashValue(hasher, static_cast<std::uint64_t>(text.size()));
            hashBytes(hasher, text.data(), text.size());
        }
    }

    StaticRefs::StaticRefs(const MWWorld::ESMStore& store, const VFS::Manager& vfs)
        : mStore(store)
        , mVfs(vfs)
    {
    }

    Puddle::Digest StaticRefs::modelHash(const std::string& path)
    {
        const auto found = mModelHashes.find(path);
        if (found != mModelHashes.end())
            return found->second;
        Puddle::Digest digest{};
        try
        {
            Files::IStreamPtr stream = mVfs.get(VFS::Path::Normalized(path));
            std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
            EdenArchive::Hasher hasher;
            hasher.update(bytes.data(), bytes.size());
            digest = hasher.finalize();
        }
        catch (const std::exception&)
        {
            // A model that cannot be read hashes to zeros: the digest then differs from one made with the model.
        }
        mModelHashes.emplace(path, digest);
        return digest;
    }

    bool StaticRefs::entry(std::int32_t cellX, std::int32_t cellY, Entry& out)
    {
        const std::pair<std::int32_t, std::int32_t> key{ cellX, cellY };
        auto found = mCache.find(key);
        if (found == mCache.end())
        {
            std::optional<Entry> made;
            try
            {
                Entry e;
                EdenArchive::Hasher hasher;
                hashValue(hasher, cellX);
                hashValue(hasher, cellY);
                // std::map order: the same list in the same order whatever the content files' order.
                for (const auto& [refNum, ref] : MWRender::collectDistantReferences({ cellX, cellY }, mStore))
                {
                    if (Misc::ResourceHelpers::isHiddenMarker(ref.mRefID))
                        continue;
                    const int type = mStore.findStatic(ref.mRefID);
                    const std::string_view model = MWRender::distantObjectModel(type, ref.mRefID, mStore);
                    if (model.empty())
                        continue;
                    const std::string path
                        = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(model)).value();
                    ++e.mCount;
                    hashValue(hasher, refNum.mIndex);
                    hashValue(hasher, refNum.mContentFile);
                    hashText(hasher, ref.mRefID.toDebugString());
                    hashText(hasher, path);
                    const Puddle::Digest hash = modelHash(path);
                    hashBytes(hasher, hash.data(), hash.size());
                    hashBytes(hasher, ref.mPos.pos, sizeof(ref.mPos.pos));
                    hashBytes(hasher, ref.mPos.rot, sizeof(ref.mPos.rot));
                    hashValue(hasher, ref.mScale);
                }
                e.mDigest = hasher.finalize();
                made = e;
            }
            catch (const std::exception& error)
            {
                Log(Debug::Warning) << "Puddle statics: cannot read the references of cell (" << cellX << ", " << cellY
                                    << "): " << error.what();
            }
            found = mCache.emplace(key, std::move(made)).first;
        }
        if (!found->second)
            return false;
        out = *found->second;
        return true;
    }

    CaptureSource::CaptureSource(
        RefsProvider& refs, Puddle::CaptureStore store, std::uint32_t side, float spacing, std::size_t cacheCells)
        : mRefs(refs)
        , mStore(std::move(store))
        , mSide(side)
        , mSpacing(spacing)
        , mCacheCells(std::max<std::size_t>(cacheCells, 1))
    {
    }

    bool CaptureSource::refsDigest(std::int32_t cellX, std::int32_t cellY, Puddle::Digest& digest)
    {
        RefsProvider::Entry e;
        if (!mRefs.entry(cellX, cellY, e))
            return false;
        digest = e.mDigest;
        return true;
    }

    bool CaptureSource::cellTops(std::int32_t cellX, std::int32_t cellY, std::vector<float>& tops)
    {
        RefsProvider::Entry e;
        if (!mRefs.entry(cellX, cellY, e))
            return false;
        const std::size_t texels = static_cast<std::size_t>(mSide) * mSide;
        if (e.mCount == 0)
        {
            tops.assign(texels, std::numeric_limits<float>::quiet_NaN());
            return true;
        }
        const std::pair<std::int32_t, std::int32_t> key{ cellX, cellY };
        const auto cached = mCache.find(key);
        if (cached != mCache.end())
        {
            tops = cached->second;
            return true;
        }
        std::optional<std::vector<float>> loaded = mStore.load(cellX, cellY, mSide, mSpacing, e.mDigest);
        if (!loaded || loaded->size() != texels)
            return false;
        if (mCache.size() >= mCacheCells && !mOrder.empty())
        {
            mCache.erase(mOrder.front());
            mOrder.pop_front();
        }
        mCache.emplace(key, *loaded);
        mOrder.push_back(key);
        tops = std::move(*loaded);
        return true;
    }

    StaticCapturer::StaticCapturer(RefsProvider& refs, Puddle::CaptureStore store, Config config)
        : mRefs(refs)
        , mStore(std::move(store))
        , mConfig(std::move(config))
    {
    }

    void StaticCapturer::nextCell()
    {
        ++mNext;
        mEntered = false;
        mWaited = 0;
        mSettled = 0;
        mAttempts = 0;
        mCasting = false;
        mRay = 0;
        mTops.clear();
    }

    bool StaticCapturer::castSome()
    {
        const auto* physics = MWBase::Environment::get().getWorld()->getPhysicsSystem();
        const auto [cx, cy] = mToCapture[mNext];
        const auto end = std::chrono::steady_clock::now() + sliceTime;
        const std::size_t texels = static_cast<std::size_t>(mConfig.mSide) * mConfig.mSide;
        while (mRay < texels)
        {
            for (std::size_t n = 0; n < raysPerLook && mRay < texels; ++n, ++mRay)
            {
                const float x = static_cast<float>(cx) * mConfig.mCellSize
                    + static_cast<float>(mRay % mConfig.mSide) * mConfig.mSpacing;
                const float y = static_cast<float>(cy) * mConfig.mCellSize
                    + static_cast<float>(mRay / mConfig.mSide) * mConfig.mSpacing;
                const auto result = physics->castRay(vsg::vec3(x, y, rayTop), vsg::vec3(x, y, rayBottom), captureMask);
                if (result.mHit && std::isfinite(result.mHitPos.z)
                    && std::abs(result.mHitPos.z) <= Puddle::staticMaxAbsHeight)
                    mTops[mRay] = result.mHitPos.z;
            }
            if (std::chrono::steady_clock::now() >= end)
                break;
        }
        return mRay >= texels;
    }

    bool StaticCapturer::update()
    {
        if (!surveyDone())
        {
            // The survey: which cells have references, which have a capture that is still current.
            const auto end = std::chrono::steady_clock::now() + sliceTime;
            do
            {
                const auto [cx, cy] = mConfig.mCells[mSurveyNext++];
                RefsProvider::Entry e;
                if (!mRefs.entry(cx, cy, e))
                {
                    ++mFailed;
                    continue;
                }
                if (e.mCount == 0)
                {
                    ++mEmpty;
                    continue;
                }
                if (mStore.load(cx, cy, mConfig.mSide, mConfig.mSpacing, e.mDigest))
                {
                    ++mReused;
                    continue;
                }
                mToCapture.emplace_back(cx, cy);
                mToCaptureDigest.push_back(e.mDigest);
            } while (!surveyDone() && std::chrono::steady_clock::now() < end);
            if (surveyDone())
                Log(Debug::Info) << "Puddle statics: " << mConfig.mCells.size() << " cells surveyed, " << mEmpty
                                 << " without references that can make a surface, " << mReused
                                 << " with a current capture, " << mToCapture.size() << " to capture, " << mFailed
                                 << " unreadable.";
            return false;
        }
        if (mNext >= mToCapture.size())
            return true;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const auto [cx, cy] = mToCapture[mNext];
        if (!mEntered)
        {
            ESM::Position pos;
            pos.pos[0] = (static_cast<float>(cx) + 0.5f) * mConfig.mCellSize;
            pos.pos[1] = (static_cast<float>(cy) + 0.5f) * mConfig.mCellSize;
            pos.pos[2] = 0.f;
            pos.rot[0] = pos.rot[1] = pos.rot[2] = 0.f;
            world->changeToCell(ESM::RefId::esm3ExteriorCell(cx, cy), pos, true);
            mEntered = true;
            mWaited = 0;
            mSettled = 0;
            mCasting = false;
            return false;
        }
        const MWWorld::CellStore* cell = world->getPlayerPtr().getCell();
        const bool right = cell && cell->getCell()->isExterior() && cell->getCell()->getGridX() == cx
            && cell->getCell()->getGridY() == cy;
        if (!mCasting)
        {
            mSettled = right ? mSettled + 1 : 0;
            if (++mWaited > giveUpFrames)
            {
                Log(Debug::Error) << "Puddle statics: exterior cell " << cx << "," << cy
                                  << " did not load; no capture for it.";
                ++mFailed;
                nextCell();
                return false;
            }
            if (mSettled < settleFrames)
                return false;
            mCasting = true;
            mRay = 0;
            mTops.assign(
                static_cast<std::size_t>(mConfig.mSide) * mConfig.mSide, std::numeric_limits<float>::quiet_NaN());
        }
        else if (!right)
        {
            // The player left the cell: the world the rays hit is gone. Try the cell again.
            if (++mAttempts >= maxAttempts)
            {
                Log(Debug::Error) << "Puddle statics: the player left cell " << cx << "," << cy
                                  << " during the capture " << maxAttempts << " times; no capture for it.";
                ++mFailed;
                nextCell();
                return false;
            }
            mEntered = false;
            return false;
        }
        if (!castSome())
            return false;
        Puddle::StaticCapture capture;
        capture.mCellX = cx;
        capture.mCellY = cy;
        capture.mSide = mConfig.mSide;
        capture.mSpacing = mConfig.mSpacing;
        capture.mRefsDigest = mToCaptureDigest[mNext];
        capture.mTops = std::move(mTops);
        if (const Puddle::Error error = mStore.store(capture))
        {
            Log(Debug::Error) << "Puddle statics: cannot store the capture of cell " << cx << "," << cy << ": "
                              << error.describe();
            ++mFailed;
        }
        else
            ++mCaptured;
        if (mCaptured % 25 == 0 && mCaptured > 0)
            Log(Debug::Info) << "Puddle statics: " << mCaptured << " of " << mToCapture.size() << " cells captured.";
        nextCell();
        return false;
    }
}
