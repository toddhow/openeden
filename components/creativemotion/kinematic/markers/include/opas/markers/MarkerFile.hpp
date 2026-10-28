#pragma once

// Reading and writing marker files (JSON). Format: docs/formats/markers.md.

#include "opas/format/LoadError.hpp"
#include "opas/markers/InteractionMarker.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace opas::markers {

// Where and why a file was rejected. Shared by every loader since M2.
using LoadError = format::LoadError;

struct LoadResult {
    std::optional<MarkerSet> value; // set on success
    LoadError error;                // meaningful only when value is empty

    [[nodiscard]] bool ok() const { return value.has_value(); }
};

// Parses and validates marker-file text. `fileName` is only used in errors.
// Never throws.
LoadResult loadMarkerSet(std::string_view text, std::string_view fileName);

// Reads a file from disk, then as loadMarkerSet. Never throws.
LoadResult loadMarkerSetFile(const std::filesystem::path& path);

// The set must be valid (as loadMarkerSet would accept it): non-empty unique
// ids, finite numbers, unit contactDirection. An invalid set is written as
// given and will not load back.
// Canonical text: fixed key order, 2-space indent, LF line endings, trailing
// newline, numbers in shortest round-trip form. Loading this text and writing
// it again gives the same bytes.
std::string writeMarkerSet(const MarkerSet& set);

} // namespace opas::markers
