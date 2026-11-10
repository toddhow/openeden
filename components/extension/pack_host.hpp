#pragma once

#include <openeden/host.hpp>
#include <openeden/pack_kind.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::extension
{
    class HostImpl;
    class PackSaveChunks;
    class PackBindings;

    /**
     * @brief A request an extension made for something its kind may not have (SDK 4.0, decision 0013).
     */
    struct CapabilityDenial
    {
        std::string extensionId;
        std::string what; ///< "save chunk service", "game phase GameSimulation", ...
        std::string message; ///< Located: names the extension, its kind and what it asked for
    };

    /**
     * @brief Where denials are recorded. Written on the main thread only; a denial is the rare path, so it may
     * allocate.
     */
    class CapabilityLog
    {
    public:
        void add(std::string_view extensionId, openeden::sdk::PackKind kind, std::string_view what);
        [[nodiscard]] const std::vector<CapabilityDenial>& denials() const noexcept { return mDenials; }

    private:
        std::vector<CapabilityDenial> mDenials;
    };

    /**
     * @brief The Host one extension sees. Everything is forwarded to the base host, except that the game services
     * (save chunks, record types, content loaders, script host, windows) are handed out only when the extension's
     * kind is "game"; any other kind gets nullptr and the request is recorded in the CapabilityLog.
     *
     * A game pack's save chunk service is scoped to its pack id, so the chunks it writes are keyed with it. The game
     * services come from the base host when that is a HostImpl; any other base host that overrides those getters is
     * forwarded to unscoped. Owned by LifecycleManager, one per extension, alive until the extension is destroyed.
     */
    class PackScopedHost final : public openeden::sdk::Host
    {
    public:
        PackScopedHost(openeden::sdk::Host& base, std::string packId, openeden::sdk::PackKind kind, CapabilityLog& log);
        ~PackScopedHost() override;

        PackScopedHost(const PackScopedHost&) = delete;
        PackScopedHost& operator=(const PackScopedHost&) = delete;

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

        [[nodiscard]] openeden::sdk::SaveChunkService* getSaveChunkService() override;
        [[nodiscard]] openeden::sdk::RecordService* getRecordService() override;
        [[nodiscard]] openeden::sdk::ContentService* getContentService() override;
        [[nodiscard]] openeden::sdk::ScriptHostService* getScriptHostService() override;
        [[nodiscard]] openeden::sdk::UiService* getUiService() override;
        [[nodiscard]] openeden::sdk::BindingsService* getBindingsService() override;
        [[nodiscard]] openeden::sdk::game::WorldViewService* getWorldViewService() override;
        [[nodiscard]] openeden::sdk::game::WorldSourceService* getWorldSourceService() override;

    private:
        bool allowed(std::string_view what);

        openeden::sdk::Host& mBase;
        std::string mPackId;
        openeden::sdk::PackKind mKind;
        CapabilityLog& mLog;
        HostImpl* mImpl = nullptr; // the base host if it is a HostImpl: where the game services live
        std::unique_ptr<PackSaveChunks> mSaveChunks; // null unless a game pack with a HostImpl base
        std::unique_ptr<PackBindings> mBindings; // same
    };
} // namespace openeden::extension
