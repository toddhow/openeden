#ifndef OPENMW_VSGPENDINGFEATURES_H
#define OPENMW_VSGPENDINGFEATURES_H

#include <string>
#include <string_view>
#include <vector>

namespace OMW
{
    /**
     * @brief Represents a feature or setting temporarily excluded/pending rebuild on the VSG renderer.
     *
     * As specified in Task 0009 (Track V Stage V1), features relying on the old OSG renderer/animation
     * (e.g. Foot Planting IK, Look-At, Smoother Turns, Procedural Lean, CreativeMotion animation runtime,
     * Jolt debug draw, Authored Transitions) are compiled out behind OPENEDEN_VSG_PENDING_FEATURES (default OFF).
     *
     * When OFF, their settings remain registered in configuration but inactive, and the engine
     * identifies and logs them once at startup.
     */
    struct PendingFeature
    {
        std::string mCategory; // Configuration category, e.g. "Game", "Physics"
        std::string mSetting; // Setting key, e.g. "foot planting", "look at"
        std::string mFeatureName; // Human-readable feature title
        std::string mReason; // Architectural rationale and target task
    };

    /// Returns true if OPENEDEN_VSG_PENDING_FEATURES is compiled in and active on this build.
    bool areVsgPendingFeaturesCompiledIn();

    /// Returns the list of features pending VSG rebuild when OPENEDEN_VSG_PENDING_FEATURES is OFF.
    std::vector<PendingFeature> getPendingVsgFeatures();

    /// Returns true if the given (category, setting) pair represents an inactive pending feature.
    bool isVsgFeaturePending(std::string_view category, std::string_view setting);

    /// Formats a startup log line for an inactive pending feature.
    std::string formatPendingFeatureLog(const PendingFeature& feature);

    /// Identifies and logs all inactive features once at startup.
    /// Returns the list of formatted log messages.
    std::vector<std::string> logPendingFeaturesAtStartup();
}

#endif // OPENMW_VSGPENDINGFEATURES_H
