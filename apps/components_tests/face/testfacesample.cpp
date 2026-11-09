#include <components/face/facecurves.hpp>
#include <components/face/faceimport.hpp>
#include <components/face/facesample.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

// Task 0071 stages c and e (pure parts): the sampler, the stock-head reduction and the importers. Written from the
// specification in facesample.hpp and faceimport.hpp, before the code.
namespace
{
    using namespace Face;

    const std::string sHash(64, 'b');

    Curves sample3()
    {
        Curves c;
        c.mAudioName = "a";
        c.mAudioDurationSeconds = 0.1;
        c.mAudioSha256 = sHash;
        c.mGeneratorName = "g";
        c.mGeneratorVersion = "1";
        c.mFrameRate = 30;
        c.mFrameCount = 3;
        c.mChannels.push_back({ "jawopen", ChannelKind::Weight, { 0, 255, 51 } });
        c.mChannels.push_back({ "gazex", ChannelKind::Signed, { -255, 0, 255 } });
        return c;
    }

    Curves constant(const std::vector<std::pair<std::string, int>>& values)
    {
        Curves c;
        c.mAudioName = "a";
        c.mAudioDurationSeconds = 0.1;
        c.mAudioSha256 = sHash;
        c.mGeneratorName = "g";
        c.mGeneratorVersion = "1";
        c.mFrameRate = 30;
        c.mFrameCount = 3;
        for (const auto& [name, v] : values)
            c.mChannels.push_back({ name, ChannelKind::Weight, { static_cast<std::int16_t>(v), static_cast<std::int16_t>(v), static_cast<std::int16_t>(v) } });
        return c;
    }

    ImportMeta meta(double duration, std::uint32_t rate = 30)
    {
        ImportMeta m;
        m.mAudioName = "vo/x.mp3";
        m.mAudioDurationSeconds = duration;
        m.mAudioSha256 = sHash;
        m.mGeneratorName = "csv";
        m.mGeneratorVersion = "1";
        m.mFrameRate = rate;
        return m;
    }
}

TEST(FaceSample, frames_are_exact_and_in_between_is_linear)
{
    const Curves c = sample3();
    float out[2] = { 9.f, 9.f };
    ASSERT_TRUE(sampleAt(c, 0.0, out, 2));
    EXPECT_FLOAT_EQ(out[0], 0.f);
    EXPECT_FLOAT_EQ(out[1], -1.f);
    ASSERT_TRUE(sampleAt(c, 1.0 / 30.0, out, 2));
    EXPECT_FLOAT_EQ(out[0], 1.f);
    EXPECT_FLOAT_EQ(out[1], 0.f);
    ASSERT_TRUE(sampleAt(c, 0.5 / 30.0, out, 2));
    EXPECT_NEAR(out[0], 0.5f, 1e-6);
    EXPECT_NEAR(out[1], -0.5f, 1e-6);
    ASSERT_TRUE(sampleAt(c, 1.5 / 30.0, out, 2));
    EXPECT_NEAR(out[0], 0.6f, 1e-6); // (255 + 51) / 2 / 255
    EXPECT_NEAR(out[1], 0.5f, 1e-6);
}

TEST(FaceSample, the_last_frame_is_held_until_the_end_and_nothing_after)
{
    const Curves c = sample3();
    float out[2];
    ASSERT_TRUE(sampleAt(c, 2.5 / 30.0, out, 2));
    EXPECT_NEAR(out[0], 0.2f, 1e-6);
    EXPECT_FLOAT_EQ(out[1], 1.f);
    EXPECT_TRUE(sampleAt(c, 0.0999999, out, 2));
    EXPECT_FALSE(sampleAt(c, 0.1, out, 2));
    EXPECT_FALSE(sampleAt(c, 5.0, out, 2));
    EXPECT_FALSE(sampleAt(c, -0.001, out, 2));
    EXPECT_FALSE(sampleAt(c, std::numeric_limits<double>::quiet_NaN(), out, 2));
    EXPECT_FALSE(sampleAt(c, std::numeric_limits<double>::infinity(), out, 2));
}

TEST(FaceSample, a_short_output_buffer_is_refused_and_not_written)
{
    const Curves c = sample3();
    float out[2] = { 7.f, 7.f };
    EXPECT_FALSE(sampleAt(c, 0.0, out, 1));
    EXPECT_EQ(out[0], 7.f);
    EXPECT_FALSE(sampleAt(c, 0.0, nullptr, 2));
}

TEST(FaceDriver, jaw_funnel_and_pucker_make_the_talk_amount)
{
    const Curves c = constant({ { "jawopen", 102 }, { "mouthfunnel", 255 }, { "mouthpucker", 255 } });
    const Driver d(c);
    const auto r = d.update(0.02);
    ASSERT_TRUE(r.has_value());
    // 0.4 + 0.5 + 0.25, clamped to 1.
    EXPECT_FLOAT_EQ(r->mTalk, 1.f);
    EXPECT_FLOAT_EQ(r->mBlink, 0.f);

    const Curves c2 = constant({ { "jawopen", 102 }, { "mouthfunnel", 51 } });
    const auto r2 = Driver(c2).update(0.0);
    ASSERT_TRUE(r2.has_value());
    EXPECT_NEAR(r2->mTalk, 0.4f + 0.5f * 0.2f, 1e-6);
}

TEST(FaceDriver, blink_is_the_larger_eye_and_missing_channels_count_as_zero)
{
    const Curves c = constant({ { "eyeblinkleft", 51 }, { "eyeblinkright", 204 } });
    const auto r = Driver(c).update(0.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->mBlink, 0.8f, 1e-6);
    EXPECT_FLOAT_EQ(r->mTalk, 0.f);
}

TEST(FaceDriver, tuning_is_used_and_outside_the_curves_means_nothing)
{
    const Curves c = constant({ { "jawopen", 255 } });
    StockHeadTuning t;
    t.mJawOpen = 0.5f;
    const Driver d(c, t);
    EXPECT_FLOAT_EQ(d.update(0.0)->mTalk, 0.5f);
    EXPECT_FALSE(d.update(1.0).has_value());
    EXPECT_FALSE(d.update(-1.0).has_value());
}

TEST(FaceCsv, one_row_per_frame_and_values_quantise_to_255)
{
    LoadError e;
    const auto c = importCsv("# comment\njawopen,gazex\n0,-1\n0.5,0\n\n1,1\n", "t.csv", meta(0.1), { "gazex" }, e);
    ASSERT_TRUE(c.has_value()) << e.format();
    EXPECT_EQ(c->mFrameCount, 3u);
    ASSERT_EQ(c->mChannels.size(), 2u);
    EXPECT_EQ(c->mChannels[0].mValues, (std::vector<std::int16_t>{ 0, 128, 255 }));
    EXPECT_EQ(c->mChannels[0].mKind, ChannelKind::Weight);
    EXPECT_EQ(c->mChannels[1].mValues, (std::vector<std::int16_t>{ -255, 0, 255 }));
    EXPECT_EQ(c->mChannels[1].mKind, ChannelKind::Signed);
    EXPECT_TRUE(validate(*c).empty());
    EXPECT_EQ(c->mGeneratorName, "csv");
    EXPECT_EQ(c->mAudioSha256, sHash);
}

TEST(FaceCsv, import_then_write_then_read_is_stable)
{
    LoadError e;
    const auto c = importCsv("jawopen\n0\n1\n0.25\n", "t.csv", meta(0.1), {}, e);
    ASSERT_TRUE(c.has_value()) << e.format();
    const auto text = writeJson(*c);
    ASSERT_TRUE(text.has_value());
    const auto back = readJson(*text, "x", e);
    ASSERT_TRUE(back.has_value()) << e.format();
    EXPECT_EQ(*writeJson(*back), *text);
}

TEST(FaceCsv, malformed_input_gets_a_located_error)
{
    LoadError e;
    EXPECT_FALSE(importCsv("", "t.csv", meta(0.1), {}, e).has_value());
    EXPECT_FALSE(e.mMessage.empty());
    EXPECT_FALSE(importCsv("jawopen\n", "t.csv", meta(0.1), {}, e).has_value()); // no rows
    EXPECT_FALSE(importCsv("jawopen,gazex\n0,0\n1\n0,0\n", "t.csv", meta(0.1), {}, e).has_value());
    EXPECT_EQ(e.mLine, 3u);
    EXPECT_EQ(e.mFile, "t.csv");
    EXPECT_FALSE(importCsv("jawopen\n0\n1.5\n0\n", "t.csv", meta(0.1), {}, e).has_value()); // above 1
    EXPECT_EQ(e.mLine, 3u);
    EXPECT_FALSE(importCsv("jawopen\n0\n-0.5\n0\n", "t.csv", meta(0.1), {}, e).has_value()); // weight below 0
    EXPECT_FALSE(importCsv("jawopen\n0\nnan\n0\n", "t.csv", meta(0.1), {}, e).has_value());
    EXPECT_FALSE(importCsv("jawopen\n0\nabc\n0\n", "t.csv", meta(0.1), {}, e).has_value());
    EXPECT_FALSE(importCsv("Jaw Open\n0\n0\n0\n", "t.csv", meta(0.1), {}, e).has_value());
    EXPECT_FALSE(importCsv("jawopen,jawopen\n0,0\n0,0\n0,0\n", "t.csv", meta(0.1), {}, e).has_value());
    EXPECT_FALSE(importCsv("jawopen\n0\n0\n0\n", "t.csv", meta(0.1), { "nosuch" }, e).has_value());
    // frame count does not fit the audio duration
    EXPECT_FALSE(importCsv("jawopen\n0\n0\n0\n0\n0\n", "t.csv", meta(0.1), {}, e).has_value());
}

TEST(FaceRhubarb, shapes_hold_until_the_next_line_and_become_viseme_channels)
{
    LoadError e;
    // 0.2 s at 30 fps = 6 frames: X for 0..0.1 (frames 0-2), D for 0.1..0.2 (frames 3-5).
    const auto c = importRhubarbTsv("0.00\tX\n0.10\tD\n", "t.tsv", meta(0.2), e);
    ASSERT_TRUE(c.has_value()) << e.format();
    EXPECT_EQ(c->mFrameCount, 6u);
    ASSERT_EQ(c->mChannels.size(), 9u);
    auto channel = [&](const char* name) -> const Channel* {
        for (const auto& ch : c->mChannels)
            if (ch.mName == name)
                return &ch;
        return nullptr;
    };
    const Channel* x = channel("viseme_x");
    const Channel* d = channel("viseme_d");
    const Channel* a = channel("viseme_a");
    ASSERT_TRUE(x && d && a);
    EXPECT_EQ(x->mValues, (std::vector<std::int16_t>{ 255, 255, 255, 0, 0, 0 }));
    EXPECT_EQ(d->mValues, (std::vector<std::int16_t>{ 0, 0, 0, 255, 255, 255 }));
    EXPECT_EQ(a->mValues, (std::vector<std::int16_t>{ 0, 0, 0, 0, 0, 0 }));
    EXPECT_TRUE(validate(*c).empty());
}

TEST(FaceRhubarb, a_late_first_line_leaves_the_start_at_rest_shape_x)
{
    LoadError e;
    const auto c = importRhubarbTsv("0.10\tB\n", "t.tsv", meta(0.2), e);
    ASSERT_TRUE(c.has_value()) << e.format();
    for (const auto& ch : c->mChannels)
        if (ch.mName == "viseme_x")
            EXPECT_EQ(ch.mValues, (std::vector<std::int16_t>{ 255, 255, 255, 0, 0, 0 }));
        else if (ch.mName == "viseme_b")
            EXPECT_EQ(ch.mValues, (std::vector<std::int16_t>{ 0, 0, 0, 255, 255, 255 }));
}

TEST(FaceRhubarb, malformed_input_gets_a_located_error)
{
    LoadError e;
    EXPECT_FALSE(importRhubarbTsv("", "t.tsv", meta(0.2), e).has_value());
    EXPECT_FALSE(importRhubarbTsv("0.0\tZ\n", "t.tsv", meta(0.2), e).has_value());
    EXPECT_EQ(e.mLine, 1u);
    EXPECT_FALSE(importRhubarbTsv("0.0\tX\n0.5\tB\n0.3\tC\n", "t.tsv", meta(0.2), e).has_value()); // not ascending
    EXPECT_EQ(e.mLine, 3u);
    EXPECT_FALSE(importRhubarbTsv("0.0 X\n", "t.tsv", meta(0.2), e).has_value()); // no tab
    EXPECT_FALSE(importRhubarbTsv("abc\tX\n", "t.tsv", meta(0.2), e).has_value());
    EXPECT_FALSE(importRhubarbTsv("-1\tX\n", "t.tsv", meta(0.2), e).has_value());
    EXPECT_FALSE(importRhubarbTsv("nan\tX\n", "t.tsv", meta(0.2), e).has_value());
    EXPECT_FALSE(importRhubarbTsv("0.0\tXX\n", "t.tsv", meta(0.2), e).has_value());
    EXPECT_FALSE(importRhubarbTsv("0.0\tX\n", "t.tsv", meta(0.0), e).has_value()); // zero duration
    EXPECT_FALSE(importRhubarbTsv("0.0\tX\n", "t.tsv", meta(1e9), e).has_value()); // too many frames
}
