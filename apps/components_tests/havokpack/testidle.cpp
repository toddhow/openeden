#include <components/havokpack/idle.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "splinegen.hpp"

// Task 0132: bone-name binding, looping time and paths for the idle clip. Expected values are worked out by hand.
namespace
{
    using namespace HavokPack;
    using namespace HavokPackTest;

    Skeleton makeSkeleton()
    {
        Skeleton s;
        s.mName = "Test";
        s.mBoneNames = { "NPC Root [Root]", "NPC Spine [Spn0]", "Head" };
        s.mParents = { -1, 0, 1 };
        s.mReferencePose.resize(3);
        return s;
    }

    // two tracks: track 0 drives bone trackToBone[0] (constant x = 1), track 1 the next (90 degrees about z)
    Clip makeClip(std::vector<std::int16_t> trackToBone = { 2, 1 })
    {
        ClipDesc c;
        c.mNumFrames = 31; // 30 intervals of 1/30 s: one second
        std::vector<TrackDesc> block(2);
        block[0].mPos = [] {
            VecDesc v;
            v.mAxis[0].mKind = VecDesc::Axis::Constant;
            v.mAxis[0].mValue = 1;
            return v;
        }();
        block[1].mRot.mKind = RotDesc::Constant;
        block[1].mRot.mConstant = { 0, 0, 0.7071068f, 0.7071068f };
        c.mBlocks = { block };
        c.mTrackToBone = std::move(trackToBone);
        PackGen g;
        g.addObject("hkRootLevelContainer", 16);
        addSplineClip(g, c);
        const auto bytes = g.build();
        return readClips(Packfile::parse(bytes))[0];
    }
}

TEST(HavokIdlePaths, the_skeleton_hkx_replaces_the_extension)
{
    EXPECT_EQ(skeletonHkxPath("meshes/actors/character/character assets/skeleton.nif"),
        "meshes/actors/character/character assets/skeleton.hkx");
    EXPECT_EQ(skeletonHkxPath("Actors\\Character\\Character Assets Female\\Skeleton_Female.NIF"),
        "meshes/actors/character/character assets female/skeleton_female.hkx");
    EXPECT_EQ(skeletonHkxPath("meshes/a/skeleton.hkx"), "");
    EXPECT_EQ(skeletonHkxPath(""), "");
}

TEST(HavokIdlePaths, the_clip_is_in_the_animations_folder_of_the_actor_folder)
{
    EXPECT_EQ(clipHkxPath("meshes/actors/character/character assets/skeleton.nif"),
        "meshes/actors/character/animations/mt_idle.hkx");
    EXPECT_EQ(clipHkxPath("actors\\character\\character assets female\\skeleton_female.nif", "Idle"),
        "meshes/actors/character/animations/idle.hkx");
    EXPECT_EQ(clipHkxPath("skeleton.nif"), "");
    EXPECT_EQ(clipHkxPath("meshes/skeleton.nif"), "");
}

TEST(HavokIdleBind, every_track_gets_the_lower_case_name_of_its_bone)
{
    const BoundClip bound = bindClip(makeSkeleton(), makeClip());
    ASSERT_EQ(bound.mTrackBones.size(), 2u);
    EXPECT_EQ(bound.mTrackBones[0], "head");
    EXPECT_EQ(bound.mTrackBones[1], "npc spine [spn0]");
}

TEST(HavokIdleBind, a_track_naming_a_missing_bone_is_refused)
{
    EXPECT_THROW(bindClip(makeSkeleton(), makeClip({ 2, 3 })), Error);
    EXPECT_THROW(bindClip(makeSkeleton(), makeClip({ 2, -1 })), Error);
}

TEST(HavokIdleLoop, time_wraps_into_the_duration)
{
    EXPECT_FLOAT_EQ(loopTime(0.25f, 1.0f), 0.25f);
    EXPECT_NEAR(loopTime(2.75f, 1.0f), 0.75f, 1e-5f);
    EXPECT_NEAR(loopTime(-0.25f, 1.0f), 0.75f, 1e-5f);
    EXPECT_FLOAT_EQ(loopTime(1.0f, 1.0f), 0.0f);
}

TEST(HavokIdleLoop, a_bad_duration_or_time_gives_zero)
{
    EXPECT_FLOAT_EQ(loopTime(3.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(loopTime(3.0f, -1.0f), 0.0f);
    EXPECT_FLOAT_EQ(loopTime(std::numeric_limits<float>::quiet_NaN(), 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(loopTime(std::numeric_limits<float>::infinity(), 1.0f), 0.0f);
}

TEST(HavokIdleSample, a_time_past_the_end_samples_like_the_wrapped_time)
{
    const BoundClip bound = bindClip(makeSkeleton(), makeClip());
    ASSERT_GT(bound.mClip.mDuration, 0.0f);
    std::vector<Transform> a(2), b(2);
    samplePose(bound, 0.3f * bound.mClip.mDuration, a);
    samplePose(bound, 5.3f * bound.mClip.mDuration, b);
    for (int i = 0; i < 2; ++i)
        for (int k = 0; k < 4; ++k)
            EXPECT_NEAR(a[i].mRotation[k], b[i].mRotation[k], 1e-4f);
    // the constants of the tracks come through: track 0 position x 1, track 1 the 90 degree rotation
    EXPECT_NEAR(a[0].mTranslation[0], 1.0f, 1e-5f);
    EXPECT_NEAR(a[1].mRotation[2], 0.7071068f, 1e-4f);
}

TEST(HavokIdleSample, a_wrong_output_size_is_ignored_not_written)
{
    const BoundClip bound = bindClip(makeSkeleton(), makeClip());
    std::vector<Transform> out(1);
    out[0].mTranslation = { 9, 9, 9 };
    samplePose(bound, 0.1f, out);
    EXPECT_FLOAT_EQ(out[0].mTranslation[0], 9.0f);
}
