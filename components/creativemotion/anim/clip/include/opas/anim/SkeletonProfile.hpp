#pragma once

// Skeleton profiles (task 0020, stage C3a; decision 0005): how a clip's bone
// names map onto a target skeleton, so clips from any source drive the same
// rig, and which target bones form a leg for scaling root motion.
// Format: docs/formats/skeletonprofile.md.

#include "opas/format/LoadError.hpp"
#include "opas/rig/Skeleton.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opas::anim {

struct BoneMapping {
    std::string source; // a clip bone name
    std::string target; // a target skeleton bone name
    friend bool operator==(const BoneMapping&, const BoneMapping&) = default;
};

struct SkeletonProfile {
    std::string name;
    std::vector<BoneMapping> bones; // each source and each target at most once
    // Target bone names from hip to ankle (at least two), for scaling the root's
    // translation by leg length; may be empty (no scaling).
    std::vector<std::string> legChain;
    friend bool operator==(const SkeletonProfile&, const SkeletonProfile&) = default;
};

struct SkeletonProfileLoadResult {
    std::optional<SkeletonProfile> value;
    format::LoadError error; // meaningful only when value is empty
};

// Parses and validates a profile. `fileName` is only used in errors. Never throws.
SkeletonProfileLoadResult loadSkeletonProfile(std::string_view text, std::string_view fileName);
SkeletonProfileLoadResult loadSkeletonProfileFile(const std::filesystem::path& path);

// Canonical text: loading it and writing again gives the same bytes.
std::string writeSkeletonProfile(const SkeletonProfile& profile);

// "" if valid, else "<pointer>: <message>": a non-empty name; source and target
// names non-empty, without control characters, each used at most once;
// legChain empty or at least two distinct names, each a mapped target.
std::string validateSkeletonProfile(const SkeletonProfile& profile);

// A profile that maps every bone of `skeleton` to itself (no leg chain).
SkeletonProfile identityProfile(const rig::Skeleton& skeleton);

} // namespace opas::anim
