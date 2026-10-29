#include "vsgpendingfeatures.hpp"

#include <components/debug/debuglog.hpp>

#include <algorithm>

namespace OMW
{
    bool areVsgPendingFeaturesCompiledIn()
    {
#if defined(OPENEDEN_VSG_PENDING_FEATURES) && OPENEDEN_VSG_PENDING_FEATURES
        return true;
#else
        return false;
#endif
    }

    std::vector<PendingFeature> getPendingVsgFeatures()
    {
        if (areVsgPendingFeaturesCompiledIn())
            return {};

        return { { "Game", "foot planting", "Foot Planting IK",
                     "Pending rebuild on vsgopenmw animation system (Task 0011)" },
            { "Game", "look at", "Look-At Controller", "Pending rebuild on vsgopenmw animation system (Task 0011)" },
            { "Game", "smoother turns", "Smoother Turns Controller",
                "Pending rebuild on vsgopenmw animation system (Task 0011)" },
            { "Game", "procedural lean", "Procedural Lean",
                "Pending rebuild on vsgopenmw animation system (Task 0011)" },
            { "Game", "creativemotion animation", "CreativeMotion Animation Runtime",
                "Pending rebuild on vsgopenmw animation system (Task 0011)" },
            { "Game", "creativemotion animation compare", "CreativeMotion Animation Comparison Mode",
                "Pending rebuild on vsgopenmw animation system (Task 0011)" },
            { "Physics", "jolt debug draw", "Jolt Physics Debug Draw",
                "Pending VSG debug geometry renderer (Task 0011)" },
            { "Game", "enable authored transitions", "Animation Blend Controller / Authored Transitions",
                "Pending rebuild on vsgopenmw animation system (Task 0011)" },
            { "compat_morrowind", "kf converter", "Compatibility Pack KF Converter",
                "Pending VSG animation/nif pipeline (Task 0011)" } };
    }

    bool isVsgFeaturePending(std::string_view category, std::string_view setting)
    {
        const auto features = getPendingVsgFeatures();
        return std::any_of(features.begin(), features.end(),
            [category, setting](const PendingFeature& f) { return f.mCategory == category && f.mSetting == setting; });
    }

    std::string formatPendingFeatureLog(const PendingFeature& feature)
    {
        return std::string("[VSG Pending Feature] Inactive on this build: ") + feature.mCategory + "."
            + feature.mSetting + " (" + feature.mFeatureName + ") - " + feature.mReason;
    }

    std::vector<std::string> logPendingFeaturesAtStartup()
    {
        std::vector<std::string> messages;
        const auto features = getPendingVsgFeatures();
        if (features.empty())
            return messages;

        Log(Debug::Info) << "=== OpenEden VSG Pending Features ===";
        Log(Debug::Info)
            << "The following features are inactive on this Vulkan build pending Stage V3 (Task 0011) rebuild:";

        for (const auto& feature : features)
        {
            std::string msg = formatPendingFeatureLog(feature);
            Log(Debug::Info) << "  - " << feature.mCategory << "." << feature.mSetting << " (" << feature.mFeatureName
                             << "): " << feature.mReason;
            messages.push_back(std::move(msg));
        }
        return messages;
    }
}
