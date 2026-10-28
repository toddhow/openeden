#pragma once

// Reading and writing body map files (JSON). Format: docs/formats/bodymap.md.

#include "opas/markers/BodyMeasurements.hpp"
#include "opas/markers/MarkerFile.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace opas::markers {

struct BodyMapLoadResult {
    std::optional<BodyMapping> value; // set on success
    LoadError error;                  // meaningful only when value is empty

    [[nodiscard]] bool ok() const { return value.has_value(); }
};

// Parses and validates body-map text. `fileName` is only used in errors.
// Checks structure only; bone names are checked against a skeleton when the
// map is used. Never throws.
BodyMapLoadResult loadBodyMap(std::string_view text, std::string_view fileName);

// Reads a file from disk, then as loadBodyMap. Never throws.
BodyMapLoadResult loadBodyMapFile(const std::filesystem::path& path);

// Canonical text (see the format doc). The mapping must be valid (as
// loadBodyMap would accept it); an invalid one is written as given and will
// not load back.
std::string writeBodyMap(const BodyMapping& mapping);

} // namespace opas::markers
