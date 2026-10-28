#pragma once

// Reading and writing rig files (JSON). Format: docs/formats/rig.md.

#include "opas/format/LoadError.hpp"
#include "opas/rig/Rig.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace opas::rig {

struct RigLoadResult {
    std::optional<Rig> value; // set on success; always passes validateRig
    format::LoadError error;  // meaningful only when value is empty

    [[nodiscard]] bool ok() const { return value.has_value(); }
};

// Parses and validates rig-file text. `fileName` is only used in errors.
// Never throws.
RigLoadResult loadRig(std::string_view text, std::string_view fileName);

// Reads a file from disk, then as loadRig. Never throws.
RigLoadResult loadRigFile(const std::filesystem::path& path);

// Canonical text (docs/formats/rig.md): loading it and writing again gives
// the same bytes. The rig must be valid (validateRig); an invalid rig is
// written as given and will not load back.
std::string writeRig(const Rig& rig);

} // namespace opas::rig
