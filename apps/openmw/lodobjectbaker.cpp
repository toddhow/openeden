#include "lodobjectbaker.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iterator>
#include <sstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

#include <components/debug/debuglog.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/files/conversion.hpp>
#include <components/lod/clusterbuild.hpp>
#include <components/lod/clusterpack.hpp>
#include <components/lod/lodworld.hpp>
#include <components/lod/clustersway.hpp>
#include <components/lod/nifflatten.hpp>
#include <components/lod/sceneryscript.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/nif/niffile.hpp>
#include <components/nifscene/build.hpp>
#include <components/worldmodel/store.hpp>
#include <openeden/game/world_source.hpp>
#include <openeden/game/world_terrain.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>

#include "lodneutralobjects.hpp"
#include "mwbase/environment.hpp"
#include "mwbase/extensionmanager.hpp"
#include "mwbase/world.hpp"
#include "mwrender/distantobjectcells.hpp"
#include "mwrender/distantobjects.hpp"
#include "mwrender/lodterrain.hpp"
#include "mwworld/esmstore.hpp"

namespace OMW
{
    namespace
    {
        // Time spent baking between two frames; the window keeps drawing in between.
        constexpr std::chrono::milliseconds sliceTime{ 10 };
        constexpr const char* worldspaceName = "Morrowind";
        constexpr std::uint32_t bakerVersion
            = 14; // 14: property controllers keep a model out (task 0096 a2); effects and scenery words in the digest
        // The largest simplification error of a level in world units (Lod::clusterLevelError: task 0059's values for
        // levels 1 to 3, level 4 from the same growth, task 0066 stage d).
        std::uint32_t levelError(std::uint32_t level)
        {
            return static_cast<std::uint32_t>(Lod::clusterLevelError(level));
        }
        // An object smaller than this many errors is left out of the level (task 0059, "classification").
        constexpr float dropFactor = 0.75f;
        constexpr long long slowTileMilliseconds = 2000;

        enum class Phase
        {
            Collect,
            Models,
            Build,
            Write,
            Done,
        };

        struct PlacedObject
        {
            ESM::RefNum mRef;
            std::int32_t mCellX = 0, mCellY = 0; // where it stands
            std::string mRefId;
            std::size_t mModel = 0;
            float mPos[3] = { 0, 0, 0 };
            float mRot[3] = { 0, 0, 0 };
            float mScale = 1;
            // Task 0133 stage 2: a neutral object carries its transform as a matrix (mRef holds the placement id in its
            // two words, see Lod::clusterRefFromPlacement).
            bool mHasMatrix = false;
            std::array<double, 12> mMatrix{};
        };

        struct ModelEntry
        {
            std::string mPath;
            bool mOk = false;
            float mMaxScale = 0; // neutral bake: the largest scale any object of the model has
            bool mTooSmall = false; // neutral bake: too small for every level, dropped after the read
            Lod::FlattenedModel mFlat;
            EdenArchive::Hash mHash{};
        };

        // The facts of the bake that the log reports at the end.
        struct LevelTotals
        {
            std::size_t mTiles = 0;
            std::size_t mObjectsSeen = 0;
            std::size_t mObjectsIncluded = 0;
            std::array<std::size_t, static_cast<std::size_t>(Lod::ExcludeReason::Count)> mExcluded{};
            std::size_t mTrianglesBefore = 0;
            std::size_t mTrianglesAfter = 0;
            std::size_t mVertices = 0;
            float mMaxError = 0;
            std::vector<std::size_t> mGroups;
            std::size_t mEffectGroups = 0; // groups that carry an effect (task 0096 stage b)
            std::size_t mEffectTriangles = 0; // their triangles
        };

        template <class T>
        void hashValue(EdenArchive::Hasher& hasher, const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            hasher.update(&value, sizeof(T));
        }

        void hashText(EdenArchive::Hasher& hasher, std::string_view text)
        {
            hashValue(hasher, static_cast<std::uint64_t>(text.size()));
            hasher.update(text.data(), text.size());
        }

        std::array<double, 12> rowMajor(const vsg::dmat4& m)
        {
            std::array<double, 12> result{};
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 4; ++col)
                    result[row * 4 + col] = m[col][row];
            return result;
        }
    }

    struct LodObjectBaker::State
    {
        State(std::filesystem::path out, Resource::ResourceSystem& resources)
            : mOut(std::move(out))
            , mResources(resources)
        {
        }

        std::filesystem::path mOut;
        Resource::ResourceSystem& mResources;
        Phase mPhase = Phase::Collect;
        bool mStarted = false;
        std::chrono::steady_clock::time_point mStart;

        // Collect
        std::vector<std::pair<int, int>> mCells;
        std::size_t mNextCell = 0;
        std::vector<PlacedObject> mObjects;
        std::vector<ModelEntry> mModels;
        std::map<std::string, std::size_t> mModelIndex;
        std::size_t mReferencesSeen = 0, mNotStatic = 0, mNoModel = 0;
        // Task 0096 stage b: [WorldLod] object cluster effects / object cluster scenery, read when the bake starts.
        bool mUvEffects = false;
        bool mScenery = false;
        // Task 0096 stage c: [WorldLod] object cluster sway (and its word list and lean), read when the bake starts.
        bool mSway = false;
        std::string mSwayModels;
        float mSwayLean = 0;
        std::size_t mSceneryObjects = 0, mSceneryNamed = 0, mSceneryScripted = 0;

        // Task 0133 stage 2: a game pack's neutral world (Skyrim SE). Set by beginNeutral.
        struct NeutralCell
        {
            std::int32_t mX = 0, mY = 0;
            openeden::sdk::game::WorldId mId = openeden::sdk::game::kNoWorldId;
        };
        bool mNeutral = false;
        std::string mGameId;
        std::string mWorldName;
        std::string mManifestName;
        std::uint32_t mCellSize = 0; // world units, from the world
        NeutralWorld::NeutralWorldStore* mNeutralStore = nullptr;
        std::vector<NeutralCell> mNeutralCells;
        std::size_t mCellsFailed = 0, mDisabled = 0, mMarkers = 0, mTooSmallModels = 0, mTooSmallObjects = 0;
        bool mFinished = false;

        // Models
        std::size_t mNextModel = 0;
        std::size_t mModelsFailed = 0;

        // Build
        std::vector<std::pair<Lod::TileId, std::vector<std::size_t>>> mWork; // tile and object indices
        std::size_t mNextTile = 0;
        std::vector<Lod::BakedCluster> mClusters;
        LevelTotals mTotals[Lod::maxClusterLevel];
        std::size_t mDuplicates = 0;
        std::size_t mBadReferences = 0;
        Lod::ClusterManifest mManifest;

        // Write: the pack is written and read back on a worker; the destructor waits for it.
        std::future<void> mWrite;

        ~State()
        {
            if (mWrite.valid())
                mWrite.wait();
        }
    };

    LodObjectBaker::LodObjectBaker(std::filesystem::path outFolder, Resource::ResourceSystem& resources)
        : mState(std::make_unique<State>(std::move(outFolder), resources))
    {
    }

    LodObjectBaker::~LodObjectBaker() = default;

    namespace
    {
        // Reads one model and flattens it. The texture names become VFS paths.
        void loadModel(const VFS::Manager& vfs, ModelEntry& entry, bool uvEffects)
        {
            const VFS::Path::Normalized path(entry.mPath);
            {
                Files::IStreamPtr stream = vfs.get(path);
                std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                EdenArchive::Hasher hasher;
                hasher.update(bytes.data(), bytes.size());
                entry.mHash = hasher.finalize();
            }
            Nif::NIFFile file(path);
            Nif::Reader reader(file);
            reader.parse(vfs.get(path));
            for (const Nif::Record* root : file.mRoots)
            {
                const auto* node = dynamic_cast<const Nif::NiAVObject*>(root);
                if (node == nullptr)
                    continue;
                Lod::FlattenedModel part = Lod::flattenNifModel(*node, true, uvEffects);
                for (Lod::SourcePiece& piece : part.mPieces)
                    entry.mFlat.mPieces.push_back(std::move(piece));
                Lod::ObjectTraits& traits = entry.mFlat.mTraits;
                traits.mAnimated = traits.mAnimated || part.mTraits.mAnimated;
                traits.mSkinned = traits.mSkinned || part.mTraits.mSkinned;
                traits.mHasParticles = traits.mHasParticles || part.mTraits.mHasParticles;
                traits.mAlphaBlended = traits.mAlphaBlended || part.mTraits.mAlphaBlended;
                traits.mRadius = std::max(traits.mRadius, part.mTraits.mRadius);
            }
            Lod::ObjectTraits& traits = entry.mFlat.mTraits;
            traits.mKind = Lod::ObjectKind::Static;
            traits.mTriangleCount = 0;
            for (Lod::SourcePiece& piece : entry.mFlat.mPieces)
            {
                traits.mTriangleCount += static_cast<std::uint32_t>(piece.mIndices.size() / 3);
                if (!piece.mTexture.empty())
                    piece.mTexture
                        = Misc::ResourceHelpers::correctTexturePath(VFS::Path::Normalized(piece.mTexture), vfs).value();
            }
            entry.mOk = true;
        }

        // Task 0133 stage 2: a Skyrim model read through the scene reader and flattened (static shapes, node transforms
        // baked in), then turned into cluster pieces. The texture names become VFS paths; one that is not in the VFS is
        // dropped (the piece is then drawn untextured, as the live path draws a missing texture).
        void loadNeutralModel(const VFS::Manager& vfs, ModelEntry& entry)
        {
            const VFS::Path::Normalized path(entry.mPath);
            std::string bytes;
            {
                Files::IStreamPtr stream = vfs.get(path);
                bytes.assign((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                EdenArchive::Hasher hasher;
                hasher.update(bytes.data(), bytes.size());
                entry.mHash = hasher.finalize();
            }
            Files::IStreamPtr stream(new std::istringstream(std::move(bytes)));
            const NifScene::Scene scene = NifScene::readScene(std::move(stream), path, NifScene::SceneOptions{});
            const NifScene::Flat::FlatModel flat = NifScene::Flat::flatten(scene);
            entry.mFlat = NeutralLod::toFlattenedModel(flat, true);
            for (Lod::SourcePiece& piece : entry.mFlat.mPieces)
                if (!piece.mTexture.empty())
                {
                    piece.mTexture = NeutralLod::textureVfsPath(piece.mTexture);
                    if (!vfs.exists(VFS::Path::Normalized(piece.mTexture)))
                        piece.mTexture.clear();
                }
            entry.mOk = true;
        }

        // An activator whose model may stand in a far cluster (task 0096 stage b): scenery nobody can use and nothing
        // changes. It has no name (the game offers no activation without one, and the tooltip needs it), and no script,
        // or a script that never enables, disables, moves, turns, scales or removes it or starts another script
        // (Lod::scriptChangesItsObject: vanilla's lava, waterfalls and steam have sound loops and the lava's damage;
        // the Ghostgate fence disables itself and stays live). Its references can still be disabled or moved by other
        // scripts, which the cluster's override rule (LodClusters::overrideReference) already covers once the cell is
        // loaded.
        bool isInertActivator(const MWWorld::ESMStore& store, const ESM::RefId& id, bool& named, bool& scripted)
        {
            const ESM::Activator* activator = store.get<ESM::Activator>().search(id);
            if (activator == nullptr)
                return false;
            named = !activator->mName.empty();
            scripted = false;
            if (!activator->mScript.empty())
            {
                const ESM::Script* script = store.get<ESM::Script>().search(activator->mScript);
                // A script that cannot be read is treated as one that may change anything.
                scripted = script == nullptr || Lod::scriptChangesItsObject(script->mScriptText);
            }
            return !named && !scripted;
        }

        // Task 0133 stage 2: a session of a game pack with a neutral world source bakes that world's static objects.
        // True when this is such a session and the bake is set up; false for Morrowind's; with state.mFinished set
        // when it IS such a session and cannot go on (the reason is logged). The world is OPENEDEN_LOD_BAKE_WORLD (an
        // editor id, default Tamriel) like the terrain baker's. The cells are the world's own, and for a cell it has
        // none of the parent's when the parent lends its land (the same rule as the terrain: WhiterunWorld shows
        // Tamriel's horizon).
        bool beginNeutral(LodObjectBaker::State& state)
        {
            if (!Settings::game().mNeutralWorld)
                return false;
            NeutralWorld::NeutralWorldStore* store
                = MWBase::Environment::get().getExtensionManager()->neutralWorldStore();
            if (store == nullptr || store->sourceCount() == 0)
                return false;
            const std::vector<std::string> games = store->gameIds();
            state.mGameId = games.front();
            openeden::sdk::game::WorldCellSource* source = store->source(state.mGameId);
            const char* requested = std::getenv("OPENEDEN_LOD_BAKE_WORLD");
            const std::string worldName
                = requested != nullptr && *requested != 0 ? std::string(requested) : std::string("Tamriel");
            const openeden::sdk::game::WorldId world = store->findWorldByEditorId(state.mGameId, worldName);
            openeden::sdk::game::WorldSpaceInfo info;
            if (source == nullptr || world == openeden::sdk::game::kNoWorldId || !source->worldInfo(world, info))
            {
                Log(Debug::Error) << "LOD object bake failed, no pack written: " << state.mGameId << " has no world '"
                                  << worldName << "' (set OPENEDEN_LOD_BAKE_WORLD to an editor id).";
                state.mFinished = true;
                return false;
            }
            state.mNeutralStore = store;
            state.mWorldName = info.editorId;
            state.mCellSize = static_cast<std::uint32_t>(info.cellSizeUnits);
            state.mManifestName = Lod::neutralWorldspaceName(MWRender::neutralWorldFacts(*source, info));
            const bool lends = info.parent != openeden::sdk::game::kNoWorldId
                && (info.parentUsage & openeden::sdk::game::TerrainParentUsage::Land) != 0;
            constexpr std::int32_t scanRadius = 100; // as the terrain baker's scan
            bool onBorder = false;
            for (std::int32_t y = -scanRadius; y <= scanRadius; ++y)
                for (std::int32_t x = -scanRadius; x <= scanRadius; ++x)
                {
                    openeden::sdk::game::WorldId id = source->findCellByGrid(world, x, y);
                    if (id == openeden::sdk::game::kNoWorldId && lends)
                        id = source->findCellByGrid(info.parent, x, y);
                    if (id == openeden::sdk::game::kNoWorldId)
                        continue;
                    state.mNeutralCells.push_back({ x, y, id });
                    onBorder = onBorder || std::abs(x) == scanRadius || std::abs(y) == scanRadius;
                }
            if (onBorder)
                Log(Debug::Warning) << "LOD object bake: cells reach the border of the scan (" << scanRadius
                                    << " cells from the origin); a larger world needs a larger scan.";
            state.mNeutral = true;
            Log(Debug::Info) << "LOD object bake: neutral world " << state.mGameId << " / " << state.mWorldName
                             << ", cells of " << state.mCellSize << " units; " << state.mNeutralCells.size()
                             << " exterior cells" << (lends ? " (the parent's where the world has none)" : "") << ".";
            return true;
        }

        std::vector<std::uint8_t> parameterBytes(std::uint32_t level, std::uint32_t error, std::uint32_t cellSize,
            bool uvEffects, bool scenery, bool sway = false, std::string_view swayModels = {}, float swayLean = 0.f)
        {
            std::vector<std::uint8_t> bytes;
            // The foliage rule (blend as cutout, the cutoff, the translucent names) changes what a model flattens to,
            // so its version and constants are part of the digest: an older pack goes stale.
            for (const std::uint32_t word :
                { bakerVersion, level, error, static_cast<std::uint32_t>(std::lround(dropFactor * 1000.f)), cellSize,
                    Lod::foliageRuleVersion, static_cast<std::uint32_t>(Lod::blendCutoutThreshold),
                    uvEffects ? static_cast<std::uint32_t>(Lod::objectClusterEffectsVersion) : 0u, scenery ? 1u : 0u })
                for (int shift = 0; shift < 32; shift += 8)
                    bytes.push_back(static_cast<std::uint8_t>((word >> shift) & 0xFF));
            // Sway joins the digest only when it is on, so a pack baked with it off keeps its digest.
            if (sway)
            {
                std::uint32_t leanBits = 0;
                std::memcpy(&leanBits, &swayLean, sizeof(leanBits));
                for (const std::uint32_t word : { 0x53574159u, leanBits })
                    for (int shift = 0; shift < 32; shift += 8)
                        bytes.push_back(static_cast<std::uint8_t>((word >> shift) & 0xFF));
                bytes.insert(bytes.end(), swayModels.begin(), swayModels.end());
            }
            return bytes;
        }
    }

    bool LodObjectBaker::update()
    {
        State& state = *mState;
        if (state.mPhase == Phase::Done)
            return true;
        if (state.mPhase == Phase::Write)
        {
            if (state.mWrite.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return false;
            state.mWrite.get();
            state.mPhase = Phase::Done;
            // Task 0133 stage 2: a Skyrim bake holds a gigabyte of clusters and the models; freeing them at quit took
            // long enough for the freeze monitor (5 s) to end the game. They are freed on a thread of their own and
            // the process exit does not wait for it.
            std::thread([clusters = std::move(state.mClusters), objects = std::move(state.mObjects),
                            models = std::move(state.mModels)]() mutable {
                clusters.clear();
                clusters.shrink_to_fit();
                objects.clear();
                objects.shrink_to_fit();
                models.clear();
                models.shrink_to_fit();
            }).detach();
            return true;
        }
        const MWWorld::ESMStore* store = MWBase::Environment::get().getESMStore();
        const VFS::Manager* vfs = state.mResources.getVFS();
        if (state.mFinished)
            return true;
        auto cellSize = static_cast<std::uint32_t>(ESM::Land::REAL_SIZE);
        if (state.mNeutral)
            cellSize = state.mCellSize;

        if (!state.mStarted)
        {
            state.mStarted = true;
            state.mStart = std::chrono::steady_clock::now();
            state.mUvEffects = Settings::worldLod().mObjectClusterEffects;
            state.mScenery = Settings::worldLod().mObjectClusterScenery;
            state.mSway = Settings::worldLod().mObjectClusterSway;
            state.mSwayModels = Settings::worldLod().mObjectClusterSwayModels;
            state.mSwayLean = std::clamp(Settings::worldLod().mObjectClusterSwayLean.get(), 0.f, Lod::maxSwayLean);
            if (state.mSwayLean <= 0.f || state.mSwayModels.empty())
                state.mSway = false;
            // The bake runs inside a new game, where the player starts in a fight and dies after about ten seconds; a
            // death stops the game's update and with it the bake. A bake is no play: switch god mode on.
            if (MWBase::World* world = MWBase::Environment::get().getWorld();
                world != nullptr && !world->getGodModeState())
                world->toggleGodMode();
            if (beginNeutral(state))
                cellSize = state.mCellSize;
            else if (state.mFinished)
                return true;
            if (!state.mNeutral)
            {
                const auto& cells = store->get<ESM::Cell>();
                for (auto it = cells.extBegin(); it != cells.extEnd(); ++it)
                    state.mCells.emplace_back(it->getGridX(), it->getGridY());
                std::sort(state.mCells.begin(), state.mCells.end());
            }
            state.mManifest.mBakerVersion = bakerVersion;
            state.mManifest.mCellSize = cellSize;
            state.mManifest.mWorldspace = state.mNeutral ? state.mManifestName : std::string(worldspaceName);
            // Levels 1 to 4. Level 5 would need 1,024 cell slots (a cluster holds 256) and keeps almost no object (the
            // survey below), so the baker stops at level 4 (task 0066 stage d).
            state.mManifest.mLevels = Lod::objectClusterLevels(state.mNeutral);
            for (const std::uint32_t level : state.mManifest.mLevels)
                state.mManifest.mErrors.push_back(levelError(level));
            Log(Debug::Info) << "LOD object bake: "
                             << (state.mNeutral ? state.mNeutralCells.size() : state.mCells.size())
                             << " exterior cells (cluster layout version " << Lod::clusterLayoutVersion << ").";
        }

        const auto sliceEnd = std::chrono::steady_clock::now() + sliceTime;
        const auto timeLeft = [&] { return std::chrono::steady_clock::now() < sliceEnd; };

        if (state.mPhase == Phase::Collect && state.mNeutral)
        {
            // Task 0133 stage 2: the neutral store builds a cell on the main thread and keeps only a few; the objects we
            // want are copied out at once.
            while (state.mNextCell < state.mNeutralCells.size() && timeLeft())
            {
                const State::NeutralCell& entry = state.mNeutralCells[state.mNextCell++];
                std::string error;
                const openeden::sdk::game::WorldCell* cell = state.mNeutralStore->cell(state.mGameId, entry.mId, error);
                if (cell == nullptr)
                {
                    if (++state.mCellsFailed <= 5)
                        Log(Debug::Warning) << "LOD object bake: cell (" << entry.mX << ", " << entry.mY
                                            << ") cannot be built: " << error;
                    continue;
                }
                for (const openeden::sdk::game::WorldObject& object : cell->objects)
                {
                    ++state.mReferencesSeen;
                    if (!NeutralLod::isFarStaticKind(object.kind))
                    {
                        ++state.mNotStatic;
                        continue;
                    }
                    if (object.initiallyDisabled)
                    {
                        ++state.mDisabled;
                        continue;
                    }
                    if (object.modelPath.empty())
                    {
                        ++state.mNoModel;
                        continue;
                    }
                    if (NeutralLod::isMarkerModel(object.modelPath))
                    {
                        ++state.mMarkers;
                        continue;
                    }
                    PlacedObject placed;
                    if (!NeutralLod::placementMatrix(object.transform, placed.mMatrix))
                    {
                        ++state.mBadReferences;
                        continue;
                    }
                    placed.mHasMatrix = true;
                    const Lod::ClusterRef ref = Lod::clusterRefFromPlacement(object.id);
                    placed.mRef.mIndex = ref.mIndex;
                    placed.mRef.mContentFile = static_cast<std::int32_t>(ref.mContentFile);
                    placed.mRefId = object.baseEditorId;
                    placed.mScale = static_cast<float>(object.transform.scale);
                    placed.mCellX = entry.mX;
                    placed.mCellY = entry.mY;
                    const std::string path = NeutralLod::meshVfsPath(object.modelPath);
                    auto [it, inserted] = state.mModelIndex.emplace(path, state.mModels.size());
                    if (inserted)
                    {
                        state.mModels.emplace_back();
                        state.mModels.back().mPath = path;
                    }
                    placed.mModel = it->second;
                    ModelEntry& model = state.mModels[it->second];
                    model.mMaxScale = std::max(model.mMaxScale, placed.mScale);
                    state.mObjects.push_back(std::move(placed));
                }
            }
            if (state.mNextCell < state.mNeutralCells.size())
                return false;
            state.mPhase = Phase::Models;
            Log(Debug::Info) << "LOD object bake: " << state.mReferencesSeen << " references, " << state.mObjects.size()
                             << " statics and trees with a model (" << state.mNotStatic
                             << " of other kinds stay live, " << state.mDisabled << " initially disabled, "
                             << state.mMarkers << " markers, " << state.mNoModel << " without a model, "
                             << state.mBadReferences << " refused, " << state.mCellsFailed << " cells failed), "
                             << state.mModels.size() << " distinct models.";
            return false;
        }

        if (state.mPhase == Phase::Collect)
        {
            while (state.mNextCell < state.mCells.size() && timeLeft())
            {
                const std::pair<int, int> cell = state.mCells[state.mNextCell++];
                for (const auto& [refNum, ref] : MWRender::collectDistantReferences(cell, *store))
                {
                    ++state.mReferencesSeen;
                    const int type = store->findStatic(ref.mRefID);
                    bool scenery = false;
                    if (type == ESM::REC_ACTI && state.mScenery)
                    {
                        bool named = false, scripted = false;
                        scenery = isInertActivator(*store, ref.mRefID, named, scripted);
                        if (!scenery)
                        {
                            state.mSceneryNamed += named ? 1 : 0;
                            state.mSceneryScripted += scripted ? 1 : 0;
                        }
                    }
                    if (type != ESM::REC_STAT && !scenery)
                    {
                        ++state.mNotStatic;
                        continue;
                    }
                    if (scenery)
                        ++state.mSceneryObjects;
                    if (Misc::ResourceHelpers::isHiddenMarker(ref.mRefID))
                        continue;
                    const std::string_view model = MWRender::distantObjectModel(type, ref.mRefID, *store);
                    if (model.empty())
                    {
                        ++state.mNoModel;
                        continue;
                    }
                    const std::string path
                        = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(model)).value();
                    auto [it, inserted] = state.mModelIndex.emplace(path, state.mModels.size());
                    if (inserted)
                    {
                        state.mModels.emplace_back();
                        state.mModels.back().mPath = path;
                    }
                    PlacedObject object;
                    object.mRef = refNum;
                    object.mRefId = ref.mRefID.toDebugString();
                    object.mModel = it->second;
                    for (int k = 0; k < 3; ++k)
                    {
                        object.mPos[k] = ref.mPos.pos[k];
                        object.mRot[k] = ref.mPos.rot[k];
                    }
                    object.mScale = ref.mScale;
                    // The content files are untrusted: a reference with a position, rotation or scale that is not a
                    // sane number is left out instead of reaching the casts and the transform.
                    bool sane = std::isfinite(ref.mScale) && ref.mScale > 0 && ref.mScale < 1000.f;
                    for (int k = 0; k < 3; ++k)
                        sane = sane && std::isfinite(object.mPos[k]) && std::abs(object.mPos[k]) < 1.0e8f
                            && std::isfinite(object.mRot[k]);
                    if (!sane)
                    {
                        ++state.mBadReferences;
                        continue;
                    }
                    // The cell the reference is stored in, as the live chunks and the active grid know it (not the cell
                    // it stands in: the two can differ).
                    object.mCellX = cell.first;
                    object.mCellY = cell.second;
                    state.mObjects.push_back(std::move(object));
                }
            }
            if (state.mNextCell < state.mCells.size())
                return false;
            state.mPhase = Phase::Models;
            Log(Debug::Info) << "LOD object bake: " << state.mReferencesSeen << " references, " << state.mObjects.size()
                             << " statics with a model (" << state.mNotStatic << " activators and doors stay live; "
                             << state.mSceneryObjects << " inert activators taken, " << state.mSceneryNamed
                             << " named and " << state.mSceneryScripted << " with a script that may change them left), "
                             << state.mModels.size() << " distinct models.";
            return false;
        }

        if (state.mPhase == Phase::Models)
        {
            while (state.mNextModel < state.mModels.size() && timeLeft())
            {
                ModelEntry& entry = state.mModels[state.mNextModel++];
                try
                {
                    if (state.mNeutral)
                    {
                        loadNeutralModel(*vfs, entry);
                        // A model whose largest placement is smaller than the first level's cut is left out by every
                        // level: do not keep its triangles (a Skyrim world has thousands of such small models).
                        if (entry.mFlat.mTraits.mRadius * entry.mMaxScale
                            < dropFactor * levelError(state.mManifest.mLevels.front()))
                        {
                            entry.mOk = false;
                            entry.mTooSmall = true;
                            entry.mFlat = {};
                            ++state.mTooSmallModels;
                        }
                    }
                    else
                        loadModel(*vfs, entry, state.mUvEffects);
                }
                catch (const std::exception& e)
                {
                    ++state.mModelsFailed;
                    entry.mOk = false;
                    entry.mFlat = {};
                    Log(Debug::Warning) << "LOD object bake: cannot read model " << entry.mPath << ": " << e.what();
                }
            }
            if (state.mNextModel < state.mModels.size())
                return false;
            // Buckets: for each level, the tile each object stands in.
            std::map<std::uint64_t, std::pair<Lod::TileId, std::vector<std::size_t>>> buckets;
            for (std::size_t i = 0; i < state.mObjects.size(); ++i)
            {
                const PlacedObject& object = state.mObjects[i];
                if (!state.mModels[object.mModel].mOk)
                    continue;
                for (const std::uint32_t level : state.mManifest.mLevels)
                {
                    const std::optional<Lod::TileId> id
                        = Lod::tileContainingCell(0, level, object.mCellX, object.mCellY);
                    const std::optional<std::uint64_t> key = id ? Lod::packClusterKey(*id) : std::nullopt;
                    if (!key)
                        continue;
                    auto& bucket = buckets[*key];
                    bucket.first = *id;
                    bucket.second.push_back(i);
                }
            }
            for (auto& [key, bucket] : buckets)
                state.mWork.emplace_back(bucket.first, std::move(bucket.second));
            // A survey of what each level keeps, from the object sizes alone. Levels 5 and 6 are not baked (their
            // errors continue the growth of three times a level); the numbers are the evidence for stopping at level 4.
            for (std::uint32_t level = 1; level <= 6; ++level)
            {
                const float error = level <= Lod::maxClusterLevel ? Lod::clusterLevelError(level)
                                                                  : Lod::clusterLevelError(Lod::maxClusterLevel)
                        * std::pow(3.f, static_cast<float>(level - Lod::maxClusterLevel));
                std::size_t kept = 0;
                std::size_t seen = 0;
                for (const PlacedObject& object : state.mObjects)
                {
                    const ModelEntry& model = state.mModels[object.mModel];
                    if (!model.mOk)
                        continue;
                    ++seen;
                    Lod::ObjectTraits traits = model.mFlat.mTraits;
                    traits.mRadius *= object.mScale;
                    if (Lod::classifyObject(traits, Lod::ClusterLevelParams{ level, error, dropFactor })
                        == Lod::ExcludeReason::None)
                        ++kept;
                }
                Log(Debug::Info) << "LOD object bake: survey, level " << level << " (error " << error
                                 << (level > Lod::maxClusterLevel ? ", not baked" : "") << "): " << kept << " of "
                                 << seen << " objects would be kept.";
            }
            state.mPhase = Phase::Build;
            Log(Debug::Info) << "LOD object bake: " << state.mModels.size() - state.mModelsFailed << " models read ("
                             << state.mModelsFailed << " failed"
                             << (state.mNeutral ? ", " + std::to_string(state.mTooSmallModels) + " too small for every level"
                                                : std::string())
                             << "), " << state.mWork.size() << " tiles to build.";
            return false;
        }

        // Phase::Build
        while (state.mNextTile < state.mWork.size() && timeLeft())
        {
            const auto& [id, indices] = state.mWork[state.mNextTile++];
            const std::size_t levelSlot = id.mLevel - 1;
            Lod::ClusterBuildInput input;
            input.mLevel.mLevel = id.mLevel;
            input.mLevel.mError = static_cast<float>(levelError(id.mLevel));
            input.mLevel.mDropFactor = dropFactor;
            input.mCellSize = cellSize;
            std::int32_t originCellX = 0, originCellY = 0;
            Lod::tileOriginCell(id, originCellX, originCellY);
            input.mOriginCellX = originCellX;
            input.mOriginCellY = originCellY;
            input.mOriginX = static_cast<double>(originCellX) * cellSize;
            input.mOriginY = static_cast<double>(originCellY) * cellSize;
            input.mSide = static_cast<double>(Lod::tileSideInCells(id)) * cellSize;

            // The objects of the tile in a fixed order, each reference once.
            std::vector<std::size_t> ordered = indices;
            std::stable_sort(ordered.begin(), ordered.end(), [&state](std::size_t a, std::size_t b) {
                const PlacedObject& x = state.mObjects[a];
                const PlacedObject& y = state.mObjects[b];
                return std::tie(x.mCellY, x.mCellX, x.mRef.mIndex, x.mRef.mContentFile)
                    < std::tie(y.mCellY, y.mCellX, y.mRef.mIndex, y.mRef.mContentFile);
            });
            EdenArchive::Hasher hasher;
            hashValue(hasher, id.mWorldspace);
            hashValue(hasher, id.mLevel);
            hashValue(hasher, id.mX);
            hashValue(hasher, id.mY);
            const std::vector<std::uint8_t> parameters = parameterBytes(id.mLevel, levelError(id.mLevel), cellSize,
                state.mUvEffects, state.mScenery, state.mSway, state.mSwayModels, state.mSwayLean);
            hasher.update(parameters.data(), parameters.size());
            std::set<std::pair<std::uint32_t, std::int32_t>> seen;
            for (const std::size_t index : ordered)
            {
                const PlacedObject& object = state.mObjects[index];
                if (!seen.emplace(object.mRef.mIndex, object.mRef.mContentFile).second)
                {
                    if (id.mLevel == state.mManifest.mLevels.front())
                        ++state.mDuplicates;
                    continue;
                }
                const ModelEntry& model = state.mModels[object.mModel];
                Lod::SourceObject source;
                source.mRef = { object.mRef.mIndex, static_cast<std::uint32_t>(object.mRef.mContentFile) };
                source.mCellX = object.mCellX;
                source.mCellY = object.mCellY;
                source.mTraits = model.mFlat.mTraits;
                source.mTraits.mRadius *= object.mScale;
                if (object.mHasMatrix)
                    source.mTransform = object.mMatrix;
                else
                    source.mTransform = rowMajor(
                        MWRender::distantObjectMatrix(vsg::vec3(object.mPos[0], object.mPos[1], object.mPos[2]),
                            vsg::vec3(object.mRot[0], object.mRot[1], object.mRot[2]), object.mScale));
                source.mPieces = model.mFlat.mPieces;
                if (state.mSway && Lod::swayCapableModel(model.mPath, state.mSwayModels))
                    source.mSwayLean = state.mSwayLean;
                input.mObjects.push_back(std::move(source));

                hashValue(hasher, object.mRef.mIndex);
                hashValue(hasher, object.mRef.mContentFile);
                hashValue(hasher, object.mCellX);
                hashValue(hasher, object.mCellY);
                hashText(hasher, object.mRefId);
                hashText(hasher, model.mPath);
                hasher.update(model.mHash.data(), model.mHash.size());
                hasher.update(object.mPos, sizeof(object.mPos));
                hasher.update(object.mRot, sizeof(object.mRot));
                hashValue(hasher, object.mScale);
                if (object.mHasMatrix)
                    hasher.update(object.mMatrix.data(), sizeof(double) * object.mMatrix.size());
            }

            LevelTotals& totals = state.mTotals[levelSlot];
            Lod::ObjectCluster cluster;
            Lod::ClusterBuildStats stats;
            if (state.mNextTile % 50 == 1)
                Log(Debug::Info) << "LOD object bake: tile " << state.mNextTile << " of " << state.mWork.size() << " ("
                                 << std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::steady_clock::now() - state.mStart)
                                        .count()
                                 << " s)";
            const auto tileStart = std::chrono::steady_clock::now();
            const Lod::LodError buildError = Lod::buildObjectCluster(input, cluster, &stats);
            const auto tileMs
                = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tileStart)
                      .count();
            // A tile that takes seconds is worth knowing about (the levels 3 and 4 of task 0066 stage d).
            if (tileMs >= slowTileMilliseconds)
                Log(Debug::Info) << "LOD object bake: level " << id.mLevel << " tile (" << id.mX << ", " << id.mY
                                 << ") took " << tileMs << " ms: " << input.mObjects.size() << " objects, "
                                 << stats.mTrianglesBefore << " triangles to " << stats.mTrianglesAfter;
            if (const Lod::LodError error = buildError)
            {
                Log(Debug::Error) << "LOD object bake failed, no pack written: tile level " << id.mLevel << " ("
                                  << id.mX << ", " << id.mY << "): " << error.describe();
                state.mPhase = Phase::Done;
                return true;
            }
            totals.mObjectsSeen += stats.mObjectsSeen;
            totals.mObjectsIncluded += stats.mObjectsIncluded;
            for (std::size_t r = 0; r < stats.mExcluded.size(); ++r)
                totals.mExcluded[r] += stats.mExcluded[r];
            totals.mTrianglesBefore += stats.mTrianglesBefore;
            totals.mTrianglesAfter += stats.mTrianglesAfter;
            totals.mMaxError = std::max(totals.mMaxError, cluster.mGeometricError);
            if (cluster.mGroups.empty())
                continue;
            ++totals.mTiles;
            totals.mVertices += cluster.mVertices.size();
            for (const Lod::ClusterGroup& group : cluster.mGroups)
                if (!group.mEffect.none())
                {
                    ++totals.mEffectGroups;
                    totals.mEffectTriangles += group.mIndexCount / 3;
                }
            totals.mGroups.push_back(cluster.mGroups.size());
            Lod::BakedCluster baked;
            baked.mId = id;
            baked.mData = std::move(cluster);
            baked.mSourceDigest = hasher.finalize();
            baked.mParameters = parameters;
            state.mClusters.push_back(std::move(baked));
        }
        if (state.mNextTile < state.mWork.size())
            return false;

        state.mPhase = Phase::Done;
        for (std::size_t slot = 0; slot < Lod::maxClusterLevel; ++slot)
        {
            LevelTotals& totals = state.mTotals[slot];
            if (totals.mObjectsSeen == 0)
                continue;
            std::sort(totals.mGroups.begin(), totals.mGroups.end());
            Log(Debug::Info) << "LOD object bake: level " << slot + 1 << " (error "
                             << levelError(static_cast<std::uint32_t>(slot + 1)) << "): " << totals.mTiles
                             << " clusters, objects included " << totals.mObjectsIncluded << " of "
                             << totals.mObjectsSeen << " (too small "
                             << totals.mExcluded[static_cast<std::size_t>(Lod::ExcludeReason::TooSmall)]
                             << ", animated "
                             << totals.mExcluded[static_cast<std::size_t>(Lod::ExcludeReason::Animated)]
                             << ", particles "
                             << totals.mExcluded[static_cast<std::size_t>(Lod::ExcludeReason::Particles)]
                             << ", blended "
                             << totals.mExcluded[static_cast<std::size_t>(Lod::ExcludeReason::AlphaBlended)]
                             << ", empty " << totals.mExcluded[static_cast<std::size_t>(Lod::ExcludeReason::Empty)]
                             << "), triangles " << totals.mTrianglesBefore << " to " << totals.mTrianglesAfter
                             << ", vertices " << totals.mVertices << ", groups per tile median "
                             << (totals.mGroups.empty() ? 0 : totals.mGroups[totals.mGroups.size() / 2]) << " max "
                             << (totals.mGroups.empty() ? 0 : totals.mGroups.back()) << ", largest error "
                             << totals.mMaxError << ", groups with an effect " << totals.mEffectGroups << " ("
                             << totals.mEffectTriangles << " triangles)";
        }
        if (state.mBadReferences > 0)
            Log(Debug::Warning)
                << "LOD object bake: " << state.mBadReferences
                << " references with a position, rotation or scale that is not a sane number were left out.";
        if (state.mDuplicates > 0)
            Log(Debug::Info) << "LOD object bake: " << state.mDuplicates << " repeated references were used once.";

        std::sort(state.mClusters.begin(), state.mClusters.end(),
            [](const Lod::BakedCluster& lhs, const Lod::BakedCluster& rhs) {
                return Lod::packClusterKey(lhs.mId).value_or(0) < Lod::packClusterKey(rhs.mId).value_or(0);
            });
        // The write (zstd level 15 over the whole pack) takes a minute: it runs on a worker so that frames, and the
        // window's message loop, go on (Windows' freeze monitor kills a process that stops answering for 5 s).
        state.mPhase = Phase::Write;
        state.mWrite = std::async(std::launch::async, [&state]() {
            const EdenArchive::Hash digest = Lod::clusterContentDigest(state.mManifest, state.mClusters);
            const std::filesystem::path path = Lod::objectPackPath(state.mOut / "openeden" / "lod",
                state.mNeutral ? state.mGameId : std::string(), state.mNeutral ? state.mWorldName : std::string());
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);

            std::uint64_t generation = 1;
            {
                Lod::LodError openError;
                const std::unique_ptr<Lod::ClusterPack> old = Lod::ClusterPack::open(path, openError);
                if (old)
                {
                    if (old->sourceDigest() == digest && old->manifest() == state.mManifest
                        && old->tileIds().size() == state.mClusters.size())
                    {
                        Log(Debug::Info) << "LOD object bake: " << Files::pathToUnicodeString(path)
                                         << " is already up to date (generation " << old->generation()
                                         << "), nothing written.";
                        return;
                    }
                    Log(Debug::Info) << "LOD object bake: the pack on disk is stale (references, models or settings "
                                        "changed, or an older layout): it is replaced.";
                    generation = old->generation() + 1;
                }
            }
            Lod::TilePackOptions options;
            options.mGeneration = generation;
            options.mSourceDigest = digest;
            options.mThreads = 4;
            // Higher than the default 3: the pack is baked once and read many times (decoding speed hardly depends
            // on it).
            options.mZstdLevel = 15;
            if (const Lod::LodError error = Lod::writeClusterPack(state.mManifest, state.mClusters, options, path))
            {
                Log(Debug::Error) << "LOD object bake failed, the old pack (if any) is unchanged: " << error.describe();
                return;
            }

            // Read every cluster back through the reader the game will use.
            Lod::LodError openError;
            const std::unique_ptr<Lod::ClusterPack> written = Lod::ClusterPack::open(path, openError);
            std::size_t good = 0;
            if (written)
            {
                for (const Lod::TileId& id : written->tileIds())
                {
                    Lod::ObjectCluster cluster;
                    if (!written->readCluster(id, cluster))
                        ++good;
                }
            }
            if (!written || good != state.mClusters.size())
            {
                Log(Debug::Error) << "LOD object bake: the pack was written but only " << good << " of "
                                  << state.mClusters.size() << " clusters read back"
                                  << (written ? "" : " (the pack did not open: " + openError.describe() + ")")
                                  << "; do not use it.";
                return;
            }
            std::error_code sizeError;
            const std::uintmax_t bytes = std::filesystem::file_size(path, sizeError);
            const double seconds
                = std::chrono::duration<double>(std::chrono::steady_clock::now() - state.mStart).count();
            Log(Debug::Info) << "LOD object bake: wrote " << Files::pathToUnicodeString(path) << " (generation "
                             << generation << "), " << state.mClusters.size() << " clusters, " << good
                             << " read back fine, " << bytes << " bytes, " << seconds << " s.";
        });
        return false;
    }
}
