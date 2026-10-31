#include "extension_loader.hpp"

#include <components/extension/manifest_io.hpp>

#include <algorithm>
#include <map>
#include <system_error>

namespace openeden::extension
{
    std::string getHostToolchain()
    {
#if defined(_MSC_VER)
        std::string s = "msvc-" + std::to_string(_MSC_VER / 100) + "." + std::to_string(_MSC_VER % 100) + "-x64-";
#ifdef NDEBUG
        s += "release";
#else
        s += "debug";
#endif
        return s;
#elif defined(__clang__)
        return "clang-x64-release";
#elif defined(__GNUC__)
        return "gcc-x64-release";
#else
        return "generic-x64-release";
#endif
    }

    std::vector<DiscoveredExtension> discoverExtensionDirectories(
        const std::vector<std::filesystem::path>& searchDirs, std::vector<ExtensionLoadError>* errors)
    {
        std::vector<DiscoveredExtension> discovered;

        for (const auto& searchDir : searchDirs)
        {
            std::error_code ec;
            if (!std::filesystem::exists(searchDir, ec) || !std::filesystem::is_directory(searchDir, ec))
                continue;

            // directory_iterator's order is unspecified (alphabetical on Windows, not on Linux); extension discovery
            // order must not depend on the file system, so each search directory is read in name order.
            std::vector<std::filesystem::path> directories;
            for (const auto& entry : std::filesystem::directory_iterator(searchDir, ec))
            {
                if (entry.is_directory(ec))
                    directories.push_back(entry.path());
            }
            std::sort(directories.begin(), directories.end());

            for (const auto& directory : directories)
            {
                const std::filesystem::path manifestPath = directory / "extension.json";
                if (!std::filesystem::exists(manifestPath, ec) || !std::filesystem::is_regular_file(manifestPath, ec))
                    continue;

                LoadError loadErr;
                auto manifest = loadManifestFile(manifestPath, loadErr);
                if (manifest.has_value())
                {
                    DiscoveredExtension item;
                    item.manifest = std::move(*manifest);
                    item.directory = directory;
                    item.manifestPath = manifestPath;
                    discovered.push_back(std::move(item));
                }
                else if (errors != nullptr)
                {
                    errors->push_back({ directory.filename().string(), "manifest", loadErr.toString() });
                }
            }
        }

        return discovered;
    }

    std::vector<InstalledExtension> listInstalledExtensions(const std::vector<std::filesystem::path>& searchDirs)
    {
        std::map<std::string, InstalledExtension> byId;

        for (const auto& searchDir : searchDirs)
        {
            std::error_code ec;
            std::vector<std::filesystem::path> directories;
            for (const auto& entry : std::filesystem::directory_iterator(searchDir, ec))
            {
                if (entry.is_directory(ec))
                    directories.push_back(entry.path());
            }
            std::sort(directories.begin(), directories.end());

            for (const auto& directory : directories)
            {
                const std::filesystem::path manifestPath = directory / "extension.json";
                if (!std::filesystem::is_regular_file(manifestPath, ec))
                    continue;

                InstalledExtension item;
                item.directory = directory;
                LoadError loadError;
                if (auto manifest = loadManifestFile(manifestPath, loadError))
                {
                    item.manifest = std::move(*manifest);
                    const openeden::sdk::Version sdk = item.manifest.sdkVersion;
                    if (!openeden::sdk::isCompatible(sdk, openeden::sdk::kSdkVersion))
                        item.error = "Built for SDK " + std::to_string(sdk.major) + "." + std::to_string(sdk.minor)
                            + "; this build provides " + std::to_string(openeden::sdk::kSdkVersion.major) + "."
                            + std::to_string(openeden::sdk::kSdkVersion.minor);
                }
                else
                {
                    item.manifest.id = directory.filename().string();
                    item.error = loadError.toString();
                }
                byId.try_emplace(item.manifest.id, std::move(item)); // An earlier directory wins
            }
        }

        std::vector<InstalledExtension> extensions;
        extensions.reserve(byId.size());
        for (auto& [id, item] : byId)
            extensions.push_back(std::move(item));
        return extensions;
    }

    std::vector<DiscoveredExtension> filterEnabledExtensions(
        const std::vector<DiscoveredExtension>& discovered, const std::vector<std::string>& enabledList)
    {
        if (enabledList.empty())
            return discovered;

        std::vector<DiscoveredExtension> filtered;
        for (const auto& item : discovered)
        {
            const auto it = std::find(enabledList.begin(), enabledList.end(), item.manifest.id);
            if (it != enabledList.end())
            {
                filtered.push_back(item);
            }
        }
        return filtered;
    }

    bool validateExtensionHandshake(const openeden::sdk::ExtensionDescriptor* descriptor,
        const ExtensionManifest& manifest, const openeden::sdk::HostInfo& hostInfo, ExtensionLoadError& error)
    {
        error.extensionId = manifest.id;
        error.stage = "handshake";
        error.message.clear();

        if (descriptor == nullptr)
        {
            error.message = "Extension entry point returned null descriptor";
            return false;
        }

        if (descriptor->id != manifest.id)
        {
            error.message = "Descriptor id '" + std::string(descriptor->id) + "' does not match manifest id '"
                + manifest.id + "'";
            return false;
        }

        if (descriptor->sdkVersion.major != hostInfo.sdkVersion.major
            || descriptor->sdkVersion.minor > hostInfo.sdkVersion.minor)
        {
            error.message = "Extension library was built for SDK " + std::to_string(descriptor->sdkVersion.major) + "."
                + std::to_string(descriptor->sdkVersion.minor) + "; this host provides "
                + std::to_string(hostInfo.sdkVersion.major) + "." + std::to_string(hostInfo.sdkVersion.minor);
            return false;
        }

        if (descriptor->sdkVersion != manifest.sdkVersion)
        {
            error.message = "Descriptor SDK version does not match manifest SDK version";
            return false;
        }

        if (descriptor->create == nullptr)
        {
            error.message = "Descriptor create function pointer is null";
            return false;
        }

        return true;
    }

    ExtensionLoadResult loadExtensionLibrary(
        const DiscoveredExtension& discovered, const openeden::sdk::HostInfo& hostInfo)
    {
        ExtensionLoadResult result;

        // 1. SDK version check
        if (discovered.manifest.sdkVersion.major != hostInfo.sdkVersion.major
            || discovered.manifest.sdkVersion.minor > hostInfo.sdkVersion.minor)
        {
            result.success = false;
            result.error.extensionId = discovered.manifest.id;
            result.error.stage = "sdk";
            result.error.message = "Incompatible SDK version in extension manifest: built for SDK "
                + std::to_string(discovered.manifest.sdkVersion.major) + "."
                + std::to_string(discovered.manifest.sdkVersion.minor) + "; this host provides "
                + std::to_string(hostInfo.sdkVersion.major) + "." + std::to_string(hostInfo.sdkVersion.minor);
            return result;
        }

        // 2. Toolchain check (Phase A)
        if (!discovered.manifest.toolchain.empty() && discovered.manifest.toolchain != getHostToolchain())
        {
            result.success = false;
            result.error.extensionId = discovered.manifest.id;
            result.error.stage = "toolchain";
            result.error.message = "Incompatible toolchain in extension manifest: " + discovered.manifest.toolchain;
            return result;
        }

        // 3. Resolve library filename
#if defined(_WIN32)
        std::string libName = discovered.manifest.library.windows;
        if (libName.empty())
            libName = discovered.manifest.id + ".dll";
#else
        std::string libName = discovered.manifest.library.linux;
        if (libName.empty())
            libName = "lib" + discovered.manifest.id + ".so";
#endif

        const std::filesystem::path libPath = discovered.directory / libName;
        std::error_code ec;
        if (!std::filesystem::exists(libPath, ec))
        {
            result.success = false;
            result.error.extensionId = discovered.manifest.id;
            result.error.stage = "load";
            result.error.message = "Extension library file not found: " + libPath.string();
            return result;
        }

        // 4. Load dynamic library
        auto lib = std::make_unique<DynamicLibrary>();
        std::string libError;
        if (!lib->load(libPath, libError))
        {
            result.success = false;
            result.error.extensionId = discovered.manifest.id;
            result.error.stage = "load";
            result.error.message = "Failed to load dynamic library: " + libError;
            return result;
        }

        // 5. Locate entry point and perform handshake
        using EntryPointFn = const openeden::sdk::ExtensionDescriptor* (*)(const openeden::sdk::HostInfo*);
        std::string symError;
        auto entryPoint = lib->getSymbol<EntryPointFn>("openeden_extension_entry", symError);
        if (!entryPoint)
        {
            lib->unload();
            result.success = false;
            result.error.extensionId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = "Missing entry point 'openeden_extension_entry': " + symError;
            return result;
        }

        const openeden::sdk::ExtensionDescriptor* descriptor = nullptr;
        try
        {
            descriptor = entryPoint(&hostInfo);
        }
        catch (const std::exception& e)
        {
            lib->unload();
            result.success = false;
            result.error.extensionId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = std::string("Exception in openeden_extension_entry: ") + e.what();
            return result;
        }
        catch (...)
        {
            lib->unload();
            result.success = false;
            result.error.extensionId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = "Unknown exception in openeden_extension_entry";
            return result;
        }

        if (!validateExtensionHandshake(descriptor, discovered.manifest, hostInfo, result.error))
        {
            lib->unload();
            result.success = false;
            return result;
        }

        std::unique_ptr<openeden::sdk::Extension> instance;
        try
        {
            instance = descriptor->create();
        }
        catch (const std::exception& e)
        {
            lib->unload();
            result.success = false;
            result.error.extensionId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = std::string("Exception in descriptor create(): ") + e.what();
            return result;
        }
        catch (...)
        {
            lib->unload();
            result.success = false;
            result.error.extensionId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = "Unknown exception in descriptor create()";
            return result;
        }

        if (!instance)
        {
            lib->unload();
            result.success = false;
            result.error.extensionId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = "Extension descriptor create() returned nullptr";
            return result;
        }

        result.success = true;
        result.extension.manifest = discovered.manifest;
        result.extension.instance = std::move(instance);
        result.extension.library = std::move(lib);
        return result;
    }
} // namespace openeden::extension
