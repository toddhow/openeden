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
    // Game packs only (SDK 4.0, docs/design/game-packs.md section 4): see openeden/game/*.hpp
    class SaveChunkService;
    class RecordService;
    class ContentService;
    class ScriptHostService;
    class UiService;

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

        // SDK 4.0: services handed out by kind. The host passes an extension to start() a Host that returns these only
        // when the extension's manifest kind is "game"; every other kind gets nullptr, so check it. A pointer got in
        // start() stays valid until stop() returns. Main thread only.
        [[nodiscard]] virtual SaveChunkService* getSaveChunkService() { return nullptr; }
        [[nodiscard]] virtual RecordService* getRecordService() { return nullptr; }
        [[nodiscard]] virtual ContentService* getContentService() { return nullptr; }
        [[nodiscard]] virtual ScriptHostService* getScriptHostService() { return nullptr; }
        [[nodiscard]] virtual UiService* getUiService() { return nullptr; }
    };
} // namespace openeden::sdk
