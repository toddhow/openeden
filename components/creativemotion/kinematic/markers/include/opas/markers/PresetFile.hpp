#pragma once

// Community preset manifests and how presets combine (A6b,
// docs/tasks/0010-a6-marker-editor-presets.md). Format: docs/formats/preset.md.

#include "opas/markers/MarkerFile.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opas::markers {

struct PresetManifest {
    std::string name;
    std::string author;
    std::string presetVersion; // the preset's own version, free text (e.g. "1.2.0")
    std::string license;       // an SPDX license expression (toddhow, 2026-10-17: required)
    int priority = 0;          // higher wins among presets for the same scene
    // Optional, free text (e.g. "OStim NG 7.3"). The JSON key is "requires",
    // which is a C++ keyword, hence the different member name.
    std::vector<std::string> requirements;
    std::string description; // optional

    friend bool operator==(const PresetManifest&, const PresetManifest&) = default;
};

struct PresetLoadResult {
    std::optional<PresetManifest> value;
    LoadError error; // meaningful only when value is empty
    [[nodiscard]] bool ok() const { return value.has_value(); }
};

// Parse and validate. Never throws; errors carry file, line and JSON pointer.
PresetLoadResult loadPresetManifest(std::string_view text, std::string_view fileName);
PresetLoadResult loadPresetManifestFile(const std::filesystem::path& path);

// Canonical text (see the format doc). The manifest must be valid.
std::string writePresetManifest(const PresetManifest& manifest);

// One place a scene's marker file can come from.
struct MarkerSource {
    bool userFile = false;  // the player's own marker folder
    std::string presetName; // for presets: the manifest name
    int priority = 0;       // for presets: the manifest priority
};

// A6b rule 3 (whole files per scene, toddhow 2026-10-17): the user's own file
// wins; among presets the highest priority wins, then the smallest name in
// byte order. Returns the index of the winner, or nullopt for no candidates.
std::optional<std::size_t> chooseMarkerSource(std::span<const MarkerSource> candidates);

} // namespace opas::markers
