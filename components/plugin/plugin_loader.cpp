#include "plugin_loader.hpp"

#include <components/plugin/manifest_io.hpp>

#include <algorithm>
#include <system_error>

namespace openeden::plugin
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

    std::vector<DiscoveredPlugin> discoverPluginDirectories(const std::vector<std::filesystem::path>& searchDirs)
    {
        std::vector<DiscoveredPlugin> discovered;

        for (const auto& searchDir : searchDirs)
        {
            std::error_code ec;
            if (!std::filesystem::exists(searchDir, ec) || !std::filesystem::is_directory(searchDir, ec))
                continue;

            // directory_iterator's order is unspecified (alphabetical on Windows, not on Linux); plugin discovery
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
                const std::filesystem::path manifestPath = directory / "plugin.json";
                if (!std::filesystem::exists(manifestPath, ec) || !std::filesystem::is_regular_file(manifestPath, ec))
                    continue;

                LoadError loadErr;
                auto manifest = loadManifestFile(manifestPath, loadErr);
                if (manifest.has_value())
                {
                    DiscoveredPlugin item;
                    item.manifest = std::move(*manifest);
                    item.directory = directory;
                    item.manifestPath = manifestPath;
                    discovered.push_back(std::move(item));
                }
            }
        }

        return discovered;
    }

    std::vector<DiscoveredPlugin> filterEnabledPlugins(
        const std::vector<DiscoveredPlugin>& discovered, const std::vector<std::string>& enabledList)
    {
        if (enabledList.empty())
            return discovered;

        std::vector<DiscoveredPlugin> filtered;
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

    bool validatePluginHandshake(const openeden::sdk::PluginDescriptor* descriptor, const PluginManifest& manifest,
        const openeden::sdk::HostInfo& hostInfo, PluginLoadError& error)
    {
        error.pluginId = manifest.id;
        error.stage = "handshake";
        error.message.clear();

        if (descriptor == nullptr)
        {
            error.message = "Plugin entry point returned null descriptor";
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
            error.message = "Plugin library was built for SDK " + std::to_string(descriptor->sdkVersion.major) + "."
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

    PluginLoadResult loadPluginLibrary(const DiscoveredPlugin& discovered, const openeden::sdk::HostInfo& hostInfo)
    {
        PluginLoadResult result;

        // 1. SDK version check
        if (discovered.manifest.sdkVersion.major != hostInfo.sdkVersion.major
            || discovered.manifest.sdkVersion.minor > hostInfo.sdkVersion.minor)
        {
            result.success = false;
            result.error.pluginId = discovered.manifest.id;
            result.error.stage = "sdk";
            result.error.message = "Incompatible SDK version in plugin manifest: built for SDK "
                + std::to_string(discovered.manifest.sdkVersion.major) + "."
                + std::to_string(discovered.manifest.sdkVersion.minor) + "; this host provides "
                + std::to_string(hostInfo.sdkVersion.major) + "." + std::to_string(hostInfo.sdkVersion.minor);
            return result;
        }

        // 2. Toolchain check (Phase A)
        if (!discovered.manifest.toolchain.empty() && discovered.manifest.toolchain != getHostToolchain())
        {
            result.success = false;
            result.error.pluginId = discovered.manifest.id;
            result.error.stage = "toolchain";
            result.error.message = "Incompatible toolchain in plugin manifest: " + discovered.manifest.toolchain;
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
            result.error.pluginId = discovered.manifest.id;
            result.error.stage = "load";
            result.error.message = "Plugin library file not found: " + libPath.string();
            return result;
        }

        // 4. Load dynamic library
        auto lib = std::make_unique<DynamicLibrary>();
        std::string libError;
        if (!lib->load(libPath, libError))
        {
            result.success = false;
            result.error.pluginId = discovered.manifest.id;
            result.error.stage = "load";
            result.error.message = "Failed to load dynamic library: " + libError;
            return result;
        }

        // 5. Locate entry point and perform handshake
        using EntryPointFn = const openeden::sdk::PluginDescriptor* (*)(const openeden::sdk::HostInfo*);
        std::string symError;
        auto entryPoint = lib->getSymbol<EntryPointFn>("openeden_plugin_entry", symError);
        if (!entryPoint)
        {
            lib->unload();
            result.success = false;
            result.error.pluginId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = "Missing entry point 'openeden_plugin_entry': " + symError;
            return result;
        }

        const openeden::sdk::PluginDescriptor* descriptor = nullptr;
        try
        {
            descriptor = entryPoint(&hostInfo);
        }
        catch (const std::exception& e)
        {
            lib->unload();
            result.success = false;
            result.error.pluginId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = std::string("Exception in openeden_plugin_entry: ") + e.what();
            return result;
        }
        catch (...)
        {
            lib->unload();
            result.success = false;
            result.error.pluginId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = "Unknown exception in openeden_plugin_entry";
            return result;
        }

        if (!validatePluginHandshake(descriptor, discovered.manifest, hostInfo, result.error))
        {
            lib->unload();
            result.success = false;
            return result;
        }

        std::unique_ptr<openeden::sdk::Plugin> instance;
        try
        {
            instance = descriptor->create();
        }
        catch (const std::exception& e)
        {
            lib->unload();
            result.success = false;
            result.error.pluginId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = std::string("Exception in descriptor create(): ") + e.what();
            return result;
        }
        catch (...)
        {
            lib->unload();
            result.success = false;
            result.error.pluginId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = "Unknown exception in descriptor create()";
            return result;
        }

        if (!instance)
        {
            lib->unload();
            result.success = false;
            result.error.pluginId = discovered.manifest.id;
            result.error.stage = "handshake";
            result.error.message = "Plugin descriptor create() returned nullptr";
            return result;
        }

        result.success = true;
        result.plugin.manifest = discovered.manifest;
        result.plugin.instance = std::move(instance);
        result.plugin.library = std::move(lib);
        return result;
    }
} // namespace openeden::plugin
