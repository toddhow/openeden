#include "host_impl.hpp"

namespace openeden::extension
{
    HostImpl::HostImpl(std::string hostName, bool editorActive)
        : mEditorService(editorActive)
    {
        mHostInfo.hostName = std::move(hostName);
        mHostInfo.sdkVersion = openeden::sdk::kSdkVersion;
    }

    void HostImpl::disableExtension(std::string_view extensionId)
    {
        mEventBus.unsubscribeAll(extensionId);
        mEntityManager.unregisterLifecycleListener(extensionId);
        mEditorService.unregisterEditorHooks(extensionId);
    }

    void HostImpl::releaseExtensionObjects()
    {
        mEventBus.releaseExtensionObjects();
        mEntityManager.releaseExtensionObjects();
        mVfsService.releaseExtensionObjects();
        mLoggingService.releaseExtensionObjects();
        mEditorService.releaseExtensionObjects();
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

    openeden::sdk::EditorService* HostImpl::getEditorService()
    {
        return &mEditorService;
    }
} // namespace openeden::extension
