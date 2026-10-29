#include "apps/openmw/vsgpendingfeatures.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace OMW;

    TEST(VsgPendingFeaturesTest, should_default_to_pending_features_off_on_vulkan_branch)
    {
        // On Track V stage V1, OPENEDEN_VSG_PENDING_FEATURES defaults to OFF
#if defined(OPENEDEN_VSG_PENDING_FEATURES) && OPENEDEN_VSG_PENDING_FEATURES
        EXPECT_TRUE(areVsgPendingFeaturesCompiledIn());
#else
        EXPECT_FALSE(areVsgPendingFeaturesCompiledIn());
#endif
    }

    TEST(VsgPendingFeaturesTest, should_enumerate_all_expected_v1_pending_features)
    {
        const auto features = getPendingVsgFeatures();

#if defined(OPENEDEN_VSG_PENDING_FEATURES) && OPENEDEN_VSG_PENDING_FEATURES
        EXPECT_TRUE(features.empty());
#else
        ASSERT_FALSE(features.empty());

        // Collect all (category, setting) pairs
        std::set<std::pair<std::string, std::string>> keys;
        for (const auto& f : features)
        {
            keys.insert({ f.mCategory, f.mSetting });
            // Every pending feature must have a non-empty name and architectural reason
            EXPECT_FALSE(f.mFeatureName.empty());
            EXPECT_FALSE(f.mReason.empty());
        }

        // Required Stage V1 excluded features specified in Task 0009:
        // 1. Foot planting IK
        EXPECT_TRUE(keys.count({ "Game", "foot planting" }));
        // 2. Look-at controller
        EXPECT_TRUE(keys.count({ "Game", "look at" }));
        // 3. Smoother turns
        EXPECT_TRUE(keys.count({ "Game", "smoother turns" }));
        // 4. Procedural lean
        EXPECT_TRUE(keys.count({ "Game", "procedural lean" }));
        // 5. CreativeMotion animation runtime
        EXPECT_TRUE(keys.count({ "Game", "creativemotion animation" }));
        EXPECT_TRUE(keys.count({ "Game", "creativemotion animation compare" }));
        // 6. Jolt physics debug drawer
        EXPECT_TRUE(keys.count({ "Physics", "jolt debug draw" }));
        // 7. Authored transitions / animation blend controller
        EXPECT_TRUE(keys.count({ "Game", "enable authored transitions" }));
        // 8. Compatibility pack KF converter
        EXPECT_TRUE(keys.count({ "compat_morrowind", "kf converter" }));
#endif
    }

    TEST(VsgPendingFeaturesTest, should_correctly_identify_pending_features_by_category_and_key)
    {
#if !defined(OPENEDEN_VSG_PENDING_FEATURES) || !OPENEDEN_VSG_PENDING_FEATURES
        // Positive tests: features that are excluded during V1
        EXPECT_TRUE(isVsgFeaturePending("Game", "foot planting"));
        EXPECT_TRUE(isVsgFeaturePending("Game", "look at"));
        EXPECT_TRUE(isVsgFeaturePending("Game", "smoother turns"));
        EXPECT_TRUE(isVsgFeaturePending("Game", "procedural lean"));
        EXPECT_TRUE(isVsgFeaturePending("Game", "creativemotion animation"));
        EXPECT_TRUE(isVsgFeaturePending("Physics", "jolt debug draw"));
        EXPECT_TRUE(isVsgFeaturePending("compat_morrowind", "kf converter"));

        // Negative tests: settings that remain fully active and valid
        EXPECT_FALSE(isVsgFeaturePending("Game", "difficulty"));
        EXPECT_FALSE(isVsgFeaturePending("Game", "actors processing range"));
        EXPECT_FALSE(isVsgFeaturePending("Physics", "async num threads"));
        EXPECT_FALSE(isVsgFeaturePending("Video", "resolution x"));
        EXPECT_FALSE(isVsgFeaturePending("Video", "resolution y"));
        EXPECT_FALSE(isVsgFeaturePending("Saves", "character"));
#endif
    }

    TEST(VsgPendingFeaturesTest, should_format_informative_startup_log_message_with_task_citation)
    {
        const PendingFeature sampleFeature{ "Game", "foot planting", "Foot Planting IK",
            "Pending rebuild on vsgopenmw animation system (Task 0011)" };

        const std::string logMsg = formatPendingFeatureLog(sampleFeature);

        EXPECT_NE(logMsg.find("Game.foot planting"), std::string::npos);
        EXPECT_NE(logMsg.find("Foot Planting IK"), std::string::npos);
        EXPECT_NE(logMsg.find("Task 0011"), std::string::npos);
        EXPECT_NE(logMsg.find("Inactive on this build"), std::string::npos);
    }

    TEST(VsgPendingFeaturesTest, should_generate_startup_log_for_all_pending_features)
    {
        const auto loggedMessages = logPendingFeaturesAtStartup();
        const auto expectedFeatures = getPendingVsgFeatures();

        EXPECT_EQ(loggedMessages.size(), expectedFeatures.size());

        for (size_t i = 0; i < loggedMessages.size(); ++i)
        {
            EXPECT_NE(loggedMessages[i].find(expectedFeatures[i].mSetting), std::string::npos);
        }
    }

    TEST(VsgPendingFeaturesTest, should_handle_nonexistent_and_edge_case_keys_gracefully)
    {
        EXPECT_FALSE(isVsgFeaturePending("", ""));
        EXPECT_FALSE(isVsgFeaturePending("UnknownCategory", "unknown_setting"));
        EXPECT_FALSE(isVsgFeaturePending("Game", ""));
        EXPECT_FALSE(isVsgFeaturePending("", "foot planting"));
    }
}
