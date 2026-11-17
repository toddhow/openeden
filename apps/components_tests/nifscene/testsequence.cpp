#include <components/nifscene/build.hpp>
#include <components/nifscene/sequence.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "seqbuilder.hpp"

// Task 0232: controller sequences read from generated Skyrim SE files, and the pure sequence maths on hand-built
// scenes. Every expected number is worked out by hand in the test that uses it.
namespace
{
    using namespace NifScene;
    using namespace NifScene::Sequences;
    using namespace NifScene::Testing;

    constexpr VFS::Path::NormalizedView path("sequence-test.nif");

    Scene read(const GamebryoFile& file)
    {
        auto input = std::make_unique<std::istringstream>(file.build());
        input->exceptions(std::ios::failbit | std::ios::badbit);
        return readScene(std::move(input), path, SceneOptions{});
    }

    constexpr float s45 = 0.70710678f;

    // Root (0) > Hinge (1) at (10, 0, 0); the sequence "Open" (4) has a track on Hinge.
    GamebryoFile doorFile(bool withBadBlock)
    {
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "DoorRoot", { 1 }, {}, 1.f, "BSFadeNode"));
        file.mBlocks.push_back(node(file, "Hinge", {}, { 10.f, 0.f, 0.f }));
        file.mBlocks.push_back(transformData({ { 0.f, { 0, 0, 0, 1 } }, { 1.f, { 0, 0, s45, s45 } } },
            { { 0.f, { 10, 0, 0 } }, { 1.f, { 10, 0, 4 } } }));
        file.mBlocks.push_back(transformInterpolator(2));
        std::vector<ControlledSpec> blocks{ { "Hinge", 3 } };
        if (withBadBlock)
            blocks.push_back({ "Other", -1 });
        file.mBlocks.push_back(controllerSequence(file, "Open", 0.f, 1.f, 2.f, blocks));
        return file;
    }

    TEST(NifSequenceRead, a_file_with_no_sequence_has_none)
    {
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "Root", {}));
        EXPECT_TRUE(read(file).mSequences.empty());
    }

    TEST(NifSequenceRead, a_sequence_keeps_its_name_times_and_track)
    {
        const Scene scene = read(doorFile(false));
        ASSERT_EQ(scene.mSequences.size(), 1u);
        const Sequence& open = scene.mSequences[0];
        EXPECT_EQ(open.mName, "Open");
        EXPECT_FLOAT_EQ(open.mStart, 0.f);
        EXPECT_FLOAT_EQ(open.mStop, 1.f);
        EXPECT_FLOAT_EQ(open.mFrequency, 2.f);
        EXPECT_EQ(open.mCycleType, 2u);
        ASSERT_EQ(open.mTracks.size(), 1u);
        const Track& track = open.mTracks[0];
        EXPECT_EQ(track.mNodeName, "Hinge");
        ASSERT_EQ(track.mTranslations.size(), 2u);
        EXPECT_FLOAT_EQ(track.mTranslations[1].mValue[2], 4.f);
        ASSERT_EQ(track.mRotations.size(), 2u);
        EXPECT_NEAR(track.mRotations[1].mValue[2], s45, 1e-6); // z of the quaternion x y z w
        EXPECT_NEAR(track.mRotations[1].mValue[3], s45, 1e-6);
        EXPECT_TRUE(track.mScales.empty());
        EXPECT_EQ(open.mUnsupportedBlocks, 0u);
    }

    TEST(NifSequenceRead, a_controlled_block_without_a_transform_interpolator_is_counted_not_read)
    {
        const Scene scene = read(doorFile(true));
        ASSERT_EQ(scene.mSequences.size(), 1u);
        EXPECT_EQ(scene.mSequences[0].mTracks.size(), 1u);
        EXPECT_EQ(scene.mSequences[0].mUnsupportedBlocks, 1u);
    }

    TEST(NifSequenceRead, euler_rotation_keys_are_kept_per_axis)
    {
        GamebryoFile file;
        file.mBlocks.push_back(node(file, "DoorRoot", { 1 }, {}, 1.f, "BSFadeNode"));
        file.mBlocks.push_back(node(file, "Hinge", {}));
        using Keys = std::vector<std::array<float, 2>>;
        file.mBlocks.push_back(transformDataEuler({ Keys{}, Keys{}, Keys{ { 0.f, 0.f }, { 1.f, 1.5f } } }, {}));
        file.mBlocks.push_back(transformInterpolator(2));
        file.mBlocks.push_back(controllerSequence(file, "Open", 0.f, 1.f, 1.f, { { "Hinge", 3 } }));
        const Scene scene = read(file);
        ASSERT_EQ(scene.mSequences.size(), 1u);
        ASSERT_EQ(scene.mSequences[0].mTracks.size(), 1u);
        const Track& track = scene.mSequences[0].mTracks[0];
        EXPECT_TRUE(track.mRotations.empty());
        EXPECT_TRUE(track.mEuler[0].empty());
        ASSERT_EQ(track.mEuler[2].size(), 2u);
        EXPECT_FLOAT_EQ(track.mEuler[2][1].mValue, 1.5f);
        EXPECT_EQ(scene.mSequences[0].mUnsupportedBlocks, 0u);
    }

    // ---- the maths, on a scene built by hand

    Node makeNode(const std::string& name, std::uint32_t parent, Vec3 translation)
    {
        Node n;
        n.mName = name;
        n.mParent = parent;
        n.mTransform.mTranslation = translation;
        return n;
    }

    Track hingeTrack()
    {
        Track track;
        track.mNodeName = "Hinge";
        track.mHasDefault = true;
        track.mTranslations = { { 0.f, { 10, 0, 0 } }, { 1.f, { 10, 0, 4 } } };
        track.mRotations = { { 0.f, { 0, 0, 0, 1 } }, { 1.f, { 0, 0, s45, s45 } } };
        return track;
    }

    // Root (0) > Hinge (1) at (10, 0, 0) > Leaf (2) at (0, 0, 0)
    Scene handScene()
    {
        Scene scene;
        scene.mNodes.push_back(makeNode("Root", sNone, { 0, 0, 0 }));
        scene.mNodes.push_back(makeNode("Hinge", 0, { 10, 0, 0 }));
        scene.mNodes.push_back(makeNode("Leaf", 1, { 0, 0, 0 }));
        Sequence open;
        open.mName = "Open";
        open.mStart = 0.f;
        open.mStop = 1.f;
        open.mTracks.push_back(hingeTrack());
        scene.mSequences.push_back(open);
        return scene;
    }

    std::array<float, 3> applyTo(const Mat4& m, std::array<float, 3> p)
    {
        return { m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14] };
    }

    TEST(NifSequenceMath, a_transform_becomes_a_column_major_matrix)
    {
        Transform t;
        t.mTranslation = { 1, 2, 3 };
        t.mScale = 2.f;
        t.mRotation = { 0, -1, 0, 1, 0, 0, 0, 0, 1 }; // row-major: x goes to y (a quarter turn about z)
        const auto p = applyTo(toMat4(t), { 1, 0, 0 });
        EXPECT_NEAR(p[0], 1.f, 1e-5);
        EXPECT_NEAR(p[1], 4.f, 1e-5); // 2 * (0, 1, 0) + (1, 2, 3)
        EXPECT_NEAR(p[2], 3.f, 1e-5);
    }

    TEST(NifSequenceMath, the_affine_inverse_undoes_the_matrix)
    {
        Transform t;
        t.mTranslation = { 5, -3, 7 };
        t.mScale = 1.5f;
        t.mRotation = { 0, -1, 0, 1, 0, 0, 0, 0, 1 };
        const Mat4 m = toMat4(t);
        const auto back = applyTo(inverseAffine(m), applyTo(m, { 2, 4, 6 }));
        EXPECT_NEAR(back[0], 2.f, 1e-4);
        EXPECT_NEAR(back[1], 4.f, 1e-4);
        EXPECT_NEAR(back[2], 6.f, 1e-4);
    }

    TEST(NifSequenceMath, multiply_applies_the_right_matrix_first)
    {
        Transform shift;
        shift.mTranslation = { 1, 0, 0 };
        Transform turn;
        turn.mRotation = { 0, -1, 0, 1, 0, 0, 0, 0, 1 };
        const auto p = applyTo(multiply(toMat4(turn), toMat4(shift)), { 0, 0, 0 }); // shift, then turn
        EXPECT_NEAR(p[0], 0.f, 1e-5);
        EXPECT_NEAR(p[1], 1.f, 1e-5);
    }

    TEST(NifSequenceMath, find_ignores_case_and_misses_cleanly)
    {
        Scene scene = handScene();
        EXPECT_EQ(find(scene, "OPEN"), 0u);
        EXPECT_EQ(find(scene, "open"), 0u);
        EXPECT_EQ(find(scene, "Close"), sNoSequence);
    }

    TEST(NifSequenceMath, duration_and_time_follow_the_frequency_and_clamp)
    {
        Sequence s;
        s.mStart = 2.f;
        s.mStop = 4.f;
        s.mFrequency = 2.f;
        EXPECT_FLOAT_EQ(duration(s), 1.f);
        EXPECT_FLOAT_EQ(timeAt(s, 0.25f), 2.5f);
        EXPECT_FLOAT_EQ(timeAt(s, 5.f), 4.f);
        EXPECT_FLOAT_EQ(timeAt(s, -1.f), 2.f);
        s.mFrequency = 0.f;
        EXPECT_FLOAT_EQ(duration(s), 0.f);
    }

    TEST(NifSequenceMath, translation_is_linear_between_keys_and_held_outside_them)
    {
        const Track track = hingeTrack();
        Transform rest;
        EXPECT_NEAR(sampleLocal(track, rest, 0.5f).mTranslation[2], 2.f, 1e-5);
        EXPECT_NEAR(sampleLocal(track, rest, -3.f).mTranslation[2], 0.f, 1e-5);
        EXPECT_NEAR(sampleLocal(track, rest, 9.f).mTranslation[2], 4.f, 1e-5);
    }

    TEST(NifSequenceMath, rotation_at_half_time_is_half_the_angle)
    {
        Transform rest;
        const Transform half = sampleLocal(hingeTrack(), rest, 0.5f);
        // 45 degrees about z: x goes to (cos 45, sin 45)
        EXPECT_NEAR(half.mRotation[0], s45, 1e-4);
        EXPECT_NEAR(half.mRotation[3], s45, 1e-4);
        const Transform full = sampleLocal(hingeTrack(), rest, 1.f);
        EXPECT_NEAR(full.mRotation[0], 0.f, 1e-4);
        EXPECT_NEAR(full.mRotation[3], 1.f, 1e-4);
    }

    TEST(NifSequenceMath, a_channel_with_no_keys_takes_the_interpolators_value_else_the_rest)
    {
        Track track;
        track.mNodeName = "N";
        track.mHasDefault = true;
        track.mDefaultTranslation = { 1, 2, 3 };
        Transform rest;
        rest.mTranslation = { 9, 9, 9 };
        rest.mScale = 3.f;
        Transform out = sampleLocal(track, rest, 0.f);
        EXPECT_FLOAT_EQ(out.mTranslation[1], 2.f);
        EXPECT_FLOAT_EQ(out.mScale, 1.f); // the interpolator's default scale
        // The NIF's "no value" marker (a huge negative number) falls back to the rest pose.
        track.mDefaultTranslation = { -3.4e38f, -3.4e38f, -3.4e38f };
        track.mDefaultScale = -3.4e38f;
        out = sampleLocal(track, rest, 0.f);
        EXPECT_FLOAT_EQ(out.mTranslation[0], 9.f);
        EXPECT_FLOAT_EQ(out.mScale, 3.f);
        track.mHasDefault = false;
        EXPECT_FLOAT_EQ(sampleLocal(track, rest, 0.f).mTranslation[2], 9.f);
    }

    TEST(NifSequenceMath, a_quadratic_key_pair_follows_the_hermite_curve)
    {
        Track track;
        track.mTranslationInterpolation = 2;
        KeyVec3 first;
        first.mTime = 0.f;
        first.mOutTan = { 2, 0, 0 };
        KeyVec3 second;
        second.mTime = 1.f;
        second.mValue = { 1, 0, 0 };
        track.mTranslations = { first, second };
        // t = 0.25: b2 = 0.15625 times the end value 1, b3 = 0.140625 times the out tangent 2: 0.4375
        EXPECT_NEAR(sampleLocal(track, Transform{}, 0.25f).mTranslation[0], 0.4375f, 1e-5);
    }

    TEST(NifSequenceMath, euler_angles_turn_x_first_then_y_then_z)
    {
        Track track;
        KeyFloat quarter;
        quarter.mValue = 1.5707963f;
        track.mEuler[0] = { quarter }; // a quarter turn about x
        track.mEuler[2] = { quarter }; // then a quarter turn about z
        const Transform out = sampleLocal(track, Transform{}, 0.f);
        // (0, 1, 0) about x becomes (0, 0, 1); about z it stays (0, 0, 1)
        const std::array<float, 3> v{ 0.f, 1.f, 0.f };
        std::array<float, 3> r{};
        for (int row = 0; row < 3; ++row)
            r[row]
                = out.mRotation[row * 3] * v[0] + out.mRotation[row * 3 + 1] * v[1] + out.mRotation[row * 3 + 2] * v[2];
        EXPECT_NEAR(r[0], 0.f, 1e-5);
        EXPECT_NEAR(r[1], 0.f, 1e-5);
        EXPECT_NEAR(r[2], 1.f, 1e-5);
        // (1, 0, 0) is not moved by the turn about x, and the turn about z takes it to (0, 1, 0)
        EXPECT_NEAR(out.mRotation[3], 1.f, 1e-5);
    }

    TEST(NifSequenceMath, a_door_turning_about_z_by_euler_swings_like_the_quaternion_one)
    {
        Scene scene = handScene();
        Track& track = scene.mSequences[0].mTracks[0];
        track.mRotations.clear();
        KeyFloat start;
        KeyFloat end;
        end.mTime = 1.f;
        end.mValue = 1.5707963f;
        start.mValue = 0.f;
        track.mEuler[2] = { start, end };
        const auto p = applyTo(delta(scene, scene.mSequences[0], 1, 1.f), { 11, 0, 0 });
        EXPECT_NEAR(p[0], 10.f, 1e-4);
        EXPECT_NEAR(p[1], 1.f, 1e-4);
        EXPECT_NEAR(p[2], 4.f, 1e-4);
    }

    TEST(NifSequenceMath, a_constant_key_pair_holds_the_earlier_value)
    {
        Track track;
        track.mTranslationInterpolation = 5;
        track.mTranslations = { { 0.f, { 1, 0, 0 } }, { 1.f, { 7, 0, 0 } } };
        EXPECT_FLOAT_EQ(sampleLocal(track, Transform{}, 0.9f).mTranslation[0], 1.f);
    }

    TEST(NifSequenceMath, bad_key_values_never_reach_the_result)
    {
        Track track;
        track.mTranslations = { { 0.f, { 0, 0, 0 } }, { 1.f, { std::nanf(""), 0, 0 } } };
        Transform rest;
        const Transform out = sampleLocal(track, rest, 0.5f);
        EXPECT_TRUE(std::isfinite(out.mTranslation[0]));
    }

    TEST(NifSequenceMath, animated_nodes_are_the_named_nodes_without_repeats_or_misses)
    {
        Scene scene = handScene();
        Sequence& s = scene.mSequences[0];
        Track missing = hingeTrack();
        missing.mNodeName = "Nothing";
        s.mTracks.push_back(missing);
        s.mTracks.push_back(hingeTrack());
        const auto nodes = animatedNodes(scene, s);
        ASSERT_EQ(nodes.size(), 1u);
        EXPECT_EQ(nodes[0], 1u);
    }

    TEST(NifSequenceMath, the_delta_is_identity_at_the_rest_pose)
    {
        const Scene scene = handScene();
        const Mat4 d = delta(scene, scene.mSequences[0], 1, 0.f);
        const auto p = applyTo(d, { 11, 0, 0 });
        EXPECT_NEAR(p[0], 11.f, 1e-4);
        EXPECT_NEAR(p[1], 0.f, 1e-4);
        EXPECT_NEAR(p[2], 0.f, 1e-4);
    }

    TEST(NifSequenceMath, a_quarter_turn_swings_a_point_about_the_hinge_and_lifts_it)
    {
        const Scene scene = handScene();
        const Mat4 d = delta(scene, scene.mSequences[0], 1, 1.f);
        // A point 1 unit along x from the hinge (10, 0, 0) ends 1 unit along y from it, 4 units up.
        const auto p = applyTo(d, { 11, 0, 0 });
        EXPECT_NEAR(p[0], 10.f, 1e-4);
        EXPECT_NEAR(p[1], 1.f, 1e-4);
        EXPECT_NEAR(p[2], 4.f, 1e-4);
    }

    TEST(NifSequenceMath, a_child_of_an_animated_node_moves_with_it_through_the_whole_chain)
    {
        Scene scene = handScene();
        // The leaf has a track that carries no keys and no value: its delta equals the hinge's.
        Track still;
        still.mNodeName = "Leaf";
        scene.mSequences[0].mTracks.push_back(still);
        const auto p = applyTo(delta(scene, scene.mSequences[0], 2, 1.f), { 11, 0, 0 });
        EXPECT_NEAR(p[0], 10.f, 1e-4);
        EXPECT_NEAR(p[1], 1.f, 1e-4);
        EXPECT_NEAR(p[2], 4.f, 1e-4);
    }

    TEST(NifSequenceMath, rest_world_composes_the_parent_chain)
    {
        const Scene scene = handScene();
        const auto p = applyTo(restWorld(scene, 2), { 0, 0, 0 });
        EXPECT_NEAR(p[0], 10.f, 1e-5);
    }

    TEST(NifSequenceMath, a_bad_node_index_gives_identity)
    {
        const Scene scene = handScene();
        const Mat4 d = delta(scene, scene.mSequences[0], 99, 1.f);
        EXPECT_EQ(d, identity());
    }

    Scene doorAndCloseScene()
    {
        Scene scene = handScene();
        Sequence close = scene.mSequences[0];
        close.mName = "Close";
        close.mStart = 10.f;
        close.mStop = 12.f;
        scene.mSequences.push_back(close);
        return scene;
    }

    TEST(NifSequenceStep, a_lid_opens_in_the_given_time_and_stops_at_the_end)
    {
        float p = 0.f;
        for (int i = 0; i < 5; ++i)
            p = stepProgress(p, true, 0.1f, 0.5f);
        EXPECT_NEAR(p, 1.f, 1e-5);
        EXPECT_FLOAT_EQ(stepProgress(p, true, 10.f, 0.5f), 1.f);
        EXPECT_NEAR(stepProgress(1.f, false, 0.25f, 1.f), 0.75f, 1e-6);
    }

    TEST(NifSequenceStep, bad_numbers_change_nothing_or_jump_to_the_target)
    {
        EXPECT_FLOAT_EQ(stepProgress(0.4f, true, -1.f, 1.f), 0.4f);
        EXPECT_FLOAT_EQ(stepProgress(0.4f, true, std::nanf(""), 1.f), 0.4f);
        EXPECT_FLOAT_EQ(stepProgress(0.4f, true, 0.1f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(stepProgress(0.4f, false, 0.1f, -2.f), 0.f);
        EXPECT_FLOAT_EQ(stepProgress(std::nanf(""), true, 0.f, 1.f), 0.f);
    }

    TEST(NifSequencePose, opening_plays_the_open_sequence_forward)
    {
        const Scene scene = doorAndCloseScene();
        const Pose pose = poseFor(scene, 0, 1, 0.25f, false);
        EXPECT_EQ(pose.mSequence, 0u);
        EXPECT_FLOAT_EQ(pose.mTime, 0.25f);
    }

    TEST(NifSequencePose, closing_with_a_close_sequence_plays_it_from_its_first_pose)
    {
        const Scene scene = doorAndCloseScene();
        const Pose pose = poseFor(scene, 0, 1, 0.75f, true); // three quarters open: a quarter into Close
        EXPECT_EQ(pose.mSequence, 1u);
        EXPECT_FLOAT_EQ(pose.mTime, 10.f + 2.f * 0.25f);
    }

    TEST(NifSequencePose, closing_without_a_close_sequence_runs_the_open_one_backwards)
    {
        const Scene scene = handScene();
        const Pose pose = poseFor(scene, 0, sNoSequence, 0.5f, true);
        EXPECT_EQ(pose.mSequence, 0u);
        EXPECT_FLOAT_EQ(pose.mTime, 0.5f);
    }

    TEST(NifSequencePose, progress_is_clamped_and_a_missing_open_gives_no_pose)
    {
        const Scene scene = handScene();
        EXPECT_FLOAT_EQ(poseFor(scene, 0, sNoSequence, 4.f, false).mTime, 1.f);
        EXPECT_FLOAT_EQ(poseFor(scene, 0, sNoSequence, -4.f, false).mTime, 0.f);
        EXPECT_EQ(poseFor(scene, sNoSequence, sNoSequence, 0.5f, false).mSequence, sNoSequence);
    }
}
