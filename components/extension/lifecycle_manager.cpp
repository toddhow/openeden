#include <components/extension/lifecycle_manager.hpp>

#include <components/extension/entity_manager.hpp>
#include <components/extension/event_bus.hpp>
#include <components/extension/host_impl.hpp>
#include <components/extension/settings_service.hpp>
#include <components/extension/vfs_service.hpp>
#include <openeden/debug_draw.hpp>
#include <openeden/memory.hpp>
#include <openeden/profiling.hpp>

#include <algorithm>
#include <components/debug/debuglog.hpp>

namespace openeden::extension
{
    LifecycleManager::LifecycleManager(openeden::sdk::Host& host)
        : mHost(host)
    {
        mRegistry.setCapabilityLog(&mCapabilityLog);
    }

    LifecycleManager::~LifecycleManager()
    {
        shutdown();
    }

    void LifecycleManager::addExtension(ExtensionManifest manifest, std::unique_ptr<openeden::sdk::Extension> extension,
        std::unique_ptr<DynamicLibrary> library)
    {
        const std::string id = manifest.id;
        ExtensionRecord record;
        record.manifest = std::move(manifest);
        record.instance = std::move(extension);
        record.library = std::move(library);
        record.kind = openeden::sdk::parsePackKind(record.manifest.kind).value_or(openeden::sdk::PackKind::Feature);
        record.started = false;
        mExtensions[id] = std::move(record);
    }

    void LifecycleManager::prepare()
    {
        if (mPrepared)
            return;
        mPrepared = true;

        if (mExtensions.empty())
        {
            return;
        }

        // One game pack per session: the first by ID is the one of this session, any other is refused before it is
        // resolved (task 0084). mExtensions is ordered by ID, so the choice does not depend on the order packs were
        // added.
        for (const auto& [id, record] : mExtensions)
        {
            if (record.kind != openeden::sdk::PackKind::Game)
                continue;
            if (mGamePackId.empty())
            {
                mGamePackId = id;
                mGamePackVersion = record.manifest.extensionVersion;
                continue;
            }
            const std::string reason = "second game pack '" + id + "' refused: '" + mGamePackId
                + "' is already the game pack of this session (one game pack per session)";
            Log(Debug::Error) << "Extension '" << id << "' failed at game: " << reason;
            mFailures.push_back(Failure{ id, "game", reason });
            mDisabledExtensions.insert(id);
        }

        // Collect manifests
        std::vector<ExtensionManifest> manifests;
        manifests.reserve(mExtensions.size());
        for (const auto& [id, record] : mExtensions)
        {
            if (mDisabledExtensions.find(id) == mDisabledExtensions.end())
                manifests.push_back(record.manifest);
        }

        // Resolve dependencies
        const ResolutionResult resolution = resolveDependencies(manifests, mHost.getHostInfo().sdkVersion);

        for (const auto& error : resolution.errors)
        {
            Log(Debug::Error) << "Extension '" << error.extensionId << "' failed at resolve: " << error.message;
            mFailures.push_back(Failure{ error.extensionId, "resolve", error.message });
            noteGamePackFailure(error.extensionId, "resolve", error.message);
        }

        for (const auto& disabledId : resolution.disabledExtensions)
        {
            mDisabledExtensions.insert(disabledId);
        }

        mLoadOrder = resolution.loadOrder;

        for (const auto& id : mLoadOrder)
        {
            if (mDisabledExtensions.find(id) == mDisabledExtensions.end())
            {
                mActiveExtensions.insert(id);
            }
        }

        // Configure host services with resolved extension load order and manifest settings
        if (auto* eb = dynamic_cast<EventBusImpl*>(mHost.getEventBus()))
        {
            eb->setExtensionLoadOrder(mLoadOrder);
        }
        if (auto* em = dynamic_cast<EntityManagerImpl*>(mHost.getEntityManager()))
        {
            em->setExtensionLoadOrder(mLoadOrder);
        }
        if (auto* ss = dynamic_cast<SettingsServiceImpl*>(mHost.getSettingsService()))
        {
            for (const auto& [id, record] : mExtensions)
            {
                ss->loadManifestDefaults(record.manifest);
            }
        }
        if (auto* vfs = dynamic_cast<VfsServiceImpl*>(mHost.getVfsService()))
        {
            vfs->setExtensionLoadOrder(mLoadOrder);
        }

        // Phase 6.4: registerServices in load order
        for (const auto& id : mLoadOrder)
        {
            if (mActiveExtensions.find(id) == mActiveExtensions.end())
                continue;

            auto it = mExtensions.find(id);
            if (it != mExtensions.end() && it->second.instance)
            {
                mRegistry.setCurrentExtension(id, it->second.kind);
                try
                {
                    it->second.instance->registerServices(mRegistry);
                }
                catch (const std::exception& e)
                {
                    markExtensionFaulted(id, "registerServices", e.what());
                }
                catch (...)
                {
                    markExtensionFaulted(id, "registerServices", "Unknown exception");
                }
            }
        }

        // A provider that just failed must not leave its dependants to resolve and start
        processDeferredFaults();

        // Phase 6.5: resolve in load order
        std::set<std::string> newlyDisabled;
        for (const auto& id : mLoadOrder)
        {
            if (mActiveExtensions.find(id) == mActiveExtensions.end())
                continue;

            auto it = mExtensions.find(id);
            if (it != mExtensions.end() && it->second.instance)
            {
                bool resolveSuccess = false;
                try
                {
                    resolveSuccess = it->second.instance->resolve(mRegistry);
                }
                catch (const std::exception& e)
                {
                    markExtensionFaulted(id, "resolve", e.what());
                    resolveSuccess = false;
                }
                catch (...)
                {
                    markExtensionFaulted(id, "resolve", "Unknown exception");
                    resolveSuccess = false;
                }

                if (!resolveSuccess)
                {
                    noteGamePackFailure(id, "resolve", "resolve() reported that the pack cannot run");
                    newlyDisabled.insert(id);
                    mDisabledExtensions.insert(id);
                    mActiveExtensions.erase(id);
                    mRegistry.withdrawExtension(id);
                }
            }
        }

        // Cascade resolve-failures to any active extension that requires a contract provided by a disabled extension
        bool cascade = !newlyDisabled.empty();
        while (cascade)
        {
            cascade = false;
            for (const auto& id : mLoadOrder)
            {
                if (mActiveExtensions.find(id) == mActiveExtensions.end())
                    continue;

                const auto& manifest = mExtensions[id].manifest;
                for (const auto& req : manifest.requiredContracts)
                {
                    // Check if provider is disabled
                    for (const auto& disabledId : mDisabledExtensions)
                    {
                        const auto& disabledManifest = mExtensions[disabledId].manifest;
                        for (const auto& provision : disabledManifest.providedContracts)
                        {
                            if (provision.contract == req.contract && provision.version.major == req.version.major)
                            {
                                const std::string reason = "depends on disabled extension '" + disabledId + "'";
                                Log(Debug::Error) << "Extension '" << id << "' failed at resolve: " << reason;
                                mFailures.push_back(Failure{ id, "resolve", reason });
                                noteGamePackFailure(id, "resolve", reason);
                                mDisabledExtensions.insert(id);
                                mActiveExtensions.erase(id);
                                mRegistry.withdrawExtension(id);
                                cascade = true;
                                break;
                            }
                        }
                        if (mActiveExtensions.find(id) == mActiveExtensions.end())
                            break;
                    }
                    if (mActiveExtensions.find(id) == mActiveExtensions.end())
                        break;
                }
            }
        }
    }

    bool LifecycleManager::initialize()
    {
        if (mInitialized)
            return true;

        mInitialized = true;

        if (mExtensions.empty())
        {
            return true;
        }

        // Registers and resolves everything, unless initializeGamePack() has done so already
        prepare();
        startPending(nullptr);
        mDisabledExtensionsList.assign(mDisabledExtensions.begin(), mDisabledExtensions.end());
        return true;
    }

    bool LifecycleManager::hasGamePack() const
    {
        return std::any_of(mExtensions.begin(), mExtensions.end(),
            [](const auto& entry) { return entry.second.kind == openeden::sdk::PackKind::Game; });
    }

    bool LifecycleManager::initializeGamePack()
    {
        if (mPrepared || mInitialized)
            return false;
        if (!hasGamePack())
            return false;

        prepare();

        // The game pack and, transitively, whatever provides a contract it requires (a pack cannot start before its
        // providers). Optional contracts do not pull their providers in: they were registered above, and start in the
        // second phase.
        std::set<std::string> closure;
        std::vector<std::string> pending;
        if (!mGamePackId.empty() && mActiveExtensions.count(mGamePackId) != 0)
        {
            closure.insert(mGamePackId);
            pending.push_back(mGamePackId);
        }
        while (!pending.empty())
        {
            const std::string id = pending.back();
            pending.pop_back();
            for (const auto& req : mExtensions[id].manifest.requiredContracts)
            {
                for (const auto& [otherId, other] : mExtensions)
                {
                    if (closure.count(otherId) != 0 || mActiveExtensions.count(otherId) == 0)
                        continue;
                    const bool provides = std::any_of(other.manifest.providedContracts.begin(),
                        other.manifest.providedContracts.end(), [&](const ContractRequirement& offer) {
                            return offer.contract == req.contract && offer.version.major == req.version.major;
                        });
                    if (provides)
                    {
                        closure.insert(otherId);
                        pending.push_back(otherId);
                    }
                }
            }
        }

        startPending(&closure);
        mDisabledExtensionsList.assign(mDisabledExtensions.begin(), mDisabledExtensions.end());
        return true;
    }

    void LifecycleManager::startPending(const std::set<std::string>* only)
    {
        // Phase 6.6: start in load order
        for (const auto& id : mLoadOrder)
        {
            if (mActiveExtensions.find(id) == mActiveExtensions.end())
                continue;
            if (only != nullptr && only->count(id) == 0)
                continue;

            auto it = mExtensions.find(id);
            if (it != mExtensions.end() && it->second.instance && !it->second.started)
            {
                try
                {
                    it->second.scopedHost
                        = std::make_unique<PackScopedHost>(mHost, id, it->second.kind, mCapabilityLog);
                    it->second.instance->start(*it->second.scopedHost);
                    it->second.started = true;
                    mStartedOrder.push_back(id);
                }
                catch (const std::exception& e)
                {
                    markExtensionFaulted(id, "start", e.what());
                }
                catch (...)
                {
                    markExtensionFaulted(id, "start", "Unknown exception");
                }
                // Before the next extension starts: its provider may just have failed
                processDeferredFaults();
            }
        }
    }

    void LifecycleManager::runPhase(openeden::sdk::Phase phase, float dt)
    {
        // Process deferred events targeted for this phase at phase boundary
        if (auto* eb = mHost.getEventBus())
        {
            eb->processDeferredEvents(phase);
        }

        // No heap allocation in hot path: traverse const reference directly. A game pack's callbacks run before the
        // callbacks of the other kinds in the same phase (pass one, then pass two), each in load order.
        const auto& callbacks = mRegistry.getFrameCallbacks(phase);
        for (const bool gamePass : { true, false })
        {
            for (const auto& entry : callbacks)
            {
                if (entry.game != gamePass)
                    continue;

                if (!entry.extensionId.empty())
                {
                    if (mActiveExtensions.find(entry.extensionId) == mActiveExtensions.end()
                        || mFaultedExtensions.find(entry.extensionId) != mFaultedExtensions.end())
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
                    if (!entry.extensionId.empty())
                    {
                        markExtensionFaulted(entry.extensionId, "runPhase", e.what());
                    }
                }
                catch (...)
                {
                    if (!entry.extensionId.empty())
                    {
                        markExtensionFaulted(entry.extensionId, "runPhase", "Unknown exception");
                    }
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
        // Stop in REVERSE order of started extensions
        for (auto it = mStartedOrder.rbegin(); it != mStartedOrder.rend(); ++it)
        {
            auto extensionIt = mExtensions.find(*it);
            if (extensionIt != mExtensions.end() && extensionIt->second.instance && extensionIt->second.started)
            {
                try
                {
                    extensionIt->second.instance->stop();
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Error) << "Extension '" << *it << "' failed at stop: " << e.what();
                }
                catch (...)
                {
                    Log(Debug::Error) << "Extension '" << *it << "' failed at stop: unknown exception";
                }
                extensionIt->second.started = false;
            }
        }

        // Destroy extension instances in REVERSE load order (consumers before providers)
        for (auto it = mLoadOrder.rbegin(); it != mLoadOrder.rend(); ++it)
        {
            auto extensionIt = mExtensions.find(*it);
            if (extensionIt != mExtensions.end() && extensionIt->second.instance)
            {
                extensionIt->second.instance.reset();
            }
        }

        // Destroy any remaining instances not in loadOrder
        for (auto& [id, record] : mExtensions)
        {
            record.instance.reset();
            record.scopedHost.reset();
        }

        // Clear all registered callbacks (std::function) and contracts BEFORE unloading dynamic libraries
        mRegistry.clear();

        // Extensions may have left objects in the services (an extension whose start threw never ran stop), and their
        // code goes away with the library.
        if (mBeforeUnload)
            mBeforeUnload();
        if (auto* host = dynamic_cast<HostImpl*>(&mHost))
            host->releaseExtensionObjects();

        // Unload dynamic libraries in REVERSE load order AFTER instances and callbacks are destructed
        for (auto it = mLoadOrder.rbegin(); it != mLoadOrder.rend(); ++it)
        {
            auto extensionIt = mExtensions.find(*it);
            if (extensionIt != mExtensions.end() && extensionIt->second.library)
            {
                extensionIt->second.library.reset();
            }
        }

        // Unload any remaining libraries not in loadOrder
        for (auto& [id, record] : mExtensions)
        {
            record.library.reset();
        }

        mStartedOrder.clear();
        mActiveExtensions.clear();
        mPendingFaultedExtensions.clear();
    }

    void LifecycleManager::markExtensionFaulted(const std::string& extensionId, const char* stage, const char* reason)
    {
        mFailures.push_back(Failure{ extensionId, stage ? stage : "unknown", reason ? reason : "no reason given" });
        Log(Debug::Error) << "Extension '" << extensionId << "' failed at " << (stage ? stage : "unknown") << ": "
                          << (reason ? reason : "no reason given");

        noteGamePackFailure(extensionId, stage ? stage : "unknown", reason ? reason : "no reason given");

        mFaultedExtensions.insert(extensionId);
        mActiveExtensions.erase(extensionId);
        mDisabledExtensions.insert(extensionId);
        mPendingFaultedExtensions.push_back(extensionId);
    }

    void LifecycleManager::noteGamePackFailure(
        const std::string& extensionId, std::string_view stage, std::string_view reason)
    {
        // Only the game pack of this session: a refused second pack is not the game. The first failure is reported.
        if (extensionId != mGamePackId || !mSessionError.empty())
            return;
        mSessionError = "Game pack '" + extensionId + "' failed at " + std::string(stage) + ": " + std::string(reason)
            + ". The game cannot go on and saving is refused.";
        Log(Debug::Error) << mSessionError;
        if (auto* host = dynamic_cast<HostImpl*>(&mHost))
            host->saveChunks().refuseSaving(mSessionError);
    }

    void LifecycleManager::processDeferredFaults()
    {
        if (mPendingFaultedExtensions.empty())
            return;

        // The list can grow while walking it: a dependant's own dependants go too
        for (std::size_t i = 0; i < mPendingFaultedExtensions.size(); ++i)
        {
            const std::string extensionId = mPendingFaultedExtensions[i];
            mRegistry.withdrawExtension(extensionId);
            if (auto* host = dynamic_cast<HostImpl*>(&mHost))
                host->disableExtension(extensionId);

            const auto faulted = mExtensions.find(extensionId);
            if (faulted == mExtensions.end())
                continue;
            for (const auto& id : mLoadOrder)
            {
                const auto record = mExtensions.find(id);
                if (record == mExtensions.end() || mActiveExtensions.find(id) == mActiveExtensions.end())
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
                    markExtensionFaulted(
                        id, "dependency", ("depends on faulted extension '" + extensionId + "'").c_str());
            }
        }
        mPendingFaultedExtensions.clear();
        mDisabledExtensionsList.assign(mDisabledExtensions.begin(), mDisabledExtensions.end());
    }

    bool LifecycleManager::isExtensionActive(std::string_view id) const
    {
        return mActiveExtensions.find(std::string(id)) != mActiveExtensions.end();
    }

    bool LifecycleManager::isExtensionFaulted(std::string_view id) const
    {
        return mFaultedExtensions.find(std::string(id)) != mFaultedExtensions.end();
    }

    size_t LifecycleManager::getActiveExtensionCount() const
    {
        return mActiveExtensions.size();
    }

    const std::vector<std::string>& LifecycleManager::getLoadOrder() const
    {
        return mLoadOrder;
    }

    const std::vector<std::string>& LifecycleManager::getDisabledExtensions() const
    {
        return mDisabledExtensionsList;
    }
} // namespace openeden::extension
