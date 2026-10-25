#pragma once

#include <components/plugin/collision_layer_registry.hpp>
#include <components/plugin/debug_draw_service.hpp>
#include <components/plugin/editor_service.hpp>
#include <components/plugin/entity_manager.hpp>
#include <components/plugin/event_bus.hpp>
#include <components/plugin/logging_service.hpp>
#include <components/plugin/physics_service.hpp>
#include <components/plugin/profiler_service.hpp>
#include <components/plugin/save_service.hpp>
#include <components/plugin/scratch_memory.hpp>
#include <components/plugin/scripting_service.hpp>
#include <components/plugin/settings_service.hpp>
#include <components/plugin/vfs_service.hpp>
#include <openeden/host.hpp>

namespace openeden::plugin
{
    /**
     * @brief Concrete host implementation managing core engine plugin services.
     */
    class HostImpl : public openeden::sdk::Host
    {
    public:
        explicit HostImpl(std::string hostName = "OpenEden", bool editorActive = false);
        ~HostImpl() override = default;

        // Non-copyable
        HostImpl(const HostImpl&) = delete;
        HostImpl& operator=(const HostImpl&) = delete;

        // Movable
        HostImpl(HostImpl&&) noexcept = default;
        HostImpl& operator=(HostImpl&&) noexcept = default;

        [[nodiscard]] const openeden::sdk::HostInfo& getHostInfo() const override;

        [[nodiscard]] openeden::sdk::EventBus* getEventBus() override;
        [[nodiscard]] openeden::sdk::EntityManager* getEntityManager() override;
        [[nodiscard]] openeden::sdk::SettingsService* getSettingsService() override;
        [[nodiscard]] openeden::sdk::SaveService* getSaveService() override;
        [[nodiscard]] openeden::sdk::VfsService* getVfsService() override;
        [[nodiscard]] openeden::sdk::PhysicsService* getPhysicsService() override;
        [[nodiscard]] openeden::sdk::LoggingService* getLoggingService() override;
        [[nodiscard]] openeden::sdk::ProfilerService* getProfilerService() override;
        [[nodiscard]] openeden::sdk::ScratchMemoryService* getScratchMemoryService() override;
        [[nodiscard]] openeden::sdk::DebugDrawService* getDebugDrawService() override;
        [[nodiscard]] openeden::sdk::ScriptingService* getScriptingService() override;
        [[nodiscard]] openeden::sdk::EditorService* getEditorService() override;

        // Concrete service accessors for engine internals
        [[nodiscard]] EventBusImpl& eventBus() noexcept { return mEventBus; }
        [[nodiscard]] const EventBusImpl& eventBus() const noexcept { return mEventBus; }

        [[nodiscard]] EntityManagerImpl& entityManager() noexcept { return mEntityManager; }
        [[nodiscard]] const EntityManagerImpl& entityManager() const noexcept { return mEntityManager; }

        [[nodiscard]] SettingsServiceImpl& settingsService() noexcept { return mSettingsService; }
        [[nodiscard]] const SettingsServiceImpl& settingsService() const noexcept { return mSettingsService; }

        [[nodiscard]] SaveServiceImpl& saveService() noexcept { return mSaveService; }
        [[nodiscard]] const SaveServiceImpl& saveService() const noexcept { return mSaveService; }

        [[nodiscard]] CollisionLayerRegistry& collisionLayers() noexcept { return mCollisionLayers; }
        [[nodiscard]] const CollisionLayerRegistry& collisionLayers() const noexcept { return mCollisionLayers; }

        [[nodiscard]] VfsServiceImpl& vfsService() noexcept { return mVfsService; }
        [[nodiscard]] const VfsServiceImpl& vfsService() const noexcept { return mVfsService; }

        [[nodiscard]] PhysicsServiceImpl& physicsService() noexcept { return mPhysicsService; }
        [[nodiscard]] const PhysicsServiceImpl& physicsService() const noexcept { return mPhysicsService; }

        [[nodiscard]] LoggingServiceImpl& loggingService() noexcept { return mLoggingService; }
        [[nodiscard]] const LoggingServiceImpl& loggingService() const noexcept { return mLoggingService; }

        [[nodiscard]] ProfilerServiceImpl& profilerService() noexcept { return mProfilerService; }
        [[nodiscard]] const ProfilerServiceImpl& profilerService() const noexcept { return mProfilerService; }

        [[nodiscard]] ScratchMemoryServiceImpl& scratchMemoryService() noexcept { return mScratchMemoryService; }
        [[nodiscard]] const ScratchMemoryServiceImpl& scratchMemoryService() const noexcept
        {
            return mScratchMemoryService;
        }

        [[nodiscard]] DebugDrawServiceImpl& debugDrawService() noexcept { return mDebugDrawService; }
        [[nodiscard]] const DebugDrawServiceImpl& debugDrawService() const noexcept { return mDebugDrawService; }

        [[nodiscard]] ScriptingServiceImpl& scriptingService() noexcept { return mScriptingService; }
        [[nodiscard]] const ScriptingServiceImpl& scriptingService() const noexcept { return mScriptingService; }

        [[nodiscard]] EditorServiceImpl& editorService() noexcept { return mEditorService; }
        [[nodiscard]] const EditorServiceImpl& editorService() const noexcept { return mEditorService; }

    private:
        openeden::sdk::HostInfo mHostInfo;
        EventBusImpl mEventBus;
        EntityManagerImpl mEntityManager;
        SettingsServiceImpl mSettingsService;
        SaveServiceImpl mSaveService;
        CollisionLayerRegistry mCollisionLayers;
        VfsServiceImpl mVfsService;
        PhysicsServiceImpl mPhysicsService;
        LoggingServiceImpl mLoggingService;
        ProfilerServiceImpl mProfilerService;
        ScratchMemoryServiceImpl mScratchMemoryService;
        DebugDrawServiceImpl mDebugDrawService;
        ScriptingServiceImpl mScriptingService;
        EditorServiceImpl mEditorService;
    };
} // namespace openeden::plugin
