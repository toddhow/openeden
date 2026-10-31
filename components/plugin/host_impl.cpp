#include "host_impl.hpp"

namespace openeden::plugin
{
    HostImpl::HostImpl(std::string hostName, bool editorActive)
        : mEditorService(editorActive)
    {
        mHostInfo.hostName = std::move(hostName);
        mHostInfo.sdkVersion = openeden::sdk::kSdkVersion;
    }

    void HostImpl::disablePlugin(std::string_view pluginId)
    {
        mEventBus.unsubscribeAll(pluginId);
        mEntityManager.unregisterLifecycleListener(pluginId);
        mSaveService.unregisterHandler(pluginId);
        mEditorService.unregisterExtensions(pluginId);
    }

    void HostImpl::releasePluginObjects()
    {
        mEventBus.releasePluginObjects();
        mEntityManager.releasePluginObjects();
        mSaveService.releasePluginObjects();
        mVfsService.releasePluginObjects();
        mLoggingService.releasePluginObjects();
        mEditorService.releasePluginObjects();
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

    openeden::sdk::SaveService* HostImpl::getSaveService()
    {
        return &mSaveService;
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

    openeden::sdk::EditorService* HostImpl::getEditorService()
    {
        return &mEditorService;
    }
} // namespace openeden::plugin
