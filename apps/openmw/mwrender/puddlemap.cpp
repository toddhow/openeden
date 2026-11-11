#include "puddlemap.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <utility>

#include <components/debug/debuglog.hpp>
#include <components/puddle/puddlebake.hpp>
#include <components/puddle/puddlegpu.hpp>
#include <components/puddle/puddlepack.hpp>
#include <components/puddle/puddlewindow.hpp>
#include <components/render/puddlemapgpu.hpp>

#include "../puddleland.hpp"
#include "../puddlestatics.hpp"

namespace MWRender
{
    PuddleMapRuntime::PuddleMapRuntime(Setup setup)
        : mSetup(std::move(setup))
    {
        mThread = std::thread([this] { run(); });
    }

    PuddleMapRuntime::~PuddleMapRuntime()
    {
        {
            std::lock_guard lock(mMutex);
            mStop = true;
        }
        mWake.notify_all();
        if (mThread.joinable())
            mThread.join();
    }

    void PuddleMapRuntime::setCamera(std::int32_t cellX, std::int32_t cellY)
    {
        std::lock_guard lock(mMutex);
        if (mHaveRequest && mRequestX == cellX && mRequestY == cellY)
            return;
        mHaveRequest = true;
        mRequestX = cellX;
        mRequestY = cellY;
        mWake.notify_one();
    }

    bool PuddleMapRuntime::takeNewest(std::uint32_t* words, std::size_t count)
    {
        std::lock_guard lock(mMutex);
        if (!mReadyNew)
            return false;
        std::copy_n(mReady.begin(), std::min(count, mReady.size()), words);
        mReadyNew = false;
        return true;
    }

    bool PuddleMapRuntime::packLoaded() const
    {
        std::lock_guard lock(mMutex);
        return mPackLoaded;
    }

    void PuddleMapRuntime::run()
    {
        try
        {
            if (!std::filesystem::exists(mSetup.mPackPath))
            {
                Log(Debug::Info) << "Puddle map: no pack at " << mSetup.mPackPath.string()
                                 << " (run openmw --bake-puddles); the noise puddles stay.";
                return;
            }
            Puddle::Error error;
            std::shared_ptr<const Puddle::PuddlePack> pack = Puddle::PuddlePack::open(mSetup.mPackPath, error);
            if (!pack)
            {
                Log(Debug::Warning) << "Puddle map: cannot use " << mSetup.mPackPath.string() << " ("
                                    << error.describe() << "); the noise puddles stay.";
                return;
            }
            const Puddle::BakeSettings settings = pack->manifest().mSettings;
            const std::uint32_t texels = settings.texelsPerCell();
            if (!Puddle::gpuTexelsFit(texels))
            {
                Log(Debug::Warning) << "Puddle map: the pack has " << texels << " texels a cell side, more than the "
                                    << Render::PuddleMap::maxTexelsPerSide
                                    << " the renderer holds (bake with a spacing of 64 units or more); the noise "
                                       "puddles stay.";
                return;
            }
            if (mSetup.mStorage == nullptr || mSetup.mStore == nullptr)
                return;

            OMW::StorageHeights heights(*mSetup.mStorage, *mSetup.mStore);
            // A pack baked with static surfaces is checked against the references of now (their digest, which the
            // content files and the models give without loading a cell); only the digest is asked for, never the tops.
            struct RefsOnly final : Puddle::StaticSurfaceSource
            {
                explicit RefsOnly(OMW::RefsProvider& refs)
                    : mRefs(refs)
                {
                }
                bool refsDigest(std::int32_t x, std::int32_t y, Puddle::Digest& digest) override
                {
                    OMW::RefsProvider::Entry e;
                    if (!mRefs.entry(x, y, e))
                        return false;
                    digest = e.mDigest;
                    return true;
                }
                bool cellTops(std::int32_t, std::int32_t, std::vector<float>&) override { return false; }
                OMW::RefsProvider& mRefs;
            };
            std::unique_ptr<OMW::StaticRefs> refs;
            std::unique_ptr<RefsOnly> statics;
            if (settings.mStatics)
            {
                if (mSetup.mVfs == nullptr)
                {
                    Log(Debug::Warning) << "Puddle map: the pack has static surfaces and the renderer has no VFS to "
                                           "check them with; the noise puddles stay.";
                    return;
                }
                refs = std::make_unique<OMW::StaticRefs>(*mSetup.mStore, *mSetup.mVfs);
                statics = std::make_unique<RefsOnly>(*refs);
            }
            Puddle::PuddleWindow window(
                pack,
                [&](std::int32_t x, std::int32_t y) -> std::optional<Puddle::Digest> {
                    try
                    {
                        return Puddle::inputDigest(heights, x, y, settings, statics.get());
                    }
                    catch (const std::exception&)
                    {
                        return std::nullopt;
                    }
                },
                [](const std::string& text) { Log(Debug::Warning) << text; },
                static_cast<int>(Render::PuddleMap::windowRadius));
            {
                std::lock_guard lock(mMutex);
                mPackLoaded = true;
            }
            Log(Debug::Info) << "Puddle map: pack with " << pack->cells().size() << " cells, " << texels
                             << " texels a cell side.";

            Puddle::GpuWindowParams params;
            params.mTexelsPerCell = texels;
            params.mSpacingUnits = settings.mSpacingUnits;
            params.mCellSizeUnits = settings.mCellSizeUnits;
            params.mSupply = mSetup.mSupply;
            params.mAbsorption = mSetup.mAbsorption;
            params.mDebugMode = mSetup.mDebugMode;
            params.mKeepStaticNoise = mSetup.mKeepStaticNoise;
            std::vector<std::uint32_t> staging(
                Render::PuddleMap::bufferWordsWithTops(Render::PuddleMap::windowSide, texels));
            {
                // Both buffers have their final size from here on, so swapping them never leaves one empty.
                std::lock_guard lock(mMutex);
                mReady.assign(staging.size(), 0u);
            }

            bool havePacked = false;
            std::int32_t packedX = 0;
            std::int32_t packedY = 0;
            for (;;)
            {
                std::int32_t cx = 0;
                std::int32_t cy = 0;
                {
                    std::unique_lock lock(mMutex);
                    mWake.wait(lock, [&] {
                        return mStop || (mHaveRequest && (!havePacked || mRequestX != packedX || mRequestY != packedY));
                    });
                    if (mStop)
                        return;
                    cx = mRequestX;
                    cy = mRequestY;
                }
                window.update(cx, cy);
                params.mCentreX = cx;
                params.mCentreY = cy;
                const Puddle::GpuPackResult packed = Puddle::packWindow(staging.data(), staging.size(), params,
                    [&](std::int32_t x, std::int32_t y) { return window.find(x, y); });
                if (packed.mSkipped > 0)
                    Log(Debug::Warning) << "Puddle map: " << packed.mSkipped << " cell(s) near (" << cx << ", " << cy
                                        << ") do not fit the renderer's "
                                           "buffer and are not used.";
                Log(Debug::Verbose) << "Puddle map: window at (" << cx << ", " << cy << "), " << packed.mPacked
                                    << " cells.";
                havePacked = true;
                packedX = cx;
                packedY = cy;
                std::lock_guard lock(mMutex);
                std::swap(mReady, staging);
                mReadyNew = true;
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Puddle map: stopped by an exception (" << e.what() << "); the noise puddles stay.";
        }
    }
}
