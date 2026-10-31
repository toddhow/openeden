#pragma once

#include <components/extension/manifest.hpp>
#include <openeden/version.hpp>

#include <string>
#include <vector>

namespace openeden::extension
{
    /**
     * @brief Error recorded during dependency resolution.
     */
    struct ResolutionError
    {
        std::string extensionId;
        std::string message;
    };

    /**
     * @brief Result of dependency graph validation and topological ordering.
     */
    struct ResolutionResult
    {
        bool success = false;
        std::vector<std::string> loadOrder;
        std::vector<std::string> disabledExtensions;
        std::vector<ResolutionError> errors;
    };

    /**
     * @brief Resolves extension dependencies deterministically from manifests alone.
     *
     * Validates SDK versions, checks contract compatibility, detects cycles,
     * breaks ties deterministically by extension ID, and produces the final load order.
     * Disabled extensions and error diagnostics are returned.
     *
     * @param manifests Collection of extension manifests to resolve.
     * @param hostSdkVersion SDK version of the hosting engine.
     * @return ResolutionResult containing load order or errors.
     */
    ResolutionResult resolveDependencies(const std::vector<ExtensionManifest>& manifests,
        openeden::sdk::Version hostSdkVersion = openeden::sdk::kSdkVersion);
} // namespace openeden::extension
