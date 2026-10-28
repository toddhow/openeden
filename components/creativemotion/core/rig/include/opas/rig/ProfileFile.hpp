#pragma once

// Reading and writing profile files (JSON). Format: docs/formats/profile.md.

#include "opas/format/LoadError.hpp"
#include "opas/rig/Profile.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace opas::rig {

struct ProfileLoadResult {
    std::optional<Profile> value; // set on success
    format::LoadError error;      // meaningful only when value is empty

    [[nodiscard]] bool ok() const { return value.has_value(); }
};

// Parses profile-file text and checks the rules that need no rig. Check it
// against its rig with validateProfile. `fileName` is only used in errors.
// Never throws.
ProfileLoadResult loadProfile(std::string_view text, std::string_view fileName);

// Reads a file from disk, then as loadProfile. Never throws.
ProfileLoadResult loadProfileFile(const std::filesystem::path& path);

// Canonical text (docs/formats/profile.md): loading it and writing again
// gives the same bytes. The profile must be valid on its own.
std::string writeProfile(const Profile& profile);

} // namespace opas::rig
