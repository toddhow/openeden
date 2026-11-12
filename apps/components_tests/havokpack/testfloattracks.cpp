#include <components/havokpack/animation.hpp>
#include <components/havokpack/skeleton.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>

#include "splinegen.hpp"

// Task 0119 stage 7: float tracks of hkaSplineCompressedAnimation, on files our own encoder writes. The layout (masks
// 0x03 and 0x12, data after the transform tracks, the block ending on a 16 byte boundary) is logged in
// docs/research/0054-provenance.md F37 to F43. Expected values are worked out by hand.
namespace
{
    using namespace HavokPack;
    using namespace HavokPackTest;

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

    FloatDesc constant(float v)
    {
        FloatDesc f;
        f.mKind = FloatDesc::Constant;
        f.mValue = v;
        return f;
    }

    // degree 1, three control points at frames 0, 5 and 10, between lo and hi: lo, hi, lo
    FloatDesc ramp(float lo, float hi)
    {
        FloatDesc f;
        f.mKind = FloatDesc::Spline;
        f.mMin = lo;
        f.mMax = hi;
        f.mControl = { 0, 65535, 0 };
        f.mDegree = 1;
        f.mKnots = { 0, 0, 5, 10, 10 };
        return f;
    }

    ClipDesc clipWith(std::vector<std::vector<FloatDesc>> floats, std::uint32_t frames = 11)
    {
        ClipDesc c;
        c.mNumFrames = frames;
        std::vector<TrackDesc> block(1);
        block[0].mPos.mAxis[0].mKind = VecDesc::Axis::Constant;
        block[0].mPos.mAxis[0].mValue = 7;
        c.mBlocks.assign(floats.size(), block);
        c.mFloats = std::move(floats);
        return c;
    }

    std::vector<std::uint8_t> fileOf(const ClipDesc& c)
    {
        PackGen g;
        g.addObject("hkRootLevelContainer", 16);
        addSplineClip(g, c);
        return g.build();
    }

    Clip decode(const ClipDesc& c)
    {
        return readClips(Packfile::parse(fileOf(c)))[0];
    }

    std::vector<float> floatsAt(const Clip& clip, float time)
    {
        std::vector<float> out(clip.mNumFloatTracks, -99.0f);
        sampleFloats(clip, time, out);
        return out;
    }
}

TEST(HavokFloatTracks, constants_are_read_and_sampled_at_any_time)
{
    const Clip clip = decode(clipWith({ { constant(1.0f), constant(0.25f), constant(0.0f) } }));
    ASSERT_EQ(clip.mNumFloatTracks, 3u);
    for (float t : { 0.0f, 0.1f, 1000.0f, -5.0f })
    {
        const auto v = floatsAt(clip, t);
        EXPECT_FLOAT_EQ(v[0], 1.0f);
        EXPECT_FLOAT_EQ(v[1], 0.25f);
        EXPECT_FLOAT_EQ(v[2], 0.0f);
    }
    EXPECT_EQ(clip.mNonFiniteValues, 0u);
    // the transform track is unaffected
    std::vector<Transform> out(1);
    sample(clip, 0, out);
    EXPECT_FLOAT_EQ(out[0].mTranslation[0], 7.0f);
}

TEST(HavokFloatTracks, a_spline_track_interpolates_between_min_and_max)
{
    const Clip clip = decode(clipWith({ { ramp(0.5f, 2.5f) } }));
    const float fd = clip.mFrameDuration;
    EXPECT_NEAR(floatsAt(clip, 0)[0], 0.5f, 1e-4f);
    EXPECT_NEAR(floatsAt(clip, 2.5f * fd)[0], 1.5f, 1e-3f); // halfway up
    EXPECT_NEAR(floatsAt(clip, 5 * fd)[0], 2.5f, 1e-4f);
    EXPECT_NEAR(floatsAt(clip, 7.5f * fd)[0], 1.5f, 1e-3f); // halfway down
    EXPECT_NEAR(floatsAt(clip, 10 * fd)[0], 0.5f, 1e-4f);
    EXPECT_NEAR(floatsAt(clip, 99.0f)[0], 0.5f, 1e-4f); // held after the end
}

TEST(HavokFloatTracks, constants_and_splines_keep_their_place_after_each_other)
{
    // spline data is 3 + 5 knot bytes, then 8 bytes of range and 6 of control points: the next track starts aligned
    const Clip clip = decode(clipWith({ { constant(3.0f), ramp(0.0f, 4.0f), constant(7.5f), ramp(1.0f, 2.0f) } }));
    const auto v = floatsAt(clip, 5 * clip.mFrameDuration);
    EXPECT_FLOAT_EQ(v[0], 3.0f);
    EXPECT_NEAR(v[1], 4.0f, 1e-4f);
    EXPECT_FLOAT_EQ(v[2], 7.5f);
    EXPECT_NEAR(v[3], 2.0f, 1e-4f);
}

TEST(HavokFloatTracks, each_block_has_its_own_floats)
{
    // 4 frames per block (3 frame steps), 7 frames: two blocks
    ClipDesc c = clipWith({ { constant(1.0f) }, { constant(0.0f) } }, 7);
    c.mMaxFramesPerBlock = 4;
    const Clip clip = decode(c);
    ASSERT_EQ(clip.mFloatBlocks.size(), 2u);
    const float fd = clip.mFrameDuration;
    EXPECT_FLOAT_EQ(floatsAt(clip, 1 * fd)[0], 1.0f);
    EXPECT_FLOAT_EQ(floatsAt(clip, 2.9f * fd)[0], 1.0f);
    EXPECT_FLOAT_EQ(floatsAt(clip, 4 * fd)[0], 0.0f);
    EXPECT_FLOAT_EQ(floatsAt(clip, 6 * fd)[0], 0.0f);
}

TEST(HavokFloatTracks, a_track_with_mask_zero_stores_nothing_and_is_zero)
{
    FloatDesc none;
    none.mKind = FloatDesc::Zero;
    const Clip clip = decode(clipWith({ { constant(2.0f), none, constant(5.0f) } }));
    const auto v = floatsAt(clip, 0);
    EXPECT_FLOAT_EQ(v[0], 2.0f);
    EXPECT_FLOAT_EQ(v[1], 0.0f);
    EXPECT_FLOAT_EQ(v[2], 5.0f);
}

TEST(HavokFloatTracks, float_tracks_map_to_slots_in_order_or_by_the_binding)
{
    ClipDesc c = clipWith({ { constant(1.0f), constant(0.0f), constant(1.0f) } });
    Clip clip = decode(c);
    EXPECT_TRUE(clip.mFloatTrackToSlot.empty());
    EXPECT_EQ(clip.slotOfFloatTrack(2), 2u);

    c.mFloatSlots = { 5, 2, 7 };
    clip = decode(c);
    EXPECT_EQ(clip.slotOfFloatTrack(0), 5u);
    EXPECT_EQ(clip.slotOfFloatTrack(1), 2u);
    EXPECT_EQ(clip.slotOfFloatTrack(2), 7u);

    c.mFloatSlots = { 5, 2 }; // two entries for three tracks
    expectError([&] { decode(c); }, "float");
    c.mFloatSlots = { 5, -1, 2 };
    expectError([&] { decode(c); }, "negative");
}

TEST(HavokFloatTracks, a_skeleton_names_its_float_slots)
{
    SkeletonDesc d;
    d.mBones = { "Root" };
    d.mParents = { -1 };
    d.mPose = { { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 } };
    PackGen plain;
    plain.addObject("hkRootLevelContainer", 16);
    addSkeleton(plain, d);
    EXPECT_TRUE(readSkeletons(Packfile::parse(plain.build()))[0].mFloatSlots.empty());

    d.mFloatSlots = { "hkFade:AnimObjectA", "hkVis:Weapon" };
    PackGen g;
    g.addObject("hkRootLevelContainer", 16);
    addSkeleton(g, d);
    const auto s = readSkeletons(Packfile::parse(g.build()))[0];
    ASSERT_EQ(s.mFloatSlots.size(), 2u);
    EXPECT_EQ(s.mFloatSlots[0], "hkFade:AnimObjectA");
    EXPECT_EQ(s.mFloatSlots[1], "hkVis:Weapon");

    SkeletonLimits l;
    l.mMaxFloatSlots = 1;
    expectError([&] { readSkeletons(Packfile::parse(g.build()), l); }, "float slots");
}

TEST(HavokFloatTracks, an_unknown_mask_is_refused)
{
    FloatDesc odd = constant(1.0f);
    odd.mRawMask = 0x55;
    expectError([&] { decode(clipWith({ { constant(1.0f), odd } })); }, "float track");
    odd.mRawMask = 0x13;
    expectError([&] { decode(clipWith({ { odd } })); }, "float track");
}

TEST(HavokFloatTracks, a_block_that_does_not_end_on_a_16_byte_boundary_is_refused)
{
    ClipDesc c = clipWith({ { constant(1.0f) } });
    c.mFloatTailExtra = 4;
    expectError([&] { decode(c); }, "16");
    c.mFloatTailExtra = 16; // another whole 16 bytes is still not where the data ends
    expectError([&] { decode(c); }, "16");
}

TEST(HavokFloatTracks, non_finite_floats_are_replaced_by_zero_and_counted)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    FloatDesc bad = ramp(nan, nan);
    const Clip clip = decode(clipWith({ { constant(nan), bad } }));
    const auto v = floatsAt(clip, 5 * clip.mFrameDuration);
    EXPECT_FLOAT_EQ(v[0], 0.0f);
    EXPECT_FLOAT_EQ(v[1], 0.0f);
    EXPECT_EQ(clip.mNonFiniteValues, 3u);
}

TEST(HavokFloatTracks, a_hostile_float_clip_is_a_value_or_an_error_30000_times)
{
    const std::vector<std::uint8_t> base = fileOf(clipWith(
        { { constant(3.0f), ramp(0.0f, 4.0f), constant(1.0f) }, { constant(0.0f), ramp(1.0f, 2.0f), constant(5.0f) } },
        11));
    std::mt19937 rng(4711);
    std::size_t accepted = 0;
    for (int i = 0; i < 30000; ++i)
    {
        std::vector<std::uint8_t> bytes = base;
        const int edits = 1 + static_cast<int>(rng() % 3);
        for (int e = 0; e < edits; ++e)
            bytes[rng() % bytes.size()] = static_cast<std::uint8_t>(rng());
        try
        {
            const auto clips = readClips(Packfile::parse(bytes));
            for (const Clip& c : clips)
            {
                std::vector<float> out(c.mNumFloatTracks);
                for (float t : { 0.0f, 0.05f, 0.2f, 10.0f })
                    sampleFloats(c, t, out);
            }
            ++accepted;
        }
        catch (const Error&)
        {
        }
    }
    EXPECT_GT(accepted, 0u);
}

// Opt-in (the user's extracted files, OPENEDEN_HAVOK_HKX_DIR): the float tracks of every installed clip decode and stay
// inside the block, and the skeletons' float slots are read. Aggregates only (provenance F37 to F43).
TEST(HavokFloatTracksRealData, every_float_track_of_the_folder_decodes)
{
    const char* dir = std::getenv("OPENEDEN_HAVOK_HKX_DIR");
    if (dir == nullptr)
        GTEST_SKIP() << "OPENEDEN_HAVOK_HKX_DIR is not set";
    std::size_t clipsWithFloats = 0, tracks = 0, constants = 0, splines = 0, zeros = 0, ones = 0, otherConstants = 0,
                outside = 0, bindingSlots = 0, skeletonsWithSlots = 0, slotNames = 0, weaponSlot = 0, nonFinite = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir))
    {
        if (!e.is_regular_file())
            continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const Packfile p = Packfile::parse(bytes);
        for (const Skeleton& s : readSkeletons(p))
            if (!s.mFloatSlots.empty())
            {
                ++skeletonsWithSlots;
                slotNames += s.mFloatSlots.size();
                for (const auto& n : s.mFloatSlots)
                    weaponSlot += n == "hkVis:Weapon" ? 1 : 0;
            }
        for (const Clip& clip : readClips(p))
        {
            if (clip.mNumFloatTracks == 0)
                continue;
            ++clipsWithFloats;
            tracks += clip.mNumFloatTracks;
            bindingSlots += clip.mFloatTrackToSlot.empty() ? 0 : 1;
            nonFinite += clip.mNonFiniteValues;
            ASSERT_EQ(clip.mFloatBlocks.size(), clip.mBlocks.size());
            for (const auto& block : clip.mFloatBlocks)
            {
                ASSERT_EQ(block.size(), clip.mNumFloatTracks);
                for (const auto& f : block)
                {
                    if (f.mAnimated)
                        ++splines;
                    else
                    {
                        ++constants;
                        zeros += f.mConstant == 0.0f ? 1 : 0;
                        ones += f.mConstant == 1.0f ? 1 : 0;
                        // the installed files also hold 0.99999994 = 1 - 2^-24
                        otherConstants
                            += (f.mConstant != 0.0f && f.mConstant != 1.0f && f.mConstant != 0.99999994f) ? 1 : 0;
                    }
                }
            }
            std::vector<float> out(clip.mNumFloatTracks);
            for (float t = 0; t <= clip.mDuration; t += clip.mDuration / 9.0f + 1e-6f)
            {
                sampleFloats(clip, t, out);
                for (float v : out)
                    outside += (v < -1e-3f || v > 1.0f + 1e-3f) ? 1 : 0;
            }
        }
    }
    std::cout << "[floattracks] " << clipsWithFloats << " clips with float tracks, " << tracks
              << " tracks; block entries: " << constants << " constant (" << zeros << " zero, " << ones << " one, "
              << otherConstants << " other), " << splines << " spline; sampled values outside [0, 1]: " << outside
              << "; binding slot arrays " << bindingSlots << "; skeletons with float slots " << skeletonsWithSlots
              << " (" << slotNames << " names, " << weaponSlot
              << " hkVis:Weapon); non-finite floats in these clips (all tracks) " << nonFinite << "\n";
    EXPECT_EQ(clipsWithFloats, 2663u);
    EXPECT_EQ(tracks, 10137u);
    EXPECT_EQ(constants, 11119u);
    EXPECT_EQ(splines, 112u);
    EXPECT_EQ(otherConstants, 0u);
    EXPECT_EQ(outside, 0u);
    EXPECT_EQ(bindingSlots, 2331u);
}
