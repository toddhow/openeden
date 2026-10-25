#pragma once

#include <components/plugin/manifest.hpp>
#include <openeden/version.hpp>

#include <string>
#include <vector>

namespace openeden::plugin
{
    /**
     * @brief Error recorded during dependency resolution.
     */
    struct ResolutionError
    {
        std::string pluginId;
        std::string message;
    };

    /**
     * @brief Result of dependency graph validation and topological ordering.
     */
    struct ResolutionResult
    {
        bool success = false;
        std::vector<std::string> loadOrder;
        std::vector<std::string> disabledPlugins;
        std::vector<ResolutionError> errors;
    };

    /**
     * @brief Resolves plugin dependencies deterministically from manifests alone.
     *
     * Validates SDK versions, checks contract compatibility, detects cycles,
     * breaks ties deterministically by plugin ID, and produces the final load order.
     * Disabled plugins and error diagnostics are returned.
     *
     * @param manifests Collection of plugin manifests to resolve.
     * @param hostSdkVersion SDK version of the hosting engine.
     * @return ResolutionResult containing load order or errors.
     */
    ResolutionResult resolveDependencies(const std::vector<PluginManifest>& manifests,
        openeden::sdk::Version hostSdkVersion = openeden::sdk::kSdkVersion);
} // namespace openeden::plugin
