#include <components/havokpack/animation.hpp>
#include <components/havokpack/paired.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>

#include "splinegen.hpp"

// Task 0267: paired (two actor) clips. The expected values are worked out by hand in each test; the real-data test
// at the end is opt-in (OPENEDEN_PAIRED_ROOT).
namespace
{
    using namespace HavokPack;
    using namespace HavokPackTest;
    using namespace HavokPack::Paired;

    constexpr double Pi = std::numbers::pi;

    VecDesc constantVec(float x, float y, float z)
    {
        VecDesc v;
        const float val[3] = { x, y, z };
        for (int i = 0; i < 3; ++i)
        {
            v.mAxis[i].mKind = VecDesc::Axis::Constant;
            v.mAxis[i].mValue = val[i];
        }
        return v;
    }

    // Names the tracks through the animation's annotation tracks (24 bytes each, the name pointer first); the name
    // "<null>" leaves the pointer null.
    void nameTracks(PackGen& g, std::uint32_t anim, const std::vector<std::string>& names)
    {
        std::vector<std::uint8_t> entries(24 * names.size(), 0);
        const std::uint32_t at = g.addBytes(entries);
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] != "<null>")
                g.pointer(at + 24 * static_cast<std::uint32_t>(i), g.addCString(names[i]));
        g.array(anim + 40, at, static_cast<std::uint32_t>(names.size()));
    }

    Clip makeClip(const std::vector<TrackDesc>& tracks, const std::vector<std::string>& names)
    {
        ClipDesc c;
        c.mNumFrames = 31; // 30 intervals of 1/30 s: one second
        c.mBlocks = { tracks };
        PackGen g;
        g.addObject("hkRootLevelContainer", 16);
        const std::uint32_t anim = addSplineClip(g, c);
        if (!names.empty())
            nameTracks(g, anim, names);
        const auto bytes = g.build();
        return readClips(Packfile::parse(bytes))[0];
    }

    TrackDesc constantTrack(float x, float y, float z)
    {
        TrackDesc t;
        t.mPos = constantVec(x, y, z);
        return t;
    }

    std::vector<TrackDesc> mountTracks()
    {
        std::vector<TrackDesc> tracks(4); // PairedRoot, 2_Horse, NPC, NPC Root [Root]
        tracks[0] = constantTrack(0, 0, 0);
        tracks[1] = constantTrack(0, 0, 0);
        tracks[2] = constantTrack(10, 0, 0); // NPC: ten units along x, a quarter turn about z
        tracks[2].mRot.mKind = RotDesc::Constant;
        tracks[2].mRot.mConstant = { 0, 0, 0.7071068f, 0.7071068f };
        tracks[3].mPos = constantVec(1, 2, 0);
        tracks[3].mPos.mAxis[2].mKind = VecDesc::Axis::Spline; // z from 0 to 130 over the clip
        tracks[3].mPos.mAxis[2].mMin = 0;
        tracks[3].mPos.mAxis[2].mMax = 130;
        tracks[3].mPos.mAxis[2].mControl = { 0, 65535 };
        tracks[3].mPos.mKnots = { 0, 0, 30, 30 };
        return tracks;
    }

    const std::vector<std::string> mountNames = { "PairedRoot", "2_Horse", "NPC", "NPC Root [Root]" };
}

TEST(HavokPairedClassify, the_root_belongs_to_neither_actor)
{
    EXPECT_EQ(classifyTrack("PairedRoot").mSide, Side::Shared);
    EXPECT_EQ(classifyTrack("pairedroot").mSide, Side::Shared);
    EXPECT_EQ(classifyTrack("PAIREDROOT").mBone, "");
}

TEST(HavokPairedClassify, the_two_prefix_names_the_second_actor_and_is_stripped)
{
    const TrackRole role = classifyTrack("2_HorseLFemur");
    EXPECT_EQ(role.mSide, Side::Second);
    EXPECT_EQ(role.mBone, "horselfemur");
    EXPECT_EQ(classifyTrack("2_NPC Root [Root]").mBone, "npc root [root]");
}

TEST(HavokPairedClassify, every_other_name_is_the_first_actors_in_lower_case)
{
    const TrackRole role = classifyTrack("NPC Root [Root]");
    EXPECT_EQ(role.mSide, Side::First);
    EXPECT_EQ(role.mBone, "npc root [root]");
    EXPECT_EQ(classifyTrack("x2_y").mSide, Side::First);
    EXPECT_EQ(classifyTrack("2x").mSide, Side::First);
    EXPECT_EQ(classifyTrack("2x").mBone, "2x");
}

TEST(HavokPairedClassify, a_name_with_no_bone_in_it_drives_none)
{
    EXPECT_EQ(classifyTrack("2_").mSide, Side::Second);
    EXPECT_EQ(classifyTrack("2_").mBone, "");
    EXPECT_EQ(classifyTrack("").mSide, Side::First);
    EXPECT_EQ(classifyTrack("").mBone, "");
}

TEST(HavokPairedName, a_clip_name_round_trips_to_a_vfs_path)
{
    const std::string name = clipName(Side::First, "Actors\\SharedKillMoves/human&horse/paired_mount");
    EXPECT_EQ(name, "~1/actors/sharedkillmoves/human&horse/paired_mount");
    const auto ref = parseClipName(name);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->mSide, Side::First);
    EXPECT_EQ(ref->mPath, "meshes/actors/sharedkillmoves/human&horse/paired_mount.hkx");
    const auto second = parseClipName("~2/auxbones/tail/animations/horse_paired_mount");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->mSide, Side::Second);
}

TEST(HavokPairedName, an_extension_is_not_added_twice)
{
    const auto ref = parseClipName("~1/a/b.hkx");
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->mPath, "meshes/a/b.hkx");
}

TEST(HavokPairedName, names_that_cannot_be_loaded_are_refused)
{
    EXPECT_FALSE(parseClipName("").has_value());
    EXPECT_FALSE(parseClipName("~").has_value());
    EXPECT_FALSE(parseClipName("~1").has_value());
    EXPECT_FALSE(parseClipName("~1/").has_value());
    EXPECT_FALSE(parseClipName("~3/a").has_value());
    EXPECT_FALSE(parseClipName("~0/a").has_value());
    EXPECT_FALSE(parseClipName("1/a").has_value());
    EXPECT_FALSE(parseClipName("~1a").has_value());
    EXPECT_FALSE(parseClipName("~1/../a").has_value());
    EXPECT_FALSE(parseClipName("~1/a/../b").has_value());
    EXPECT_FALSE(parseClipName("~1//a").has_value());
    EXPECT_FALSE(parseClipName("~1/c:/a").has_value());
    EXPECT_FALSE(parseClipName("~1/" + std::string(241, 'a')).has_value());
    EXPECT_TRUE(parseClipName("~1/" + std::string(200, 'a')).has_value());
    EXPECT_EQ(clipName(Side::Shared, "a/b"), "");
    EXPECT_EQ(clipName(Side::First, "../a"), "");
}

TEST(HavokPairedName, a_plain_clip_name_is_not_a_paired_one)
{
    EXPECT_FALSE(parseClipName("horse_rider/idle").has_value());
    EXPECT_FALSE(parseClipName("mt_idle").has_value());
}

TEST(HavokPairedRead, the_annotation_tracks_name_the_transform_tracks)
{
    const Clip clip = makeClip(mountTracks(), mountNames);
    ASSERT_EQ(clip.mTrackNames.size(), 4u);
    EXPECT_EQ(clip.mTrackNames[0], "PairedRoot");
    EXPECT_EQ(clip.mTrackNames[3], "NPC Root [Root]");
}

TEST(HavokPairedRead, a_clip_with_no_annotation_tracks_has_no_names)
{
    EXPECT_TRUE(makeClip(mountTracks(), {}).mTrackNames.empty());
}

TEST(HavokPairedRead, a_count_that_is_not_the_track_count_gives_no_names)
{
    EXPECT_TRUE(makeClip(mountTracks(), { "a", "b", "c" }).mTrackNames.empty());
    EXPECT_TRUE(makeClip(mountTracks(), { "a", "b", "c", "d", "e" }).mTrackNames.empty());
}

TEST(HavokPairedRead, a_null_name_is_an_empty_one)
{
    const Clip clip = makeClip(mountTracks(), { "PairedRoot", "<null>", "NPC", "NPC Root [Root]" });
    ASSERT_EQ(clip.mTrackNames.size(), 4u);
    EXPECT_EQ(clip.mTrackNames[1], "");
    EXPECT_EQ(clip.mTrackNames[2], "NPC");
}

TEST(HavokPairedBind, each_side_gets_the_names_of_its_own_tracks_only)
{
    const Clip clip = makeClip(mountTracks(), mountNames);
    const BoundClip first = bindSide(clip, Side::First);
    ASSERT_EQ(first.mTrackBones.size(), 4u);
    EXPECT_EQ(first.mTrackBones[0], "");
    EXPECT_EQ(first.mTrackBones[1], "");
    EXPECT_EQ(first.mTrackBones[2], "npc");
    EXPECT_EQ(first.mTrackBones[3], "npc root [root]");
    const BoundClip second = bindSide(clip, Side::Second);
    EXPECT_EQ(second.mTrackBones[0], "");
    EXPECT_EQ(second.mTrackBones[1], "horse");
    EXPECT_EQ(second.mTrackBones[2], "");
    EXPECT_EQ(second.mTrackBones[3], "");
    const BoundClip shared = bindSide(clip, Side::Shared);
    EXPECT_EQ(shared.mTrackBones[0], "");
    EXPECT_EQ(shared.mClip.mNumTransformTracks, 4u);
}

TEST(HavokPairedBind, a_clip_with_no_names_is_not_a_paired_clip)
{
    const Clip clip = makeClip(mountTracks(), {});
    EXPECT_THROW((void)bindSide(clip, Side::First), Error);
}

TEST(HavokPairedDescribe, the_root_is_the_npc_track_composed_with_the_root_track)
{
    const auto d = describe(makeClip(mountTracks(), mountNames));
    ASSERT_TRUE(d.has_value());
    EXPECT_NEAR(d->mDuration, 1.0, 1e-4);
    // root (1, 2, 0) turned a quarter about z is (-2, 1, 0); plus the NPC track's (10, 0, 0)
    EXPECT_NEAR(d->mStart.mX, 8.0, 1e-3);
    EXPECT_NEAR(d->mStart.mY, 1.0, 1e-3);
    EXPECT_NEAR(d->mStart.mZ, 0.0, 1e-2);
    EXPECT_NEAR(d->mStart.mYaw, Pi / 2, 1e-3);
    EXPECT_NEAR(d->mEnd.mX, 8.0, 1e-3);
    EXPECT_NEAR(d->mEnd.mY, 1.0, 1e-3);
    EXPECT_NEAR(d->mEnd.mZ, 130.0, 1e-1);
    EXPECT_NEAR(d->mEnd.mYaw, Pi / 2, 1e-3);
}

TEST(HavokPairedDescribe, a_clip_with_only_the_root_track_uses_it_alone)
{
    std::vector<TrackDesc> tracks(2);
    tracks[0] = constantTrack(0, 0, 0);
    tracks[1] = constantTrack(3, -4, 5);
    tracks[1].mRot.mKind = RotDesc::Constant;
    tracks[1].mRot.mConstant = { 0, 0, -0.7071068f, 0.7071068f }; // a quarter turn the other way
    const auto d = describe(makeClip(tracks, { "PairedRoot", "NPC Root [Root]" }));
    ASSERT_TRUE(d.has_value());
    EXPECT_NEAR(d->mEnd.mX, 3.0, 1e-3);
    EXPECT_NEAR(d->mEnd.mY, -4.0, 1e-3);
    EXPECT_NEAR(d->mEnd.mZ, 5.0, 1e-3);
    EXPECT_NEAR(d->mEnd.mYaw, -Pi / 2, 1e-3);
}

TEST(HavokPairedDescribe, a_clip_with_no_root_track_describes_nothing)
{
    std::vector<TrackDesc> tracks(2);
    tracks[0] = constantTrack(0, 0, 0);
    tracks[1] = constantTrack(3, 4, 5);
    EXPECT_FALSE(describe(makeClip(tracks, { "PairedRoot", "NPC COM [COM ]" })).has_value());
}

TEST(HavokPairedDescribe, the_second_sides_tracks_are_not_the_first_actors_root)
{
    std::vector<TrackDesc> tracks(2);
    tracks[0] = constantTrack(0, 0, 0);
    tracks[1] = constantTrack(3, 4, 5);
    EXPECT_FALSE(describe(makeClip(tracks, { "PairedRoot", "2_NPC Root [Root]" })).has_value());
    EXPECT_FALSE(describe(makeClip(tracks, {})).has_value()); // a clip with no names
}

// Opt-in: OPENEDEN_PAIRED_ROOT names a folder with the extracted game files (it contains meshes/...). The numbers are
// the survey's (docs/tasks/0267-skyrim-paired-animations.md section 1).
TEST(HavokPairedRealData, the_mount_and_dismount_clips_split_and_end_where_the_survey_found)
{
    const char* root = std::getenv("OPENEDEN_PAIRED_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "OPENEDEN_PAIRED_ROOT is not set";
    auto load = [&](const std::string& rel) {
        std::ifstream in(std::string(root) + "/" + rel, std::ios::binary);
        EXPECT_TRUE(in.good()) << rel;
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return readClips(Packfile::parse(bytes)).at(0);
    };
    auto count = [](const BoundClip& b) {
        std::size_t n = 0;
        for (const std::string& bone : b.mTrackBones)
            n += bone.empty() ? 0 : 1;
        return n;
    };
    const Clip mount = load("meshes/actors/sharedkillmoves/human&horse/paired_mount.hkx");
    ASSERT_EQ(mount.mTrackNames.size(), 159u);
    const BoundClip first = bindSide(mount, Side::First);
    EXPECT_EQ(count(first), 101u);
    EXPECT_EQ(count(bindSide(mount, Side::Second)), 56u); // 57 tracks, one is only the prefix
    const auto d = describe(mount);
    ASSERT_TRUE(d.has_value());
    std::cout << "PAIRED mount start " << d->mStart.mX << " " << d->mStart.mY << " " << d->mStart.mZ << " yaw "
              << d->mStart.mYaw << "\n";
    std::cout << "PAIRED mount end " << d->mEnd.mX << " " << d->mEnd.mY << " " << d->mEnd.mZ << " yaw " << d->mEnd.mYaw
              << " duration " << d->mDuration << "\n";
    EXPECT_NEAR(d->mDuration, 1.66667, 1e-3);
    // The clips are in the horse's frame: the rider starts at its left flank (x to its right, y forward) turned a
    // quarter turn clockwise towards it, and ends on the saddle facing forward.
    EXPECT_NEAR(d->mStart.mX, -63.479, 0.05);
    EXPECT_NEAR(d->mStart.mY, 0.0, 0.05);
    EXPECT_NEAR(d->mStart.mZ, 0.0, 0.05);
    EXPECT_NEAR(d->mStart.mYaw, -Pi / 2, 0.01);
    EXPECT_NEAR(d->mEnd.mX, 1.6778, 0.05);
    EXPECT_NEAR(d->mEnd.mY, 4.1356, 0.05);
    EXPECT_NEAR(d->mEnd.mZ, 130.422, 0.05);
    EXPECT_NEAR(d->mEnd.mYaw, 0.0, 0.01);

    const Clip dismount = load("meshes/actors/sharedkillmoves/human&horse/paired_dismount.hkx");
    const auto e = describe(dismount);
    ASSERT_TRUE(e.has_value());
    std::cout << "PAIRED dismount start " << e->mStart.mX << " " << e->mStart.mY << " " << e->mStart.mZ << " yaw "
              << e->mStart.mYaw << "\n";
    std::cout << "PAIRED dismount end " << e->mEnd.mX << " " << e->mEnd.mY << " " << e->mEnd.mZ << " yaw "
              << e->mEnd.mYaw << " duration " << e->mDuration << "\n";
    EXPECT_NEAR(e->mDuration, 2.13333, 1e-3);
    EXPECT_NEAR(e->mStart.mX, 1.6737, 0.05);
    EXPECT_NEAR(e->mStart.mY, 4.417, 0.05);
    EXPECT_NEAR(e->mStart.mZ, 130.44, 0.05);
    EXPECT_NEAR(e->mEnd.mX, -111.604, 0.05);
    EXPECT_NEAR(e->mEnd.mY, 10.089, 0.05);
    EXPECT_NEAR(e->mEnd.mZ, 0.223, 0.05);
    EXPECT_NEAR(e->mEnd.mYaw, 0.0, 0.01);
}
