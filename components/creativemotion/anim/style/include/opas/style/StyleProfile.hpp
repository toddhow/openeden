#pragma once

// Style Profile data model, loader and canonical serializer (milestone C5, task 0023).
// Documentation: docs/formats/style.md.

#include "opas/format/LoadError.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opas::style {

struct PostureParams {
    double slouchRad = 0.0;             // forward spine pitch [-0.6, 0.6]
    double headPitchRad = 0.0;          // chin up/down [-0.6, 0.6]
    double headYawRad = 0.0;            // head turn offset [-0.8, 0.8]
    double shoulderElevationRad = 0.0;  // shrug / hunch [-0.3, 0.6]
    double spineLeanRad = 0.0;          // lateral torso tilt [-0.4, 0.4]

    bool operator==(const PostureParams&) const = default;
};

struct GaitParams {
    double strideScale = 1.0;           // step displacement (0.1, 3.0]
    double cadenceScale = 1.0;          // playback rate (0.1, 3.0]
    double armSwingScale = 1.0;         // sagittal arm swing [0.0, 3.0]
    double hipSwayScale = 1.0;          // lateral pelvic sway [0.0, 3.0]
    double stepWidthOffsetM = 0.0;      // lateral foot spacing [-0.2, 0.5]

    bool operator==(const GaitParams&) const = default;
};

struct IdleParams {
    double breathingRateHz = 0.25;      // respiration frequency [0.05, 2.0]
    double breathingAmplitudeM = 0.015; // chest expansion [0.0, 0.1]
    double swayRateHz = 0.15;           // weight-shift frequency [0.02, 1.0]
    double swayAmplitudeM = 0.012;      // sway translation magnitude [0.0, 0.08]
    double fidgetIntervalS = 8.0;       // mean time between fidgets [1.0, 60.0]
    std::uint64_t seed = 1337;          // deterministic noise seed

    bool operator==(const IdleParams&) const = default;
};

struct TimingParams {
    double stiffnessMultiplier = 1.0;   // motor stiffness scale (0.05, 5.0]
    double dampingMultiplier = 1.0;     // motor damping scale (0.05, 5.0]
    double followThroughLagS = 0.0;     // procedural delay [0.0, 0.5]

    bool operator==(const TimingParams&) const = default;
};

struct GazeParams {
    double glanceFrequencyHz = 0.3;     // glance shift rate [0.0, 3.0]
    double glanceSpeedScale = 1.0;      // saccade speed (0.1, 3.0]
    double headGazeFraction = 0.6;      // head vs eye contribution [0.0, 1.0]

    bool operator==(const GazeParams&) const = default;
};

struct LayerConfig {
    std::string name;                   // e.g. "limp", "hunch", "protect_arm", "shiver"
    double weight = 0.0;                // [0.0, 1.0]
    std::string bone;                   // optional target bone (e.g. "LeftForearm")

    bool operator==(const LayerConfig&) const = default;
};

struct StyleProfile {
    std::string name;
    PostureParams posture;
    GaitParams gait;
    IdleParams idle;
    TimingParams timing;
    GazeParams gaze;
    std::vector<LayerConfig> layers;

    bool operator==(const StyleProfile&) const = default;
};

struct StyleLoadResult {
    std::optional<StyleProfile> value;
    format::LoadError error; // meaningful when value is empty

    [[nodiscard]] bool ok() const { return value.has_value(); }
};

// Parses and validates style profile text. Never throws.
StyleLoadResult loadStyleProfile(std::string_view text, std::string_view fileName);
StyleLoadResult loadStyleProfileFile(const std::filesystem::path& path);

// Canonical JSON format (docs/formats/style.md); roundtrips byte-for-byte.
std::string writeStyleProfile(const StyleProfile& profile);

} // namespace opas::style
