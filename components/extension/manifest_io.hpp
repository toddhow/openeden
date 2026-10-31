#pragma once

#include <components/extension/load_error.hpp>
#include <components/extension/manifest.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace openeden::extension
{
    /**
     * @brief Parses an extension manifest from JSON text with located errors.
     * @param jsonText JSON string to parse
     * @param filename Display filename for error diagnostics
     * @param error Output error populated on parse failure
     * @return Parsed manifest or nullopt if parsing or validation failed
     */
    std::optional<ExtensionManifest> parseManifest(
        std::string_view jsonText, std::string_view filename, LoadError& error);

    /**
     * @brief Serializes a manifest to canonical JSON text, guaranteeing byte-for-byte round trip.
     */
    std::string writeCanonicalManifest(const ExtensionManifest& manifest);

    /**
     * @brief Reads and parses an extension manifest file from disk.
     */
    std::optional<ExtensionManifest> loadManifestFile(const std::filesystem::path& path, LoadError& error);
} // namespace openeden::extension
