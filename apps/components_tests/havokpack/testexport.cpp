#include <components/havokpack/export.hpp>

#include <gtest/gtest.h>

#include <cmath>

#include "splinegen.hpp"

// Task 0119: world transforms, pose assembly and the glTF writer. Expected values are worked out by hand.
namespace
{
    using namespace HavokPack;
    using namespace HavokPackTest;

    // Root at the origin, Spine 10 up the Z axis of the root, Head 5 further up the Spine's X axis.
    Skeleton makeSkeleton()
    {
        Skeleton s;
        s.mName = "Test";
        s.mBoneNames = { "Root", "Spine", "Head" };
        s.mParents = { -1, 0, 1 };
        s.mReferencePose.resize(3);
        s.mReferencePose[1].mTranslation = { 0, 0, 10 };
        s.mReferencePose[2].mTranslation = { 5, 0, 0 };
        return s;
    }

    Clip makeClip()
    {
        ClipDesc c;
        c.mNumFrames = 3;
        std::vector<TrackDesc> block(2);
        block[0].mPos = [] {
            VecDesc v;
            v.mAxis[0].mKind = VecDesc::Axis::Constant;
            v.mAxis[0].mValue = 1;
            return v;
        }();
        // track 1 drives the Spine: 90 degrees about z, constant
        block[1].mRot.mKind = RotDesc::Constant;
        block[1].mRot.mConstant = { 0, 0, 0.7071068f, 0.7071068f };
        c.mBlocks = { block };
        c.mTrackToBone = { 0, 1 };
        PackGen g;
        g.addObject("hkRootLevelContainer", 16);
        addSplineClip(g, c);
        const auto bytes = g.build();
        return readClips(Packfile::parse(bytes))[0];
    }
}

TEST(HavokExport, world_transforms_compose_parent_rotation_and_translation)
{
    const Skeleton s = makeSkeleton();
    std::vector<Transform> local = s.mReferencePose;
    local[1].mRotation = { 0, 0, 0.7071068f, 0.7071068f }; // 90 degrees about z
    const auto world = toWorld(s, local);
    EXPECT_NEAR(world[1].mTranslation[2], 10.0f, 1e-5f);
    // the Head's offset (5, 0, 0) is rotated by the Spine's 90 degrees about z into (0, 5, 0)
    EXPECT_NEAR(world[2].mTranslation[0], 0.0f, 1e-5f);
    EXPECT_NEAR(world[2].mTranslation[1], 5.0f, 1e-5f);
    EXPECT_NEAR(world[2].mTranslation[2], 10.0f, 1e-5f);
    EXPECT_NEAR(world[2].mRotation[2], 0.7071068f, 1e-5f);
    EXPECT_THROW(toWorld(s, std::span<const Transform>(local.data(), 2)), Error);
}

TEST(HavokExport, a_childs_rotation_is_applied_before_its_parents)
{
    const Skeleton s = makeSkeleton();
    std::vector<Transform> local = s.mReferencePose;
    local[1].mRotation = { 0, 0, 0.7071068f, 0.7071068f }; // parent: 90 degrees about z
    local[2].mRotation = { 0.7071068f, 0, 0, 0.7071068f }; // child: 90 degrees about x
    const auto world = toWorld(s, local);
    // parent * child by hand: (0.5, 0.5, 0.5, 0.5); child * parent would be (0.5, -0.5, 0.5, 0.5)
    EXPECT_NEAR(world[2].mRotation[0], 0.5f, 1e-5f);
    EXPECT_NEAR(world[2].mRotation[1], 0.5f, 1e-5f);
    EXPECT_NEAR(world[2].mRotation[2], 0.5f, 1e-5f);
    EXPECT_NEAR(world[2].mRotation[3], 0.5f, 1e-5f);
}

TEST(HavokExport, pose_at_writes_tracks_over_the_reference_pose)
{
    const Skeleton s = makeSkeleton();
    const Clip clip = makeClip();
    const auto pose = poseAt(s, clip, 0.0f);
    ASSERT_EQ(pose.size(), 3u);
    EXPECT_FLOAT_EQ(pose[0].mTranslation[0], 1.0f); // track 0
    EXPECT_NEAR(pose[1].mRotation[2], 0.7071068f, 2e-3f); // track 1 rotates the Spine
    EXPECT_FLOAT_EQ(pose[1].mTranslation[2], 0.0f); // track 1 stores no position: the track's default, not the reference
    EXPECT_FLOAT_EQ(pose[2].mTranslation[0], 5.0f); // no track: the reference pose
}

TEST(HavokExport, pose_at_refuses_a_track_for_a_missing_bone)
{
    Skeleton s = makeSkeleton();
    s.mBoneNames.resize(1);
    s.mParents.resize(1);
    s.mReferencePose.resize(1);
    EXPECT_THROW(poseAt(s, makeClip(), 0.0f), Error);
}

TEST(HavokExport, gltf_has_the_bones_the_animation_and_an_embedded_buffer)
{
    const Skeleton s = makeSkeleton();
    const Clip clip = makeClip();
    const std::string text = writeGltf(s, clip, "test_clip");
    auto has = [&](const std::string& part) { return text.find(part) != std::string::npos; };
    EXPECT_TRUE(has("\"version\":\"2.0\""));
    EXPECT_TRUE(has("\"name\":\"Root\""));
    EXPECT_TRUE(has("\"name\":\"Head\""));
    EXPECT_TRUE(has("\"children\":[1]"));
    EXPECT_TRUE(has("\"name\":\"test_clip\""));
    EXPECT_TRUE(has("\"path\":\"rotation\""));
    EXPECT_TRUE(has("\"path\":\"translation\""));
    EXPECT_TRUE(has("\"interpolation\":\"LINEAR\""));
    EXPECT_TRUE(has("data:application/octet-stream;base64,"));
    // the Spine's reference translation, as written
    EXPECT_TRUE(has("\"translation\":[0,0,10]"));
    // brackets and braces balance
    int curly = 0, square = 0;
    bool inString = false;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (ch == '"' && (i == 0 || text[i - 1] != '\\'))
            inString = !inString;
        if (inString)
            continue;
        curly += (ch == '{') - (ch == '}');
        square += (ch == '[') - (ch == ']');
        EXPECT_GE(curly, 0);
        EXPECT_GE(square, 0);
    }
    EXPECT_EQ(curly, 0);
    EXPECT_EQ(square, 0);
}

TEST(HavokExport, gltf_escapes_names)
{
    Skeleton s = makeSkeleton();
    s.mBoneNames[0] = "a\"b\\c";
    const std::string text = writeGltf(s, makeClip(), "n");
    EXPECT_NE(text.find("a\\\"b\\\\c"), std::string::npos);
}
