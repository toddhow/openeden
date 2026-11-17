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

// Task 0278: where the SECOND actor's own root node ("2_", no bone) is in a paired clip (a kill move's victim stands
// at it). Expected values are worked out by hand; the real-data survey at the end is opt-in (OPENEDEN_PAIRED_ROOT).
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

    void nameTracks(PackGen& g, std::uint32_t anim, const std::vector<std::string>& names)
    {
        std::vector<std::uint8_t> entries(24 * names.size(), 0);
        const std::uint32_t at = g.addBytes(entries);
        for (std::size_t i = 0; i < names.size(); ++i)
            g.pointer(at + 24 * static_cast<std::uint32_t>(i), g.addCString(names[i]));
        g.array(anim + 40, at, static_cast<std::uint32_t>(names.size()));
    }

    Clip makeClip(const std::vector<TrackDesc>& tracks, const std::vector<std::string>& names)
    {
        ClipDesc c;
        c.mNumFrames = 31;
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

    std::vector<std::uint8_t> readFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
}

TEST(HavokPairedSecond, the_victim_node_is_the_track_named_two_underscore_with_no_bone)
{
    std::vector<TrackDesc> tracks(4); // PairedRoot, 2_ (the victim's node), NPC Root [Root], 2_NPC Root [Root]
    tracks[0] = constantTrack(0, 0, 0);
    tracks[1] = constantTrack(3, 157, 1);
    tracks[1].mRot.mKind = RotDesc::Constant;
    tracks[1].mRot.mConstant = { 0, 0, 1.0f, 0.0f }; // a half turn about z
    tracks[2] = constantTrack(7, 8, 9);
    tracks[3] = constantTrack(-50, -200, 0);
    const auto d = describeSecond(makeClip(tracks, { "PairedRoot", "2_", "NPC Root [Root]", "2_NPC Root [Root]" }));
    ASSERT_TRUE(d.has_value());
    EXPECT_NEAR(d->mStart.mX, 3.0, 1e-4);
    EXPECT_NEAR(d->mStart.mY, 157.0, 1e-4);
    EXPECT_NEAR(d->mStart.mZ, 1.0, 1e-4);
    EXPECT_NEAR(std::fabs(d->mStart.mYaw), Pi, 1e-4);
    EXPECT_NEAR(d->mEnd.mY, 157.0, 1e-4);
    EXPECT_NEAR(d->mDuration, 1.0f, 1e-4f);
    // the first actor's description is not disturbed
    const auto first = describe(makeClip(tracks, { "PairedRoot", "2_", "NPC Root [Root]", "2_NPC Root [Root]" }));
    ASSERT_TRUE(first.has_value());
    EXPECT_NEAR(first->mStart.mX, 7.0, 1e-4);
}

TEST(HavokPairedSecond, a_clip_with_no_such_track_or_no_names_has_no_second_root)
{
    std::vector<TrackDesc> tracks(3);
    tracks[0] = constantTrack(0, 0, 0);
    tracks[1] = constantTrack(1, 1, 1);
    tracks[2] = constantTrack(2, 2, 2);
    EXPECT_FALSE(describeSecond(makeClip(tracks, { "PairedRoot", "NPC Root [Root]", "2_NPC Root [Root]" })).has_value())
        << "a bone track of the second actor is not its node";
    EXPECT_FALSE(describeSecond(makeClip(tracks, {})).has_value());
}

TEST(HavokPairedSecond, the_real_human_kill_move_places_the_victim_in_front_of_the_killer)
{
    const char* root = std::getenv("OPENEDEN_PAIRED_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "OPENEDEN_PAIRED_ROOT is not set";
    for (const char* f : { "meshes/actors/character/animations/paired_1hmkillmove.hkx",
             "meshes/actors/character/animations/paired_1hmkillmove_short_a.hkx",
             "meshes/actors/sharedkillmoves/human&bear/paired_1hmkillmovebeara.hkx",
             "meshes/actors/sharedkillmoves/human&horse/paired_mount.hkx" })
    {
        const auto bytes = readFile(std::string(root) + "/" + f);
        if (bytes.empty())
            continue;
        const Clip clip = readClips(Packfile::parse(bytes)).at(0);
        const auto first = describe(clip);
        const auto second = describeSecond(clip);
        std::cout << "KILLMOVE file " << f << " duration " << clip.mDuration << " tracks " << clip.mNumTransformTracks
                  << "\n";
        if (first)
            std::cout << "KILLMOVE   first start " << first->mStart.mX << " " << first->mStart.mY << " "
                      << first->mStart.mZ << " yaw " << first->mStart.mYaw << " end " << first->mEnd.mX << " "
                      << first->mEnd.mY << " " << first->mEnd.mZ << " yaw " << first->mEnd.mYaw << "\n";
        if (second)
            std::cout << "KILLMOVE   second start " << second->mStart.mX << " " << second->mStart.mY << " "
                      << second->mStart.mZ << " yaw " << second->mStart.mYaw << " end " << second->mEnd.mX << " "
                      << second->mEnd.mY << " " << second->mEnd.mZ << " yaw " << second->mEnd.mYaw << "\n";
        else
            std::cout << "KILLMOVE   no second root\n";
    }
}
