#include "pack_host.hpp"

#include <components/extension/host_impl.hpp>

namespace openeden::extension
{
    void CapabilityLog::add(std::string_view extensionId, openeden::sdk::PackKind kind, std::string_view what)
    {
        CapabilityDenial denial;
        denial.extensionId = std::string(extensionId);
        denial.what = std::string(what);
        denial.message = "Extension '" + denial.extensionId + "' (kind " + std::string(openeden::sdk::toString(kind))
            + ") asked for the " + denial.what + ", which only a pack of kind \"game\" may use (decisions 0003 and 0005 "
              "stay in force for every other kind).";
        mDenials.push_back(std::move(denial));
    }

    /// The save chunk service as one game pack sees it: every handler it registers is keyed with the pack's id.
    class PackSaveChunks final : public openeden::sdk::SaveChunkService
    {
    public:
        PackSaveChunks(SaveChunkServiceImpl& store, std::string packId)
            : mStore(store)
            , mPackId(std::move(packId))
        {
        }

        [[nodiscard]] bool registerHandler(openeden::sdk::SaveChunkHandler& handler) override
        {
            return mStore.registerHandler(mPackId, handler);
        }

    private:
        SaveChunkServiceImpl& mStore;
        std::string mPackId;
    };

    /// The script bindings service as one game pack sees it: namespaces are checked against the pack's id, which is the
    /// session's game id.
    class PackBindings final : public openeden::sdk::BindingsService
    {
    public:
        PackBindings(BindingsStore& store, std::string packId)
            : mStore(store)
            , mPackId(std::move(packId))
        {
        }

        [[nodiscard]] openeden::sdk::DeclareResult declareNamespace(
            const openeden::sdk::NamespaceDescriptor& descriptor) override
        {
            return mStore.declare(mPackId, mPackId, descriptor);
        }

    private:
        BindingsStore& mStore;
        std::string mPackId;
    };

    PackScopedHost::PackScopedHost(
        openeden::sdk::Host& base, std::string packId, openeden::sdk::PackKind kind, CapabilityLog& log)
        : mBase(base)
        , mPackId(std::move(packId))
        , mKind(kind)
        , mLog(log)
        , mImpl(dynamic_cast<HostImpl*>(&base))
    {
        if (mImpl != nullptr && openeden::sdk::hasGameCapabilities(kind))
        {
            mSaveChunks = std::make_unique<PackSaveChunks>(mImpl->saveChunks(), mPackId);
            mBindings = std::make_unique<PackBindings>(mImpl->bindings(), mPackId);
        }
    }

    PackScopedHost::~PackScopedHost() = default;

    bool PackScopedHost::allowed(std::string_view what)
    {
        if (openeden::sdk::hasGameCapabilities(mKind))
            return true;
        mLog.add(mPackId, mKind, what);
        return false;
    }

    const openeden::sdk::HostInfo& PackScopedHost::getHostInfo() const
    {
        return mBase.getHostInfo();
    }

    openeden::sdk::EventBus* PackScopedHost::getEventBus()
    {
        return mBase.getEventBus();
    }

    openeden::sdk::EntityManager* PackScopedHost::getEntityManager()
    {
        return mBase.getEntityManager();
    }

    openeden::sdk::SettingsService* PackScopedHost::getSettingsService()
    {
        return mBase.getSettingsService();
    }

    openeden::sdk::VfsService* PackScopedHost::getVfsService()
    {
        return mBase.getVfsService();
    }

    openeden::sdk::PhysicsQueryService* PackScopedHost::getPhysicsQueryService()
    {
        return mBase.getPhysicsQueryService();
    }

    openeden::sdk::LoggingService* PackScopedHost::getLoggingService()
    {
        return mBase.getLoggingService();
    }

    openeden::sdk::ProfilerService* PackScopedHost::getProfilerService()
    {
        return mBase.getProfilerService();
    }

    openeden::sdk::ScratchMemoryService* PackScopedHost::getScratchMemoryService()
    {
        return mBase.getScratchMemoryService();
    }

    openeden::sdk::DebugDrawService* PackScopedHost::getDebugDrawService()
    {
        return mBase.getDebugDrawService();
    }

    openeden::sdk::SaveChunkService* PackScopedHost::getSaveChunkService()
    {
        if (!allowed("save chunk service"))
            return nullptr;
        return mSaveChunks ? mSaveChunks.get() : mBase.getSaveChunkService();
    }

    openeden::sdk::RecordService* PackScopedHost::getRecordService()
    {
        if (!allowed("record service"))
            return nullptr;
        return mImpl != nullptr ? static_cast<openeden::sdk::RecordService*>(&mImpl->records())
                                : mBase.getRecordService();
    }

    openeden::sdk::ContentService* PackScopedHost::getContentService()
    {
        if (!allowed("content service"))
            return nullptr;
        return mImpl != nullptr ? static_cast<openeden::sdk::ContentService*>(&mImpl->content())
                                : mBase.getContentService();
    }

    openeden::sdk::ScriptHostService* PackScopedHost::getScriptHostService()
    {
        if (!allowed("script host service"))
            return nullptr;
        return mImpl != nullptr ? static_cast<openeden::sdk::ScriptHostService*>(&mImpl->scriptHost())
                                : mBase.getScriptHostService();
    }

    openeden::sdk::UiService* PackScopedHost::getUiService()
    {
        if (!allowed("window (ui) service"))
            return nullptr;
        return mImpl != nullptr ? static_cast<openeden::sdk::UiService*>(&mImpl->ui()) : mBase.getUiService();
    }

    openeden::sdk::BindingsService* PackScopedHost::getBindingsService()
    {
        if (!allowed("script bindings service"))
            return nullptr;
        return mBindings ? mBindings.get() : mBase.getBindingsService();
    }
    openeden::sdk::game::WorldViewService* PackScopedHost::getWorldViewService()
    {
        if (!allowed("world view service"))
            return nullptr;
        return mImpl != nullptr ? static_cast<openeden::sdk::game::WorldViewService*>(&mImpl->worldView())
                                : mBase.getWorldViewService();
    }
} // namespace openeden::extension
