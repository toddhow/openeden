#include "extensionhost.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>
#include <system_error>

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/extension/dependency_resolver.hpp>
#include <components/extension/game_conventions.hpp>
#include <components/contentsource/sessiontag.hpp>
#include <components/extension/session_content.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/gameconventions.hpp>
#include <components/settings/values.hpp>

#include "extensionworld.hpp"
#include "mwbase/environment.hpp"
#include "mwbase/statemanager.hpp"
#include "mwbase/world.hpp"
#include "mwphysics/physicssystem.hpp"
#include "mwworld/craftingrules.hpp"
#include "mwworld/gamerules.hpp"
#include "mwworld/ptr.hpp"
#include <components/extension/extension_loader.hpp>

#include <openeden/game/game_rules.hpp>
#include <openeden/logging.hpp>
#include <openeden/pack_kind.hpp>

namespace OMW
{
    namespace
    {
        constexpr std::string_view kEngineLogSource = "engine";

        // Events held while no game runs. An extension publishing every frame at the main menu must not grow without
        // end.
        constexpr std::size_t kMaxPendingLuaEvents = 1024;

        // True on a thread while a line crosses between the extension and engine logs, in either direction. The engine
        // listener runs under Log's lock, so an extension sink that logs while receiving an engine line must not reach
        // Log again: that line is dropped. And an extension's line written to the engine log must not come back to the
        // sinks.
        thread_local bool sCrossingLogs = false;

        struct CrossingLogs
        {
            CrossingLogs() { sCrossingLogs = true; }
            ~CrossingLogs() { sCrossingLogs = false; }
        };

        Debug::Level toDebugLevel(openeden::sdk::LogLevel level)
        {
            switch (level)
            {
                case openeden::sdk::LogLevel::Trace:
                    return Debug::Debug; // The engine's most detailed level, above Verbose
                case openeden::sdk::LogLevel::Debug:
                    return Debug::Verbose;
                case openeden::sdk::LogLevel::Info:
                    return Debug::Info;
                case openeden::sdk::LogLevel::Warning:
                    return Debug::Warning;
                case openeden::sdk::LogLevel::Error:
                case openeden::sdk::LogLevel::Fatal:
                    return Debug::Error;
            }
            return Debug::Info;
        }

        openeden::sdk::LogLevel toExtensionLevel(Debug::Level level)
        {
            switch (level)
            {
                case Debug::Error:
                    return openeden::sdk::LogLevel::Error;
                case Debug::Warning:
                    return openeden::sdk::LogLevel::Warning;
                case Debug::Info:
                    return openeden::sdk::LogLevel::Info;
                case Debug::Verbose:
                    return openeden::sdk::LogLevel::Debug;
                default:
                    return openeden::sdk::LogLevel::Trace;
            }
        }

        // Extension log lines to the engine log. formattedMessage already has the [extensionId] prefix.
        class EngineLogSink final : public openeden::sdk::LogSink
        {
        public:
            void log(
                std::string_view extensionId, openeden::sdk::LogLevel level, std::string_view formattedMessage) override
            {
                if (extensionId == kEngineLogSource || sCrossingLogs)
                    return;
                const CrossingLogs crossing;
                Log(toDebugLevel(level)) << formattedMessage;
            }
        };
    }

    // Engine log lines to the extensions' sinks. Debug's listener can run on any thread and outlives this host, so it
    // holds this object, not the host. The link is cut under the lock before extension libraries unload, which waits
    // for a line in flight. Nothing under the lock reaches Log, because sCrossingLogs stops it, so the lock order is
    // safe.
    struct ExtensionHost::LogBridge
    {
        std::mutex mMutex;
        openeden::extension::LoggingServiceImpl* mService = nullptr;

        void forward(Debug::Level level, std::string_view message)
        {
            if (sCrossingLogs)
                return;
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                message.remove_suffix(1);
            const CrossingLogs crossing;
            const std::lock_guard lock(mMutex);
            if (mService != nullptr)
                mService->dispatch(kEngineLogSource, toExtensionLevel(level), message);
        }
    };

    /// The engine's side of the world source service: sources go into the neutral world store and leave it when the
    /// pack withdraws them (by game id; the source object may be gone).
    struct ExtensionHost::StoreListener final : openeden::extension::WorldSourceServiceImpl::Listener
    {
        explicit StoreListener(NeutralWorld::NeutralWorldStore& store)
            : mStore(store)
        {
        }

        bool sourceRegistered(openeden::sdk::game::WorldCellSource& source, std::string& error) override
        {
            return mStore.addSource(source, error);
        }

        void sourceWithdrawn(std::string_view gameId) override { mStore.removeSource(gameId); }

        NeutralWorld::NeutralWorldStore& mStore;
    };

    ExtensionHost::ExtensionHost(const Config& config)
        : mLifecycle(mHost)
    {
        namespace extension = openeden::extension;

        mHost.settingsService().useEngineSettings();

        // The neutral world store (task 0103, stage N3a-1). Built only when [Game] neutral world asks for it; with it
        // off the service is told nobody reads sources, a pack does not register, and nothing else is made.
        if (config.neutralWorld)
        {
            mNeutralWorldStore = std::make_unique<NeutralWorld::NeutralWorldStore>();
            mStoreListener = std::make_unique<StoreListener>(*mNeutralWorldStore);
            mHost.worldSources().setListener(mStoreListener.get());
            mHost.worldSources().setConsumerEnabled(true);
        }

        std::vector<extension::ExtensionLoadError> errors;
        const std::vector<extension::DiscoveredExtension> discovered
            = extension::discoverExtensionDirectories(config.searchDirs, &errors);

        const auto isEnabled = [&](const std::string& id) {
            return id == config.gamePackId
                || std::find(config.enabled.begin(), config.enabled.end(), id) != config.enabled.end();
        };

        // A broken manifest has no reliable ID to check against the enable list, so it is always reported
        for (const extension::ExtensionLoadError& error : errors)
        {
            Log(Debug::Error) << "Extension '" << error.extensionId << "' failed at " << error.stage << ": "
                              << error.message;
            // Only if asked for: a broken folder the player did not enable is no reason for a message every run
            if (isEnabled(error.extensionId))
                mLoadFailures.push_back(error.extensionId + ": " + error.message);
        }

        std::vector<extension::ExtensionManifest> loadedManifests;
        std::map<std::string, std::filesystem::path> loadedData;

        std::set<std::string> seen;
        for (const extension::DiscoveredExtension& item : discovered)
        {
            const std::string& id = item.manifest.id;
            if (!seen.insert(id).second)
            {
                Log(Debug::Warning) << "Extension '" << id << "' in " << item.directory
                                    << " ignored: found earlier in another extension directory";
                continue;
            }
            if (!isEnabled(id))
            {
                Log(Debug::Info) << "Extension '" << id << "' is installed but not enabled";
                continue;
            }

            // One game pack per session, chosen by game= (task 0084)
            if (item.manifest.kind == openeden::sdk::kKindGame && id != config.gamePackId)
            {
                const std::string reason = config.gamePackId.empty()
                    ? "no game= line chooses a game pack"
                    : "this session's game pack is '" + config.gamePackId + "'";
                Log(Debug::Error) << "Game pack '" << id << "' not loaded: " << reason;
                mLoadFailures.push_back(id + ": game pack not loaded, " + reason);
                continue;
            }

            Log(Debug::Info) << "Loading extension '" << id << "' from " << item.directory;
            extension::ExtensionLoadResult result = extension::loadExtensionLibrary(item, mHost.getHostInfo());
            if (!result.success)
            {
                Log(Debug::Error) << "Extension '" << id << "' failed at " << result.error.stage << ": "
                                  << result.error.message;
                mLoadFailures.push_back(id + ": " + result.error.message);
                continue;
            }
            loadedManifests.push_back(result.extension.manifest);
            if (!result.extension.manifest.dataPath.empty())
                loadedData[id] = item.directory / result.extension.manifest.dataPath;
            mLifecycle.addExtension(std::move(result.extension.manifest), std::move(result.extension.instance),
                std::move(result.extension.library));
            mLoaded = true;
        }

        // A game pack linked into the executable stands in for one that is not installed (stage I1b)
        if (!config.gamePackId.empty() && seen.find(config.gamePackId) == seen.end())
        {
            for (const StaticPack& pack : config.staticPacks)
            {
                if (pack.manifest.id != config.gamePackId || !pack.create)
                    continue;
                Log(Debug::Info) << "Loading game pack '" << pack.manifest.id << "' (built in)";
                std::unique_ptr<openeden::sdk::Extension> instance;
                try
                {
                    instance = pack.create();
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Error) << "Game pack '" << pack.manifest.id << "' failed at create: " << e.what();
                    mLoadFailures.push_back(pack.manifest.id + ": " + e.what());
                    break;
                }
                if (!instance)
                    break;
                seen.insert(pack.manifest.id);
                mGamePackBuiltIn = true;
                loadedManifests.push_back(pack.manifest);
                mLifecycle.addExtension(pack.manifest, std::move(instance), nullptr);
                mLoaded = true;
                break;
            }
        }

        for (const std::string& id : config.enabled)
        {
            if (seen.find(id) == seen.end())
                Log(Debug::Warning) << "Extension '" << id << "' is enabled but not installed";
        }
        if (!config.gamePackId.empty() && seen.find(config.gamePackId) == seen.end())
        {
            Log(Debug::Error) << "Game pack '" << config.gamePackId << "' (game=) is not installed";
            mLoadFailures.push_back(config.gamePackId + ": the game pack chosen by game= is not installed");
        }

        // The VFS is built before extensions start, and start() is where the lifecycle resolves them too. Resolving is
        // a pure function of the manifests, so both get the same order.
        const extension::ResolutionResult order
            = extension::resolveDependencies(loadedManifests, mHost.getHostInfo().sdkVersion);
        for (const std::string& id : order.loadOrder)
        {
            const auto data = loadedData.find(id);
            std::error_code ec;
            if (data != loadedData.end() && std::filesystem::is_directory(data->second, ec)
                && std::find(order.disabledExtensions.begin(), order.disabledExtensions.end(), id)
                    == order.disabledExtensions.end())
                mDataDirs.push_back({ id, data->second });
        }
    }

    void ExtensionHost::reportDataOverrides(const VFS::Manager& gameData) const
    {
        for (const openeden::extension::DataOverride& item :
            openeden::extension::findDataOverrides(gameData, mDataDirs))
        {
            if (item.overriddenExtensionId.empty())
                Log(Debug::Info) << "Extension '" << item.extensionId << "' overrides game data file " << item.path;
            else
                Log(Debug::Info) << "Extension '" << item.extensionId << "' overrides file " << item.path
                                 << " of extension '" << item.overriddenExtensionId << "'";
        }
    }

    void ExtensionHost::setLuaManager(MWBase::LuaManager* lua)
    {
        mLuaManager = lua;
    }

    void ExtensionHost::sendPendingLuaEventsNow()
    {
        if (mLuaManager == nullptr
            || MWBase::Environment::get().getStateManager()->getState() != MWBase::StateManager::State_Running)
            return;
        for (PendingLuaEvent& event : mPendingLuaEvents)
        {
            try
            {
                mLuaManager->sendExtensionEvent(event.mName, event.mFields);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Extension event '" << event.mName << "' dropped: " << e.what();
            }
        }
        mPendingLuaEvents.clear();
    }

    std::string ExtensionHost::takeFailureReport()
    {
        std::string report;
        const auto add = [&](const std::string& line) {
            if (!report.empty())
                report += '\n';
            report += line;
        };
        for (const std::string& line : mLoadFailures)
            add(line);
        mLoadFailures.clear();

        const auto& failures = mLifecycle.getFailures();
        for (; mReportedFailures < failures.size(); ++mReportedFailures)
        {
            const auto& failure = failures[mReportedFailures];
            add(failure.extensionId + ": " + failure.reason);
        }
        return report;
    }

    std::string ExtensionHost::savingRefusal() const
    {
        if (!mLifecycle.getSessionError().empty())
            return mLifecycle.getSessionError();
        if (mHost.worldSources().sourceCount() > 0)
            return "Saving is refused: the game pack's world has no save support yet";
        return {};
    }

    bool ExtensionHost::collectGamePackSave(std::vector<std::byte>& bytes, std::string& error)
    {
        bytes.clear();
        error.clear();
        // Only a game pack has chunks (decision 0005 still holds for every other kind)
        if (!mLoaded || mLifecycle.getGamePackId().empty())
            return true;
        const openeden::extension::ChunkCollectResult collected = mHost.saveChunks().collect();
        if (!collected.ok)
        {
            error = collected.error;
            return false;
        }
        if (!collected.chunks.empty())
            bytes = openeden::extension::encodeChunks(collected.chunks);
        return true;
    }

    bool ExtensionHost::restoreGamePackSave(std::span<const std::byte> bytes, std::string& error)
    {
        error.clear();
        if (bytes.empty() || !mLoaded || mLifecycle.getGamePackId().empty())
            return true;
        std::vector<openeden::extension::SavedChunk> chunks;
        if (!openeden::extension::decodeChunks(bytes, chunks, error))
            return false;
        const openeden::extension::ChunkRestoreResult restored = mHost.saveChunks().restore(chunks);
        if (!restored.ok)
            error = restored.error;
        return restored.ok;
    }

    std::string ExtensionHost::takeSessionFault()
    {
        if (mSessionFaultTaken || mLifecycle.getSessionError().empty())
            return {};
        mSessionFaultTaken = true;
        return mLifecycle.getSessionError();
    }

    void ExtensionHost::attachWorld(MWPhysics::PhysicsSystem& physics)
    {
        // A game pack may have been started first, with the world already attached (startGamePack)
        if (!mLoaded || mWorld)
            return;
        attachWorldNow(physics);
    }

    void ExtensionHost::attachWorldNow(MWPhysics::PhysicsSystem& physics)
    {
        mPhysics = &physics;
        mWorld = std::make_unique<ExtensionWorld>(mHost, &physics);
        mHost.attachPhysicsWorld(physics.getJoltWorld(), Constants::UnitsPerMeter);
        mHost.entityManager().setWorld(mWorld.get());

        // Names for query masks, so that extensions never see the layers' bit numbers (joltlayers.hpp)
        openeden::extension::PhysicsServiceImpl& queries = *mHost.physicsService();
        queries.setEntityBodies(mWorld.get());
        queries.publishQueryMask("world", MWPhysics::Layers::WORLD);
        queries.publishQueryMask("door", MWPhysics::Layers::DOOR);
        queries.publishQueryMask("actor", MWPhysics::Layers::ACTOR);
        queries.publishQueryMask("heightmap", MWPhysics::Layers::HEIGHTMAP);
        queries.publishQueryMask("projectile", MWPhysics::Layers::PROJECTILE);
        queries.publishQueryMask("water", MWPhysics::Layers::WATER);
        queries.publishQueryMask("camera_only", MWPhysics::Layers::CAMERA_ONLY);
        queries.publishQueryMask("visual_only", MWPhysics::Layers::VISUAL_ONLY);
        queries.publishQueryMask("dynamic_world", MWPhysics::Layers::DYNAMIC_WORLD);
        queries.publishQueryMask("debris", MWPhysics::Layers::DEBRIS);
        queries.publishQueryMask("sensor", MWPhysics::Layers::SENSOR);

        physics.setContactObserver(mWorld.get());
    }

    void ExtensionHost::deliverWorldEvents()
    {
        if (mWorld && mRunning)
            mWorld->deliver();
    }

    void ExtensionHost::objectAddedToScene(const MWWorld::Ptr& ptr)
    {
        if (mWorld)
            mWorld->objectAdded(ptr);
    }

    void ExtensionHost::objectRemovedFromScene(const MWWorld::Ptr& ptr)
    {
        if (mWorld)
            mWorld->objectRemoved(ptr);
    }

    void ExtensionHost::cellLoaded(std::string_view name)
    {
        if (mWorld)
            mWorld->cellLoaded(name);
    }

    void ExtensionHost::cellUnloaded(std::string_view name)
    {
        if (mWorld)
            mWorld->cellUnloaded(name);
    }

    void ExtensionHost::cellChanged(std::string_view name)
    {
        if (mWorld)
            mWorld->cellChanged(name);
    }

    void ExtensionHost::actorDied(const MWWorld::Ptr& actor)
    {
        if (mWorld)
            mWorld->actorDied(actor);
    }

    void ExtensionHost::actorRevived(const MWWorld::Ptr& actor)
    {
        if (mWorld)
            mWorld->actorRevived(actor);
    }

    void ExtensionHost::actorHit(
        const MWWorld::Ptr& attacker, const MWWorld::Ptr& target, float damage, bool successful)
    {
        if (mWorld)
            mWorld->actorHit(attacker, target, damage, successful);
    }

    void ExtensionHost::gameLoaded()
    {
        if (mWorld)
            mWorld->gameLoaded(MWBase::Environment::get().getWorld()->getPlayerPtr());
    }

    void ExtensionHost::playerRebuilt(const MWWorld::Ptr& player)
    {
        if (mWorld)
            mWorld->playerRebuilt(player);
    }

    void ExtensionHost::gameSaved()
    {
        if (mWorld)
            mWorld->gameSaved();
    }

    void ExtensionHost::setSessionContent(
        const std::vector<std::string>& contentFiles, const Files::Collections& collections)
    {
        // Off means unchanged: with [Game] neutral world off nothing reads the list, so no file is opened.
        if (mNeutralWorldStore == nullptr)
            return;
        openeden::extension::SessionContentCollection collected = openeden::extension::collectSessionContent(
            contentFiles, [&collections](const std::string& name) -> std::optional<std::filesystem::path> {
                if (!collections.doesExist(name))
                    return std::nullopt;
                return collections.getPath(name);
            },
            [](const std::filesystem::path& path,
                std::string& error) -> std::optional<openeden::extension::SessionContentHeader> {
                const std::optional<Content::SessionTag> tag = Content::readSessionTag(path, error);
                if (!tag.has_value())
                    return std::nullopt;
                return openeden::extension::SessionContentHeader{ tag->mTag, tag->mFormatVersion };
            });
        for (const std::string& name : collected.missing)
            Log(Debug::Warning) << "Content file " << name
                                << " is not in any data folder; the game pack is not given it";
        for (const std::string& problem : collected.unreadable)
            Log(Debug::Warning) << "Content file not given to the game pack: " << problem;
        Log(Debug::Verbose) << "Session content for the game pack: " << collected.entries.size()
                            << " plugin headers read";
        mHost.worldSources().setSessionContent(std::move(collected.entries));
        mHost.worldSources().setContentFilter([this](const openeden::sdk::ContentHeader& header) {
            return mHost.content().findLoader(header) != nullptr;
        });
    }

    ExtensionHost::~ExtensionHost()
    {
        stop();
    }

    ExtensionHost::GamePackStartStep ExtensionHost::planGamePackStart(const GamePackStartState& state) noexcept
    {
        if (!state.mLoaded || !state.mHasGamePack)
            return GamePackStartStep::Nothing;
        // The first phase ran earlier (task 0113): only the world may still be missing
        if (state.mGamePackStarted)
            return state.mPhysicsGiven && !state.mWorldAttached ? GamePackStartStep::AttachWorldOnly
                                                                : GamePackStartStep::Nothing;
        if (state.mRunning)
            return GamePackStartStep::Nothing;
        return GamePackStartStep::Start;
    }

    bool ExtensionHost::startGamePackEarly()
    {
        // Off means unchanged: without the neutral world the Game starts the pack, as it always did
        if (mNeutralWorldStore == nullptr)
            return false;
        return startGamePack(nullptr);
    }

    bool ExtensionHost::startGamePack(MWPhysics::PhysicsSystem* physics)
    {
        // No game pack: nothing at all happens here, and start() is the single start it always was
        const GamePackStartStep step = planGamePackStart({ mLoaded, mLifecycle.hasGamePack(), mRunning,
            mGamePackStarted, physics != nullptr, mWorld != nullptr });
        if (step == GamePackStartStep::AttachWorldOnly)
        {
            // The pack started earlier, before the World existed (startGamePackEarly); it gets the world now
            attachWorldNow(*physics);
            return false;
        }
        if (step != GamePackStartStep::Start)
            return false;

        // The log bridge and the world must exist first, as they do for the single start
        beginRunning();
        mGamePackStarted = true;
        if (physics != nullptr && !mWorld)
            attachWorldNow(*physics);
        if (!mLifecycle.initializeGamePack())
            return false;
        Log(Debug::Info) << "Game pack '" << mLifecycle.getGamePackId() << "' started before the game's managers";
        installPackRules();
        return true;
    }

    void ExtensionHost::installPackRules()
    {
        // A pack that failed to start installs nothing: its objects may be half set up
        if (!mLifecycle.getSessionError().empty())
            return;
        if (openeden::sdk::game::GameRules* rules = mLifecycle.serviceLocator().find<openeden::sdk::game::GameRules>())
        {
            MWWorld::installGameRules(*rules);
            // The core owns what the pack made. A pack from a library must not leave its objects behind when the
            // library unloads; a built-in pack's code stays for the whole run.
            mRulesFromPack = !mGamePackBuiltIn;
            Log(Debug::Info) << "Game rules of '" << rules->gameId() << "' installed";
        }
    }

    void ExtensionHost::start()
    {
        if (!mLoaded || mStarted)
            return;
        mStarted = true;
        beginRunning();
        mLifecycle.initialize();
        finishStart();
    }

    void ExtensionHost::beginRunning()
    {
        if (mRunning)
            return;

        mLogBridge = std::make_shared<LogBridge>();
        mLogBridge->mService = &mHost.loggingService();
        mHost.loggingService().addSink(std::make_shared<EngineLogSink>());
        Debug::setExtraLogListener([bridge = mLogBridge](Debug::Level level, std::string_view,
                                       std::string_view message) { bridge->forward(level, message); });

        // The name scheme is in task 0016. Extension code never runs in Lua: the payload is copied here.
        mHost.eventBus().setForwardSink(
            [this](const openeden::extension::ForwardedEventRecord& record, const void* data, std::size_t size) {
                std::vector<openeden::extension::DecodedField> decoded;
                std::string error;
                if (!openeden::extension::decodeForwardedEvent(record, data, size, decoded, error))
                {
                    Log(Debug::Warning) << "Extension '" << record.extensionId << "' event '" << record.name
                                        << "' dropped: " << error;
                    return;
                }
                std::string name = "openeden_" + record.extensionId + "_" + record.name;
                std::vector<MWBase::LuaManager::ExtensionEventField> fields;
                fields.reserve(decoded.size());
                for (openeden::extension::DecodedField& field : decoded)
                    fields.push_back({ std::move(field.name), std::move(field.value) });
                // Through the queue, so that an event never overtakes one still waiting
                if (mPendingLuaEvents.size() >= kMaxPendingLuaEvents)
                {
                    if (!mPendingLuaEventsOverflowed)
                        Log(Debug::Warning) << "Extension '" << record.extensionId << "' event '" << record.name
                                            << "' dropped: too many events waiting for a game to start";
                    mPendingLuaEventsOverflowed = true;
                    return;
                }
                mPendingLuaEvents.push_back({ std::move(name), std::move(fields) });
            });

        mLifecycle.setBeforeUnload([this, bridge = mLogBridge] {
            // Stops physics threads queueing before anything they could reach goes
            if (mPhysics != nullptr)
                mPhysics->setContactObserver(nullptr);
            // Rules made by a pack in a library must not outlive it: the core owns them, but their code is the
            // library's
            if (mRulesFromPack)
            {
                MWWorld::installCraftingRules(nullptr);
                mRulesFromPack = false;
            }
            mHost.eventBus().setForwardSink(nullptr);
            mHost.vfsService().setEngineVfs(nullptr);
            const std::lock_guard lock(bridge->mMutex);
            bridge->mService = nullptr;
        });

        // The debug draw display and the profiler overlay (task 0017 stage 5), both off by default. Their storage is
        // made here, once; with both off no hook is installed and the frame is exactly as before.
        mDebugDrawShown = Settings::game().mExtensionDebugDraw;
        mProfilerShown = Settings::game().mExtensionProfiler;
        if (mDebugDrawShown)
            mDebugFrame = std::make_unique<openeden::extension::DebugDrawFrame>();
        if (mProfilerShown)
        {
            mProfilerSnapshot = std::make_unique<openeden::extension::ProfilerSnapshot>();
            mLifecycle.setPhaseTiming(true);
        }
        if (mDebugDrawShown || mProfilerShown)
            mLifecycle.setFrameEndHook(&ExtensionHost::frameEndHook, this);

        mRunning = true;
        Misc::resetGameConventions(); // a session without a pack plays with the defaults
    }

    // Main thread, inside runPhase(Phase::Ui): after the extensions' Ui callbacks and before the manager resets debug
    // draw and the profiler. The renderer draws mDebugFrame later in the same frame; the debug window formats
    // mProfilerSnapshot when it updates.
    void ExtensionHost::frameEndHook(void* host, float)
    {
        static_cast<ExtensionHost*>(host)->collectFrameEnd();
    }

    void ExtensionHost::collectFrameEnd()
    {
        if (mDebugDrawShown)
        {
            mDebugFrame->collect(mHost.debugDrawService());
            if (mDebugFrame->dropped().total() > 0 && !mReportedDebugOverflow)
            {
                mReportedDebugOverflow = true;
                const auto& dropped = mDebugFrame->dropped();
                Log(Debug::Warning) << "Extension debug draw over its caps; dropped this frame: " << dropped.points
                                    << " points, " << dropped.lines << " lines, " << dropped.boxes << " boxes, "
                                    << dropped.spheres << " spheres, " << dropped.labels << " labels (reported once)";
            }
        }
        if (mProfilerShown)
            mHost.profilerService().snapshot(*mProfilerSnapshot);
    }

    std::string ExtensionHost::profilerReport()
    {
        if (!mProfilerShown)
            return {};
        // 16 KiB holds a full snapshot (ProfilerSnapshot::MaxRows rows); the window asks a few times a second
        std::string text(16384, '\0');
        text.resize(openeden::extension::formatProfilerReport(*mProfilerSnapshot, text, true));
        return text;
    }

    void ExtensionHost::finishStart()
    {
        // The pack's data conventions go to the generic engine (task 0085, move 1). With no pack, or one that provides
        // the contract below version 1.1, the engine keeps its defaults, which are Morrowind's.
        // A pack that failed to start (session error) installs nothing: its contract may be half set up.
        if (const openeden::contracts::GameContract* game = mLifecycle.getSessionError().empty()
                ? mLifecycle.serviceLocator().find<openeden::contracts::GameContract>()
                : nullptr)
        {
            Misc::setGameConventions(openeden::extension::gameConventionsFrom(*game));
            Log(Debug::Info) << "Game conventions of '" << game->getGameId() << "' installed";
        }

        for (const std::string& id : mLifecycle.getLoadOrder())
        {
            if (mLifecycle.isExtensionActive(id))
                Log(Debug::Info) << "Extension '" << id << "' started";
        }
        for (const std::string& id : mLifecycle.getDisabledExtensions())
            Log(Debug::Error) << "Extension '" << id << "' is disabled";
    }

    void ExtensionHost::stop()
    {
        if (!mLoaded)
            return;
        mLoaded = false;
        mRunning = false;

        Log(Debug::Info) << "Stopping extensions";
        mLifecycle.shutdown();
        // Nothing of this session is drawn or shown after it ends (task 0017 stage 5): the renderer asks debugDrawFrame()
        // each frame and gets null, and no hook points at this object any more
        mLifecycle.setFrameEndHook(nullptr, nullptr);
        mDebugDrawShown = false;
        mProfilerShown = false;
        // The conventions are not reset here: workers may still be reading them while the session ends, the values are
        // copies (nothing refers to the unloaded pack), and the next session sets them again at start().

        if (mWorld)
        {
            mHost.entityManager().setWorld(nullptr);
            mHost.detachPhysicsWorld();
            mWorld.reset();
            mPhysics = nullptr;
        }
    }
}
