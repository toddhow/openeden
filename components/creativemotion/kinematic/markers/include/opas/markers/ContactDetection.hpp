#pragma once

// Automatic contact detection from a pose clip (A4a). The exact rules are in
// docs/tasks/0006-a4a-contact-detection.md; thresholds come from a detection
// profile (docs/formats/detection.md).

#include "opas/markers/BodyMeasurements.hpp"
#include "opas/markers/InteractionMarker.hpp"
#include "opas/markers/MarkerFile.hpp"
#include "opas/markers/PoseClip.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opas::markers {

// Defaults: toddhow's starting values (A4 decision 1, 2026-10-17).
struct DetectionSettings {
    double distanceThresholdM = 0.03;
    double minDurationS = 0.25;
    double maxRelativeSpeedMps = 0.2;
    double markerToleranceM = 0.02;
    bool includeLowConfidence = false;

    friend bool operator==(const DetectionSettings&, const DetectionSettings&) = default;
};

struct DetectionProfileLoadResult {
    std::optional<DetectionSettings> value;
    LoadError error;
    [[nodiscard]] bool ok() const { return value.has_value(); }
};

DetectionProfileLoadResult loadDetectionProfile(std::string_view text, std::string_view fileName);
DetectionProfileLoadResult loadDetectionProfileFile(const std::filesystem::path& path);
std::string writeDetectionProfile(const DetectionSettings& settings);

enum class Confidence : std::uint8_t { Low, Medium, High };

struct DetectedContact {
    InteractionMarker marker; // ready to use; priority encodes the tier (High 2, Medium 1, Low 0)
    Confidence confidence = Confidence::Low;
    double durationS = 0.0;
    double meanDistanceM = 0.0; // mean surface distance (negative = inside)
    double meanSpeedMps = 0.0;  // mean speed relative to the region's frame
};

struct DetectionResult {
    bool valid = false;
    std::string error;                     // set when valid is false
    std::vector<DetectedContact> contacts; // sorted by window.startS, then id

    // The contacts' markers as a MarkerSet, in the same order.
    [[nodiscard]] MarkerSet markerSet() const;
};

// Detects contacts between the clip's two actors. `bodyMaps[i]` describes
// actor i. Deterministic; never throws.
DetectionResult detectContacts(const PoseClip& clip, const std::array<BodyMapping, 2>& bodyMaps,
                               const DetectionSettings& settings = {});

} // namespace opas::markers
