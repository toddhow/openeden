#include "host_impl.hpp"

namespace openeden::plugin
{
    HostImpl::HostImpl(std::string hostName, bool editorActive)
        : mEditorService(editorActive)
    {
        mHostInfo.hostName = std::move(hostName);
        mHostInfo.sdkVersion = openeden::sdk::kSdkVersion;
        mPhysicsService.setCollisionLayerRegistry(&mCollisionLayers);
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

    openeden::sdk::PhysicsService* HostImpl::getPhysicsService()
    {
        return &mPhysicsService;
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

    openeden::sdk::ScriptingService* HostImpl::getScriptingService()
    {
        return &mScriptingService;
    }

    openeden::sdk::EditorService* HostImpl::getEditorService()
    {
        return &mEditorService;
    }
} // namespace openeden::plugin
