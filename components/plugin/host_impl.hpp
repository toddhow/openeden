#pragma once

#include <components/plugin/debug_draw_service.hpp>
#include <components/plugin/editor_service.hpp>
#include <components/plugin/entity_manager.hpp>
#include <components/plugin/event_bus.hpp>
#include <components/plugin/logging_service.hpp>
#include <components/plugin/physics_service.hpp>
#include <components/plugin/profiler_service.hpp>
#include <components/plugin/save_service.hpp>
#include <components/plugin/scratch_memory.hpp>
#include <components/plugin/settings_service.hpp>
#include <components/plugin/vfs_service.hpp>
#include <openeden/host.hpp>

#include <optional>

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
        [[nodiscard]] openeden::sdk::PhysicsQueryService* getPhysicsQueryService() override;
        [[nodiscard]] openeden::sdk::LoggingService* getLoggingService() override;
        [[nodiscard]] openeden::sdk::ProfilerService* getProfilerService() override;
        [[nodiscard]] openeden::sdk::ScratchMemoryService* getScratchMemoryService() override;
        [[nodiscard]] openeden::sdk::DebugDrawService* getDebugDrawService() override;
        [[nodiscard]] openeden::sdk::EditorService* getEditorService() override;

        /**
         * @brief Gives plugins read-only queries on a Jolt world the host owns.
         *
         * Attach and publish query masks before any plugin starts, and detach only after every plugin has
         * stopped: plugins keep the service pointer, and detaching destroys the service. The world must outlive
         * the attachment. Attaching again replaces the world and forgets published masks. With no world attached
         * getPhysicsQueryService() returns null. The host never creates a world itself.
         */
        void attachPhysicsWorld(JPH::PhysicsSystem& world, float worldUnitsPerMeter = 1.0f);
        void detachPhysicsWorld() noexcept;

        /// Stops the services calling a plugin that has been disabled. Its format readers and log sinks carry no
        /// plugin ID, so they stay until releasePluginObjects.
        void disablePlugin(std::string_view pluginId);

        /// Drops every object plugins left in the services. LifecycleManager calls it before unloading their libraries.
        void releasePluginObjects();

        // Concrete service accessors for engine internals
        [[nodiscard]] EventBusImpl& eventBus() noexcept { return mEventBus; }
        [[nodiscard]] const EventBusImpl& eventBus() const noexcept { return mEventBus; }

        [[nodiscard]] EntityManagerImpl& entityManager() noexcept { return mEntityManager; }
        [[nodiscard]] const EntityManagerImpl& entityManager() const noexcept { return mEntityManager; }

        [[nodiscard]] SettingsServiceImpl& settingsService() noexcept { return mSettingsService; }
        [[nodiscard]] const SettingsServiceImpl& settingsService() const noexcept { return mSettingsService; }

        [[nodiscard]] SaveServiceImpl& saveService() noexcept { return mSaveService; }
        [[nodiscard]] const SaveServiceImpl& saveService() const noexcept { return mSaveService; }

        [[nodiscard]] VfsServiceImpl& vfsService() noexcept { return mVfsService; }
        [[nodiscard]] const VfsServiceImpl& vfsService() const noexcept { return mVfsService; }

        // Null when no world is attached
        [[nodiscard]] PhysicsServiceImpl* physicsService() noexcept
        {
            return mPhysicsService ? &*mPhysicsService : nullptr;
        }

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

        [[nodiscard]] EditorServiceImpl& editorService() noexcept { return mEditorService; }
        [[nodiscard]] const EditorServiceImpl& editorService() const noexcept { return mEditorService; }

    private:
        openeden::sdk::HostInfo mHostInfo;
        EventBusImpl mEventBus;
        EntityManagerImpl mEntityManager;
        SettingsServiceImpl mSettingsService;
        SaveServiceImpl mSaveService;
        VfsServiceImpl mVfsService;
        std::optional<PhysicsServiceImpl> mPhysicsService;
        LoggingServiceImpl mLoggingService;
        ProfilerServiceImpl mProfilerService;
        ScratchMemoryServiceImpl mScratchMemoryService;
        DebugDrawServiceImpl mDebugDrawService;
        EditorServiceImpl mEditorService;
    };
} // namespace openeden::plugin
