#include <components/havokpack/animation.hpp>
#include <components/havokpack/skeleton.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>

#include "splinegen.hpp"

// Task 0119: skeletons, 40-bit quaternions and spline compressed clips, on files our own encoder writes. Expected
// values are worked out by hand in each test.
namespace
{
    using namespace HavokPack;
    using namespace HavokPackTest;

    constexpr float Eps = 1e-3f;

    template <class F>
    void expectError(F&& f, const std::string& part = {})
    {
        try
        {
            f();
            FAIL() << "no error";
        }
        catch (const Error& e)
        {
            if (!part.empty())
            {
                EXPECT_NE(std::string(e.what()).find(part), std::string::npos) << e.what();
            }
        }
    }

    VecDesc constantVec(float x, float y, float z, std::uint8_t axes = 7)
    {
        VecDesc v;
        const float val[3] = { x, y, z };
        for (int i = 0; i < 3; ++i)
            if ((axes >> i) & 1)
            {
                v.mAxis[i].mKind = VecDesc::Axis::Constant;
                v.mAxis[i].mValue = val[i];
            }
        return v;
    }

    // axis `axis` animated from lo to hi with the given 16-bit control values
    void animate(VecDesc& v, int axis, float lo, float hi, std::vector<std::uint16_t> control)
    {
        v.mAxis[axis].mKind = VecDesc::Axis::Spline;
        v.mAxis[axis].mMin = lo;
        v.mAxis[axis].mMax = hi;
        v.mAxis[axis].mControl = std::move(control);
    }

    std::vector<std::uint8_t> clipFile(const ClipDesc& c)
    {
        PackGen g;
        g.addObject("hkRootLevelContainer", 16);
        addSplineClip(g, c);
        return g.build();
    }
}

TEST(HavokQuaternion40, decodes_the_identity_and_each_dropped_component)
{
    const std::array<std::array<float, 4>, 6> cases = { { { 0, 0, 0, 1 }, { 0, 0, 0, -1 }, { 1, 0, 0, 0 }, { 0, 1, 0, 0 },
        { 0.5f, 0.5f, 0.5f, 0.5f }, { -0.1f, 0.2f, -0.3f, -0.927362f } } };
    for (const auto& q : cases)
    {
        const auto packed = packQuaternion40(q);
        const auto back = unpackQuaternion40(packed.data());
        for (int i = 0; i < 4; ++i)
            EXPECT_NEAR(back[i], q[i], 2e-3f) << "component " << i;
    }
}

TEST(HavokQuaternion40, the_bytes_of_the_identity_are_known_by_hand)
{
    // Dropped component w (index 3), positive: x = y = z = 2047 in 12 bits each, index bits 11 at 36..37 -> 3.
    const std::uint64_t v = 0x7FFull | (0x7FFull << 12) | (0x7FFull << 24) | (3ull << 36);
    std::uint8_t b[5];
    for (int i = 0; i < 5; ++i)
        b[i] = static_cast<std::uint8_t>(v >> (8 * i));
    const auto q = unpackQuaternion40(b);
    EXPECT_FLOAT_EQ(q[0], 0.0f);
    EXPECT_FLOAT_EQ(q[1], 0.0f);
    EXPECT_FLOAT_EQ(q[2], 0.0f);
    EXPECT_FLOAT_EQ(q[3], 1.0f);
    // the sign bit (38) negates the dropped component
    b[4] |= 0x40;
    EXPECT_FLOAT_EQ(unpackQuaternion40(b)[3], -1.0f);
}

TEST(HavokSkeleton, reads_names_parents_and_pose)
{
    PackGen g;
    g.addObject("hkRootLevelContainer", 16);
    SkeletonDesc s;
    s.mName = "Root";
    s.mBones = { "Root", "Spine", "Head" };
    s.mParents = { -1, 0, 1 };
    s.mPose = { { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }, { 0, 0, 10, 0, 0, 0.7071068f, 0.7071068f, 1, 1, 1 },
        { 1, 2, 3, 0, 0, 0, 1, 2, 2, 2 } };
    addSkeleton(g, s);
    const auto bytes = g.build();
    const Packfile p = Packfile::parse(bytes);
    const auto skeletons = readSkeletons(p);
    ASSERT_EQ(skeletons.size(), 1u);
    const Skeleton& k = skeletons[0];
    EXPECT_EQ(k.mName, "Root");
    ASSERT_EQ(k.mBoneNames.size(), 3u);
    EXPECT_EQ(k.mBoneNames[1], "Spine");
    EXPECT_EQ(k.mParents[2], 1);
    EXPECT_EQ(k.mParents[0], -1);
    EXPECT_FLOAT_EQ(k.mReferencePose[1].mTranslation[2], 10.0f);
    EXPECT_NEAR(k.mReferencePose[1].mRotation[2], 0.7071068f, 1e-6f);
    EXPECT_FLOAT_EQ(k.mReferencePose[2].mScale[1], 2.0f);
}

TEST(HavokSkeleton, refuses_a_parent_that_comes_later_and_a_non_unit_rotation)
{
    for (int mode = 0; mode < 2; ++mode)
    {
        PackGen g;
        g.addObject("hkRootLevelContainer", 16);
        SkeletonDesc s;
        s.mBones = { "A", "B" };
        s.mParents = { -1, 0 };
        s.mPose = { { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 } };
        if (mode == 0)
            s.mParents = { 1, 0 };
        else
            s.mPose[1][6] = 3.0f;
        addSkeleton(g, s);
        const auto bytes = g.build();
        const Packfile p = Packfile::parse(bytes);
        expectError([&] { readSkeletons(p); }, "hkaSkeleton");
    }
}

TEST(HavokSkeleton, caps_the_bone_count_and_checks_array_sizes_agree)
{
    PackGen g;
    g.addObject("hkRootLevelContainer", 16);
    SkeletonDesc s;
    s.mBones = { "A", "B" };
    s.mParents = { -1, 0 };
    s.mPose = { { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }, { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 } };
    const auto obj = addSkeleton(g, s);
    {
        const auto bytes = g.build();
        const Packfile p = Packfile::parse(bytes);
        SkeletonLimits l;
        l.mMaxBones = 1;
        expectError([&] { readSkeletons(p, l); }, "too many bones");
    }
    g.put32(obj + 40 + 8, 3); // bones array claims 3 while parents has 2
    const auto bytes = g.build();
    const Packfile p = Packfile::parse(bytes);
    expectError([&] { readSkeletons(p); }, "differ");
}

TEST(HavokSpline, a_constant_track_set_decodes_exactly)
{
    ClipDesc c;
    c.mNumFrames = 4;
    std::vector<TrackDesc> block(3);
    // track 0 stays identity; track 1: constant position x only and a constant rotation; track 2: constant scale
    block[1].mPos = constantVec(2.5f, 0, 0, 1);
    block[1].mRot.mKind = RotDesc::Constant;
    block[1].mRot.mConstant = { 0, 0, 0.7071068f, 0.7071068f };
    block[2].mScale = constantVec(2, 3, 4);
    block[2].mPos = constantVec(0, -1.5f, 7.25f, 6);
    c.mBlocks = { block };
    const auto bytes = clipFile(c);
    const Packfile p = Packfile::parse(bytes);
    const auto clips = readClips(p);
    ASSERT_EQ(clips.size(), 1u);
    const Clip& clip = clips[0];
    EXPECT_EQ(clip.mNumTransformTracks, 3u);
    EXPECT_EQ(clip.mSkeletonName, "Skeleton");
    EXPECT_TRUE(clip.mTrackToBone.empty());
    std::vector<Transform> out(3);
    sample(clip, 0.05f, out);
    EXPECT_FLOAT_EQ(out[0].mTranslation[0], 0.0f);
    EXPECT_FLOAT_EQ(out[0].mRotation[3], 1.0f);
    EXPECT_FLOAT_EQ(out[0].mScale[0], 1.0f);
    EXPECT_FLOAT_EQ(out[1].mTranslation[0], 2.5f);
    EXPECT_FLOAT_EQ(out[1].mTranslation[1], 0.0f);
    EXPECT_NEAR(out[1].mRotation[2], 0.7071068f, 2e-3f);
    EXPECT_NEAR(out[1].mRotation[3], 0.7071068f, 2e-3f);
    EXPECT_FLOAT_EQ(out[2].mScale[2], 4.0f);
    EXPECT_FLOAT_EQ(out[2].mTranslation[1], -1.5f);
    EXPECT_FLOAT_EQ(out[2].mTranslation[2], 7.25f);
    // time is clamped to the clip
    std::vector<Transform> late(3);
    sample(clip, 100.0f, late);
    EXPECT_FLOAT_EQ(late[1].mTranslation[0], 2.5f);
}

TEST(HavokSpline, a_degree_one_spline_is_a_linear_blend_between_its_control_points)
{
    ClipDesc c;
    c.mNumFrames = 11; // frames 0..10, one block
    std::vector<TrackDesc> block(1);
    // x animated 10 -> 20 over frames 0..10 (control values 0 and 65535 between min 10 and max 20)
    animate(block[0].mPos, 0, 10, 20, { 0, 65535 });
    block[0].mPos.mKnots = { 0, 0, 10, 10 };
    // rotation 0 -> 90 degrees about z over the same frames
    block[0].mRot.mKind = RotDesc::Spline;
    block[0].mRot.mControl = { { 0, 0, 0, 1 }, { 0, 0, 0.7071068f, 0.7071068f } };
    block[0].mRot.mKnots = { 0, 0, 10, 10 };
    c.mBlocks = { block };
    const auto bytes = clipFile(c);
    const Packfile p = Packfile::parse(bytes);
    const Clip clip = readClips(p)[0];
    std::vector<Transform> out(1);
    sample(clip, 0.0f, out);
    EXPECT_NEAR(out[0].mTranslation[0], 10.0f, Eps);
    sample(clip, 5.0f / 30.0f, out); // frame 5, halfway
    EXPECT_NEAR(out[0].mTranslation[0], 15.0f, Eps);
    // halfway between 0 and 90 degrees, normalised: 45 degrees about z
    EXPECT_NEAR(out[0].mRotation[2], std::sin(0.3926991f), 3e-3f);
    EXPECT_NEAR(out[0].mRotation[3], std::cos(0.3926991f), 3e-3f);
    sample(clip, 10.0f / 30.0f, out);
    EXPECT_NEAR(out[0].mTranslation[0], 20.0f, Eps);
    EXPECT_NEAR(out[0].mRotation[2], 0.7071068f, 3e-3f);
}

TEST(HavokSpline, a_cubic_spline_through_equal_control_points_is_constant_and_symmetric_ones_are_symmetric)
{
    ClipDesc c;
    c.mNumFrames = 7;
    std::vector<TrackDesc> block(1);
    // y animated, degree 3, 5 control points 0, 0, 65535, 0, 0 with clamped knots: symmetric about frame 3
    animate(block[0].mPos, 1, 0, 6, { 0, 0, 65535, 0, 0 });
    block[0].mPos.mDegree = 3;
    block[0].mPos.mKnots = { 0, 0, 0, 0, 3, 6, 6, 6, 6 };
    c.mBlocks = { block };
    const Packfile p = Packfile::parse(clipFile(c));
    const Clip clip = readClips(p)[0];
    std::vector<Transform> a(1), b(1), mid(1);
    sample(clip, 2.0f / 30.0f, a);
    sample(clip, 4.0f / 30.0f, b);
    sample(clip, 3.0f / 30.0f, mid);
    EXPECT_NEAR(a[0].mTranslation[1], b[0].mTranslation[1], 1e-3f);
    EXPECT_GT(mid[0].mTranslation[1], a[0].mTranslation[1]);
    // Hand value by de Boor at u = 3 (the interior knot): 0.5 of the control value, so half of the range 6.
    EXPECT_NEAR(mid[0].mTranslation[1], 3.0f, 1e-3f);
    // x and z stay at their defaults
    EXPECT_FLOAT_EQ(mid[0].mTranslation[0], 0.0f);
    EXPECT_FLOAT_EQ(mid[0].mTranslation[2], 0.0f);
}

TEST(HavokSpline, constant_and_animated_axes_are_stored_axis_by_axis)
{
    // x constant 2.5, y animated 0..10, z constant -4: the stream is  2.5 | 0 10 | -4  (per axis, not grouped)
    ClipDesc c;
    c.mNumFrames = 3;
    std::vector<TrackDesc> block(1);
    block[0].mPos = constantVec(2.5f, 0, -4.0f, 5);
    animate(block[0].mPos, 1, 0, 10, { 0, 65535 });
    block[0].mPos.mKnots = { 0, 0, 2, 2 };
    c.mBlocks = { block };
    const Packfile p = Packfile::parse(clipFile(c));
    const Clip clip = readClips(p)[0];
    std::vector<Transform> out(1);
    sample(clip, 1.0f / 30.0f, out);
    EXPECT_FLOAT_EQ(out[0].mTranslation[0], 2.5f);
    EXPECT_NEAR(out[0].mTranslation[1], 5.0f, Eps);
    EXPECT_FLOAT_EQ(out[0].mTranslation[2], -4.0f);
}

TEST(HavokSpline, neighbouring_rotation_control_points_are_put_in_one_hemisphere)
{
    // q and -q are one rotation. Control points identity, then the same rotation with every sign flipped (the encoder
    // writes x y z of the dropped-largest form, so the second is stored as (0,0,-0.7071,-0.7071)): without the fix the
    // midpoint blend is near zero and normalises to junk; with it, the rotation stays the 90 degree turn.
    ClipDesc c;
    c.mNumFrames = 3;
    std::vector<TrackDesc> block(1);
    block[0].mRot.mKind = RotDesc::Spline;
    block[0].mRot.mControl = { { 0, 0, 0.7071068f, 0.7071068f }, { 0, 0, -0.7071068f, -0.7071068f } };
    block[0].mRot.mKnots = { 0, 0, 2, 2 };
    c.mBlocks = { block };
    const Packfile p = Packfile::parse(clipFile(c));
    const Clip clip = readClips(p)[0];
    std::vector<Transform> out(1);
    sample(clip, 1.0f / 30.0f, out);
    EXPECT_NEAR(std::fabs(out[0].mRotation[2]), 0.7071068f, 3e-3f);
    EXPECT_NEAR(std::fabs(out[0].mRotation[3]), 0.7071068f, 3e-3f);
}

TEST(HavokSpline, a_nan_range_or_constant_is_replaced_by_the_default_and_counted)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    ClipDesc c;
    c.mNumFrames = 3;
    std::vector<TrackDesc> block(2);
    animate(block[0].mPos, 0, nan, nan, { 0, 65535 });
    block[0].mPos.mKnots = { 0, 0, 2, 2 };
    block[1].mScale = constantVec(nan, 5, 5);
    c.mBlocks = { block };
    const Packfile p = Packfile::parse(clipFile(c));
    const Clip clip = readClips(p)[0];
    EXPECT_EQ(clip.mNonFiniteValues, 3u);
    std::vector<Transform> out(2);
    sample(clip, 1.0f / 30.0f, out);
    EXPECT_FLOAT_EQ(out[0].mTranslation[0], 0.0f);
    EXPECT_FLOAT_EQ(out[1].mScale[0], 1.0f);
    EXPECT_FLOAT_EQ(out[1].mScale[1], 5.0f);
}

TEST(HavokSpline, an_axis_marked_both_constant_and_animated_is_refused)
{
    ClipDesc c;
    c.mNumFrames = 3;
    std::vector<TrackDesc> block(1);
    block[0].mPos = constantVec(1, 0, 0, 1);
    c.mBlocks = { block };
    auto bytes = clipFile(c);
    const Packfile p = Packfile::parse(bytes);
    const auto clip = p.objectsOfClass("hkaSplineCompressedAnimation")[0];
    const auto data = p.arrayAt({ clip->mSection, clip->mOffset + 152 });
    bytes[static_cast<std::size_t>(p.sections()[2].mDataStart) + data.mData->mOffset + 1] = 0x11; // x constant and animated
    const Packfile q = Packfile::parse(bytes);
    expectError([&] { readClips(q); }, "both constant and animated");
}

TEST(HavokSpline, blocks_overlap_by_one_frame_and_each_has_its_own_tracks)
{
    ClipDesc c;
    c.mMaxFramesPerBlock = 4; // 3 frame intervals per block
    c.mNumFrames = 6; // frames 0..5: block 0 covers 0..3, block 1 covers 3..5
    std::vector<TrackDesc> b0(1), b1(1);
    b0[0].mPos = constantVec(1, 0, 0, 1);
    b1[0].mPos = constantVec(9, 0, 0, 1);
    c.mBlocks = { b0, b1 };
    const Packfile p = Packfile::parse(clipFile(c));
    const Clip clip = readClips(p)[0];
    ASSERT_EQ(clip.mBlocks.size(), 2u);
    std::vector<Transform> out(1);
    sample(clip, 2.0f / 30.0f, out);
    EXPECT_FLOAT_EQ(out[0].mTranslation[0], 1.0f);
    // the first frame of block 1 is frame 3 (the blocks overlap by that one frame)
    sample(clip, 3.0f / 30.0f, out);
    EXPECT_FLOAT_EQ(out[0].mTranslation[0], 9.0f);
    sample(clip, 4.0f / 30.0f, out);
    EXPECT_FLOAT_EQ(out[0].mTranslation[0], 9.0f);
    sample(clip, 5.0f / 30.0f, out);
    EXPECT_FLOAT_EQ(out[0].mTranslation[0], 9.0f);
}

TEST(HavokSpline, the_binding_maps_tracks_to_bones)
{
    ClipDesc c;
    c.mNumFrames = 2;
    c.mBlocks = { std::vector<TrackDesc>(2) };
    c.mTrackToBone = { 5, 3 };
    c.mSkeletonName = "Other";
    const Packfile p = Packfile::parse(clipFile(c));
    const Clip clip = readClips(p)[0];
    EXPECT_EQ(clip.boneOfTrack(0), 5u);
    EXPECT_EQ(clip.boneOfTrack(1), 3u);
    EXPECT_EQ(clip.mSkeletonName, "Other");

    c.mTrackToBone = { 5 }; // one entry for two tracks
    const Packfile q = Packfile::parse(clipFile(c));
    expectError([&] { readClips(q); }, "binding");
}

TEST(HavokSpline, with_float_tracks_the_mask_area_is_larger_than_four_bytes_a_track)
{
    ClipDesc c;
    c.mNumFrames = 2;
    c.mFloatTracks = 4;
    std::vector<TrackDesc> block(2);
    block[1].mPos = constantVec(7, 0, 0, 1);
    c.mBlocks = { block };
    const Packfile p = Packfile::parse(clipFile(c));
    const Clip clip = readClips(p)[0];
    EXPECT_EQ(clip.mNumFloatTracks, 4u);
    std::vector<Transform> out(2);
    sample(clip, 0, out);
    EXPECT_FLOAT_EQ(out[1].mTranslation[0], 7.0f);
}

TEST(HavokSpline, a_block_that_does_not_end_where_the_file_says_is_refused)
{
    ClipDesc c;
    c.mNumFrames = 2;
    std::vector<TrackDesc> block(1);
    block[0].mPos = constantVec(1, 2, 3);
    c.mBlocks = { block };
    PackGen g;
    g.addObject("hkRootLevelContainer", 16);
    const auto anim = addSplineClip(g, c);
    // find the float block offsets array (second array) and add 4
    const std::uint32_t slot = anim + 104;
    std::uint32_t arr;
    // the pointer target is the local fix-up; read the array through a parse
    const auto bytes = g.build();
    const Packfile p = Packfile::parse(bytes);
    const auto target = p.pointerAt({ 2, slot });
    ASSERT_TRUE(target.has_value());
    arr = target->mOffset;
    const std::uint32_t wrong = p.u32(*target) + 4;
    g.put32(arr, wrong);
    const auto bad = g.build();
    const Packfile q = Packfile::parse(bad);
    expectError([&] { readClips(q); }, "ends at");
}

TEST(HavokSpline, refuses_unsupported_quantisation_and_bad_headers)
{
    ClipDesc c;
    c.mNumFrames = 2;
    c.mBlocks = { std::vector<TrackDesc>(1) };
    {
        PackGen g;
        g.addObject("hkRootLevelContainer", 16);
        addSplineClip(g, c);
        const auto bytes = g.build();
        const Packfile p = Packfile::parse(bytes);
        // a quantisation byte other than 0x45 sits at the start of the data array
        const auto clip = p.objectsOfClass("hkaSplineCompressedAnimation")[0];
        const auto data = p.arrayAt({ clip->mSection, clip->mOffset + 152 });
        auto copy = bytes;
        const auto absolute = static_cast<std::size_t>(p.sections()[2].mDataStart) + data.mData->mOffset;
        copy[absolute] = 0x44;
        const Packfile q = Packfile::parse(copy);
        expectError([&] { readClips(q); }, "quantisation");
    }
    for (int what = 0; what < 5; ++what)
    {
        ClipDesc d = c;
        auto bytes = [&] {
            PackGen g;
            g.addObject("hkRootLevelContainer", 16);
            const auto anim = addSplineClip(g, d);
            switch (what)
            {
                case 0: g.put32(anim + 16, 4); break; // not a spline animation
                case 1: g.put32(anim + 24, 100000); break; // too many tracks
                case 2: g.put32(anim + 60, 0); break; // no blocks
                case 3: g.putF32(anim + 80, 0.0f); break; // frame duration zero
                default: g.put32(anim + 64, 1); break; // one frame per block
            }
            return g.build();
        }();
        const Packfile p = Packfile::parse(bytes);
        expectError([&] { readClips(p); });
    }
}

TEST(HavokSpline, refuses_bad_knots_and_degrees)
{
    for (int what = 0; what < 4; ++what)
    {
        ClipDesc c;
        c.mNumFrames = 11;
        std::vector<TrackDesc> block(1);
        animate(block[0].mPos, 0, 0, 1, { 0, 65535 });
        block[0].mPos.mKnots = { 0, 0, 10, 10 };
        if (what == 0)
            block[0].mPos.mKnots = { 0, 10, 0, 10 }; // out of order
        if (what == 1)
        {
            c.mMaxFramesPerBlock = 16;
            block[0].mPos.mKnots = { 0, 0, 200, 200 }; // beyond the block's frames
        }
        if (what == 2)
            block[0].mPos.mDegree = 9;
        if (what == 3)
            block[0].mPos.mDegree = 0;
        c.mBlocks = { block };
        const Packfile p = Packfile::parse(clipFile(c));
        expectError([&] { readClips(p); }, "spline");
    }
}

TEST(HavokSpline, survives_random_mutations_of_a_clip_file)
{
    ClipDesc c;
    c.mNumFrames = 11;
    std::vector<TrackDesc> block(3);
    animate(block[0].mPos, 0, 0, 1, { 0, 30000, 65535 });
    block[0].mPos.mKnots = { 0, 0, 5, 10, 10 };
    block[0].mRot.mKind = RotDesc::Spline;
    block[0].mRot.mControl = { { 0, 0, 0, 1 }, { 0, 0, 0.7071068f, 0.7071068f } };
    block[0].mRot.mKnots = { 0, 0, 10, 10 };
    block[1].mPos = constantVec(1, 2, 3);
    block[2].mScale = constantVec(1, 1, 2);
    c.mBlocks = { block };
    const auto bytes = clipFile(c);
    std::uint32_t state = 777;
    int refused = 0, accepted = 0;
    for (int i = 0; i < 30000; ++i)
    {
        state = state * 1664525u + 1013904223u;
        auto b = bytes;
        const int flips = 1 + (state >> 30);
        for (int k = 0; k < flips; ++k)
        {
            state = state * 1664525u + 1013904223u;
            b[(state >> 8) % b.size()] = static_cast<std::uint8_t>(state >> 24);
        }
        try
        {
            const Packfile p = Packfile::parse(b);
            const auto clips = readClips(p);
            ++accepted;
            for (const Clip& clip : clips)
            {
                std::vector<Transform> out(clip.mNumTransformTracks);
                for (float t : { 0.0f, 0.1f, 0.33f, 1000.0f })
                    sample(clip, t, out);
                for (const Transform& x : out)
                    for (float v : x.mRotation)
                        ASSERT_TRUE(std::isfinite(v));
            }
        }
        catch (const Error&)
        {
            ++refused;
        }
    }
    EXPECT_GT(refused, 0);
    EXPECT_GT(accepted, 0);
}

// Opt-in (the user's extracted files): every skeleton and every spline clip of a folder decodes, every block ends where
// the file says, and sampled rotations are unit length. Aggregates only.
TEST(HavokRealData, every_clip_of_the_folder_decodes)
{
    const char* dir = std::getenv("OPENEDEN_HAVOK_HKX_DIR");
    if (dir == nullptr)
        GTEST_SKIP() << "OPENEDEN_HAVOK_HKX_DIR is not set";
    std::size_t files = 0, clips = 0, skeletons = 0, failed = 0, blocks = 0, bones = 0;
    double worstNorm = 0;
    std::size_t nonFinite = 0;
    std::map<std::string, std::size_t> errors;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir))
    {
        if (!e.is_regular_file())
            continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        ++files;
        try
        {
            const Packfile p = Packfile::parse(bytes);
            for (const Skeleton& s : readSkeletons(p))
            {
                ++skeletons;
                bones += s.mBoneNames.size();
            }
            for (const Clip& clip : readClips(p))
            {
                ++clips;
                nonFinite += clip.mNonFiniteValues;
                blocks += clip.mBlocks.size();
                std::vector<Transform> out(clip.mNumTransformTracks);
                for (float t = 0; t <= clip.mDuration; t += clip.mDuration / 7.0f + 1e-6f)
                {
                    sample(clip, t, out);
                    for (const Transform& x : out)
                    {
                        double n = 0;
                        for (float v : x.mRotation)
                            n += v * v;
                        worstNorm = std::max(worstNorm, std::fabs(n - 1.0));
                    }
                }
            }
        }
        catch (const Error& ex)
        {
            ++failed;
            ++errors[std::string(ex.what()).substr(0, 60)];
        }
    }
    std::cout << "havok real data: files " << files << ", skeletons " << skeletons << " (" << bones << " bones), clips "
              << clips << " (" << blocks << " blocks), failed " << failed << ", non-finite floats replaced " << nonFinite << ", worst |q|^2-1 " << worstNorm << "\n";
    for (const auto& [text, count] : errors)
        std::cout << "  " << count << " x " << text << "\n";
    EXPECT_EQ(failed, 0u);
    EXPECT_LT(worstNorm, 1e-3);
}
