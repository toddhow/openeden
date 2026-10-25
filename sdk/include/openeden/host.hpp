#pragma once

#include <openeden/version.hpp>

#include <string>

namespace openeden::sdk
{
    class EventBus;
    class EntityManager;
    class SettingsService;
    class SaveService;
    class VfsService;
    class PhysicsService;
    class LoggingService;
    class ProfilerService;
    class ScratchMemoryService;
    class DebugDrawService;
    class ScriptingService;
    class EditorService;

    /**
     * @brief Host metadata provided to plugins.
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
        [[nodiscard]] virtual SaveService* getSaveService() { return nullptr; }
        [[nodiscard]] virtual VfsService* getVfsService() { return nullptr; }
        [[nodiscard]] virtual PhysicsService* getPhysicsService() { return nullptr; }
        [[nodiscard]] virtual LoggingService* getLoggingService() { return nullptr; }
        [[nodiscard]] virtual ProfilerService* getProfilerService() { return nullptr; }
        [[nodiscard]] virtual ScratchMemoryService* getScratchMemoryService() { return nullptr; }
        [[nodiscard]] virtual DebugDrawService* getDebugDrawService() { return nullptr; }
        [[nodiscard]] virtual ScriptingService* getScriptingService() { return nullptr; }
        [[nodiscard]] virtual EditorService* getEditorService() { return nullptr; }
    };
} // namespace openeden::sdk
