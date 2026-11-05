#ifndef CSM_MODULE_MODULEHOST_H
#define CSM_MODULE_MODULEHOST_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <openeden/editor_module/module.hpp>

namespace openeden::extension
{
    class DynamicLibrary;
}

namespace CSMModule
{
    class ModuleRegistry;

    /// Where an editor module's library comes from, and who named it.
    struct ModuleSource
    {
        enum class Origin
        {
            Core, ///< The fixed list of core systems' tools (decision 0007).
            Extension ///< An extension's manifest "editor" entry.
        };

        std::string mId;
        std::filesystem::path mLibrary;
        Origin mOrigin = Origin::Core;
    };

    struct ModuleInfo
    {
        enum class State
        {
            Loaded,
            Failed, ///< It did not load, did not register, or one of its callbacks threw later.
            NotInstalled ///< A core module whose library is not there (it is optional in a build).
        };

        std::string mId;
        ModuleSource::Origin mOrigin = ModuleSource::Origin::Core;
        State mState = State::Loaded;
        std::string mMessage; ///< Why, for Failed and NotInstalled.
    };

    /// One entry of the fixed core list: a core system's editor library.
    struct CoreModule
    {
        std::string mId;
        std::string mLibrary; ///< File name with the platform's shared library prefix and suffix.
    };

    /// The fixed list of core systems' editor libraries (decision 0007 left its format open: this is a plain list in
    /// code). Today: CreativeMotion's, built as creativemotion_editor (task 0032), and AdvancedBodies', built as
    /// advancedbodies_editor (task 0069, stage f).
    std::vector<CoreModule> coreModules();

    /// The core list as sources, looking for each library in \a directory.
    std::vector<ModuleSource> coreModuleSources(const std::filesystem::path& directory);

    /// Loads editor modules, registers them in a ModuleRegistry and takes them down again (plan section 4; task 0031).
    ///
    /// A module that fails to load or register is recorded as Failed with the reason, its partial registrations are
    /// withdrawn and the others go on. When a module's callback throws later, the registry reports it and the module
    /// becomes Failed here. Shutdown is in reverse load order: shutdown(), then the registrations are withdrawn, then
    /// the module is destroyed, and only then is its library unloaded (its code must stay mapped until the module is
    /// gone).
    ///
    /// Ownership: owns the modules and their libraries; the registry and context are not owned and must outlive the
    /// host. Main thread only.
    class ModuleHost
    {
    public:
        ModuleHost(ModuleRegistry& registry, openeden::editor::EditorContext& context);
        ~ModuleHost();

        ModuleHost(const ModuleHost&) = delete;
        ModuleHost& operator=(const ModuleHost&) = delete;

        /// In the order given. Never throws for a bad module.
        void load(const std::vector<ModuleSource>& sources);

        /// For a module linked into the program (tests). \a descriptor is read during the call only.
        void loadStatic(
            std::string id, const openeden::editor::EditorModuleDescriptor& descriptor, ModuleSource::Origin origin);

        /// The Editor Module API version this host says it provides (default: the headers' kApiVersion). A module that
        /// wants a newer minor, or another major, is refused with a message naming both versions. Only tests lower it,
        /// to show how an older Eden Studio treats a newer module. Call before load().
        void setProvidedApiVersion(openeden::sdk::Version version);

        std::vector<ModuleInfo> modules() const;

        /// Safe to repeat. The destructor calls it.
        void shutdown();

    private:
        struct Loaded;

        void loadLibrary(const ModuleSource& source);
        void install(const std::string& id, const openeden::editor::EditorModuleDescriptor& descriptor,
            ModuleSource::Origin origin, std::unique_ptr<openeden::extension::DynamicLibrary> library);
        void markFailed(const std::string& id, const std::string& message);

        ModuleRegistry& mRegistry;
        openeden::editor::EditorContext& mContext;
        openeden::sdk::Version mProvidedApi = openeden::editor::kApiVersion;
        std::vector<ModuleInfo> mInfos;
        std::vector<std::unique_ptr<Loaded>> mLoaded;
    };
}

#endif
