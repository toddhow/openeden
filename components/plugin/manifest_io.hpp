#pragma once

#include <components/plugin/load_error.hpp>
#include <components/plugin/manifest.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace openeden::plugin
{
    /**
     * @brief Parses a plugin manifest from JSON text with located errors.
     * @param jsonText JSON string to parse
     * @param filename Display filename for error diagnostics
     * @param error Output error populated on parse failure
     * @return Parsed manifest or nullopt if parsing or validation failed
     */
    std::optional<PluginManifest> parseManifest(std::string_view jsonText, std::string_view filename, LoadError& error);

    /**
     * @brief Serializes a manifest to canonical JSON text, guaranteeing byte-for-byte round trip.
     */
    std::string writeCanonicalManifest(const PluginManifest& manifest);

    /**
     * @brief Reads and parses a plugin manifest file from disk.
     */
    std::optional<PluginManifest> loadManifestFile(const std::filesystem::path& path, LoadError& error);
} // namespace openeden::plugin
