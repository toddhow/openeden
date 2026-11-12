#include <components/havokpack/export.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

#include "splinegen.hpp"

// Task 0119 stage 7: the glTF writer carries a clip's sidecar data. Skeleton, clip and sidecar are generated here;
// expected values are worked out by hand.
namespace
{
    using namespace HavokPack;
    using namespace HavokPackTest;

    // Root at the origin; Spine 10 above it.
    Skeleton makeSkeleton()
    {
        Skeleton s;
        s.mName = "Test";
        s.mBoneNames = { "Root", "Spine" };
        s.mParents = { -1, 0 };
        s.mReferencePose.resize(2);
        s.mReferencePose[1].mTranslation = { 0, 0, 10 };
        return s;
    }

    // Three frames (0, 1/30, 2/30). Track 0 drives bone `bone` with a constant position (1, 0, 0).
    Clip makeClip(std::int16_t bone)
    {
        ClipDesc c;
        c.mNumFrames = 3;
        std::vector<TrackDesc> block(1);
        block[0].mPos.mAxis[0].mKind = VecDesc::Axis::Constant;
        block[0].mPos.mAxis[0].mValue = 1;
        block[0].mPos.mAxis[1].mKind = VecDesc::Axis::Constant;
        block[0].mPos.mAxis[1].mValue = 0;
        block[0].mPos.mAxis[2].mKind = VecDesc::Axis::Constant;
        block[0].mPos.mAxis[2].mValue = 0;
        c.mBlocks = { block };
        c.mTrackToBone = { bone };
        PackGen g;
        g.addObject("hkRootLevelContainer", 16);
        addSplineClip(g, c);
        return readClips(Packfile::parse(g.build()))[0];
    }

    AnimationProject makeProject()
    {
        AnimationProject p;
        SidecarClip a;
        a.mName = "Alpha \"one\"";
        a.mCacheIndex = 3;
        a.mPlaybackSpeed = 0.75f;
        a.mEvents = { { "StepRight", 0.5f }, { "End", 0.8f } };
        SidecarClip b;
        b.mName = "Beta";
        b.mCacheIndex = 3;
        b.mPlaybackSpeed = 1.0f;
        p.mClips = { a, b };
        return p;
    }

    // Duration of the clip is 2/30. y reaches 6 at the end, x 2; the rotation row is a quarter turn about z.
    MotionTable makeMotion()
    {
        MotionTable m;
        MotionBlock b;
        b.mCacheIndex = 3;
        b.mDuration = 2.0f / 30.0f;
        b.mTranslation = { { 2.0f / 30.0f, 2.0f, 6.0f, 0.0f } };
        b.mRotation = { { 2.0f / 30.0f, 0, 0, 0.70710678f, 0.70710678f } };
        m.mBlocks = { b };
        return m;
    }

    ClipLink fullLink(const AnimationProject& p, const MotionTable& m)
    {
        ClipLink l;
        l.mClips = { &p.mClips[0], &p.mClips[1] };
        l.mMotion = &m.mBlocks[0];
        return l;
    }

    std::vector<std::uint8_t> decodeBase64(const std::string& text)
    {
        const std::string marker = "base64,";
        const std::size_t at = text.find(marker);
        std::vector<std::uint8_t> out;
        if (at == std::string::npos)
            return out;
        std::uint32_t acc = 0;
        int bits = 0;
        for (std::size_t i = at + marker.size(); i < text.size() && text[i] != '"'; ++i)
        {
            const char ch = text[i];
            int v;
            if (ch >= 'A' && ch <= 'Z')
                v = ch - 'A';
            else if (ch >= 'a' && ch <= 'z')
                v = ch - 'a' + 26;
            else if (ch >= '0' && ch <= '9')
                v = ch - '0' + 52;
            else if (ch == '+')
                v = 62;
            else if (ch == '/')
                v = 63;
            else
                continue;
            acc = (acc << 6) | static_cast<std::uint32_t>(v);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
            }
        }
        return out;
    }

    // The floats of buffer view `index` (in the order the writer lists them).
    std::vector<float> viewFloats(const std::string& text, std::size_t index)
    {
        const auto bytes = decodeBase64(text);
        std::size_t at = text.find("\"bufferViews\":[");
        for (std::size_t i = 0; i <= index; ++i)
        {
            at = text.find("\"byteOffset\":", at + 1);
            if (at == std::string::npos)
                return {};
        }
        const std::size_t offset = std::stoul(text.substr(at + 13));
        const std::size_t length = std::stoul(text.substr(text.find("\"byteLength\":", at) + 13));
        std::vector<float> out(length / 4);
        std::memcpy(out.data(), bytes.data() + offset, length);
        return out;
    }

    std::size_t count(const std::string& text, const std::string& part)
    {
        std::size_t n = 0;
        for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
            ++n;
        return n;
    }

    bool balanced(const std::string& text)
    {
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
            if (curly < 0 || square < 0)
                return false;
        }
        return curly == 0 && square == 0;
    }
}

TEST(HavokExportMotion, root_motion_is_added_to_the_root_tracks_translation_channel)
{
    const Skeleton s = makeSkeleton();
    const Clip clip = makeClip(0); // track 0 drives the root
    const auto p = makeProject();
    const auto m = makeMotion();
    const std::string text = writeGltf(s, clip, "c", fullLink(p, m));
    EXPECT_TRUE(balanced(text));
    // one translation channel on node 0, not two
    EXPECT_EQ(count(text, "\"node\":0,\"path\":\"translation\""), 1u);
    // view 0 is the times, view 1 the root's translation: (1,0,0) plus the motion at frames 0, 1 and 2
    const auto v = viewFloats(text, 1);
    ASSERT_EQ(v.size(), 9u);
    EXPECT_NEAR(v[0], 1.0f, 1e-5f);
    EXPECT_NEAR(v[1], 0.0f, 1e-5f);
    EXPECT_NEAR(v[3], 1.0f + 1.0f, 1e-5f); // x: 2 * (1/2)
    EXPECT_NEAR(v[4], 3.0f, 1e-5f); // y: 6 * (1/2)
    EXPECT_NEAR(v[6], 1.0f + 2.0f, 1e-5f);
    EXPECT_NEAR(v[7], 6.0f, 1e-5f);
    EXPECT_NEAR(v[8], 0.0f, 1e-5f);
}

TEST(HavokExportMotion, a_root_without_a_track_gets_a_channel_on_top_of_its_reference_pose)
{
    Skeleton s = makeSkeleton();
    s.mReferencePose[0].mTranslation = { 0, 0, 4 };
    const Clip clip = makeClip(1); // the only track drives the spine
    const auto p = makeProject();
    const auto m = makeMotion();
    const std::string text = writeGltf(s, clip, "c", fullLink(p, m));
    EXPECT_TRUE(balanced(text));
    EXPECT_EQ(count(text, "\"node\":0,\"path\":\"translation\""), 1u);
    EXPECT_EQ(count(text, "\"node\":1,\"path\":\"translation\""), 1u);
    // the new channel's data is the last view: reference (0,0,4) plus the motion
    std::size_t last = count(text, "\"byteOffset\":") - 1;
    const auto v = viewFloats(text, last);
    ASSERT_EQ(v.size(), 9u);
    EXPECT_NEAR(v[0], 0.0f, 1e-5f);
    EXPECT_NEAR(v[2], 4.0f, 1e-5f);
    EXPECT_NEAR(v[3], 1.0f, 1e-5f);
    EXPECT_NEAR(v[4], 3.0f, 1e-5f);
    EXPECT_NEAR(v[6], 2.0f, 1e-5f);
    EXPECT_NEAR(v[7], 6.0f, 1e-5f);
    EXPECT_NEAR(v[8], 4.0f, 1e-5f);
}

TEST(HavokExportMotion, the_rotation_rows_are_not_applied)
{
    const Skeleton s = makeSkeleton();
    const Clip clip = makeClip(0);
    const auto p = makeProject();
    const auto m = makeMotion();
    const std::string text = writeGltf(s, clip, "c", fullLink(p, m));
    const auto q = viewFloats(text, 2); // the root track's rotation
    ASSERT_EQ(q.size(), 12u);
    for (std::size_t f = 0; f < 3; ++f)
    {
        EXPECT_FLOAT_EQ(q[4 * f + 2], 0.0f);
        EXPECT_FLOAT_EQ(q[4 * f + 3], 1.0f);
    }
    EXPECT_NE(text.find("\"rotationApplied\":false"), std::string::npos);
}

TEST(HavokExportMotion, events_and_generators_go_to_the_animations_extras)
{
    const Skeleton s = makeSkeleton();
    const Clip clip = makeClip(0);
    const auto p = makeProject();
    const auto m = makeMotion();
    const std::string text = writeGltf(s, clip, "c", fullLink(p, m));
    auto has = [&](const std::string& part) { return text.find(part) != std::string::npos; };
    EXPECT_TRUE(has("\"extras\":{\"sidecar\":{\"generators\":["));
    EXPECT_TRUE(has("\"name\":\"Alpha \\\"one\\\"\",\"cacheIndex\":3,\"playbackSpeed\":0.75,\"events\":["));
    // stored time 0.5 at speed 0.75 is 0.375 inside the animation
    EXPECT_TRUE(has("{\"name\":\"StepRight\",\"time\":0.5,\"animationTime\":0.375}"));
    EXPECT_TRUE(has("{\"name\":\"End\",\"time\":0.800000012,\"animationTime\":0.600000024}"));
    EXPECT_TRUE(has("\"name\":\"Beta\",\"cacheIndex\":3,\"playbackSpeed\":1,\"events\":[]"));
    EXPECT_TRUE(
        has("\"rootMotion\":{\"cacheIndex\":3,\"duration\":0.0666666701,\"translationRows\":1,\"rotationRows\":1,"
            "\"rotationApplied\":false}"));
    EXPECT_TRUE(balanced(text));
}

TEST(HavokExportMotion, events_without_motion_leave_the_translation_alone)
{
    const Skeleton s = makeSkeleton();
    const Clip clip = makeClip(0);
    const auto p = makeProject();
    ClipLink l;
    l.mClips = { &p.mClips[0] };
    const std::string text = writeGltf(s, clip, "c", l);
    EXPECT_EQ(text.find("rootMotion"), std::string::npos);
    EXPECT_NE(text.find("StepRight"), std::string::npos);
    const auto v = viewFloats(text, 1);
    EXPECT_NEAR(v[4], 0.0f, 1e-6f); // unchanged
    EXPECT_NEAR(v[7], 0.0f, 1e-6f);
}

TEST(HavokExportMotion, an_empty_link_gives_exactly_the_plain_output)
{
    const Skeleton s = makeSkeleton();
    const Clip clip = makeClip(0);
    EXPECT_EQ(writeGltf(s, clip, "c", ClipLink{}), writeGltf(s, clip, "c"));
}

TEST(HavokExportMotion, motion_alone_adds_a_rootMotion_note_and_no_generators)
{
    const Skeleton s = makeSkeleton();
    const Clip clip = makeClip(0);
    const auto m = makeMotion();
    ClipLink l;
    l.mMotion = &m.mBlocks[0];
    const std::string text = writeGltf(s, clip, "c", l);
    EXPECT_NE(text.find("\"generators\":[]"), std::string::npos);
    EXPECT_NE(text.find("\"rootMotion\":{"), std::string::npos);
    EXPECT_TRUE(balanced(text));
}
