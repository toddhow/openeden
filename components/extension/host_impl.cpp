#include "host_impl.hpp"

namespace openeden::extension
{
    HostImpl::HostImpl(std::string hostName)
    {
        mHostInfo.hostName = std::move(hostName);
        mHostInfo.sdkVersion = openeden::sdk::kSdkVersion;
    }

    void HostImpl::disableExtension(std::string_view extensionId)
    {
        mEventBus.unsubscribeAll(extensionId);
        mEntityManager.unregisterLifecycleListener(extensionId);
        // A disabled game pack's handlers point into the pack; its record types, loaders, languages and windows go
        // with it (the pack is the only registrant, and there is one per session).
        mSaveChunks.unregisterPack(extensionId);
        mBindings.retirePack(extensionId);
    }

    void HostImpl::releaseExtensionObjects()
    {
        mEventBus.releaseExtensionObjects();
        mEntityManager.releaseExtensionObjects();
        mVfsService.releaseExtensionObjects();
        mLoggingService.releaseExtensionObjects();
        mSaveChunks.clear();
        mRecords.clear();
        mContent.clear();
        mScriptHost.clear();
        mUi.clear();
        mBindings.retireAll(); // the namespaces stay as retired shells: a script still holding one gets an error
    }

    const openeden::sdk::HostInfo& HostImpl::getHostInfo() const
    {
        return mHostInfo;
    }

    openeden::sdk::EventBus* HostImpl::getEventBus()
    {
        return &mEventBus;
    }

    openeden::sdk::EntityManager* HostImpl::getEntityManager()
    {
        return &mEntityManager;
    }

    openeden::sdk::SettingsService* HostImpl::getSettingsService()
    {
        return &mSettingsService;
    }

    openeden::sdk::VfsService* HostImpl::getVfsService()
    {
        return &mVfsService;
    }

    openeden::sdk::PhysicsQueryService* HostImpl::getPhysicsQueryService()
    {
        return physicsService();
    }

    void HostImpl::attachPhysicsWorld(JPH::PhysicsSystem& world, float worldUnitsPerMeter)
    {
        mPhysicsService.emplace(world, worldUnitsPerMeter);
    }

    void HostImpl::detachPhysicsWorld() noexcept
    {
        mPhysicsService.reset();
    }

    openeden::sdk::LoggingService* HostImpl::getLoggingService()
    {
        return &mLoggingService;
    }

    openeden::sdk::ProfilerService* HostImpl::getProfilerService()
    {
        return &mProfilerService;
    }

    openeden::sdk::ScratchMemoryService* HostImpl::getScratchMemoryService()
    {
        return &mScratchMemoryService;
    }

    openeden::sdk::DebugDrawService* HostImpl::getDebugDrawService()
    {
        return &mDebugDrawService;
    }
} // namespace openeden::extension
