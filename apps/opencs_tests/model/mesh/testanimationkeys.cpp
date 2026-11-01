#include "apps/opencs/model/mesh/animationkeys.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace CSMMesh
{
    namespace
    {
        bool endsWith(const std::string& text, const std::string& suffix)
        {
            return text.size() >= suffix.size()
                && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        TEST(CSMMeshAnimationKeysTest, noKeysGiveNoGroups)
        {
            EXPECT_TRUE(groupTextKeys({}).empty());
        }

        TEST(CSMMeshAnimationKeysTest, keysAreGroupedByThePartBeforeTheFirstColonAndSpace)
        {
            const std::vector<TextKey> keys{ { 0.0f, "Idle: Start" }, { 1.5f, "Idle: Stop" }, { 0.0f, "Walk: Start" },
                { 0.8f, "Walk: Loop Start" }, { 2.0f, "Walk: Stop" } };
            const std::vector<AnimationGroup> groups = groupTextKeys(keys);
            ASSERT_EQ(groups.size(), 2u);
            EXPECT_EQ(groups[0].name, "Idle");
            EXPECT_EQ(groups[0].keys.size(), 2u);
            EXPECT_EQ(groups[1].name, "Walk");
            EXPECT_EQ(groups[1].keys.size(), 3u);
        }

        TEST(CSMMeshAnimationKeysTest, groupsAreInOrderOfFirstAppearance)
        {
            const std::vector<TextKey> keys{ { 5.0f, "Zeta: Start" }, { 0.0f, "Alpha: Start" },
                { 6.0f, "Zeta: Stop" } };
            const std::vector<AnimationGroup> groups = groupTextKeys(keys);
            ASSERT_EQ(groups.size(), 2u);
            EXPECT_EQ(groups[0].name, "Zeta");
            EXPECT_EQ(groups[1].name, "Alpha");
        }

        TEST(CSMMeshAnimationKeysTest, startAndStopAreTheSmallestAndLargestKeyTime)
        {
            const std::vector<TextKey> keys{ { 2.0f, "Run: Mid" }, { 0.5f, "Run: Start" }, { 3.25f, "Run: Stop" } };
            const std::vector<AnimationGroup> groups = groupTextKeys(keys);
            ASSERT_EQ(groups.size(), 1u);
            EXPECT_FLOAT_EQ(groups[0].start, 0.5f);
            EXPECT_FLOAT_EQ(groups[0].stop, 3.25f);
        }

        TEST(CSMMeshAnimationKeysTest, keysInAGroupAreSortedByTime)
        {
            const std::vector<TextKey> keys{ { 3.0f, "Run: c" }, { 1.0f, "Run: a" }, { 2.0f, "Run: b" } };
            const std::vector<AnimationGroup> groups = groupTextKeys(keys);
            ASSERT_EQ(groups.size(), 1u);
            ASSERT_EQ(groups[0].keys.size(), 3u);
            EXPECT_FLOAT_EQ(groups[0].keys[0].time, 1.0f);
            EXPECT_FLOAT_EQ(groups[0].keys[1].time, 2.0f);
            EXPECT_FLOAT_EQ(groups[0].keys[2].time, 3.0f);
            EXPECT_TRUE(endsWith(groups[0].keys[0].text, "a"));
            EXPECT_TRUE(endsWith(groups[0].keys[2].text, "c"));
        }

        TEST(CSMMeshAnimationKeysTest, equalTimesKeepTheirInputOrder)
        {
            const std::vector<TextKey> keys{ { 1.0f, "Run: first" }, { 0.0f, "Run: zero" }, { 1.0f, "Run: second" },
                { 1.0f, "Run: third" } };
            const std::vector<AnimationGroup> groups = groupTextKeys(keys);
            ASSERT_EQ(groups.size(), 1u);
            ASSERT_EQ(groups[0].keys.size(), 4u);
            EXPECT_TRUE(endsWith(groups[0].keys[0].text, "zero"));
            EXPECT_TRUE(endsWith(groups[0].keys[1].text, "first"));
            EXPECT_TRUE(endsWith(groups[0].keys[2].text, "second"));
            EXPECT_TRUE(endsWith(groups[0].keys[3].text, "third"));
        }

        TEST(CSMMeshAnimationKeysTest, keysWithoutTheSeparatorAreSkipped)
        {
            const std::vector<TextKey> keys{ { 0.0f, "sound" }, { 0.0f, "Idle:Start" }, { 0.0f, "Idle: Start" },
                { 1.0f, "" }, { 2.0f, "Idle:" } };
            const std::vector<AnimationGroup> groups = groupTextKeys(keys);
            ASSERT_EQ(groups.size(), 1u);
            EXPECT_EQ(groups[0].name, "Idle");
            EXPECT_EQ(groups[0].keys.size(), 1u);
        }

        TEST(CSMMeshAnimationKeysTest, onlyTheFirstSeparatorSplits)
        {
            const std::vector<TextKey> keys{ { 0.0f, "Idle: Sound: Gen 1" } };
            const std::vector<AnimationGroup> groups = groupTextKeys(keys);
            ASSERT_EQ(groups.size(), 1u);
            EXPECT_EQ(groups[0].name, "Idle");
            EXPECT_EQ(groups[0].keys.size(), 1u);
        }

        TEST(CSMMeshAnimationKeysTest, onlyUnseparatedKeysGiveNoGroups)
        {
            EXPECT_TRUE(groupTextKeys({ { 0.0f, "a" }, { 1.0f, "b" } }).empty());
        }

        TEST(CSMMeshAnimationKeysTest, groupingIsDeterministic)
        {
            const std::vector<TextKey> keys{ { 1.0f, "B: x" }, { 0.0f, "A: y" }, { 0.5f, "B: z" } };
            const auto first = groupTextKeys(keys);
            const auto second = groupTextKeys(keys);
            ASSERT_EQ(first.size(), second.size());
            for (std::size_t i = 0; i < first.size(); ++i)
            {
                EXPECT_EQ(first[i].name, second[i].name);
                EXPECT_FLOAT_EQ(first[i].start, second[i].start);
                EXPECT_FLOAT_EQ(first[i].stop, second[i].stop);
                EXPECT_EQ(first[i].keys.size(), second[i].keys.size());
            }
        }

        TEST(CSMMeshAnimationKeysTest, kfSidecarForAnXPrefixedNifIsTheSamePathWithKf)
        {
            EXPECT_EQ(kfSidecarFor("meshes/x/xbase_anim.nif"), "meshes/x/xbase_anim.kf");
            EXPECT_EQ(kfSidecarFor("xbase_anim.nif"), "xbase_anim.kf");
            EXPECT_EQ(kfSidecarFor("meshes/r/xmudcrab.nif"), "meshes/r/xmudcrab.kf");
        }

        TEST(CSMMeshAnimationKeysTest, kfSidecarPrefixAndExtensionAreCaseInsensitive)
        {
            EXPECT_EQ(kfSidecarFor("meshes/x/XBase_anim.nif"), "meshes/x/XBase_anim.kf");
            EXPECT_EQ(kfSidecarFor("meshes/x/xbase_anim.NIF"), "meshes/x/xbase_anim.kf");
            EXPECT_EQ(kfSidecarFor("meshes/x/XBASE.Nif"), "meshes/x/XBASE.kf");
        }

        TEST(CSMMeshAnimationKeysTest, kfSidecarIsEmptyWhenTheBasenameDoesNotStartWithX)
        {
            EXPECT_TRUE(kfSidecarFor("meshes/i/in_hlaalu_hall.nif").empty());
            EXPECT_TRUE(kfSidecarFor("ex_hut.nif").empty());
            // only the basename counts, not a directory that starts with x
            EXPECT_TRUE(kfSidecarFor("xdir/base.nif").empty());
            EXPECT_TRUE(kfSidecarFor("meshes/x/base_xanim.nif").empty());
        }

        TEST(CSMMeshAnimationKeysTest, kfSidecarIsEmptyForNonNifFiles)
        {
            EXPECT_TRUE(kfSidecarFor("meshes/x/xbase.dae").empty());
            EXPECT_TRUE(kfSidecarFor("meshes/x/xbase.kf").empty());
            EXPECT_TRUE(kfSidecarFor("meshes/x/xbase.nif.bak").empty());
            EXPECT_TRUE(kfSidecarFor("meshes/x/xbase").empty());
            EXPECT_TRUE(kfSidecarFor("").empty());
        }
    }
}
