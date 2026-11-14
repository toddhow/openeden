#include <components/havokpack/animationdata.hpp>
#include <components/havokpack/export.hpp>
#include <components/havokpack/idle.hpp>
#include <components/havokpack/locomotion.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <tuple>

// Task 0148: the speed-to-blend and playback maths. Every expected value is worked out by hand from the defaults.
namespace
{
    using namespace HavokPack;
    using namespace HavokPack::Gait;

    TEST(HavokGaitWeights, standing_still_only_idles)
    {
        const Weights w = weights(0.f, 0.f);
        EXPECT_FLOAT_EQ(w.mIdle, 1.f);
        EXPECT_FLOAT_EQ(w.mWalk, 0.f);
        EXPECT_FLOAT_EQ(w.mRun, 0.f);
        EXPECT_FLOAT_EQ(w.mCyclesPerSecond, 0.f);
        EXPECT_FLOAT_EQ(weights(5.f, 0.f).mIdle, 1.f); // the stop speed itself
    }

    TEST(HavokGaitWeights, the_idle_fades_out_linearly_between_stop_and_full_speed)
    {
        const Weights w = weights(17.5f, 0.f); // halfway between 5 and 30
        EXPECT_NEAR(w.mIdle, 0.5f, 1e-6);
        EXPECT_NEAR(w.mWalk, 0.5f, 1e-6);
        EXPECT_NEAR(w.mRun, 0.f, 1e-6);
        EXPECT_NEAR(w.mCyclesPerSecond, 17.5f / 93.448f, 1e-5); // 0.187271
    }

    TEST(HavokGaitWeights, the_defaults_are_the_measured_clip_speeds)
    {
        const Params p;
        EXPECT_NEAR(p.mWalkSpeed, 82.45f, 0.01f); // 93.448 units in 1.13333 s
        EXPECT_NEAR(p.mRunSpeed, 350.87f, 0.01f); // 222.219 units in 0.633333 s
        EXPECT_NEAR(p.mWalkSpeed, p.mWalkStride / 1.13333f, 0.01f);
        EXPECT_NEAR(p.mRunSpeed, p.mRunStride / 0.633333f, 0.01f);
    }

    TEST(HavokGaitWeights, the_walk_speed_is_a_pure_walk_at_the_clips_own_rate)
    {
        const Weights w = weights(82.45f, 0.f);
        EXPECT_NEAR(w.mIdle, 0.f, 1e-6);
        EXPECT_NEAR(w.mWalk, 1.f, 1e-6);
        EXPECT_NEAR(w.mRun, 0.f, 1e-6);
        EXPECT_NEAR(w.mCyclesPerSecond, 82.45f / 93.448f, 1e-5); // 0.882; times the 1.13333 s clip: rate 1.0
        EXPECT_NEAR(w.mCyclesPerSecond * 1.13333f, 1.f, 2e-3);
    }

    TEST(HavokGaitWeights, the_run_speed_is_a_pure_run_at_the_clips_own_rate)
    {
        const Weights w = weights(350.87f, 1.f);
        EXPECT_NEAR(w.mWalk, 0.f, 1e-6);
        EXPECT_NEAR(w.mRun, 1.f, 1e-6);
        EXPECT_NEAR(w.mCyclesPerSecond, 350.87f / 222.219f, 1e-5); // 1.579; times 0.633333 s: rate 1.0
        EXPECT_NEAR(w.mCyclesPerSecond * 0.633333f, 1.f, 2e-3);
    }

    TEST(HavokGaitWeights, the_run_mix_splits_the_gait_weights_and_mixes_the_strides)
    {
        const Weights w = weights(200.f, 0.5f); // stride = (93.448 + 222.219) / 2 = 157.8335
        EXPECT_NEAR(w.mWalk, 0.5f, 1e-6);
        EXPECT_NEAR(w.mRun, 0.5f, 1e-6);
        EXPECT_NEAR(w.mIdle, 0.f, 1e-6);
        EXPECT_NEAR(w.mCyclesPerSecond, 200.f / 157.8335f, 1e-5); // 1.267177
    }

    TEST(HavokGaitWeights, the_speed_alone_never_mixes_the_gaits)
    {
        // whatever the speed, a mix of 0 is a walk and a mix of 1 a run: the choice is made by wantRun and the
        // cross-fade
        for (float s : { 40.f, 100.f, 200.f, 300.f, 500.f })
        {
            EXPECT_NEAR(weights(s, 0.f).mRun, 0.f, 1e-6) << s;
            EXPECT_NEAR(weights(s, 1.f).mWalk, 0.f, 1e-6) << s;
        }
    }

    TEST(HavokGaitWeights, bad_speeds_idle_and_a_bad_mix_is_clamped)
    {
        for (float s : { -10.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
        {
            const Weights w = weights(s, 0.f);
            EXPECT_FLOAT_EQ(w.mIdle, 1.f) << s;
            EXPECT_FLOAT_EQ(w.mCyclesPerSecond, 0.f) << s;
        }
        EXPECT_NEAR(weights(100.f, 7.f).mRun, 1.f, 1e-6);
        EXPECT_NEAR(weights(100.f, -3.f).mWalk, 1.f, 1e-6);
        EXPECT_NEAR(weights(100.f, std::numeric_limits<float>::quiet_NaN()).mWalk, 1.f, 1e-6);
    }

    TEST(HavokGaitWeights, the_weights_always_add_up_to_one_and_stay_in_range)
    {
        for (float mix : { 0.f, 0.3f, 1.f })
            for (float s = 0.f; s < 600.f; s += 0.7f)
            {
                const Weights w = weights(s, mix);
                EXPECT_NEAR(w.mIdle + w.mWalk + w.mRun, 1.f, 1e-5) << s;
                EXPECT_GE(w.mIdle, 0.f);
                EXPECT_GE(w.mWalk, 0.f);
                EXPECT_GE(w.mRun, 0.f);
            }
    }

    TEST(HavokGaitSwitch, the_switch_speed_is_half_way_between_the_clips_and_has_a_band)
    {
        const Params p; // switch (82.45 + 350.87) / 2 = 216.66; band 10 percent: 194.99 and 238.33
        EXPECT_FALSE(wantRun(82.45f, false, p));
        EXPECT_TRUE(wantRun(350.87f, false, p));
        EXPECT_TRUE(wantRun(350.87f, true, p));
        EXPECT_FALSE(wantRun(82.45f, true, p));
        EXPECT_FALSE(wantRun(238.0f, false, p)); // just under the upper edge: still a walk
        EXPECT_TRUE(wantRun(238.7f, false, p));
        EXPECT_TRUE(wantRun(195.5f, true, p)); // just over the lower edge: still a run
        EXPECT_FALSE(wantRun(194.5f, true, p));
    }

    TEST(HavokGaitSwitch, inside_the_band_the_gait_stays_as_it_was)
    {
        const Params p;
        for (float s : { 200.f, 216.f, 230.f })
        {
            EXPECT_FALSE(wantRun(s, false, p)) << s;
            EXPECT_TRUE(wantRun(s, true, p)) << s;
        }
    }

    TEST(HavokGaitSwitch, a_bad_speed_is_not_a_run)
    {
        const Params p;
        EXPECT_FALSE(wantRun(std::numeric_limits<float>::quiet_NaN(), true, p));
        EXPECT_FALSE(wantRun(-5.f, true, p));
    }

    TEST(HavokGaitSwitch, the_cross_fade_moves_the_mix_at_one_over_its_length)
    {
        EXPECT_NEAR(advanceRunMix(0.f, true, 0.1f, 0.2f), 0.5f, 1e-6);
        EXPECT_NEAR(advanceRunMix(1.f, false, 0.05f, 0.2f), 0.75f, 1e-6);
        EXPECT_FLOAT_EQ(advanceRunMix(0.9f, true, 0.2f, 0.2f), 1.f); // clamped
        EXPECT_FLOAT_EQ(advanceRunMix(0.1f, false, 0.5f, 0.2f), 0.f);
        EXPECT_FLOAT_EQ(advanceRunMix(0.3f, true, 0.f, 0.2f), 0.3f); // no time, no change
        EXPECT_FLOAT_EQ(advanceRunMix(0.3f, true, -1.f, 0.2f), 0.3f);
        EXPECT_FLOAT_EQ(advanceRunMix(0.3f, true, 0.01f, 0.f), 1.f); // no fade: at once
        EXPECT_FLOAT_EQ(advanceRunMix(0.3f, true, std::numeric_limits<float>::quiet_NaN(), 0.2f), 0.3f);
    }

    TEST(HavokGaitSwitch, the_default_cross_fade_is_a_fifth_of_a_second)
    {
        EXPECT_FLOAT_EQ(Params().mCrossFade, 0.2f);
    }

    TEST(HavokGaitSmooth, one_time_constant_covers_sixty_three_percent)
    {
        EXPECT_NEAR(smoothSpeed(0.f, 100.f, 0.12f, 0.12f), 63.2121f, 1e-3); // 1 - 1/e
        EXPECT_NEAR(smoothSpeed(100.f, 0.f, 0.24f, 0.12f), 100.f * std::exp(-2.f), 1e-3);
    }

    TEST(HavokGaitSmooth, degenerate_inputs)
    {
        EXPECT_FLOAT_EQ(smoothSpeed(7.f, 100.f, 0.f, 0.12f), 7.f);
        EXPECT_FLOAT_EQ(smoothSpeed(7.f, 100.f, -1.f, 0.12f), 7.f);
        EXPECT_FLOAT_EQ(smoothSpeed(7.f, 100.f, std::numeric_limits<float>::quiet_NaN(), 0.12f), 7.f);
        EXPECT_FLOAT_EQ(smoothSpeed(7.f, 100.f, 0.1f, 0.f), 100.f);
    }

    TEST(HavokGaitPhase, wraps_into_zero_to_one)
    {
        EXPECT_NEAR(advancePhase(0.9f, 2.f, 0.1f), 0.1f, 1e-6); // 0.9 + 0.2 = 1.1
        EXPECT_NEAR(advancePhase(0.f, 1.5f, 1.f), 0.5f, 1e-6);
        EXPECT_NEAR(advancePhase(0.25f, 0.f, 1.f), 0.25f, 1e-6);
    }

    TEST(HavokGaitPhase, bad_input_keeps_the_phase)
    {
        EXPECT_FLOAT_EQ(advancePhase(0.3f, -1.f, 0.1f), 0.3f);
        EXPECT_FLOAT_EQ(advancePhase(0.3f, 1.f, -0.1f), 0.3f);
        EXPECT_FLOAT_EQ(advancePhase(0.3f, std::numeric_limits<float>::quiet_NaN(), 0.1f), 0.3f);
        EXPECT_FLOAT_EQ(advancePhase(0.3f, 1.f, std::numeric_limits<float>::infinity()), 0.3f);
    }

    TEST(HavokGaitPhase, the_clip_time_puts_the_left_foot_at_phase_zero)
    {
        EXPECT_NEAR(clipTime(0.f, 0.2f, 2.f), 0.4f, 1e-6); // the foot lands 20 percent into a 2 s clip
        EXPECT_NEAR(clipTime(0.9f, 0.2f, 1.f), 0.1f, 1e-6); // 1.1 wraps
        EXPECT_NEAR(clipTime(0.5f, 0.f, 0.633333f), 0.3166665f, 1e-6);
        EXPECT_FLOAT_EQ(clipTime(0.5f, 0.2f, 0.f), 0.f);
    }

    Transform makeTransform(float tx, float rz, float s)
    {
        Transform t;
        t.mTranslation = { tx, 0.f, 0.f };
        t.mRotation = { 0.f, 0.f, std::sin(rz / 2.f), std::cos(rz / 2.f) };
        t.mScale = { s, s, s };
        return t;
    }

    TEST(HavokGaitBlend, the_ends_are_the_inputs)
    {
        const Transform a = makeTransform(1.f, 0.f, 1.f);
        const Transform b = makeTransform(5.f, 1.f, 3.f);
        const Transform r0 = blend(a, b, 0.f);
        const Transform r1 = blend(a, b, 1.f);
        EXPECT_FLOAT_EQ(r0.mTranslation[0], 1.f);
        EXPECT_FLOAT_EQ(r1.mTranslation[0], 5.f);
        EXPECT_FLOAT_EQ(r1.mScale[2], 3.f);
        EXPECT_NEAR(r1.mRotation[2], b.mRotation[2], 1e-6);
        EXPECT_FLOAT_EQ(blend(a, b, -4.f).mTranslation[0], 1.f); // clamped
        EXPECT_FLOAT_EQ(blend(a, b, 9.f).mTranslation[0], 5.f);
    }

    TEST(HavokGaitBlend, the_middle_of_identity_and_a_quarter_turn_is_an_eighth_of_a_turn)
    {
        const float quarter = 1.57079633f;
        const Transform r = blend(makeTransform(0.f, 0.f, 1.f), makeTransform(4.f, quarter, 3.f), 0.5f);
        EXPECT_NEAR(r.mTranslation[0], 2.f, 1e-6);
        EXPECT_NEAR(r.mScale[1], 2.f, 1e-6);
        EXPECT_NEAR(r.mRotation[2], std::sin(quarter / 4.f), 1e-6); // sin 22.5 degrees = 0.382683
        EXPECT_NEAR(r.mRotation[3], std::cos(quarter / 4.f), 1e-6);
    }

    TEST(HavokGaitBlend, the_shorter_arc_is_taken_and_the_result_is_a_unit_quaternion)
    {
        const Transform a = makeTransform(0.f, 0.f, 1.f);
        Transform b = makeTransform(0.f, 1.2f, 1.f);
        for (float& v : b.mRotation)
            v = -v; // the same rotation
        const Transform r = blend(a, b, 0.5f);
        const float len = std::sqrt(r.mRotation[0] * r.mRotation[0] + r.mRotation[1] * r.mRotation[1]
            + r.mRotation[2] * r.mRotation[2] + r.mRotation[3] * r.mRotation[3]);
        EXPECT_NEAR(len, 1.f, 1e-6);
        EXPECT_NEAR(std::abs(r.mRotation[2]), std::sin(0.3f), 1e-6); // half of 1.2 rad is 0.6; its half-angle is 0.3
        EXPECT_GT(r.mRotation[3], 0.f);
    }

    // Opt-in: OPENEDEN_HAVOK_ANIMDATA_DIR is the extracted meshes/animationdata folder of Skyrim - Animations.bsa.
    // Pins the stride constants in Gait::Params to the player's own root motion (task 0148).
    TEST(HavokGaitRealData, the_strides_are_the_root_motion_of_the_walk_and_run_clips)
    {
        const char* dir = std::getenv("OPENEDEN_HAVOK_ANIMDATA_DIR");
        if (dir == nullptr)
            GTEST_SKIP() << "OPENEDEN_HAVOK_ANIMDATA_DIR is not set";
        const auto read = [](const std::string& path) {
            std::ifstream in(path, std::ios::binary);
            EXPECT_TRUE(in.good()) << path;
            std::ostringstream s;
            s << in.rdbuf();
            return s.str();
        };
        const Params defaults;
        for (const char* sex : { "defaultmale", "defaultfemale" })
        {
            const AnimationProject project = readAnimationProject(read(std::string(dir) + "/" + sex + ".txt"));
            const MotionTable motion = readMotionTable(read(std::string(dir) + "/boundanims/anims_" + sex + ".txt"));
            for (const auto& [name, stride, duration] :
                { std::tuple<const char*, float, float>{ "mt_walkforward", defaults.mWalkStride, 1.13333f },
                    std::tuple<const char*, float, float>{ "mt_runforward", defaults.mRunStride, 0.633333f } })
            {
                const SidecarClip* clip = nullptr;
                for (const SidecarClip& c : project.mClips)
                    if (c.mName.size() == std::string_view(name).size()
                        && std::equal(c.mName.begin(), c.mName.end(), name,
                            [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == y; }))
                        clip = &c;
                ASSERT_NE(clip, nullptr) << sex << ' ' << name;
                const MotionBlock* block = motion.find(clip->mCacheIndex);
                ASSERT_NE(block, nullptr) << sex << ' ' << name;
                EXPECT_NEAR(block->mDuration, duration, 1e-3) << sex << ' ' << name;
                const auto end = rootTranslationAt(*block, block->mDuration);
                const float length = std::sqrt(end[0] * end[0] + end[1] * end[1] + end[2] * end[2]);
                EXPECT_NEAR(length, stride, 0.05f) << sex << ' ' << name;
                std::cout << "GAIT " << sex << ' ' << name << " duration " << block->mDuration << " displacement "
                          << length << " speed " << length / block->mDuration << " units/s" << std::endl;
            }
        }
    }

    // Opt-in: OPENEDEN_HAVOK_IDLE_ROOT is the extracted Skyrim - Animations.bsa plus the skeleton (as for
    // HavokIdleRealData). The slip measure of task 0148: while a foot is on the ground (its ankle within 2 units of the
    // lowest it reaches in the cycle) it should not move in the world, i.e. its speed along the walking direction
    // relative to the body should equal minus the ground speed the clip's root motion implies. The slip is the
    // difference as a share of that speed. A clip played at its stride-matched rate (what Gait does at any ground
    // speed) scales the foot and the ground speed alike, so the share does not depend on the ground speed.
    TEST(HavokGaitRealData, the_stance_foot_slip_of_the_walk_and_run_clips)
    {
        const char* root = std::getenv("OPENEDEN_HAVOK_IDLE_ROOT");
        if (root == nullptr)
            GTEST_SKIP() << "OPENEDEN_HAVOK_IDLE_ROOT is not set";
        const std::string nif = "meshes/actors/character/character assets/skeleton.nif";
        const auto read = [&](const std::string& rel) {
            std::ifstream in(std::string(root) + "/" + rel, std::ios::binary);
            EXPECT_TRUE(in.good()) << rel;
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        };
        const auto skeletons = readSkeletons(Packfile::parse(read(skeletonHkxPath(nif))));
        ASSERT_FALSE(skeletons.empty());
        const Skeleton& skeleton = skeletons[0];
        const auto boneIndex = [&](std::string_view prefix) {
            for (std::size_t i = 0; i < skeleton.mBoneNames.size(); ++i)
            {
                std::string n = skeleton.mBoneNames[i];
                for (char& c : n)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (n.rfind(prefix, 0) == 0)
                    return i;
            }
            return skeleton.mBoneNames.size();
        };
        const std::size_t feet[2] = { boneIndex("npc l foot"), boneIndex("npc r foot") };
        ASSERT_LT(feet[0], skeleton.mBoneNames.size());
        ASSERT_LT(feet[1], skeleton.mBoneNames.size());
        const Params defaults;
        for (const char* sex : { "male", "female" })
            for (const auto& [name, stride] : { std::pair<const char*, float>{ "mt_walkforward", defaults.mWalkStride },
                     std::pair<const char*, float>{ "mt_runforward", defaults.mRunStride } })
            {
                auto clips = readClips(Packfile::parse(read(clipHkxPath(nif, name, sex))));
                ASSERT_FALSE(clips.empty());
                const Clip& clip = clips[0];
                const float speed = stride / clip.mDuration; // the ground speed of the clip's root motion
                constexpr int steps = 400;
                const float dt = clip.mDuration / steps;
                std::vector<std::array<float, 3>> pos[2];
                for (int i = 0; i <= steps; ++i)
                {
                    const auto world = toWorld(skeleton, poseAt(skeleton, clip, dt * static_cast<float>(i)));
                    for (int f = 0; f < 2; ++f)
                        pos[f].push_back(world[feet[f]].mTranslation);
                }
                double slipSum = 0;
                double slipMax = 0;
                int contact = 0;
                for (int f = 0; f < 2; ++f)
                {
                    float low = 1e9f;
                    for (const auto& p : pos[f])
                        low = std::min(low, p[2]);
                    for (int i = 0; i < steps; ++i)
                    {
                        if (pos[f][i][2] > low + 2.f || pos[f][i + 1][2] > low + 2.f)
                            continue;
                        const double v = (pos[f][i + 1][1] - pos[f][i][1]) / dt; // body-relative, along +Y
                        const double slip = std::abs(v + speed) / speed;
                        slipSum += slip;
                        slipMax = std::max(slipMax, slip);
                        ++contact;
                    }
                }
                std::cout << "GAITSLIP " << sex << ' ' << name << " ground speed " << speed
                          << " units/s, contact steps " << contact << " of " << 2 * steps << ", mean slip "
                          << (contact ? slipSum / contact : -1.0) << " of the speed, max " << slipMax << std::endl;
                ASSERT_GT(contact, 20);
            }
    }
}
