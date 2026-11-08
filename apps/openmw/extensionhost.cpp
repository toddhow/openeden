#include "extensionhost.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string_view>
#include <system_error>

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/extension/dependency_resolver.hpp>
#include <components/extension/game_conventions.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/gameconventions.hpp>

#include "extensionworld.hpp"
#include "mwbase/environment.hpp"
#include "mwbase/statemanager.hpp"
#include "mwbase/world.hpp"
#include "mwphysics/physicssystem.hpp"
#include "mwworld/ptr.hpp"
#include <components/extension/extension_loader.hpp>

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

    ExtensionHost::ExtensionHost(const Config& config)
        : mLifecycle(mHost)
    {
        namespace extension = openeden::extension;

        mHost.settingsService().useEngineSettings();

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

    void ExtensionHost::attachWorld(MWPhysics::PhysicsSystem& physics)
    {
        if (!mLoaded)
            return;

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

    ExtensionHost::~ExtensionHost()
    {
        stop();
    }

    void ExtensionHost::start()
    {
        if (!mLoaded || mRunning)
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
            mHost.eventBus().setForwardSink(nullptr);
            mHost.vfsService().setEngineVfs(nullptr);
            const std::lock_guard lock(bridge->mMutex);
            bridge->mService = nullptr;
        });

        mRunning = true;
        Misc::resetGameConventions(); // a session without a pack plays with the defaults
        mLifecycle.initialize();

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
