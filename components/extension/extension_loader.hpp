#pragma once

#include <components/extension/dynamic_library.hpp>
#include <components/extension/manifest.hpp>
#include <openeden/extension.hpp>
#include <openeden/host.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace openeden::extension
{
    /**
     * @brief Information about an extension discovered on the filesystem.
     */
    struct DiscoveredExtension
    {
        ExtensionManifest manifest;
        std::filesystem::path directory;
        std::filesystem::path manifestPath;
    };

    /**
     * @brief Detailed error information when an extension fails loading or handshake.
     */
    struct ExtensionLoadError
    {
        std::string extensionId;
        std::string stage; // "sdk", "toolchain", "load", "handshake"
        std::string message;
    };

    /**
     * @brief Successfully loaded dynamic extension and its owning library.
     */
    struct LoadedExtension
    {
        ExtensionManifest manifest;
        std::unique_ptr<openeden::sdk::Extension> instance;
        std::unique_ptr<DynamicLibrary> library;
    };

    /**
     * @brief Outcome of attempting to dynamically load an extension.
     */
    struct ExtensionLoadResult
    {
        bool success = false;
        LoadedExtension extension;
        ExtensionLoadError error;
    };

    /**
     * @brief Returns the host compiler/platform toolchain identifier string.
     */
    std::string getHostToolchain();

    /**
     * @brief Scans search directories for extension folders containing valid extension.json manifests.
     * A folder whose manifest cannot be read is skipped; errors, if given, receives its reason (stage "manifest").
     */
    std::vector<DiscoveredExtension> discoverExtensionDirectories(
        const std::vector<std::filesystem::path>& searchDirs, std::vector<ExtensionLoadError>* errors = nullptr);

    /**
     * @brief An extension folder as a launcher shows it, whether or not its manifest can be read.
     */
    struct InstalledExtension
    {
        ExtensionManifest manifest; ///< With a broken manifest, only id is set, to the folder's name
        std::filesystem::path directory;
        std::string error; ///< Why it cannot be enabled: its manifest, or an SDK this build does not provide
    };

    /**
     * @brief Lists every extension folder in the search directories, sorted by ID. Runs no extension code and loads no
     * library. If several directories hold one ID, the earliest wins, as in the game.
     */
    std::vector<InstalledExtension> listInstalledExtensions(const std::vector<std::filesystem::path>& searchDirs);

    /**
     * @brief Filters discovered extensions by an enabled list, preserving discovery order.
     * If enabledList is empty, all discovered extensions are returned.
     */
    std::vector<DiscoveredExtension> filterEnabledExtensions(
        const std::vector<DiscoveredExtension>& discovered, const std::vector<std::string>& enabledList);

    /**
     * @brief Validates the descriptor returned by openeden_extension_entry against the manifest.
     */
    bool validateExtensionHandshake(const openeden::sdk::ExtensionDescriptor* descriptor,
        const ExtensionManifest& manifest, const openeden::sdk::HostInfo& hostInfo, ExtensionLoadError& error);

    /**
     * @brief Dynamically loads a discovered extension library and performs the handshake.
     */
    ExtensionLoadResult loadExtensionLibrary(
        const DiscoveredExtension& discovered, const openeden::sdk::HostInfo& hostInfo);
} // namespace openeden::extension
