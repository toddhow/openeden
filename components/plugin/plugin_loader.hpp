#pragma once

#include <components/plugin/dynamic_library.hpp>
#include <components/plugin/manifest.hpp>
#include <openeden/host.hpp>
#include <openeden/plugin.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace openeden::plugin
{
    /**
     * @brief Information about a plugin discovered on the filesystem.
     */
    struct DiscoveredPlugin
    {
        PluginManifest manifest;
        std::filesystem::path directory;
        std::filesystem::path manifestPath;
    };

    /**
     * @brief Detailed error information when a plugin fails loading or handshake.
     */
    struct PluginLoadError
    {
        std::string pluginId;
        std::string stage; // "sdk", "toolchain", "load", "handshake"
        std::string message;
    };

    /**
     * @brief Successfully loaded dynamic plugin and its owning library.
     */
    struct LoadedPlugin
    {
        PluginManifest manifest;
        std::unique_ptr<openeden::sdk::Plugin> instance;
        std::unique_ptr<DynamicLibrary> library;
    };

    /**
     * @brief Outcome of attempting to dynamically load a plugin.
     */
    struct PluginLoadResult
    {
        bool success = false;
        LoadedPlugin plugin;
        PluginLoadError error;
    };

    /**
     * @brief Returns the host compiler/platform toolchain identifier string.
     */
    std::string getHostToolchain();

    /**
     * @brief Scans search directories for plugin folders containing valid plugin.json manifests.
     */
    std::vector<DiscoveredPlugin> discoverPluginDirectories(const std::vector<std::filesystem::path>& searchDirs);

    /**
     * @brief Filters discovered plugins by an enabled list, preserving discovery order.
     * If enabledList is empty, all discovered plugins are returned.
     */
    std::vector<DiscoveredPlugin> filterEnabledPlugins(
        const std::vector<DiscoveredPlugin>& discovered, const std::vector<std::string>& enabledList);

    /**
     * @brief Validates the descriptor returned by openeden_plugin_entry against the manifest.
     */
    bool validatePluginHandshake(const openeden::sdk::PluginDescriptor* descriptor, const PluginManifest& manifest,
        const openeden::sdk::HostInfo& hostInfo, PluginLoadError& error);

    /**
     * @brief Dynamically loads a discovered plugin library and performs the handshake.
     */
    PluginLoadResult loadPluginLibrary(const DiscoveredPlugin& discovered, const openeden::sdk::HostInfo& hostInfo);
} // namespace openeden::plugin
