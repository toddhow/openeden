#include <components/plugin/lifecycle_manager.hpp>

#include <components/plugin/entity_manager.hpp>
#include <components/plugin/event_bus.hpp>
#include <components/plugin/host_impl.hpp>
#include <components/plugin/settings_service.hpp>
#include <components/plugin/vfs_service.hpp>
#include <openeden/debug_draw.hpp>
#include <openeden/memory.hpp>
#include <openeden/profiling.hpp>

#include <algorithm>
#include <components/debug/debuglog.hpp>

namespace openeden::plugin
{
    LifecycleManager::LifecycleManager(openeden::sdk::Host& host)
        : mHost(host)
    {
    }

    LifecycleManager::~LifecycleManager()
    {
        shutdown();
    }

    void LifecycleManager::addPlugin(
        PluginManifest manifest, std::unique_ptr<openeden::sdk::Plugin> plugin, std::unique_ptr<DynamicLibrary> library)
    {
        const std::string id = manifest.id;
        PluginRecord record;
        record.manifest = std::move(manifest);
        record.instance = std::move(plugin);
        record.library = std::move(library);
        record.started = false;
        mPlugins[id] = std::move(record);
    }

    bool LifecycleManager::initialize()
    {
        if (mInitialized)
            return true;

        mInitialized = true;

        if (mPlugins.empty())
        {
            return true;
        }

        // Collect manifests
        std::vector<PluginManifest> manifests;
        manifests.reserve(mPlugins.size());
        for (const auto& [id, record] : mPlugins)
        {
            manifests.push_back(record.manifest);
        }

        // Resolve dependencies
        const ResolutionResult resolution = resolveDependencies(manifests, mHost.getHostInfo().sdkVersion);

        for (const auto& error : resolution.errors)
        {
            Log(Debug::Error) << "Plugin '" << error.pluginId << "' failed at resolve: " << error.message;
        }

        for (const auto& disabledId : resolution.disabledPlugins)
        {
            mDisabledPlugins.insert(disabledId);
        }

        mLoadOrder = resolution.loadOrder;

        for (const auto& id : mLoadOrder)
        {
            if (mDisabledPlugins.find(id) == mDisabledPlugins.end())
            {
                mActivePlugins.insert(id);
            }
        }

        // Configure host services with resolved plugin load order and manifest settings
        if (auto* eb = dynamic_cast<EventBusImpl*>(mHost.getEventBus()))
        {
            eb->setPluginLoadOrder(mLoadOrder);
        }
        if (auto* em = dynamic_cast<EntityManagerImpl*>(mHost.getEntityManager()))
        {
            em->setPluginLoadOrder(mLoadOrder);
        }
        if (auto* ss = dynamic_cast<SettingsServiceImpl*>(mHost.getSettingsService()))
        {
            for (const auto& [id, record] : mPlugins)
            {
                ss->loadManifestDefaults(record.manifest);
            }
        }
        if (auto* vfs = dynamic_cast<VfsServiceImpl*>(mHost.getVfsService()))
        {
            vfs->setPluginLoadOrder(mLoadOrder);
        }

        // Phase 6.4: registerServices in load order
        for (const auto& id : mLoadOrder)
        {
            if (mActivePlugins.find(id) == mActivePlugins.end())
                continue;

            auto it = mPlugins.find(id);
            if (it != mPlugins.end() && it->second.instance)
            {
                mRegistry.setCurrentPlugin(id);
                try
                {
                    it->second.instance->registerServices(mRegistry);
                }
                catch (const std::exception& e)
                {
                    markPluginFaulted(id, "registerServices", e.what());
                }
                catch (...)
                {
                    markPluginFaulted(id, "registerServices", "Unknown exception");
                }
            }
        }

        // A provider that just failed must not leave its dependants to resolve and start
        processDeferredFaults();

        // Phase 6.5: resolve in load order
        std::set<std::string> newlyDisabled;
        for (const auto& id : mLoadOrder)
        {
            if (mActivePlugins.find(id) == mActivePlugins.end())
                continue;

            auto it = mPlugins.find(id);
            if (it != mPlugins.end() && it->second.instance)
            {
                bool resolveSuccess = false;
                try
                {
                    resolveSuccess = it->second.instance->resolve(mRegistry);
                }
                catch (const std::exception& e)
                {
                    markPluginFaulted(id, "resolve", e.what());
                    resolveSuccess = false;
                }
                catch (...)
                {
                    markPluginFaulted(id, "resolve", "Unknown exception");
                    resolveSuccess = false;
                }

                if (!resolveSuccess)
                {
                    newlyDisabled.insert(id);
                    mDisabledPlugins.insert(id);
                    mActivePlugins.erase(id);
                    mRegistry.withdrawPlugin(id);
                }
            }
        }

        // Cascade resolve-failures to any active plugin that requires a contract provided by a disabled plugin
        bool cascade = !newlyDisabled.empty();
        while (cascade)
        {
            cascade = false;
            for (const auto& id : mLoadOrder)
            {
                if (mActivePlugins.find(id) == mActivePlugins.end())
                    continue;

                const auto& manifest = mPlugins[id].manifest;
                for (const auto& req : manifest.requiredContracts)
                {
                    // Check if provider is disabled
                    for (const auto& disabledId : mDisabledPlugins)
                    {
                        const auto& disabledManifest = mPlugins[disabledId].manifest;
                        for (const auto& provision : disabledManifest.providedContracts)
                        {
                            if (provision.contract == req.contract && provision.version.major == req.version.major)
                            {
                                Log(Debug::Error) << "Plugin '" << id << "' failed at resolve: depends on disabled plugin '"
                                                  << disabledId << "'";
                                mDisabledPlugins.insert(id);
                                mActivePlugins.erase(id);
                                mRegistry.withdrawPlugin(id);
                                cascade = true;
                                break;
                            }
                        }
                        if (mActivePlugins.find(id) == mActivePlugins.end())
                            break;
                    }
                    if (mActivePlugins.find(id) == mActivePlugins.end())
                        break;
                }
            }
        }

        // Phase 6.6: start in load order
        for (const auto& id : mLoadOrder)
        {
            if (mActivePlugins.find(id) == mActivePlugins.end())
                continue;

            auto it = mPlugins.find(id);
            if (it != mPlugins.end() && it->second.instance)
            {
                try
                {
                    it->second.instance->start(mHost);
                    it->second.started = true;
                    mStartedOrder.push_back(id);
                }
                catch (const std::exception& e)
                {
                    markPluginFaulted(id, "start", e.what());
                }
                catch (...)
                {
                    markPluginFaulted(id, "start", "Unknown exception");
                }
                // Before the next plugin starts: its provider may just have failed
                processDeferredFaults();
            }
        }

        mDisabledPluginsList.assign(mDisabledPlugins.begin(), mDisabledPlugins.end());
        return true;
    }

    void LifecycleManager::runPhase(openeden::sdk::Phase phase, float dt)
    {
        // Process deferred events targeted for this phase at phase boundary
        if (auto* eb = mHost.getEventBus())
        {
            eb->processDeferredEvents(phase);
        }

        // No heap allocation in hot path: traverse const reference directly
        const auto& callbacks = mRegistry.getFrameCallbacks(phase);
        for (const auto& entry : callbacks)
        {
            if (!entry.pluginId.empty())
            {
                if (mActivePlugins.find(entry.pluginId) == mActivePlugins.end()
                    || mFaultedPlugins.find(entry.pluginId) != mFaultedPlugins.end())
                {
                    continue;
                }
            }

            try
            {
                if (entry.callback)
                {
                    entry.callback(dt);
                }
            }
            catch (const std::exception& e)
            {
                if (!entry.pluginId.empty())
                {
                    markPluginFaulted(entry.pluginId, "runPhase", e.what());
                }
            }
            catch (...)
            {
                if (!entry.pluginId.empty())
                {
                    markPluginFaulted(entry.pluginId, "runPhase", "Unknown exception");
                }
            }
        }

        processDeferredFaults();

        // End-of-frame maintenance at final phase (Ui)
        if (phase == openeden::sdk::Phase::Ui)
        {
            if (auto* mem = mHost.getScratchMemoryService())
            {
                mem->reset();
            }
            if (auto* dd = mHost.getDebugDrawService())
            {
                dd->endFrame(dt);
            }
            if (auto* profiler = mHost.getProfilerService())
            {
                profiler->resetFrame();
            }
        }
    }

    void LifecycleManager::shutdown()
    {
        // Stop in REVERSE order of started plugins
        for (auto it = mStartedOrder.rbegin(); it != mStartedOrder.rend(); ++it)
        {
            auto pluginIt = mPlugins.find(*it);
            if (pluginIt != mPlugins.end() && pluginIt->second.instance && pluginIt->second.started)
            {
                try
                {
                    pluginIt->second.instance->stop();
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Error) << "Plugin '" << *it << "' failed at stop: " << e.what();
                }
                catch (...)
                {
                    Log(Debug::Error) << "Plugin '" << *it << "' failed at stop: unknown exception";
                }
                pluginIt->second.started = false;
            }
        }

        // Destroy plugin instances in REVERSE load order (consumers before providers)
        for (auto it = mLoadOrder.rbegin(); it != mLoadOrder.rend(); ++it)
        {
            auto pluginIt = mPlugins.find(*it);
            if (pluginIt != mPlugins.end() && pluginIt->second.instance)
            {
                pluginIt->second.instance.reset();
            }
        }

        // Destroy any remaining instances not in loadOrder
        for (auto& [id, record] : mPlugins)
        {
            record.instance.reset();
        }

        // Clear all registered callbacks (std::function) and contracts BEFORE unloading dynamic libraries
        mRegistry.clear();

        // Plugins may have left objects in the services (a plugin whose start threw never ran stop), and their code
        // goes away with the library.
        if (mBeforeUnload)
            mBeforeUnload();
        if (auto* host = dynamic_cast<HostImpl*>(&mHost))
            host->releasePluginObjects();

        // Unload dynamic libraries in REVERSE load order AFTER instances and callbacks are destructed
        for (auto it = mLoadOrder.rbegin(); it != mLoadOrder.rend(); ++it)
        {
            auto pluginIt = mPlugins.find(*it);
            if (pluginIt != mPlugins.end() && pluginIt->second.library)
            {
                pluginIt->second.library.reset();
            }
        }

        // Unload any remaining libraries not in loadOrder
        for (auto& [id, record] : mPlugins)
        {
            record.library.reset();
        }

        mStartedOrder.clear();
        mActivePlugins.clear();
        mPendingFaultedPlugins.clear();
    }

    void LifecycleManager::markPluginFaulted(const std::string& pluginId, const char* stage, const char* reason)
    {
        Log(Debug::Error) << "Plugin '" << pluginId << "' failed at " << (stage ? stage : "unknown") << ": "
                          << (reason ? reason : "no reason given");

        mFaultedPlugins.insert(pluginId);
        mActivePlugins.erase(pluginId);
        mDisabledPlugins.insert(pluginId);
        mPendingFaultedPlugins.push_back(pluginId);
    }

    void LifecycleManager::processDeferredFaults()
    {
        if (mPendingFaultedPlugins.empty())
            return;

        // The list can grow while walking it: a dependant's own dependants go too
        for (std::size_t i = 0; i < mPendingFaultedPlugins.size(); ++i)
        {
            const std::string pluginId = mPendingFaultedPlugins[i];
            mRegistry.withdrawPlugin(pluginId);
            if (auto* host = dynamic_cast<HostImpl*>(&mHost))
                host->disablePlugin(pluginId);

            const auto faulted = mPlugins.find(pluginId);
            if (faulted == mPlugins.end())
                continue;
            for (const auto& id : mLoadOrder)
            {
                const auto record = mPlugins.find(id);
                if (record == mPlugins.end() || mActivePlugins.find(id) == mActivePlugins.end())
                    continue;
                const auto& required = record->second.manifest.requiredContracts;
                const bool dependsOnFaulted
                    = std::any_of(required.begin(), required.end(), [&](const ContractRequirement& req) {
                          const auto& provided = faulted->second.manifest.providedContracts;
                          return std::any_of(provided.begin(), provided.end(), [&](const ContractRequirement& offer) {
                              return offer.contract == req.contract && offer.version.major == req.version.major;
                          });
                      });
                if (dependsOnFaulted)
                    markPluginFaulted(id, "dependency", ("depends on faulted plugin '" + pluginId + "'").c_str());
            }
        }
        mPendingFaultedPlugins.clear();
        mDisabledPluginsList.assign(mDisabledPlugins.begin(), mDisabledPlugins.end());
    }

    bool LifecycleManager::isPluginActive(std::string_view id) const
    {
        return mActivePlugins.find(std::string(id)) != mActivePlugins.end();
    }

    bool LifecycleManager::isPluginFaulted(std::string_view id) const
    {
        return mFaultedPlugins.find(std::string(id)) != mFaultedPlugins.end();
    }

    size_t LifecycleManager::getActivePluginCount() const
    {
        return mActivePlugins.size();
    }

    const std::vector<std::string>& LifecycleManager::getLoadOrder() const
    {
        return mLoadOrder;
    }

    const std::vector<std::string>& LifecycleManager::getDisabledPlugins() const
    {
        return mDisabledPluginsList;
    }
} // namespace openeden::plugin
