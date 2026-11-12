#include <components/havokpack/animationdata.hpp>
#include <components/havokpack/export.hpp>
#include <components/havokpack/idle.hpp>
#include <components/havokpack/locomotion.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <array>
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
        const Weights w = weights(0.f);
        EXPECT_FLOAT_EQ(w.mIdle, 1.f);
        EXPECT_FLOAT_EQ(w.mWalk, 0.f);
        EXPECT_FLOAT_EQ(w.mRun, 0.f);
        EXPECT_FLOAT_EQ(w.mCyclesPerSecond, 0.f);
        EXPECT_FLOAT_EQ(weights(5.f).mIdle, 1.f); // the stop speed itself
    }

    TEST(HavokGaitWeights, the_idle_fades_out_linearly_between_stop_and_full_speed)
    {
        const Weights w = weights(17.5f); // halfway between 5 and 30
        EXPECT_NEAR(w.mIdle, 0.5f, 1e-6);
        EXPECT_NEAR(w.mWalk, 0.5f, 1e-6);
        EXPECT_NEAR(w.mRun, 0.f, 1e-6);
        EXPECT_NEAR(w.mCyclesPerSecond, 17.5f / 93.448f, 1e-5); // 0.187271
    }

    TEST(HavokGaitWeights, walk_speed_is_a_pure_walk_at_the_walk_stride)
    {
        const Weights w = weights(120.f);
        EXPECT_NEAR(w.mIdle, 0.f, 1e-6);
        EXPECT_NEAR(w.mWalk, 1.f, 1e-6);
        EXPECT_NEAR(w.mRun, 0.f, 1e-6);
        EXPECT_NEAR(w.mCyclesPerSecond, 120.f / 93.448f, 1e-5); // 1.284129
    }

    TEST(HavokGaitWeights, run_speed_is_a_pure_run_at_the_run_stride)
    {
        const Weights w = weights(280.f);
        EXPECT_NEAR(w.mWalk, 0.f, 1e-6);
        EXPECT_NEAR(w.mRun, 1.f, 1e-6);
        EXPECT_NEAR(w.mCyclesPerSecond, 280.f / 222.219f, 1e-5); // 1.260
    }

    TEST(HavokGaitWeights, halfway_between_walk_and_run_mixes_the_gaits_and_their_strides)
    {
        const Weights w = weights(200.f); // u = (200 - 120) / 160 = 0.5, stride = 157.8335
        EXPECT_NEAR(w.mWalk, 0.5f, 1e-6);
        EXPECT_NEAR(w.mRun, 0.5f, 1e-6);
        EXPECT_NEAR(w.mCyclesPerSecond, 200.f / 157.8335f, 1e-5); // 1.267177
    }

    TEST(HavokGaitWeights, faster_than_the_run_speed_stays_a_run_and_the_cycle_speeds_up)
    {
        const Weights w = weights(400.f);
        EXPECT_NEAR(w.mRun, 1.f, 1e-6);
        EXPECT_NEAR(w.mCyclesPerSecond, 400.f / 222.219f, 1e-5);
    }

    TEST(HavokGaitWeights, bad_speeds_idle)
    {
        for (float s : { -10.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
        {
            const Weights w = weights(s);
            EXPECT_FLOAT_EQ(w.mIdle, 1.f) << s;
            EXPECT_FLOAT_EQ(w.mCyclesPerSecond, 0.f) << s;
        }
    }

    TEST(HavokGaitWeights, the_weights_always_add_up_to_one_and_stay_in_range)
    {
        for (float s = 0.f; s < 600.f; s += 0.7f)
        {
            const Weights w = weights(s);
            EXPECT_NEAR(w.mIdle + w.mWalk + w.mRun, 1.f, 1e-5) << s;
            EXPECT_GE(w.mIdle, 0.f);
            EXPECT_GE(w.mWalk, 0.f);
            EXPECT_GE(w.mRun, 0.f);
        }
    }

    TEST(HavokGaitWeights, other_parameters_move_the_thresholds)
    {
        Params p;
        p.mWalkSpeed = 82.f;
        p.mRunSpeed = 350.f;
        const Weights w = weights(82.f, p);
        EXPECT_NEAR(w.mWalk, 1.f, 1e-6);
        EXPECT_NEAR(w.mCyclesPerSecond, 82.f / 93.448f, 1e-5);
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
            for (const auto& [name, stride, duration] : { std::tuple<const char*, float, float>{ "mt_walkforward",
                                                              defaults.mWalkStride, 1.13333f },
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
                std::cout << "GAITSLIP " << sex << ' ' << name << " ground speed " << speed << " units/s, contact steps "
                          << contact << " of " << 2 * steps << ", mean slip " << (contact ? slipSum / contact : -1.0)
                          << " of the speed, max " << slipMax << std::endl;
                ASSERT_GT(contact, 20);
            }
    }
}
