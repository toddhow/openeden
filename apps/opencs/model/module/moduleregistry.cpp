#include "moduleregistry.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <utility>

#include <components/debug/debuglog.hpp>

namespace CSMModule
{
    using namespace openeden::editor;

    class ModuleRegistry::NullContext final : public EditorContext
    {
    public:
        void pushCommand(std::unique_ptr<Command>) override {}
        void markDirty(std::string_view) override {}
        void log(LogLevel, std::string_view) override {}
        void requestRedraw() override {}
        AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
        AssetDocument* activeAsset() override { return nullptr; }
    };

    namespace
    {
        template <class Entries>
        bool hasId(const Entries& entries, const std::string& id)
        {
            return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) { return entry.mDesc.id == id; });
        }

        template <class Entries>
        void eraseModule(Entries& entries, const std::string& moduleId)
        {
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                              [&](const auto& entry) { return entry.mModule == moduleId; }),
                entries.end());
        }

        bool endsWith(const std::string& text, const std::string& suffix)
        {
            return text.size() >= suffix.size()
                && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::string describe(const std::exception_ptr& error)
        {
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::exception& e)
            {
                return e.what();
            }
            catch (...)
            {
                return "an exception that is not a std::exception";
            }
        }
    }

    ModuleRegistry::ModuleRegistry()
        : mSettings([](const std::string& text) { Log(Debug::Warning) << "Editor module setting: " << text; })
        , mNullContext(std::make_unique<NullContext>())
    {
        mBus.setThrowReport([this](const std::string& owner, const std::string& what) {
            fail(owner, "an event handler threw: " + what);
        });
    }

    ModuleRegistry::~ModuleRegistry() = default;

    void ModuleRegistry::beginModule(const std::string& moduleId)
    {
        mCurrent = moduleId;
        mHasCurrent = true;
    }

    void ModuleRegistry::endModule()
    {
        mCurrent.clear();
        mHasCurrent = false;
    }

    void ModuleRegistry::removeModule(const std::string& moduleId)
    {
        eraseModule(mWorkspaces, moduleId);
        eraseModule(mPanels, moduleId);
        eraseModule(mCommands, moduleId);
        eraseModule(mGizmoProviders, moduleId);
        eraseModule(mAssetTypes, moduleId);
        eraseModule(mSections, moduleId);
        eraseModule(mTrackProviders, moduleId);
        mBus.removeOwner(moduleId);
        mSettings.removeModule(moduleId);
    }

    void ModuleRegistry::fail(const std::string& moduleId, const std::string& message)
    {
        Log(Debug::Error) << "Editor module '" << moduleId << "' failed and is disabled: " << message;
        removeModule(moduleId);
        if (mOnFailure)
            mOnFailure(moduleId, message);
    }

    Registration ModuleRegistry::refuse(const std::string& why) const
    {
        Registration result;
        result.ok = false;
        result.error = why;
        Log(Debug::Warning) << "Editor module '" << mCurrent << "': registration refused: " << why;
        return result;
    }

    EditorContext& ModuleRegistry::context()
    {
        return mContext != nullptr ? *mContext : *mNullContext;
    }

    Registration ModuleRegistry::registerSetting(const SettingDesc& desc)
    {
        if (!mHasCurrent)
            return refuse("no module is being registered");
        const Registration result = mSettings.add(mCurrent, desc);
        if (!result.ok)
            return refuse(result.error);
        return result;
    }

    Registration ModuleRegistry::subscribe(EventHandler handler)
    {
        if (!mHasCurrent)
            return refuse("no module is being registered");
        if (!handler)
            return refuse("a subscription needs a handler");
        mBus.subscribe(mCurrent, std::move(handler));
        return {};
    }

    Registration ModuleRegistry::registerWorkspace(const WorkspaceDesc& desc)
    {
        if (!mHasCurrent)
            return refuse("no module is being registered");
        if (desc.id.empty())
            return refuse("a workspace needs an id");
        if (hasId(mWorkspaces, desc.id))
            return refuse("the workspace id '" + desc.id + "' is already registered");
        mWorkspaces.push_back({ mCurrent, desc });
        return {};
    }

    Registration ModuleRegistry::registerPanel(const PanelDesc& desc)
    {
        if (!mHasCurrent)
            return refuse("no module is being registered");
        if (desc.id.empty())
            return refuse("a panel needs an id");
        if (hasId(mPanels, desc.id))
            return refuse("the panel id '" + desc.id + "' is already registered");
        if (!hasId(mWorkspaces, desc.workspaceId))
            return refuse(
                "the panel '" + desc.id + "' names the workspace '" + desc.workspaceId + "', which nobody registered");
        if (!desc.source)
            return refuse("the panel '" + desc.id + "' has no source callback");
        mPanels.push_back({ mCurrent, desc });
        return {};
    }

    Registration ModuleRegistry::registerCommand(const CommandDesc& desc)
    {
        if (!mHasCurrent)
            return refuse("no module is being registered");
        if (desc.id.empty())
            return refuse("a command needs an id");
        if (hasId(mCommands, desc.id))
            return refuse("the command id '" + desc.id + "' is already registered");
        if (!desc.handler)
            return refuse("the command '" + desc.id + "' has no handler");
        mCommands.push_back({ mCurrent, desc });
        return {};
    }

    Registration ModuleRegistry::registerGizmoProvider(const GizmoProviderDesc& desc)
    {
        if (!mHasCurrent)
            return refuse("no module is being registered");
        if (desc.id.empty())
            return refuse("a gizmo provider needs an id");
        if (hasId(mGizmoProviders, desc.id))
            return refuse("the gizmo provider id '" + desc.id + "' is already registered");
        if (!desc.gizmos)
            return refuse("the gizmo provider '" + desc.id + "' has no callback");
        mGizmoProviders.push_back({ mCurrent, desc });
        return {};
    }

    Registration ModuleRegistry::registerAssetType(const AssetTypeDesc& desc)
    {
        if (!mHasCurrent)
            return refuse("no module is being registered");
        if (desc.formatId.empty())
            return refuse("an asset type needs a format id");
        if (desc.fileExtension.empty())
            return refuse("the asset type '" + desc.formatId + "' needs a file extension");
        for (const AssetTypeEntry& entry : mAssetTypes)
        {
            if (entry.mDesc.formatId == desc.formatId)
                return refuse("the asset type '" + desc.formatId + "' is already registered");
            if (entry.mDesc.fileExtension == desc.fileExtension)
                return refuse("the file extension '" + desc.fileExtension + "' belongs to the asset type '"
                    + entry.mDesc.formatId + "' already");
        }
        if (!desc.open)
            return refuse("the asset type '" + desc.formatId + "' has no open callback");
        mAssetTypes.push_back({ mCurrent, desc });
        return {};
    }

    Registration ModuleRegistry::registerInspectorSection(const InspectorSectionDesc& desc)
    {
        if (!mHasCurrent)
            return refuse("no module is being registered");
        if (desc.id.empty())
            return refuse("an inspector section needs an id");
        if (hasId(mSections, desc.id))
            return refuse("the inspector section id '" + desc.id + "' is already registered");
        if (!desc.create)
            return refuse("the inspector section '" + desc.id + "' has no create callback");
        mSections.push_back({ mCurrent, desc });
        return {};
    }

    Registration ModuleRegistry::registerTimelineTrackProvider(const TimelineTrackProviderDesc& desc)
    {
        if (!mHasCurrent)
            return refuse("no module is being registered");
        if (desc.id.empty())
            return refuse("a timeline track provider needs an id");
        if (hasId(mTrackProviders, desc.id))
            return refuse("the timeline track provider id '" + desc.id + "' is already registered");
        if (!desc.tracks)
            return refuse("the timeline track provider '" + desc.id + "' has no callback");
        mTrackProviders.push_back({ mCurrent, desc });
        return {};
    }

    const ModuleRegistry::AssetTypeEntry* ModuleRegistry::findAssetType(const std::filesystem::path& path) const
    {
        const std::string name = path.filename().string();
        const AssetTypeEntry* best = nullptr;
        for (const AssetTypeEntry& entry : mAssetTypes)
            if (endsWith(name, entry.mDesc.fileExtension)
                && (best == nullptr || entry.mDesc.fileExtension.size() > best->mDesc.fileExtension.size()))
                best = &entry;
        return best;
    }

    std::string ModuleRegistry::assetOwner(const std::filesystem::path& path) const
    {
        const AssetTypeEntry* type = findAssetType(path);
        return type != nullptr ? type->mModule : std::string();
    }

    std::string ModuleRegistry::panelOwner(std::string_view panelId) const
    {
        const auto found = std::find_if(
            mPanels.begin(), mPanels.end(), [&](const PanelEntry& entry) { return entry.mDesc.id == panelId; });
        return found != mPanels.end() ? found->mModule : std::string();
    }

    bool ModuleRegistry::appliesTo(const std::string& registeredBy, const std::string& activeOwner)
    {
        return activeOwner.empty() || registeredBy == activeOwner;
    }

    std::vector<std::filesystem::path> ModuleRegistry::findAssets(
        const std::vector<std::filesystem::path>& roots, const std::string& ownerFilter) const
    {
        std::vector<std::filesystem::path> found;
        for (const std::filesystem::path& root : roots)
        {
            std::error_code error;
            if (!std::filesystem::is_directory(root, error))
                continue;
            std::filesystem::recursive_directory_iterator it(
                root, std::filesystem::directory_options::skip_permission_denied, error);
            for (const std::filesystem::recursive_directory_iterator end; !error && it != end; it.increment(error))
            {
                if (!it->is_regular_file(error))
                    continue;
                const AssetTypeEntry* type = findAssetType(it->path());
                if (type != nullptr && (ownerFilter.empty() || type->mModule == ownerFilter))
                    found.push_back(it->path());
            }
        }
        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        return found;
    }

    PropertySource* ModuleRegistry::panelSource(std::string_view panelId, const std::string& activeOwner)
    {
        if (!appliesTo(panelOwner(panelId), activeOwner))
            return nullptr;
        return panelSource(panelId);
    }

    bool ModuleRegistry::runCommand(std::string_view commandId)
    {
        const auto found = std::find_if(
            mCommands.begin(), mCommands.end(), [&](const CommandEntry& entry) { return entry.mDesc.id == commandId; });
        if (found == mCommands.end())
            return false;
        // Copies: a failing callback withdraws the entry while its own code is still running.
        const std::string module = found->mModule;
        const CommandDesc desc = found->mDesc;
        try
        {
            if (desc.enabledWhen && !desc.enabledWhen())
                return false;
            desc.handler(context());
            return true;
        }
        catch (...)
        {
            fail(module, describe(std::current_exception()));
            return false;
        }
    }

    bool ModuleRegistry::isCommandEnabled(std::string_view commandId)
    {
        const auto found = std::find_if(
            mCommands.begin(), mCommands.end(), [&](const CommandEntry& entry) { return entry.mDesc.id == commandId; });
        if (found == mCommands.end())
            return false;
        const std::string module = found->mModule;
        const auto enabledWhen = found->mDesc.enabledWhen;
        try
        {
            return !enabledWhen || enabledWhen();
        }
        catch (...)
        {
            fail(module, describe(std::current_exception()));
            return false;
        }
    }

    PropertySource* ModuleRegistry::panelSource(std::string_view panelId)
    {
        const auto found = std::find_if(
            mPanels.begin(), mPanels.end(), [&](const PanelEntry& entry) { return entry.mDesc.id == panelId; });
        if (found == mPanels.end())
            return nullptr;
        const std::string module = found->mModule;
        const auto source = found->mDesc.source;
        try
        {
            return source();
        }
        catch (...)
        {
            fail(module, describe(std::current_exception()));
            return nullptr;
        }
    }

    std::vector<Gizmo*> ModuleRegistry::gizmosOf(std::string_view providerId)
    {
        const auto found = std::find_if(mGizmoProviders.begin(), mGizmoProviders.end(),
            [&](const GizmoProviderEntry& entry) { return entry.mDesc.id == providerId; });
        if (found == mGizmoProviders.end())
            return {};
        const std::string module = found->mModule;
        const auto gizmos = found->mDesc.gizmos;
        try
        {
            return gizmos();
        }
        catch (...)
        {
            fail(module, describe(std::current_exception()));
            return {};
        }
    }

    std::vector<TimelineTrack*> ModuleRegistry::tracksOf(std::string_view providerId)
    {
        const auto found = std::find_if(mTrackProviders.begin(), mTrackProviders.end(),
            [&](const TrackProviderEntry& entry) { return entry.mDesc.id == providerId; });
        if (found == mTrackProviders.end())
            return {};
        const std::string module = found->mModule;
        const auto tracks = found->mDesc.tracks;
        try
        {
            return tracks();
        }
        catch (...)
        {
            fail(module, describe(std::current_exception()));
            return {};
        }
    }

    PropertySource* ModuleRegistry::sectionSource(std::string_view sectionId, AssetDocument& document)
    {
        const auto found = std::find_if(
            mSections.begin(), mSections.end(), [&](const SectionEntry& entry) { return entry.mDesc.id == sectionId; });
        if (found == mSections.end())
            return nullptr;
        const std::string module = found->mModule;
        const auto create = found->mDesc.create;
        try
        {
            return create(document);
        }
        catch (...)
        {
            fail(module, describe(std::current_exception()));
            return nullptr;
        }
    }

    std::unique_ptr<AssetDocument> ModuleRegistry::openAsset(const std::filesystem::path& path, std::string& error)
    {
        const AssetTypeEntry* type = findAssetType(path);
        if (type == nullptr)
        {
            error = "no editor module handles the file '" + path.filename().string() + "'";
            return nullptr;
        }
        const std::string module = type->mModule;
        const auto open = type->mDesc.open;
        try
        {
            error.clear();
            std::unique_ptr<AssetDocument> document = open(path, error);
            if (document == nullptr && error.empty())
                error = "the editor module '" + module + "' could not open the file";
            return document;
        }
        catch (...)
        {
            error = describe(std::current_exception());
            fail(module, error);
            return nullptr;
        }
    }
}
