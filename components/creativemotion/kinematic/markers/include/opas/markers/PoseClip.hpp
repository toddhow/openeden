#pragma once

// Pose clips: two actors' bone world transforms sampled over time.
// Format: docs/formats/poseclip.md.

#include "opas/markers/MarkerFile.hpp"
#include "opas/math/Transform.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opas::markers {

struct ClipActor {
    std::string bodyMap;                              // body map name, e.g. "humanoid"
    std::vector<std::string> bones;                   // unique names
    std::vector<std::vector<math::Transform>> frames; // frames[f][b] = world transform of bones[b]

    friend bool operator==(const ClipActor&, const ClipActor&) = default;
};

struct PoseClip {
    static constexpr int kFormatVersion = 1;
    double sampleRateHz = 30.0;
    std::array<ClipActor, 2> actors;

    [[nodiscard]] std::size_t frameCount() const { return actors[0].frames.size(); }
    friend bool operator==(const PoseClip&, const PoseClip&) = default;
};

struct PoseClipLoadResult {
    std::optional<PoseClip> value;
    LoadError error;
    [[nodiscard]] bool ok() const { return value.has_value(); }
};

// Parse and validate. Never throws; errors carry file, line and JSON pointer.
PoseClipLoadResult loadPoseClip(std::string_view text, std::string_view fileName);
PoseClipLoadResult loadPoseClipFile(const std::filesystem::path& path);

// Canonical text (see the format doc). The clip must be valid.
std::string writePoseClip(const PoseClip& clip);

} // namespace opas::markers
