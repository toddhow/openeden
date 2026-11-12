// Task 0147: the face editor's sparse key model against the per-frame integers of opas.facecurves.
#include "editor_modules/face_editor/facekeys.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>

namespace
{
    using namespace face_editor;
    using openeden::editor::CurveKey;
}

TEST(FaceKeys, aFlatChannelIsTwoKeys)
{
    std::vector<std::int16_t> frames(30, 100);
    auto keys = sparsify(frames, 30);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_DOUBLE_EQ(keys[0].timeSeconds, 0.0);
    EXPECT_DOUBLE_EQ(keys[1].timeSeconds, 29.0 / 30.0);
    EXPECT_DOUBLE_EQ(keys[0].value, 100.0 / 255.0);
}

TEST(FaceKeys, aRampKeepsOnlyItsEnds)
{
    std::vector<std::int16_t> frames;
    for (int i = 0; i <= 20; ++i)
        frames.push_back(static_cast<std::int16_t>(i * 10));
    EXPECT_EQ(sparsify(frames, 30).size(), 2u);
}

TEST(FaceKeys, aPeakKeepsItsApex)
{
    std::vector<std::int16_t> frames{ 0, 0, 0, 200, 0, 0, 0 };
    auto keys = sparsify(frames, 10);
    bool apex = false;
    for (const auto& k : keys)
        apex = apex || (std::abs(k.timeSeconds - 0.3) < 1e-9 && std::abs(k.value - 200.0 / 255.0) < 1e-9);
    EXPECT_TRUE(apex);
}

TEST(FaceKeys, emptyAndSingleFrame)
{
    EXPECT_TRUE(sparsify({}, 30).empty());
    EXPECT_EQ(sparsify({ 7 }, 30).size(), 1u);
    EXPECT_EQ(resample(sparsify({ 7 }, 30), 30, 3, 0, 255), (std::vector<std::int16_t>{ 7, 7, 7 }));
}

TEST(FaceKeys, resampleOfSparsifyIsExactForRandomChannels)
{
    std::mt19937 rng(1234);
    for (int trial = 0; trial < 200; ++trial)
    {
        const std::size_t n = 1 + rng() % 700; // beyond kMaxSegmentFrames too
        std::vector<std::int16_t> frames(n);
        const int style = trial % 4;
        for (std::size_t i = 0; i < n; ++i)
        {
            int v = 0;
            if (style == 0)
                v = static_cast<int>(rng() % 256);
            else if (style == 1)
                v = static_cast<int>(i % 40) * 6;
            else if (style == 2)
                v = static_cast<int>(100 + 90 * std::sin(i * 0.1));
            else
                v = (i / 25) % 2 ? 255 : 0;
            frames[i] = static_cast<std::int16_t>(v);
        }
        auto keys = sparsify(frames, 30);
        EXPECT_LE(keys.size(), n);
        EXPECT_EQ(resample(keys, 30, static_cast<std::uint32_t>(n), 0, 255), frames) << "trial " << trial;
    }
}

TEST(FaceKeys, signedChannelsRoundTripToo)
{
    std::vector<std::int16_t> frames;
    for (int i = 0; i < 100; ++i)
        frames.push_back(static_cast<std::int16_t>((i % 51) * 10 - 255));
    EXPECT_EQ(resample(sparsify(frames, 24), 24, 100, -255, 255), frames);
}

TEST(FaceKeys, resampleInterpolatesHoldsAndClamps)
{
    std::vector<CurveKey> keys{ { 0.1, 0.0 }, { 0.3, 1.0 } };
    auto v = resample(keys, 10, 6, 0, 255); // times 0.0, 0.1 ... 0.5
    EXPECT_EQ(v[0], 0); // held before the first
    EXPECT_EQ(v[1], 0);
    EXPECT_EQ(v[2], 128); // halfway, rounded
    EXPECT_EQ(v[3], 255);
    EXPECT_EQ(v[5], 255); // held after the last
    std::vector<CurveKey> over{ { 0.0, 3.0 } };
    EXPECT_EQ(resample(over, 10, 1, 0, 255)[0], 255);
    EXPECT_EQ(resample({}, 10, 3, 0, 255), (std::vector<std::int16_t>{ 0, 0, 0 }));
}

TEST(FaceKeys, nonFiniteKeysAreIgnored)
{
    std::vector<CurveKey> keys{ { 0.0, 0.5 }, { std::numeric_limits<double>::quiet_NaN(), 1.0 },
        { 0.2, std::numeric_limits<double>::infinity() } };
    auto v = resample(keys, 10, 4, 0, 255);
    for (auto x : v)
        EXPECT_EQ(x, 128);
}

TEST(FaceKeys, insertKeepsTimeOrder)
{
    std::vector<CurveKey> keys{ { 0.0, 0.0 }, { 1.0, 1.0 } };
    EXPECT_EQ(insertKey(keys, { 0.5, 0.2 }), 1u);
    EXPECT_EQ(insertKey(keys, { 1.0, 0.7 }), 3u); // equal time goes after
    EXPECT_EQ(insertKey(keys, { -1.0, 0.1 }), 0u);
    for (std::size_t i = 1; i < keys.size(); ++i)
        EXPECT_LE(keys[i - 1].timeSeconds, keys[i].timeSeconds);
}
