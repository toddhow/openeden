#include "puddlebaker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadland.hpp>
#include <components/files/conversion.hpp>
#include <components/puddle/puddlebake.hpp>
#include <components/puddle/puddlejob.hpp>
#include <components/render/processmemory.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/terrain/bounds.hpp>

#include "mwbase/environment.hpp"
#include "mwbase/world.hpp"
#include "mwrender/terrainstorage.hpp"
#include "mwworld/esmstore.hpp"

namespace OMW
{
    namespace
    {
        // Time spent baking between two frames; the window keeps drawing in between (at least one cell is baked).
        constexpr std::chrono::milliseconds sliceTime{ 10 };
        // The water level of an exterior cell: MWWorld::Cell sets mWaterHeight to -1 for every exterior cell
        // (apps/openmw/mwworld/cell.cpp, the constructor for ESM::Cell records: "if (isExterior()) mWaterHeight = -1").
        constexpr std::int32_t exteriorWaterLevel = -1;

        // The land of the loaded game data through the terrain storage's vertex data: the heights the live terrain is
        // built from (lod 0, one cell, 65 x 65 vertices, x fastest, south row first).
        class StorageHeights final : public Puddle::LandHeightSource
        {
        public:
            StorageHeights(MWRender::TerrainStorage& storage, const MWWorld::ESMStore& store)
                : mStorage(storage)
                , mStore(store)
            {
            }

            bool cellHeights(std::int32_t cellX, std::int32_t cellY, std::vector<float>& heights) override
            {
                if (mStore.get<ESM::Land>().search(cellX, cellY) == nullptr)
                    return false;
                Terrain::Bounds bounds;
                bounds.size = 1.f;
                bounds.min = vsg::vec2(static_cast<float>(cellX), static_cast<float>(cellY));
                const Terrain::Storage::VertexData data = mStorage.getVertexData(0, bounds);
                const std::size_t count = static_cast<std::size_t>(data.heights->width()) * data.heights->height();
                heights.assign(data.heights->data(), data.heights->data() + count);
                return true;
            }

        private:
            MWRender::TerrainStorage& mStorage;
            const MWWorld::ESMStore& mStore;
        };

        bool parseCell(const std::string& text, std::int32_t& x, std::int32_t& y)
        {
            const std::size_t comma = text.find(',');
            if (comma == std::string::npos)
                return false;
            try
            {
                std::size_t used = 0;
                x = std::stoi(text.substr(0, comma), &used);
                if (used != comma)
                    return false;
                const std::string rest = text.substr(comma + 1);
                y = std::stoi(rest, &used);
                return used == rest.size();
            }
            catch (const std::exception&)
            {
                return false;
            }
        }

        bool parseRange(const std::string& text, std::optional<std::array<std::int32_t, 4>>& range)
        {
            range.reset();
            if (text == "all")
                return true;
            std::int32_t x0 = 0;
            std::int32_t y0 = 0;
            std::int32_t x1 = 0;
            std::int32_t y1 = 0;
            const std::size_t colon = text.find(':');
            if (colon == std::string::npos)
            {
                if (!parseCell(text, x0, y0))
                    return false;
                x1 = x0;
                y1 = y0;
            }
            else if (!parseCell(text.substr(0, colon), x0, y0) || !parseCell(text.substr(colon + 1), x1, y1))
                return false;
            range
                = std::array<std::int32_t, 4>{ std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1) };
            return true;
        }

        float percentile(std::vector<float> values, double fraction)
        {
            if (values.empty())
                return 0.f;
            std::sort(values.begin(), values.end());
            return values[std::min(values.size() - 1, static_cast<std::size_t>(fraction * values.size()))];
        }
    }

    struct PuddleBaker::State
    {
        State(std::string range, std::filesystem::path out, Resource::ResourceSystem& resources)
            : mRange(std::move(range))
            , mOut(std::move(out))
            , mStorage(&resources)
        {
        }

        std::string mRange;
        std::filesystem::path mOut;
        MWRender::TerrainStorage mStorage;
        std::unique_ptr<StorageHeights> mHeights;
        std::unique_ptr<Puddle::BakeJob> mJob;
        bool mStarted = false;
        bool mFinished = false;
        bool mFailed = false;
        bool mStatistics = false;
        std::chrono::steady_clock::time_point mStart;
        std::chrono::steady_clock::time_point mLastProgress;
    };

    PuddleBaker::PuddleBaker(std::string range, std::filesystem::path outFolder, Resource::ResourceSystem& resources)
        : mState(std::make_unique<State>(std::move(range), std::move(outFolder), resources))
    {
    }

    PuddleBaker::~PuddleBaker() = default;

    bool PuddleBaker::failed() const
    {
        return mState->mFailed;
    }

    bool PuddleBaker::update()
    {
        State& state = *mState;
        if (state.mFinished)
            return true;

        if (!state.mStarted)
        {
            state.mStarted = true;
            state.mStart = state.mLastProgress = std::chrono::steady_clock::now();
            const MWWorld::ESMStore* store = MWBase::Environment::get().getESMStore();
            state.mStorage.setStore(store);
            // The bake runs inside a new game, where the player starts in a fight and dies after about ten seconds; a
            // death stops the game's update and with it the bake (found by the object baker, task 0059). A bake is no
            // play: switch god mode on.
            if (MWBase::World* world = MWBase::Environment::get().getWorld();
                world != nullptr && !world->getGodModeState())
                world->toggleGodMode();

            Puddle::JobConfig config;
            if (!parseRange(state.mRange, config.mRange))
            {
                Log(Debug::Error) << "Puddle bake: \"" << state.mRange
                                  << "\" is not \"all\", \"<x>,<y>\" or \"<x0>,<y0>:<x1>,<y1>\"; nothing baked.";
                state.mFinished = true;
                state.mFailed = true;
                return true;
            }
            const auto& rendering = Settings::rendering();
            Puddle::BakeSettings& s = config.mSettings;
            s.mSpacingUnits = static_cast<std::uint32_t>(rendering.mPuddleBakeSpacing.get());
            s.mCapacityCapSteps
                = static_cast<std::uint32_t>(std::lround(rendering.mPuddleBakeCapacityCap.get() * 16.f));
            s.mMinPondTexels = static_cast<std::uint32_t>(rendering.mPuddleBakeMinArea.get());
            s.mCatchmentRadiusUnits = static_cast<std::uint32_t>(rendering.mPuddleBakeCatchmentRadius.get());
            s.mHalo = static_cast<std::uint32_t>(rendering.mPuddleBakeHalo.get());
            s.mStatics = rendering.mPuddleBakeStatics.get();
            s.mWaterLevelUnits = exteriorWaterLevel;
            // The cell size and the vertices come from the storage, never from a literal here.
            s.mCellSizeUnits = static_cast<std::uint32_t>(state.mStorage.cellWorldSize);
            s.mCellVertices = state.mStorage.cellVertices;
            if (const Puddle::Error error = Puddle::validateSettings(s))
            {
                Log(Debug::Error) << "Puddle bake: the settings cannot be used (" << error.mField
                                  << "): " << error.mMessage << "; nothing baked.";
                state.mFinished = true;
                state.mFailed = true;
                return true;
            }
            for (const ESM::Land& land : store->get<ESM::Land>())
                config.mLandCells.emplace_back(land.mX, land.mY);
            config.mStatistics = rendering.mPuddleBakeStatistics.get();
            state.mStatistics = config.mStatistics;
            config.mFolder = state.mOut / "openeden" / "puddles";
            state.mHeights = std::make_unique<StorageHeights>(state.mStorage, *store);
            Log(Debug::Info) << "Puddle bake: " << config.mLandCells.size() << " land cells, spacing "
                             << s.mSpacingUnits << " units (" << s.texelsPerCell() << " texels a cell), deepest puddle "
                             << s.capacityCap() << " units, smallest pond " << s.mMinPondTexels
                             << " texels, catchment radius " << s.mCatchmentRadiusUnits << " units, halo " << s.mHalo
                             << " cells, range " << state.mRange << ".";
            state.mJob = std::make_unique<Puddle::BakeJob>(*state.mHeights, std::move(config));
        }

        bool done = false;
        try
        {
            done = state.mJob->step(sliceTime);
        }
        catch (const std::exception& e)
        {
            // A cell that fails with an exception (the heap, say) ends the bake with a message; the cells already
            // baked are in the work folder and the next run resumes from them.
            Log(Debug::Error) << "Puddle bake stopped by an exception after " << state.mJob->unitsDone() << " of "
                              << state.mJob->unitsTotal() << " cells in this pass: " << e.what() << " (working set "
                              << Render::processWorkingSetBytes() / (1024 * 1024) << " MiB).";
            state.mFinished = true;
            state.mFailed = true;
            return true;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!done && now - state.mLastProgress > std::chrono::seconds(10))
        {
            state.mLastProgress = now;
            Log(Debug::Info) << "Puddle bake: " << state.mJob->unitsDone() << " of " << state.mJob->unitsTotal()
                             << " cells in this pass, " << state.mJob->result().mBaked << " baked so far (working set "
                             << Render::processWorkingSetBytes() / (1024 * 1024) << " MiB).";
        }
        if (!done)
            return false;

        state.mFinished = true;
        const Puddle::JobResult& r = state.mJob->result();
        const double seconds = std::chrono::duration<double>(now - state.mStart).count();
        if (r.mError)
        {
            state.mFailed = true;
            Log(Debug::Error) << "Puddle bake failed, the old pack (if any) is unchanged: " << r.mError.describe();
            return true;
        }
        if (r.mOldPackOtherSettings)
        {
            Log(Debug::Warning) << "Puddle bake: the pack on disk was made with other settings or another layout, so "
                                   "none of its cells was reused"
                                << (state.mRange != "all" ? "; because only the cells of the range were baked, the new "
                                                            "pack holds only those cells (bake \"all\" for the whole "
                                                            "world)"
                                                          : "")
                                << ".";
        }
        if (r.mOldPackUnreadable)
            Log(Debug::Warning) << "Puddle bake: the pack on disk could not be opened and is replaced.";
        if (r.mBadCells != 0)
        {
            Log(Debug::Warning) << "Puddle bake: " << r.mBadCells
                                << " cells skipped because a height was not finite or "
                                   "beyond +-1e7 (they have no map); the first: "
                                << r.mFirstBadCell;
        }
        const std::filesystem::path path = state.mOut / "openeden" / "puddles" / "puddles.edenpak";
        if (r.mUpToDate)
        {
            Log(Debug::Info) << "Puddle bake: " << Files::pathToUnicodeString(path)
                             << " is already up to date (generation " << r.mGeneration << ", " << r.mCellsInPack
                             << " cells), nothing written, " << seconds << " s.";
            return true;
        }
        Log(Debug::Info) << "Puddle bake: wrote " << Files::pathToUnicodeString(path) << " (generation "
                         << r.mGeneration << "), " << r.mCellsInPack << " cells (" << r.mBaked << " baked, "
                         << r.mFromPack << " kept from the old pack, " << r.mFromPartial << " resumed, " << r.mSkipped
                         << " skipped), " << r.mPackBytes << " bytes, " << r.mReadBack << " read back fine, " << seconds
                         << " s.";
        Log(Debug::Info) << "Puddle bake: " << r.mPitsInCells << " pits in the cells baked (" << r.mPitsDroppedByArea
                         << " small ponds dropped), " << r.mWetTexels << " wet texels, " << r.mHeightsClamped
                         << " cells with heights beyond the 16 bit range, " << r.mPondTexelsDroppedClamped
                         << " pond texels dropped under such heights.";
        Log(Debug::Info) << "Puddle bake: catchment ratio of the pits, bounded: median "
                         << percentile(r.mKappasBounded, 0.5) << ", 90th " << percentile(r.mKappasBounded, 0.9)
                         << ", max " << percentile(r.mKappasBounded, 1.0)
                         << "; beyond the stored range (131072): " << r.mClippedBounded << ".";
        if (state.mStatistics)
        {
            Log(Debug::Info) << "Puddle bake: without the radius bound: median " << percentile(r.mKappasUnbounded, 0.5)
                             << ", 90th " << percentile(r.mKappasUnbounded, 0.9) << ", max "
                             << percentile(r.mKappasUnbounded, 1.0)
                             << "; beyond the stored range (131072): " << r.mClippedUnbounded << ".";
        }
        return true;
    }
}
