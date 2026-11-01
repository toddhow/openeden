#pragma once

#include <openeden/version.hpp>

#include <string>

namespace openeden::sdk
{
    class EventBus;
    class EntityManager;
    class SettingsService;
    class VfsService;
    class PhysicsQueryService;
    class LoggingService;
    class ProfilerService;
    class ScratchMemoryService;
    class DebugDrawService;

    /**
     * @brief Host metadata provided to extensions.
     */
    struct HostInfo
    {
        Version sdkVersion = kSdkVersion;
        std::string hostName;
    };

    /**
     * @brief Context representing the embedding host program (game, studio, test host).
     */
    class Host
    {
    public:
        virtual ~Host() = default;

        /**
         * @brief Returns host metadata.
         */
        [[nodiscard]] virtual const HostInfo& getHostInfo() const = 0;

        [[nodiscard]] virtual EventBus* getEventBus() { return nullptr; }
        [[nodiscard]] virtual EntityManager* getEntityManager() { return nullptr; }
        [[nodiscard]] virtual SettingsService* getSettingsService() { return nullptr; }
        [[nodiscard]] virtual VfsService* getVfsService() { return nullptr; }
        /// Null if the host has no Jolt world, so check it. A pointer got in start() stays valid until stop() returns.
        [[nodiscard]] virtual PhysicsQueryService* getPhysicsQueryService() { return nullptr; }
        [[nodiscard]] virtual LoggingService* getLoggingService() { return nullptr; }
        [[nodiscard]] virtual ProfilerService* getProfilerService() { return nullptr; }
        [[nodiscard]] virtual ScratchMemoryService* getScratchMemoryService() { return nullptr; }
        [[nodiscard]] virtual DebugDrawService* getDebugDrawService() { return nullptr; }
    };
} // namespace openeden::sdk
