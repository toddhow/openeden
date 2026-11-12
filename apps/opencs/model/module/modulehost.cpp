#include "modulehost.hpp"

#include <algorithm>
#include <exception>
#include <sstream>

#include <components/debug/debuglog.hpp>
#include <components/extension/dynamic_library.hpp>

#include "moduleregistry.hpp"

namespace CSMModule
{
    using openeden::extension::DynamicLibrary;
    using namespace openeden::editor;

    /// A loaded module. The library is declared first so that it is destroyed last: the module's code must be mapped
    /// while the module is destroyed (and it is destroyed by shutdown() before this goes anyway).
    struct ModuleHost::Loaded
    {
        std::unique_ptr<DynamicLibrary> mLibrary;
        std::string mId;
        EditorModule* mModule = nullptr;
        void (*mDestroy)(EditorModule*) = nullptr;
    };

    namespace
    {
        std::string versionText(const openeden::sdk::Version& version)
        {
            return std::to_string(version.major) + "." + std::to_string(version.minor);
        }
    }

    std::vector<CoreModule> coreModules()
    {
#if defined(_WIN32)
        constexpr const char* prefix = "";
        constexpr const char* suffix = ".dll";
#elif defined(__APPLE__)
        constexpr const char* prefix = "lib";
        constexpr const char* suffix = ".dylib";
#else
        constexpr const char* prefix = "lib";
        constexpr const char* suffix = ".so";
#endif
        return { { "creativemotion_editor", std::string(prefix) + "creativemotion_editor" + suffix },
            { "advancedbodies_editor", std::string(prefix) + "advancedbodies_editor" + suffix },
            { "face_editor", std::string(prefix) + "face_editor" + suffix } };
    }

    std::vector<ModuleSource> coreModuleSources(const std::filesystem::path& directory)
    {
        std::vector<ModuleSource> sources;
        for (const CoreModule& core : coreModules())
            sources.push_back({ core.mId, directory / core.mLibrary, ModuleSource::Origin::Core });
        return sources;
    }

    ModuleHost::ModuleHost(ModuleRegistry& registry, EditorContext& context)
        : mRegistry(registry)
        , mContext(context)
    {
        mRegistry.setContext(&mContext);
        mRegistry.setFailureHandler(
            [this](const std::string& id, const std::string& message) { markFailed(id, message); });
    }

    ModuleHost::~ModuleHost()
    {
        shutdown();
        mRegistry.setFailureHandler({});
        mRegistry.setContext(nullptr);
    }

    void ModuleHost::markFailed(const std::string& id, const std::string& message)
    {
        for (ModuleInfo& info : mInfos)
            if (info.mId == id)
            {
                info.mState = ModuleInfo::State::Failed;
                info.mMessage = message;
            }
    }

    void ModuleHost::setProvidedApiVersion(openeden::sdk::Version version)
    {
        mProvidedApi = version;
    }

    std::vector<ModuleInfo> ModuleHost::modules() const
    {
        return mInfos;
    }

    void ModuleHost::load(const std::vector<ModuleSource>& sources)
    {
        for (const ModuleSource& source : sources)
            loadLibrary(source);
    }

    void ModuleHost::loadLibrary(const ModuleSource& source)
    {
        ModuleInfo info;
        info.mId = source.mId;
        info.mOrigin = source.mOrigin;

        std::error_code ignored;
        if (!std::filesystem::exists(source.mLibrary, ignored))
        {
            // A bare name such as "kernel32.dll" is looked up by the system, so only a path with a folder part is
            // judged by the file system.
            if (source.mLibrary.has_parent_path() || source.mLibrary.is_absolute())
            {
                info.mState = source.mOrigin == ModuleSource::Origin::Core ? ModuleInfo::State::NotInstalled
                                                                           : ModuleInfo::State::Failed;
                info.mMessage = "the library '" + source.mLibrary.string() + "' does not exist";
                if (info.mState == ModuleInfo::State::Failed)
                    Log(Debug::Error) << "Editor module '" << source.mId << "': " << info.mMessage;
                else
                    Log(Debug::Info) << "Editor module '" << source.mId << "' is not installed";
                mInfos.push_back(std::move(info));
                return;
            }
        }

        auto library = std::make_unique<DynamicLibrary>();
        std::string error;
        if (!library->load(source.mLibrary, error))
        {
            info.mState = ModuleInfo::State::Failed;
            info.mMessage = "the library did not load: " + error;
            Log(Debug::Error) << "Editor module '" << source.mId << "': " << info.mMessage;
            mInfos.push_back(std::move(info));
            return;
        }
        using EntryPoint = const EditorModuleDescriptor* (*)();
        const auto entry = library->getSymbol<EntryPoint>(kEditorModuleEntryPoint, error);
        if (entry == nullptr)
        {
            info.mState = ModuleInfo::State::Failed;
            info.mMessage = std::string("the library has no entry point '") + kEditorModuleEntryPoint + "'";
            Log(Debug::Error) << "Editor module '" << source.mId << "': " << info.mMessage;
            mInfos.push_back(std::move(info));
            return;
        }
        const EditorModuleDescriptor* descriptor = nullptr;
        try
        {
            descriptor = entry();
        }
        catch (...)
        {
            descriptor = nullptr;
        }
        if (descriptor == nullptr)
        {
            info.mState = ModuleInfo::State::Failed;
            info.mMessage = "the entry point gave no descriptor";
            Log(Debug::Error) << "Editor module '" << source.mId << "': " << info.mMessage;
            mInfos.push_back(std::move(info));
            return;
        }
        install(source.mId, *descriptor, source.mOrigin, std::move(library));
    }

    void ModuleHost::loadStatic(std::string id, const EditorModuleDescriptor& descriptor, ModuleSource::Origin origin)
    {
        install(id, descriptor, origin, nullptr);
    }

    void ModuleHost::install(const std::string& id, const EditorModuleDescriptor& descriptor,
        ModuleSource::Origin origin, std::unique_ptr<DynamicLibrary> library)
    {
        ModuleInfo info;
        info.mId = id;
        info.mOrigin = origin;
        info.mState = ModuleInfo::State::Failed;

        const auto fail = [&](const std::string& why) {
            info.mMessage = why;
            Log(Debug::Error) << "Editor module '" << id << "' is disabled: " << why;
            mInfos.push_back(info);
        };

        if (std::any_of(mInfos.begin(), mInfos.end(), [&](const ModuleInfo& other) { return other.mId == id; }))
        {
            // Not through fail(): its record would share the id, and a failure of this one must not touch the first.
            Log(Debug::Error) << "Editor module '" << id << "' ignored: a module with this id was loaded already";
            return;
        }
        if (!openeden::sdk::isCompatible(descriptor.apiVersion, mProvidedApi))
        {
            fail("it was built for editor API " + versionText(descriptor.apiVersion) + ", this Eden Studio provides "
                + versionText(mProvidedApi));
            return;
        }
        if (descriptor.id == nullptr || id != descriptor.id)
        {
            fail(std::string("the descriptor's id '") + (descriptor.id != nullptr ? descriptor.id : "") + "' is not '"
                + id + "'");
            return;
        }
        if (descriptor.create == nullptr || descriptor.destroy == nullptr)
        {
            fail("the descriptor has no create or destroy function");
            return;
        }

        EditorModule* module = nullptr;
        try
        {
            module = descriptor.create();
        }
        catch (const std::exception& e)
        {
            fail(std::string("create threw: ") + e.what());
            return;
        }
        catch (...)
        {
            fail("create threw");
            return;
        }
        if (module == nullptr)
        {
            fail("create returned no module");
            return;
        }

        auto loaded = std::make_unique<Loaded>();
        loaded->mLibrary = std::move(library);
        loaded->mId = id;
        loaded->mModule = module;
        loaded->mDestroy = descriptor.destroy;

        mRegistry.beginModule(id);
        std::string registerError;
        try
        {
            module->registerUi(mRegistry);
        }
        catch (const std::exception& e)
        {
            registerError = std::string("registerUi threw: ") + e.what();
        }
        catch (...)
        {
            registerError = "registerUi threw";
        }
        mRegistry.endModule();

        if (!registerError.empty())
        {
            // Partial registrations go, then the module, then (with `loaded`) its library.
            mRegistry.removeModule(id);
            try
            {
                loaded->mDestroy(loaded->mModule);
            }
            catch (...)
            {
            }
            loaded->mModule = nullptr;
            fail(registerError);
            return;
        }

        info.mState = ModuleInfo::State::Loaded;
        Log(Debug::Info) << "Editor module '" << id << "' loaded";
        mInfos.push_back(info);
        mLoaded.push_back(std::move(loaded));
    }

    void ModuleHost::shutdown()
    {
        while (!mLoaded.empty())
        {
            std::unique_ptr<Loaded> loaded = std::move(mLoaded.back());
            mLoaded.pop_back();
            try
            {
                loaded->mModule->shutdown();
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Editor module '" << loaded->mId << "': shutdown threw: " << e.what();
            }
            catch (...)
            {
                Log(Debug::Error) << "Editor module '" << loaded->mId << "': shutdown threw";
            }
            mRegistry.removeModule(loaded->mId);
            try
            {
                loaded->mDestroy(loaded->mModule);
            }
            catch (...)
            {
                Log(Debug::Error) << "Editor module '" << loaded->mId << "': destroy threw";
            }
            loaded->mModule = nullptr;
            // `loaded` goes here: the library unloads after the module is gone.
        }
    }
}
