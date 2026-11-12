#include <components/havokpack/animation.hpp>
#include <components/havokpack/animationdata.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>

// Task 0119 stage 7: the plain-text sidecars (project files and root-motion files). The text is generated here with
// invented names; expected values are worked out by hand. The layout is logged in docs/research/0054-provenance.md.
namespace
{
    using namespace HavokPack;

    // Lines joined and terminated with CR LF, the way every installed file is.
    std::string text(std::initializer_list<std::string> lines, const char* eol = "\r\n")
    {
        std::string out;
        for (const auto& l : lines)
            out += l + eol;
        return out;
    }

    std::vector<std::string> head()
    {
        return { "1", "3", "Behaviors\\Test.hkx", "Characters\\Test.hkx", "Character Assets\\skeleton.HKX" };
    }

    std::string project(std::vector<std::string> lines)
    {
        std::vector<std::string> all = head();
        all.insert(all.end(), lines.begin(), lines.end());
        std::string out;
        for (const auto& l : all)
            out += l + "\r\n";
        return out;
    }

    template <class F>
    void expectError(F&& f, const std::string& part)
    {
        try
        {
            f();
            FAIL() << "expected an Error mentioning '" << part << "'";
        }
        catch (const Error& e)
        {
            EXPECT_NE(std::string(e.what()).find(part), std::string::npos) << e.what();
        }
    }
}

TEST(HavokAnimData, a_project_without_clips_lists_its_files)
{
    const auto p = readAnimationProject(project({ "0" }));
    ASSERT_EQ(p.mFiles.size(), 3u);
    EXPECT_EQ(p.mFiles[0], "Behaviors\\Test.hkx");
    EXPECT_EQ(p.mFiles[2], "Character Assets\\skeleton.HKX");
    EXPECT_TRUE(p.mClips.empty());
}

TEST(HavokAnimData, clips_have_name_index_speed_two_unknowns_and_events)
{
    const auto p = readAnimationProject(
        project({ "1", "Alpha_Walk", "925", "1", "0", "0", "2", "StepRight:0.688", "StepLeft:1.243", "", "Beta Idle",
            "7", "0.75", "0.333333", "0.1", "0", "", "Gamma.hkx", "0", "-1", "0", "0", "1", "Back:-1.5", "" }));
    ASSERT_EQ(p.mClips.size(), 3u);
    EXPECT_EQ(p.mClips[0].mName, "Alpha_Walk");
    EXPECT_EQ(p.mClips[0].mCacheIndex, 925u);
    EXPECT_FLOAT_EQ(p.mClips[0].mPlaybackSpeed, 1.0f);
    ASSERT_EQ(p.mClips[0].mEvents.size(), 2u);
    EXPECT_EQ(p.mClips[0].mEvents[0].mName, "StepRight");
    EXPECT_FLOAT_EQ(p.mClips[0].mEvents[0].mTime, 0.688f);
    EXPECT_EQ(p.mClips[0].mEvents[1].mName, "StepLeft");
    EXPECT_FLOAT_EQ(p.mClips[0].mEvents[1].mTime, 1.243f);
    EXPECT_EQ(p.mClips[1].mName, "Beta Idle"); // spaces are part of a name
    EXPECT_FLOAT_EQ(p.mClips[1].mPlaybackSpeed, 0.75f);
    EXPECT_FLOAT_EQ(p.mClips[1].mUnknownA, 0.333333f);
    EXPECT_FLOAT_EQ(p.mClips[1].mUnknownB, 0.1f);
    EXPECT_TRUE(p.mClips[1].mEvents.empty());
    EXPECT_EQ(p.mClips[2].mName, "Gamma.hkx");
    EXPECT_FLOAT_EQ(p.mClips[2].mPlaybackSpeed, -1.0f);
    EXPECT_FLOAT_EQ(p.mClips[2].mEvents[0].mTime, -1.5f);
}

TEST(HavokAnimData, an_event_name_is_everything_before_the_last_colon_and_exponents_parse)
{
    const auto p
        = readAnimationProject(project({ "1", "C", "1", "1", "0", "0", "2", "Sound.Foo:2.5e-007", "A:B:3", "" }));
    ASSERT_EQ(p.mClips[0].mEvents.size(), 2u);
    EXPECT_EQ(p.mClips[0].mEvents[0].mName, "Sound.Foo");
    EXPECT_NEAR(p.mClips[0].mEvents[0].mTime, 2.5e-7f, 1e-12f);
    EXPECT_EQ(p.mClips[0].mEvents[1].mName, "A:B");
    EXPECT_FLOAT_EQ(p.mClips[0].mEvents[1].mTime, 3.0f);
}

TEST(HavokAnimData, a_lone_line_feed_is_accepted)
{
    std::string t = project({ "1", "C", "1", "1", "0", "0", "0", "" });
    t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());
    EXPECT_EQ(readAnimationProject(t).mClips.size(), 1u);
}

TEST(HavokAnimData, the_event_time_inside_the_animation_is_the_stored_time_times_the_speed)
{
    SidecarClip c;
    c.mPlaybackSpeed = 0.75f;
    EXPECT_NEAR(eventAnimationTime(c, { "End", 1.77778f }), 1.333335f, 1e-5f);
    c.mPlaybackSpeed = -1.0f; // played backwards: stored times are negative, animation times positive
    EXPECT_FLOAT_EQ(eventAnimationTime(c, { "End", -1.5f }), 1.5f);
}

TEST(HavokAnimData, malformed_projects_are_refused_with_the_line)
{
    expectError([] { readAnimationProject(text({ "0", "0", "0" })); }, "(line 1)"); // the first line must be 1
    expectError([] { readAnimationProject(text({ "x", "0", "0" })); }, "(line 1)");
    expectError([] { readAnimationProject(text({ "1", "-1", "0" })); }, "(line 2)");
    expectError([] { readAnimationProject(text({ "1", "2", "A.hkx" })); }, "end of file"); // list is short
    expectError([] { readAnimationProject(text({ "1", "1", "A.hkx", "2" })); }, "(line 4)"); // flag is 0 or 1
    expectError([] { readAnimationProject(text({ "1", "1", "A.hkx" })); }, "end of file"); // no flag
    expectError([] { readAnimationProject(text({ "1", "1", "", "0" })); }, "(line 3)"); // empty file name
}

TEST(HavokAnimData, data_after_a_zero_flag_is_refused)
{
    expectError([] { readAnimationProject(project({ "0", "Stray" })); }, "(line 7)");
}

TEST(HavokAnimData, malformed_clips_are_refused_with_the_line)
{
    // a clip block missing its terminating blank line
    expectError([] { readAnimationProject(project({ "1", "C", "1", "1", "0", "0", "0" })); }, "end of file");
    // the blank line is missing: the next clip's name stands where it should be
    expectError([] { readAnimationProject(project({ "1", "C", "1", "1", "0", "0", "0", "D" })); }, "(line 13)");
    // number problems; line 7 is the first clip line (the flag is line 6)
    expectError([] { readAnimationProject(project({ "1", "", "1", "1", "0", "0", "0", "" })); }, "(line 7)"); // name
    expectError([] { readAnimationProject(project({ "1", "C", "-1", "1", "0", "0", "0", "" })); }, "(line 8)");
    expectError([] { readAnimationProject(project({ "1", "C", "1.5", "1", "0", "0", "0", "" })); }, "(line 8)");
    expectError([] { readAnimationProject(project({ "1", "C", "4294967296", "1", "0", "0", "0", "" })); }, "(line 8)");
    expectError([] { readAnimationProject(project({ "1", "C", "1", "nan", "0", "0", "0", "" })); }, "(line 9)");
    expectError([] { readAnimationProject(project({ "1", "C", "1", "1", "inf", "0", "0", "" })); }, "(line 10)");
    expectError([] { readAnimationProject(project({ "1", "C", "1", "1", "0", "1,5", "0", "" })); }, "(line 11)");
    expectError([] { readAnimationProject(project({ "1", "C", "1", "1", "0", "0", "x", "" })); }, "(line 12)");
    expectError([] { readAnimationProject(project({ "1", "C", "1", " 1", "0", "0", "0", "" })); }, "(line 9)");
    expectError([] { readAnimationProject(project({ "1", "C", "1", "+1", "0", "0", "0", "" })); }, "(line 9)");
    // events
    expectError(
        [] { readAnimationProject(project({ "1", "C", "1", "1", "0", "0", "1", "NoColon", "" })); }, "(line 13)");
    expectError([] { readAnimationProject(project({ "1", "C", "1", "1", "0", "0", "1", ":1.0", "" })); }, "(line 13)");
    expectError([] { readAnimationProject(project({ "1", "C", "1", "1", "0", "0", "1", "A:x", "" })); }, "(line 13)");
    expectError([] { readAnimationProject(project({ "1", "C", "1", "1", "0", "0", "1", "A:nan", "" })); }, "(line 13)");
    expectError([] { readAnimationProject(project({ "1", "C", "1", "1", "0", "0", "2", "A:1", "" })); }, "(line 14)");
}

TEST(HavokAnimData, caps_are_checked_before_allocating)
{
    TextLimits l;
    l.mMaxFiles = 2;
    expectError([&] { readAnimationProject(project({ "0" }), l); }, "(line 2)");
    l = {};
    l.mMaxClips = 1;
    expectError(
        [&] {
            readAnimationProject(
                project({ "1", "A", "1", "1", "0", "0", "0", "", "B", "1", "1", "0", "0", "0", "" }), l);
        },
        "clips");
    l = {};
    l.mMaxEventsPerClip = 1;
    expectError([&] { readAnimationProject(project({ "1", "A", "1", "1", "0", "0", "2", "X:1", "Y:2", "" }), l); },
        "(line 12)");
    l = {};
    l.mMaxLineLength = 8;
    expectError([&] { readAnimationProject(project({ "0" }), l); }, "(line 3)");
    l = {};
    l.mMaxFileBytes = 10;
    expectError([&] { readAnimationProject(project({ "0" }), l); }, "too large");
    // a count of 4 billion events must be refused by the cap, not attempted
    expectError([] { readAnimationProject(project({ "1", "A", "1", "1", "0", "0", "4000000000", "" })); }, "(line 12)");
}

TEST(HavokAnimData, a_project_survives_30000_random_mutations_as_a_value_or_an_error)
{
    const std::string base = project(
        { "1", "Alpha", "5", "1", "0", "0", "2", "A:0.5", "B:1.5", "", "Beta", "6", "-1", "0", "0", "1", "C:-2", "" });
    std::mt19937 rng(12345);
    for (int i = 0; i < 30000; ++i)
    {
        std::string t = base;
        const int edits = 1 + static_cast<int>(rng() % 3);
        for (int e = 0; e < edits; ++e)
        {
            const std::size_t at = rng() % t.size();
            switch (rng() % 3)
            {
                case 0:
                    t[at] = static_cast<char>(rng());
                    break;
                case 1:
                    t.erase(at, 1 + rng() % 4);
                    break;
                default:
                    t.insert(at, 1, "0123456789:.-\r\n e"[rng() % 17]);
                    break;
            }
            if (t.empty())
                t = "x";
        }
        try
        {
            (void)readAnimationProject(t);
        }
        catch (const Error&)
        {
        }
    }
}

// ---------------------------------------------------------------------------------------------------- motion

TEST(HavokAnimData, a_motion_file_has_blocks_of_translation_and_rotation_rows)
{
    const auto m
        = readMotionTable(text({ "4", "0.766667", "2", "0.0666667 -4.16469 -2.46944 0", "0.766667 -85.7063 -50.8193 0",
            "1", "0.766667 0 0 0.707108 0.707105", "", "2", "2", "1", "2 0 0 0", "1", "2 0 0 0 1", "" }));
    ASSERT_EQ(m.mBlocks.size(), 2u);
    EXPECT_EQ(m.mBlocks[0].mCacheIndex, 4u);
    EXPECT_FLOAT_EQ(m.mBlocks[0].mDuration, 0.766667f);
    ASSERT_EQ(m.mBlocks[0].mTranslation.size(), 2u);
    EXPECT_FLOAT_EQ(m.mBlocks[0].mTranslation[1][1], -85.7063f);
    ASSERT_EQ(m.mBlocks[0].mRotation.size(), 1u);
    EXPECT_FLOAT_EQ(m.mBlocks[0].mRotation[0][3], 0.707108f);
    EXPECT_EQ(m.find(2), &m.mBlocks[1]);
    EXPECT_EQ(m.find(4), &m.mBlocks[0]);
    EXPECT_EQ(m.find(3), nullptr);
    EXPECT_TRUE(readMotionTable("").mBlocks.empty());
}

TEST(HavokAnimData, malformed_motion_files_are_refused_with_the_line)
{
    const std::string rowsOk = "1\r\n1 0 0 0\r\n1\r\n1 0 0 0 1\r\n\r\n";
    expectError([&] { readMotionTable("0\r\n0\r\n" + rowsOk); }, "(line 2)"); // duration must be positive
    expectError([&] { readMotionTable("0\r\n-1\r\n" + rowsOk); }, "(line 2)");
    expectError([&] { readMotionTable("0\r\n1\r\n1\r\n1 0 0\r\n1\r\n1 0 0 0 1\r\n\r\n"); }, "(line 4)"); // 3 numbers
    expectError(
        [&] { readMotionTable("0\r\n1\r\n1\r\n1 0 0 0 0\r\n1\r\n1 0 0 0 1\r\n\r\n"); }, "(line 4)"); // 5 numbers
    expectError(
        [&] { readMotionTable("0\r\n1\r\n1\r\n1  0 0 0\r\n1\r\n1 0 0 0 1\r\n\r\n"); }, "(line 4)"); // double space
    expectError([&] { readMotionTable("0\r\n1\r\n1\r\n1 0 0 0\r\n1\r\n1 0 0 0\r\n\r\n"); }, "(line 6)"); // rotation w
    expectError([&] { readMotionTable("0\r\n1\r\n1\r\n1 0 nan 0\r\n1\r\n1 0 0 0 1\r\n\r\n"); }, "(line 4)");
    expectError([&] { readMotionTable("0\r\n1\r\n2\r\n1 0 0 0\r\n1 0 0 0\r\n1\r\n1 0 0 0 1\r\n\r\n"); },
        "(line 5)"); // equal times
    expectError([&] { readMotionTable("0\r\n1\r\n2\r\n2 0 0 0\r\n1 0 0 0\r\n1\r\n1 0 0 0 1\r\n\r\n"); },
        "(line 5)"); // decreasing
    expectError(
        [&] { readMotionTable("0\r\n1\r\n1\r\n-1 0 0 0\r\n1\r\n1 0 0 0 1\r\n\r\n"); }, "(line 4)"); // negative time
    expectError([&] { readMotionTable("0\r\n1\r\n1\r\n1 0 0 0\r\n1\r\n1 0 0 0 1\r\n"); }, "end of file"); // no blank
    expectError([&] { readMotionTable("0\r\n1\r\n1\r\n1 0 0 0\r\n1\r\n1 0 0 0 1\r\nx\r\n"); }, "(line 7)");
    expectError([&] { readMotionTable("0\r\n1\r\n3\r\n1 0 0 0\r\n"); }, "end of file");
    expectError(
        [&] {
            readMotionTable("0\r\n1\r\n1\r\n1 0 0 0\r\n1\r\n1 0 0 0 1\r\n\r\n"
                + std::string("0\r\n1\r\n1\r\n1 0 0 0\r\n1\r\n1 0 0 0 1\r\n\r\n"));
        },
        "twice"); // the same cache index again
    TextLimits l;
    l.mMaxRowsPerBlock = 1;
    expectError([&] { readMotionTable("0\r\n1\r\n2\r\n1 0 0 0\r\n2 0 0 0\r\n1\r\n1 0 0 0 1\r\n\r\n", l); }, "(line 3)");
    l = {};
    l.mMaxBlocks = 1;
    expectError([&] { readMotionTable("0\r\n1\r\n" + rowsOk + "1\r\n1\r\n" + rowsOk, l); }, "blocks");
}

TEST(HavokAnimData, a_motion_file_survives_30000_random_mutations_as_a_value_or_an_error)
{
    const std::string base = text({ "4", "0.766667", "2", "0.0666667 -4.16 -2.46 0", "0.766667 -85.7 -50.8 0", "1",
        "0.766667 0 0 0.707108 0.707105", "", "9", "1.5", "1", "1.5 0 3 0", "1", "1.5 0 0 0 1", "" });
    std::mt19937 rng(777);
    for (int i = 0; i < 30000; ++i)
    {
        std::string t = base;
        const int edits = 1 + static_cast<int>(rng() % 3);
        for (int e = 0; e < edits; ++e)
        {
            const std::size_t at = rng() % t.size();
            switch (rng() % 3)
            {
                case 0:
                    t[at] = static_cast<char>(rng());
                    break;
                case 1:
                    t.erase(at, 1 + rng() % 4);
                    break;
                default:
                    t.insert(at, 1, "0123456789:.-\r\n e"[rng() % 17]);
                    break;
            }
            if (t.empty())
                t = "x";
        }
        try
        {
            (void)readMotionTable(t);
        }
        catch (const Error&)
        {
        }
    }
}

namespace
{
    MotionBlock walkBlock()
    {
        MotionBlock b;
        b.mDuration = 2.0f;
        b.mTranslation = { { 1.0f, 0, 10, 0 }, { 2.0f, 4, 30, 2 } };
        // a half turn about z, reached at time 2: (0, 0, sin 90, cos 90) = (0, 0, 1, 0)
        b.mRotation = { { 2.0f, 0, 0, 1, 0 } };
        return b;
    }
}

TEST(HavokAnimData, root_translation_is_linear_from_zero_and_held_after_the_last_row)
{
    const MotionBlock b = walkBlock();
    auto t = rootTranslationAt(b, 0.0f);
    EXPECT_FLOAT_EQ(t[1], 0.0f);
    t = rootTranslationAt(b, 0.5f); // halfway from the origin to (0, 10, 0)
    EXPECT_FLOAT_EQ(t[1], 5.0f);
    t = rootTranslationAt(b, 1.0f);
    EXPECT_FLOAT_EQ(t[1], 10.0f);
    t = rootTranslationAt(b, 1.5f); // halfway from (0, 10, 0) to (4, 30, 2)
    EXPECT_FLOAT_EQ(t[0], 2.0f);
    EXPECT_FLOAT_EQ(t[1], 20.0f);
    EXPECT_FLOAT_EQ(t[2], 1.0f);
    t = rootTranslationAt(b, 99.0f);
    EXPECT_FLOAT_EQ(t[0], 4.0f);
    t = rootTranslationAt(b, -3.0f);
    EXPECT_FLOAT_EQ(t[1], 0.0f);
    EXPECT_EQ(rootTranslationAt(MotionBlock{}, 1.0f), (std::array<float, 3>{ 0, 0, 0 }));
}

TEST(HavokAnimData, root_rotation_goes_from_the_identity_to_the_row_and_stays_unit_length)
{
    const MotionBlock b = walkBlock();
    auto q = rootRotationAt(b, 0.0f);
    EXPECT_FLOAT_EQ(q[3], 1.0f);
    q = rootRotationAt(b, 2.0f);
    EXPECT_NEAR(q[2], 1.0f, 1e-6f);
    // halfway: normalised mix of (0,0,0,1) and (0,0,1,0) is (0, 0, 0.7071, 0.7071)
    q = rootRotationAt(b, 1.0f);
    EXPECT_NEAR(q[2], 0.70710678f, 1e-5f);
    EXPECT_NEAR(q[3], 0.70710678f, 1e-5f);
    EXPECT_NEAR(std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]), 1.0f, 1e-5f);
    EXPECT_EQ(rootRotationAt(MotionBlock{}, 1.0f), (std::array<float, 4>{ 0, 0, 0, 1 }));
    // the shorter way round: a row (0,0,-0.7071,-0.7071) is the same turn as (0,0,0.7071,0.7071)
    MotionBlock n;
    n.mRotation = { { 1.0f, 0, 0, -0.70710678f, -0.70710678f } };
    q = rootRotationAt(n, 0.5f);
    EXPECT_GT(q[3], 0.9f); // about 22.5 degrees, not 112.5
}

// ---------------------------------------------------------------------------------------------------- linking

namespace
{
    AnimationProject linkProject()
    {
        return readAnimationProject(project({ "1", "Walk", "1", "1", "0", "0", "1", "Step:0.5", "", "walk", "2", "0.5",
            "0", "0", "1", "Slow:1.0", "", // same name, other animation
            "Run.hkx", "3", "1", "0", "0", "0", "", "Orphan", "9", "1", "0", "0", "1", "Pop:0.1", "" }));
    }

    MotionTable linkMotion()
    {
        return readMotionTable(text({ "1", "1.5", "1", "1.5 0 90 0", "1", "1.5 0 0 0 1", "", "2", "3", "1", "3 0 40 0",
            "1", "3 0 0 0 1", "", "3", "0.6", "1", "0.6 0 200 0", "1", "0.6 0 0 0 1", "" }));
    }
}

TEST(HavokAnimData, a_clip_links_by_name_and_duration)
{
    const auto p = linkProject();
    const auto m = linkMotion();
    const ClipLink l = linkClip(p, m, "walk", 1.5f);
    ASSERT_EQ(l.mClips.size(), 1u); // "Walk" fits 1.5 s; the second "walk" has a block of 3 s
    EXPECT_EQ(l.mClips[0]->mName, "Walk");
    ASSERT_NE(l.mMotion, nullptr);
    EXPECT_EQ(l.mMotion->mCacheIndex, 1u);
    EXPECT_FLOAT_EQ(l.mMotion->mTranslation[0][2], 90.0f);

    const ClipLink l3 = linkClip(p, m, "WALK", 3.0f);
    ASSERT_EQ(l3.mClips.size(), 1u);
    EXPECT_EQ(l3.mClips[0]->mCacheIndex, 2u);
    EXPECT_EQ(l3.mMotion->mCacheIndex, 2u);
}

TEST(HavokAnimData, the_duration_may_differ_by_a_millisecond_but_not_more)
{
    const auto p = linkProject();
    const auto m = linkMotion();
    EXPECT_EQ(linkClip(p, m, "walk", 1.5004f).mClips.size(), 1u);
    EXPECT_TRUE(linkClip(p, m, "walk", 1.51f).mClips.empty());
    EXPECT_EQ(linkClip(p, m, "walk", 1.51f).mMotion, nullptr);
}

TEST(HavokAnimData, a_trailing_hkx_in_the_generator_name_is_ignored)
{
    const auto p = linkProject();
    const auto m = linkMotion();
    const ClipLink l = linkClip(p, m, "run", 0.6f);
    ASSERT_EQ(l.mClips.size(), 1u);
    EXPECT_EQ(l.mClips[0]->mName, "Run.hkx");
    EXPECT_EQ(linkClip(p, m, "run.hkx", 0.6f).mClips.size(), 1u);
}

TEST(HavokAnimData, a_generator_without_a_motion_block_is_kept_because_it_cannot_be_checked)
{
    const auto p = linkProject();
    const auto m = linkMotion();
    const ClipLink l = linkClip(p, m, "orphan", 123.0f);
    ASSERT_EQ(l.mClips.size(), 1u);
    EXPECT_EQ(l.mMotion, nullptr);
    EXPECT_EQ(l.mClips[0]->mEvents[0].mName, "Pop");
}

TEST(HavokAnimData, no_generator_with_the_name_links_nothing)
{
    const auto p = linkProject();
    const auto m = linkMotion();
    const ClipLink l = linkClip(p, m, "nothing", 1.0f);
    EXPECT_TRUE(l.mClips.empty());
    EXPECT_EQ(l.mMotion, nullptr);
}

// ---------------------------------------------------------------------------------------------------- real data

namespace
{
    std::string slurp(const std::filesystem::path& p)
    {
        std::ifstream in(p, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }
}

// Opt-in: OPENEDEN_HAVOK_ANIMDATA_DIR is the extracted meshes/animationdata folder of Skyrim - Animations.bsa.
// Aggregates only are printed. These are the invariants that justify the strict readers (provenance F37 to F52).
TEST(HavokAnimDataRealData, every_project_and_motion_file_parses_and_keeps_its_invariants)
{
    const char* dir = std::getenv("OPENEDEN_HAVOK_ANIMDATA_DIR");
    if (dir == nullptr)
        GTEST_SKIP() << "OPENEDEN_HAVOK_ANIMDATA_DIR is not set";
    const std::filesystem::path root(dir);

    std::size_t projects = 0, withClips = 0, clips = 0, events = 0, speedChecked = 0, scaledWithin = 0;
    std::map<std::string, AnimationProject> byName;
    for (const auto& e : std::filesystem::directory_iterator(root))
    {
        if (!e.is_regular_file())
            continue;
        const std::string name = e.path().filename().string();
        const std::string t = slurp(e.path());
        if (name == "dirlist.txt")
        {
            expectError([&] { readAnimationProject(t); }, "(line 1)"); // a different format: the project list
            continue;
        }
        const auto p = readAnimationProject(t);
        ++projects;
        withClips += p.mClips.empty() ? 0 : 1;
        clips += p.mClips.size();
        for (const auto& c : p.mClips)
            events += c.mEvents.size();
        byName[name] = p;
    }
    std::size_t tables = 0, blocks = 0, lastIsDuration = 0, unit = 0, rotations = 0, onGrid = 0, rows = 0;
    std::size_t clipsWithBlock = 0, clipsWithoutBlock = 0, withEvents = 0;
    for (const auto& e : std::filesystem::directory_iterator(root / "boundanims"))
    {
        const std::string name = e.path().filename().string();
        ASSERT_EQ(name.rfind("anims_", 0), 0u) << name;
        const auto m = readMotionTable(slurp(e.path()));
        ++tables;
        blocks += m.mBlocks.size();
        const auto project = byName.find(name.substr(6));
        ASSERT_NE(project, byName.end()) << name << " has no project file";
        EXPECT_FALSE(project->second.mClips.empty()) << name;
        for (const auto& b : m.mBlocks)
        {
            ASSERT_FALSE(b.mTranslation.empty());
            ASSERT_FALSE(b.mRotation.empty());
            lastIsDuration
                += (std::fabs(b.mTranslation.back()[0] - b.mDuration) <= 1e-3f * std::max(1.0f, b.mDuration)
                       && std::fabs(b.mRotation.back()[0] - b.mDuration) <= 1e-3f * std::max(1.0f, b.mDuration))
                ? 1
                : 0;
            EXPECT_GT(b.mTranslation.front()[0], 0.0f); // no row at time 0
            EXPECT_GT(b.mRotation.front()[0], 0.0f);
            for (const auto& r : b.mRotation)
            {
                ++rotations;
                const float n = r[1] * r[1] + r[2] * r[2] + r[3] * r[3] + r[4] * r[4];
                unit += std::fabs(n - 1.0f) < 2e-3f ? 1 : 0;
            }
            for (const auto& r : b.mTranslation)
            {
                ++rows;
                onGrid += std::fabs(r[0] * 30.0f - std::round(r[0] * 30.0f)) < 0.01f ? 1 : 0;
            }
        }
        for (const auto& c : project->second.mClips)
        {
            const MotionBlock* b = m.find(c.mCacheIndex);
            (b ? clipsWithBlock : clipsWithoutBlock) += 1;
            if (!b)
                continue;
            for (const auto& ev : c.mEvents)
            {
                ++speedChecked;
                scaledWithin
                    += eventAnimationTime(c, ev) <= b->mDuration * 1.001f + 0.01f && eventAnimationTime(c, ev) >= -0.01f
                    ? 1
                    : 0;
            }
        }
    }
    for (const auto& [name, p] : byName)
        for (const auto& c : p.mClips)
            withEvents += c.mEvents.empty() ? 0 : 1;
    std::cout << "[animdata] " << projects << " project files (" << withClips << " with clips), " << clips
              << " generators, " << events << " events; " << tables << " motion files, " << blocks << " blocks, "
              << lastIsDuration << " end at the duration, " << unit << "/" << rotations << " unit rotations, " << onGrid
              << "/" << rows << " translation times on the 30 Hz grid; generators with a block " << clipsWithBlock
              << ", without " << clipsWithoutBlock << "; generators with events " << withEvents
              << "; event times x speed within the duration " << scaledWithin << "/" << speedChecked << "\n";
    EXPECT_EQ(projects, 323u);
    EXPECT_EQ(withClips, 39u);
    EXPECT_EQ(tables, 39u);
    EXPECT_EQ(blocks, 5456u);
    EXPECT_EQ(clips, 8827u);
    EXPECT_EQ(lastIsDuration, blocks);
    EXPECT_EQ(unit, rotations);
    EXPECT_EQ(onGrid, rows);
    EXPECT_EQ(scaledWithin, speedChecked);
}

// Opt-in: also needs OPENEDEN_HAVOK_HKX_DIR (the extracted .hkx files). Counts how many clip files get root motion and
// events through linkClip.
TEST(HavokAnimDataRealData, how_many_clip_files_link_to_root_motion_and_events)
{
    const char* dir = std::getenv("OPENEDEN_HAVOK_ANIMDATA_DIR");
    const char* hkx = std::getenv("OPENEDEN_HAVOK_HKX_DIR");
    if (dir == nullptr || hkx == nullptr)
        GTEST_SKIP() << "OPENEDEN_HAVOK_ANIMDATA_DIR and OPENEDEN_HAVOK_HKX_DIR are needed";
    namespace fs = std::filesystem;
    auto lower = [](std::string s) {
        std::transform(
            s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };

    struct File
    {
        std::string mPath, mStem;
        float mDuration;
    };
    std::vector<File> files;
    std::set<std::string> all;
    for (const auto& e : fs::recursive_directory_iterator(hkx))
    {
        if (!e.is_regular_file())
            continue;
        std::string rel = lower(fs::relative(e.path(), hkx).generic_string());
        all.insert(rel);
        const std::string bytes = slurp(e.path());
        const std::vector<std::uint8_t> v(bytes.begin(), bytes.end());
        const auto clips = readClips(Packfile::parse(v));
        for (const auto& c : clips)
            files.push_back({ rel, fs::path(rel).stem().string(), c.mDuration });
    }

    // projects: the directory D holding "<D>/<first behaviour>" and the character file
    struct Project
    {
        std::string mDir;
        AnimationProject mProject;
        MotionTable mMotion;
    };
    std::vector<Project> projects;
    for (const auto& e : fs::directory_iterator(fs::path(dir) / "boundanims"))
    {
        const std::string name = e.path().filename().string().substr(6);
        Project p;
        p.mProject = readAnimationProject(slurp(fs::path(dir) / name));
        p.mMotion = readMotionTable(slurp(e.path()));
        const std::string behaviour = lower(p.mProject.mFiles.front());
        std::string character;
        for (const auto& f : p.mProject.mFiles)
            if (lower(f).rfind("characters", 0) == 0) // "Characters" or "Characters Female"
                character = lower(f);
        for (const auto& a : all)
        {
            if (a.size() <= behaviour.size() + 1
                || a.compare(a.size() - behaviour.size(), behaviour.size(), behaviour) != 0
                || a[a.size() - behaviour.size() - 1] != '/')
                continue;
            const std::string d = a.substr(0, a.size() - behaviour.size() - 1);
            if (all.count(d + "/" + character) || all.count(d + "/" + fs::path(character).filename().generic_string()))
                p.mDir = d;
        }
        ASSERT_FALSE(p.mDir.empty()) << name;
        projects.push_back(std::move(p));
    }

    std::size_t withMotion = 0, withEvents = 0, withEither = 0, outsideProjects = 0, sharedDirectory = 0;
    for (const auto& f : files)
    {
        // the projects whose directory is the longest prefix of the file (male and female share one directory)
        std::size_t longest = 0;
        for (const auto& p : projects)
            if (f.mPath.rfind(p.mDir + "/", 0) == 0)
                longest = std::max(longest, p.mDir.size());
        if (longest == 0)
        {
            ++outsideProjects;
            continue;
        }
        ClipLink best;
        std::size_t candidates = 0;
        for (const auto& p : projects)
        {
            if (p.mDir.size() != longest || f.mPath.rfind(p.mDir + "/", 0) != 0)
                continue;
            ++candidates;
            ClipLink l = linkClip(p.mProject, p.mMotion, f.mStem, f.mDuration);
            if (best.mClips.empty() || (best.mMotion == nullptr && l.mMotion != nullptr))
                best = std::move(l);
        }
        sharedDirectory += candidates > 1 ? 1 : 0;
        bool events = false;
        for (const auto* c : best.mClips)
            events = events || !c->mEvents.empty();
        withMotion += best.mMotion != nullptr ? 1 : 0;
        withEvents += events ? 1 : 0;
        withEither += (best.mMotion != nullptr || events) ? 1 : 0;
    }
    std::cout << "[animdata] clip files " << files.size() << " (" << outsideProjects
              << " outside every project's directory, " << sharedDirectory
              << " in a directory two projects share): root motion " << withMotion << ", events " << withEvents
              << ", either " << withEither << "\n";
    EXPECT_GT(withMotion, 1000u);
}
