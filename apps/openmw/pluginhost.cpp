#include "pluginhost.hpp"

#include <algorithm>
#include <map>
#include <system_error>
#include <mutex>
#include <set>
#include <string_view>

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/defs.hpp>
#include <components/misc/constants.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/plugin/dependency_resolver.hpp>

#include "mwbase/environment.hpp"
#include "mwbase/world.hpp"
#include "mwphysics/physicssystem.hpp"
#include "mwworld/ptr.hpp"
#include "pluginworld.hpp"
#include "mwbase/statemanager.hpp"
#include <components/plugin/plugin_loader.hpp>

#include <openeden/logging.hpp>

namespace OMW
{
    namespace
    {
        constexpr std::string_view kEngineLogSource = "engine";

        // Events held while no game runs. A plugin publishing every frame at the main menu must not grow without end.
        constexpr std::size_t kMaxPendingLuaEvents = 1024;

        // True on a thread while a line crosses between the plugin and engine logs, in either direction. The engine
        // listener runs under Log's lock, so a plugin sink that logs while receiving an engine line must not reach Log
        // again: that line is dropped. And a plugin's line written to the engine log must not come back to the sinks.
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

        openeden::sdk::LogLevel toPluginLevel(Debug::Level level)
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

        // Plugin log lines to the engine log. formattedMessage already has the [pluginId] prefix.
        class EngineLogSink final : public openeden::sdk::LogSink
        {
        public:
            void log(std::string_view pluginId, openeden::sdk::LogLevel level, std::string_view formattedMessage) override
            {
                if (pluginId == kEngineLogSource || sCrossingLogs)
                    return;
                const CrossingLogs crossing;
                Log(toDebugLevel(level)) << formattedMessage;
            }
        };
    }

    // Engine log lines to the plugins' sinks. Debug's listener can run on any thread and outlives this host, so it
    // holds this object, not the host. The link is cut under the lock before plugin libraries unload, which waits for
    // a line in flight. Nothing under the lock reaches Log, because sCrossingLogs stops it, so the lock order is safe.
    struct PluginHost::LogBridge
    {
        std::mutex mMutex;
        openeden::plugin::LoggingServiceImpl* mService = nullptr;

        void forward(Debug::Level level, std::string_view message)
        {
            if (sCrossingLogs)
                return;
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                message.remove_suffix(1);
            const CrossingLogs crossing;
            const std::lock_guard lock(mMutex);
            if (mService != nullptr)
                mService->dispatch(kEngineLogSource, toPluginLevel(level), message);
        }
    };

    PluginHost::PluginHost(const Config& config)
        : mLifecycle(mHost)
    {
        namespace plugin = openeden::plugin;

        mHost.settingsService().useEngineSettings();

        std::vector<plugin::PluginLoadError> errors;
        const std::vector<plugin::DiscoveredPlugin> discovered
            = plugin::discoverPluginDirectories(config.searchDirs, &errors);

        const auto isEnabled = [&](const std::string& id) {
            return std::find(config.enabled.begin(), config.enabled.end(), id) != config.enabled.end();
        };

        // A broken manifest has no reliable ID to check against the enable list, so it is always reported
        for (const plugin::PluginLoadError& error : errors)
        {
            Log(Debug::Error) << "Plugin '" << error.pluginId << "' failed at " << error.stage << ": " << error.message;
            // Only if asked for: a broken folder the player did not enable is no reason for a message every run
            if (isEnabled(error.pluginId))
                mLoadFailures.push_back(error.pluginId + ": " + error.message);
        }

        std::vector<plugin::PluginManifest> loadedManifests;
        std::map<std::string, std::filesystem::path> loadedData;

        std::set<std::string> seen;
        for (const plugin::DiscoveredPlugin& item : discovered)
        {
            const std::string& id = item.manifest.id;
            if (!seen.insert(id).second)
            {
                Log(Debug::Warning) << "Plugin '" << id << "' in " << item.directory
                                    << " ignored: found earlier in another plugin directory";
                continue;
            }
            if (!isEnabled(id))
            {
                Log(Debug::Info) << "Plugin '" << id << "' is installed but not enabled";
                continue;
            }

            Log(Debug::Info) << "Loading plugin '" << id << "' from " << item.directory;
            plugin::PluginLoadResult result = plugin::loadPluginLibrary(item, mHost.getHostInfo());
            if (!result.success)
            {
                Log(Debug::Error) << "Plugin '" << id << "' failed at " << result.error.stage << ": "
                                  << result.error.message;
                mLoadFailures.push_back(id + ": " + result.error.message);
                continue;
            }
            loadedManifests.push_back(result.plugin.manifest);
            if (!result.plugin.manifest.dataPath.empty())
                loadedData[id] = item.directory / result.plugin.manifest.dataPath;
            mLifecycle.addPlugin(
                std::move(result.plugin.manifest), std::move(result.plugin.instance), std::move(result.plugin.library));
            mLoaded = true;
        }

        for (const std::string& id : config.enabled)
        {
            if (seen.find(id) == seen.end())
                Log(Debug::Warning) << "Plugin '" << id << "' is enabled but not installed";
        }

        // The VFS is built before plugins start, and start() is where the lifecycle resolves them too. Resolving is a
        // pure function of the manifests, so both get the same order.
        const plugin::ResolutionResult order
            = plugin::resolveDependencies(loadedManifests, mHost.getHostInfo().sdkVersion);
        for (const std::string& id : order.loadOrder)
        {
            const auto data = loadedData.find(id);
            std::error_code ec;
            if (data != loadedData.end() && std::filesystem::is_directory(data->second, ec)
                && std::find(order.disabledPlugins.begin(), order.disabledPlugins.end(), id)
                    == order.disabledPlugins.end())
                mDataDirs.push_back({ id, data->second });
        }
    }

    void PluginHost::reportDataOverrides(const VFS::Manager& gameData) const
    {
        for (const openeden::plugin::DataOverride& item : openeden::plugin::findDataOverrides(gameData, mDataDirs))
        {
            if (item.overriddenPluginId.empty())
                Log(Debug::Info) << "Plugin '" << item.pluginId << "' overrides game data file " << item.path;
            else
                Log(Debug::Info) << "Plugin '" << item.pluginId << "' overrides file " << item.path << " of plugin '"
                                 << item.overriddenPluginId << "'";
        }
    }

    void PluginHost::setLuaManager(MWBase::LuaManager* lua)
    {
        mLuaManager = lua;
    }

    void PluginHost::sendPendingLuaEventsNow()
    {
        if (mLuaManager == nullptr
            || MWBase::Environment::get().getStateManager()->getState() != MWBase::StateManager::State_Running)
            return;
        for (PendingLuaEvent& event : mPendingLuaEvents)
        {
            try
            {
                mLuaManager->sendPluginEvent(event.mName, event.mFields);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Plugin event '" << event.mName << "' dropped: " << e.what();
            }
        }
        mPendingLuaEvents.clear();
    }

    void PluginHost::prepareSave()
    {
        mChunksToSave = mHost.saveService().collectChunks();
    }

    int PluginHost::countSavedGameRecords() const
    {
        return static_cast<int>(mChunksToSave.size());
    }

    void PluginHost::write(ESM::ESMWriter& writer)
    {
        ESM::PluginChunk record;
        for (openeden::plugin::SaveServiceImpl::Chunk& chunk : mChunksToSave)
        {
            record.mPluginId = std::move(chunk.pluginId);
            record.mVersion = chunk.version;
            record.mData = std::move(chunk.bytes);
            writer.startRecord(ESM::REC_OEPL);
            record.save(writer);
            writer.endRecord(ESM::REC_OEPL);
        }
        mChunksToSave.clear();
    }

    void PluginHost::readRecord(ESM::ESMReader& reader, std::uint32_t type)
    {
        if (type != ESM::REC_OEPL)
            return;
        mLoadedChunks.emplace_back().load(reader);
    }

    void PluginHost::applyLoadedRecords()
    {
        for (ESM::PluginChunk& record : mLoadedChunks)
            mHost.saveService().loadChunk({ std::move(record.mPluginId), record.mVersion, std::move(record.mData) });
        mLoadedChunks.clear();
    }

    void PluginHost::clearSaveData()
    {
        mLoadedChunks.clear();
        mHost.saveService().clearUnreadChunks();
    }

    std::string PluginHost::takeFailureReport()
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
            add(failure.pluginId + ": " + failure.reason);
        }
        return report;
    }

    void PluginHost::attachWorld(MWPhysics::PhysicsSystem& physics)
    {
        if (!mLoaded)
            return;

        mPhysics = &physics;
        mWorld = std::make_unique<PluginWorld>(mHost, &physics);
        mHost.attachPhysicsWorld(physics.getJoltWorld(), Constants::UnitsPerMeter);
        mHost.entityManager().setWorld(mWorld.get());

        // Names for query masks, so that plugins never see the layers' bit numbers (joltlayers.hpp)
        openeden::plugin::PhysicsServiceImpl& queries = *mHost.physicsService();
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

    void PluginHost::deliverWorldEvents()
    {
        if (mWorld && mRunning)
            mWorld->deliver();
    }

    void PluginHost::objectAddedToScene(const MWWorld::Ptr& ptr)
    {
        if (mWorld)
            mWorld->objectAdded(ptr);
    }

    void PluginHost::objectRemovedFromScene(const MWWorld::Ptr& ptr)
    {
        if (mWorld)
            mWorld->objectRemoved(ptr);
    }

    void PluginHost::cellLoaded(std::string_view name)
    {
        if (mWorld)
            mWorld->cellLoaded(name);
    }

    void PluginHost::cellUnloaded(std::string_view name)
    {
        if (mWorld)
            mWorld->cellUnloaded(name);
    }

    void PluginHost::cellChanged(std::string_view name)
    {
        if (mWorld)
            mWorld->cellChanged(name);
    }

    void PluginHost::actorDied(const MWWorld::Ptr& actor)
    {
        if (mWorld)
            mWorld->actorDied(actor);
    }

    void PluginHost::actorRevived(const MWWorld::Ptr& actor)
    {
        if (mWorld)
            mWorld->actorRevived(actor);
    }

    void PluginHost::actorHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& target, float damage, bool successful)
    {
        if (mWorld)
            mWorld->actorHit(attacker, target, damage, successful);
    }

    void PluginHost::gameLoaded()
    {
        if (mWorld)
            mWorld->gameLoaded(MWBase::Environment::get().getWorld()->getPlayerPtr());
    }

    void PluginHost::playerRebuilt(const MWWorld::Ptr& player)
    {
        if (mWorld)
            mWorld->playerRebuilt(player);
    }

    void PluginHost::gameSaved()
    {
        if (mWorld)
            mWorld->gameSaved();
    }

    PluginHost::~PluginHost()
    {
        stop();
    }

    void PluginHost::start()
    {
        if (!mLoaded || mRunning)
            return;

        mLogBridge = std::make_shared<LogBridge>();
        mLogBridge->mService = &mHost.loggingService();
        mHost.loggingService().addSink(std::make_shared<EngineLogSink>());
        Debug::setExtraLogListener([bridge = mLogBridge](Debug::Level level, std::string_view, std::string_view message) {
            bridge->forward(level, message);
        });

        // The name scheme is in task 0016. Plugin code never runs in Lua: the payload is copied here.
        mHost.eventBus().setForwardSink([this](const openeden::plugin::ForwardedEventRecord& record, const void* data,
                                            std::size_t size) {
            std::vector<openeden::plugin::DecodedField> decoded;
            std::string error;
            if (!openeden::plugin::decodeForwardedEvent(record, data, size, decoded, error))
            {
                Log(Debug::Warning) << "Plugin '" << record.pluginId << "' event '" << record.name
                                    << "' dropped: " << error;
                return;
            }
            std::string name = "openeden_" + record.pluginId + "_" + record.name;
            std::vector<MWBase::LuaManager::PluginEventField> fields;
            fields.reserve(decoded.size());
            for (openeden::plugin::DecodedField& field : decoded)
                fields.push_back({ std::move(field.name), std::move(field.value) });
            // Through the queue, so that an event never overtakes one still waiting
            if (mPendingLuaEvents.size() >= kMaxPendingLuaEvents)
            {
                if (!mPendingLuaEventsOverflowed)
                    Log(Debug::Warning) << "Plugin '" << record.pluginId << "' event '" << record.name
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
        mLifecycle.initialize();

        for (const std::string& id : mLifecycle.getLoadOrder())
        {
            if (mLifecycle.isPluginActive(id))
                Log(Debug::Info) << "Plugin '" << id << "' started";
        }
        for (const std::string& id : mLifecycle.getDisabledPlugins())
            Log(Debug::Error) << "Plugin '" << id << "' is disabled";
    }

    void PluginHost::stop()
    {
        if (!mLoaded)
            return;
        mLoaded = false;
        mRunning = false;

        Log(Debug::Info) << "Stopping plugins";
        mLifecycle.shutdown();

        if (mWorld)
        {
            mHost.entityManager().setWorld(nullptr);
            mHost.detachPhysicsWorld();
            mWorld.reset();
            mPhysics = nullptr;
        }
    }
}
