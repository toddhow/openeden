#pragma once

#include <components/extension/bindings_store.hpp>
#include <components/extension/debug_draw_service.hpp>
#include <components/extension/entity_manager.hpp>
#include <components/extension/event_bus.hpp>
#include <components/extension/game_services.hpp>
#include <components/extension/logging_service.hpp>
#include <components/extension/physics_service.hpp>
#include <components/extension/profiler_service.hpp>
#include <components/extension/save_chunk_store.hpp>
#include <components/extension/scratch_memory.hpp>
#include <components/extension/settings_service.hpp>
#include <components/extension/vfs_service.hpp>
#include <openeden/host.hpp>

#include <optional>

namespace openeden::extension
{
    /**
     * @brief Concrete host implementation managing core engine extension services.
     */
    class HostImpl : public openeden::sdk::Host
    {
    public:
        explicit HostImpl(std::string hostName = "OpenEden");
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
        [[nodiscard]] openeden::sdk::VfsService* getVfsService() override;
        [[nodiscard]] openeden::sdk::PhysicsQueryService* getPhysicsQueryService() override;
        [[nodiscard]] openeden::sdk::LoggingService* getLoggingService() override;
        [[nodiscard]] openeden::sdk::ProfilerService* getProfilerService() override;
        [[nodiscard]] openeden::sdk::ScratchMemoryService* getScratchMemoryService() override;
        [[nodiscard]] openeden::sdk::DebugDrawService* getDebugDrawService() override;

        /**
         * @brief Gives extensions read-only queries on a Jolt world the host owns.
         *
         * Attach and publish query masks before any extension starts, and detach only after every extension has
         * stopped: extensions keep the service pointer, and detaching destroys the service. The world must outlive
         * the attachment. Attaching again replaces the world and forgets published masks. With no world attached
         * getPhysicsQueryService() returns null. The host never creates a world itself.
         */
        void attachPhysicsWorld(JPH::PhysicsSystem& world, float worldUnitsPerMeter = 1.0f);
        void detachPhysicsWorld() noexcept;

        /// Stops the services calling an extension that has been disabled. Its format readers and log sinks carry no
        /// extension ID, so they stay until releaseExtensionObjects.
        void disableExtension(std::string_view extensionId);

        /// Drops every object extensions left in the services. LifecycleManager calls it before unloading their
        /// libraries.
        void releaseExtensionObjects();

        // Concrete service accessors for engine internals
        [[nodiscard]] EventBusImpl& eventBus() noexcept { return mEventBus; }
        [[nodiscard]] const EventBusImpl& eventBus() const noexcept { return mEventBus; }

        [[nodiscard]] EntityManagerImpl& entityManager() noexcept { return mEntityManager; }
        [[nodiscard]] const EntityManagerImpl& entityManager() const noexcept { return mEntityManager; }

        [[nodiscard]] SettingsServiceImpl& settingsService() noexcept { return mSettingsService; }
        [[nodiscard]] const SettingsServiceImpl& settingsService() const noexcept { return mSettingsService; }

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

        // The game services (SDK 4.0). HostImpl does not hand them out through Host::get...Service(): a game pack gets
        // them from the Host that LifecycleManager passes to its start() (PackScopedHost), scoped to its pack id.
        [[nodiscard]] SaveChunkServiceImpl& saveChunks() noexcept { return mSaveChunks; }
        [[nodiscard]] RecordServiceImpl& records() noexcept { return mRecords; }
        [[nodiscard]] ContentServiceImpl& content() noexcept { return mContent; }
        [[nodiscard]] ScriptHostServiceImpl& scriptHost() noexcept { return mScriptHost; }
        [[nodiscard]] UiServiceImpl& ui() noexcept { return mUi; }
        [[nodiscard]] WorldViewServiceImpl& worldView() noexcept { return mWorldView; }
        [[nodiscard]] WorldSourceServiceImpl& worldSources() noexcept { return mWorldSources; }
        [[nodiscard]] BindingsStore& bindings() noexcept { return mBindings; }
        [[nodiscard]] const BindingsStore& bindings() const noexcept { return mBindings; }

        [[nodiscard]] DebugDrawServiceImpl& debugDrawService() noexcept { return mDebugDrawService; }
        [[nodiscard]] const DebugDrawServiceImpl& debugDrawService() const noexcept { return mDebugDrawService; }

    private:
        openeden::sdk::HostInfo mHostInfo;
        EventBusImpl mEventBus;
        EntityManagerImpl mEntityManager;
        SettingsServiceImpl mSettingsService;
        VfsServiceImpl mVfsService;
        std::optional<PhysicsServiceImpl> mPhysicsService;
        LoggingServiceImpl mLoggingService;
        ProfilerServiceImpl mProfilerService;
        ScratchMemoryServiceImpl mScratchMemoryService;
        DebugDrawServiceImpl mDebugDrawService;
        SaveChunkServiceImpl mSaveChunks;
        RecordServiceImpl mRecords;
        ContentServiceImpl mContent;
        ScriptHostServiceImpl mScriptHost;
        UiServiceImpl mUi;
        BindingsStore mBindings;
        WorldViewServiceImpl mWorldView;
        WorldSourceServiceImpl mWorldSources;
    };
} // namespace openeden::extension
