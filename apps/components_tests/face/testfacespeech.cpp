#include <components/face/facecurves.hpp>
#include <components/face/facesample.hpp>
#include <components/face/facespeech.hpp>

#include <gtest/gtest.h>

#include <map>
#include <string>

// Task 0071, engine wiring stage: the path convention, loading, cache, playback and the animation-time mapping.
// Written from the specification in facespeech.hpp and the task's stage section, before the code.
namespace
{
    using namespace Face;

    Curves makeCurves(int jaw, int blink, std::uint32_t frames = 30)
    {
        Curves c;
        c.mAudioName = "a";
        c.mAudioDurationSeconds = frames / 30.0;
        c.mAudioSha256 = std::string(64, 'c');
        c.mGeneratorName = "g";
        c.mGeneratorVersion = "1";
        c.mFrameRate = 30;
        c.mFrameCount = frames;
        auto flat = [&](int v) { return std::vector<std::int16_t>(frames, static_cast<std::int16_t>(v)); };
        c.mChannels.push_back({ "jawopen", ChannelKind::Weight, flat(jaw) });
        c.mChannels.push_back({ "eyeblinkleft", ChannelKind::Weight, flat(blink) });
        return c;
    }

    struct Files
    {
        std::map<std::string, std::vector<std::uint8_t>> mFiles;
        int mReads = 0;
        ReadFile reader()
        {
            return [this](std::string_view path) -> std::optional<std::vector<std::uint8_t>> {
                ++mReads;
                const auto it = mFiles.find(std::string(path));
                if (it == mFiles.end())
                    return std::nullopt;
                return it->second;
            };
        }
        void putBinary(const std::string& path, const Curves& c) { mFiles[path] = *writeBinary(c); }
        void putJson(const std::string& path, const Curves& c)
        {
            const std::string text = *writeJson(c);
            mFiles[path] = std::vector<std::uint8_t>(text.begin(), text.end());
        }
    };

    const std::string sVoice = "sound/vo/a/f/hlo_af001.wav";
    const std::string sBin = "sound/vo/a/f/hlo_af001.facecurves";
    const std::string sJson = "sound/vo/a/f/hlo_af001.facecurves.json";
}

TEST(FaceSpeechPaths, the_last_extension_is_replaced)
{
    const CurvePaths p = curvePathsFor(sVoice);
    EXPECT_EQ(p.mBinary, sBin);
    EXPECT_EQ(p.mJson, sJson);
}

TEST(FaceSpeechPaths, a_name_without_extension_gets_one_appended_and_a_dotted_folder_is_not_an_extension)
{
    EXPECT_EQ(curvePathsFor("sound/vo/abc").mBinary, "sound/vo/abc.facecurves");
    EXPECT_EQ(curvePathsFor("sound/v.o/abc").mBinary, "sound/v.o/abc.facecurves");
    EXPECT_EQ(curvePathsFor("a.b.mp3").mJson, "a.b.facecurves.json");
}

TEST(FaceSpeechPaths, an_empty_path_gives_empty_paths)
{
    EXPECT_TRUE(curvePathsFor("").mBinary.empty());
    EXPECT_TRUE(curvePathsFor("").mJson.empty());
}

TEST(FaceSpeechPaths, the_entry_type_is_the_first_unallocated_id)
{
    EXPECT_EQ(sEntryTypeFaceCurves, 0x0100);
}

TEST(FaceSpeechLoad, binary_is_found)
{
    Files f;
    f.putBinary(sBin, makeCurves(255, 0));
    const LoadResult r = loadSpeechCurves(sVoice, f.reader());
    ASSERT_TRUE(r.mCurves);
    EXPECT_TRUE(r.mWarning.empty());
    EXPECT_EQ(r.mCurves->mFrameCount, 30u);
}

TEST(FaceSpeechLoad, json_is_found_when_there_is_no_binary)
{
    Files f;
    f.putJson(sJson, makeCurves(255, 0));
    EXPECT_TRUE(loadSpeechCurves(sVoice, f.reader()).mCurves);
}

TEST(FaceSpeechLoad, binary_wins_over_json)
{
    Files f;
    f.putBinary(sBin, makeCurves(255, 0, 30));
    f.putJson(sJson, makeCurves(255, 0, 15));
    EXPECT_EQ(loadSpeechCurves(sVoice, f.reader()).mCurves->mFrameCount, 30u);
}

TEST(FaceSpeechLoad, nothing_found_is_no_curves_and_no_warning)
{
    Files f;
    const LoadResult r = loadSpeechCurves(sVoice, f.reader());
    EXPECT_FALSE(r.mCurves);
    EXPECT_TRUE(r.mWarning.empty());
}

TEST(FaceSpeechLoad, a_corrupt_binary_warns_with_its_name_and_hides_the_json)
{
    Files f;
    f.mFiles[sBin] = { 'O', 'P', 'F', 'C', 1, 2, 3 };
    f.putJson(sJson, makeCurves(255, 0));
    const LoadResult r = loadSpeechCurves(sVoice, f.reader());
    EXPECT_FALSE(r.mCurves);
    EXPECT_NE(r.mWarning.find(sBin), std::string::npos);
}

TEST(FaceSpeechLoad, a_corrupt_json_warns)
{
    Files f;
    f.mFiles[sJson] = { '{', 'x' };
    const LoadResult r = loadSpeechCurves(sVoice, f.reader());
    EXPECT_FALSE(r.mCurves);
    EXPECT_NE(r.mWarning.find(sJson), std::string::npos);
}

TEST(FaceSpeechLoad, an_empty_path_reads_nothing)
{
    Files f;
    EXPECT_FALSE(loadSpeechCurves("", f.reader()).mCurves);
    EXPECT_EQ(f.mReads, 0);
}

TEST(FaceSpeechCache, a_hit_does_not_read_again_and_a_miss_is_cached)
{
    Files f;
    f.putBinary(sBin, makeCurves(255, 0));
    SpeechCurveCache cache;
    auto a = cache.find(sVoice, f.reader());
    const int reads = f.mReads;
    auto b = cache.find(sVoice, f.reader());
    EXPECT_EQ(a, b);
    EXPECT_EQ(f.mReads, reads);
    EXPECT_FALSE(cache.find("sound/none.wav", f.reader()));
    const int afterMiss = f.mReads;
    EXPECT_FALSE(cache.find("sound/none.wav", f.reader()));
    EXPECT_EQ(f.mReads, afterMiss);
    EXPECT_EQ(cache.loadCount(), 2u);
}

TEST(FaceSpeechCache, the_oldest_use_is_evicted_at_the_limit)
{
    Files f;
    SpeechCurveCache cache(2);
    cache.find("a.wav", f.reader());
    cache.find("b.wav", f.reader());
    cache.find("a.wav", f.reader()); // a is newer than b now
    cache.find("c.wav", f.reader()); // evicts b
    EXPECT_EQ(cache.size(), 2u);
    const std::size_t loads = cache.loadCount();
    cache.find("a.wav", f.reader());
    EXPECT_EQ(cache.loadCount(), loads); // a stayed
    cache.find("b.wav", f.reader());
    EXPECT_EQ(cache.loadCount(), loads + 1); // b was loaded again
}

TEST(FaceSpeechCache, a_fresh_load_reports_its_warning_once)
{
    Files f;
    f.mFiles[sBin] = { 1, 2, 3 };
    SpeechCurveCache cache;
    std::string w;
    cache.find(sVoice, f.reader(), &w);
    EXPECT_FALSE(w.empty());
    w.clear();
    cache.find(sVoice, f.reader(), &w);
    EXPECT_TRUE(w.empty());
}

TEST(FaceSpeechPlayback, follows_the_stream_position)
{
    Files f;
    Curves c = makeCurves(0, 0, 30);
    for (std::uint32_t i = 0; i < 30; ++i)
        c.mChannels[0].mValues[i] = static_cast<std::int16_t>(255 * i / 29);
    f.putBinary(sBin, c);
    SpeechCurveCache cache;
    SpeechPlayback p;
    const SpeechFrame early = p.update(cache, f.reader(), sVoice, 0.0);
    const SpeechFrame late = p.update(cache, f.reader(), sVoice, 0.9);
    EXPECT_TRUE(early.mActive);
    EXPECT_TRUE(late.mActive);
    EXPECT_NEAR(early.mTalk, 0.f, 0.01f);
    EXPECT_GT(late.mTalk, 0.8f);
}

TEST(FaceSpeechPlayback, past_the_end_the_mouth_is_closed_but_still_active)
{
    Files f;
    f.putBinary(sBin, makeCurves(255, 255, 30));
    SpeechCurveCache cache;
    SpeechPlayback p;
    EXPECT_GT(p.update(cache, f.reader(), sVoice, 0.5).mTalk, 0.9f);
    const SpeechFrame after = p.update(cache, f.reader(), sVoice, 1.5);
    EXPECT_TRUE(after.mActive);
    EXPECT_EQ(after.mTalk, 0.f);
    EXPECT_EQ(after.mBlink, 0.f);
}

TEST(FaceSpeechPlayback, no_curves_is_inactive_so_the_loudness_drive_stays)
{
    Files f;
    SpeechCurveCache cache;
    SpeechPlayback p;
    EXPECT_FALSE(p.update(cache, f.reader(), sVoice, 0.5).mActive);
}

TEST(FaceSpeechPlayback, an_empty_path_is_inactive_and_forgets)
{
    Files f;
    f.putBinary(sBin, makeCurves(255, 0));
    SpeechCurveCache cache;
    SpeechPlayback p;
    EXPECT_TRUE(p.update(cache, f.reader(), sVoice, 0.1).mActive);
    EXPECT_FALSE(p.update(cache, f.reader(), "", 0.1).mActive);
    EXPECT_TRUE(p.update(cache, f.reader(), sVoice, 0.1).mActive);
}

TEST(FaceSpeechPlayback, a_new_path_switches_curves)
{
    Files f;
    f.putBinary(sBin, makeCurves(255, 0));
    f.putBinary("sound/b.facecurves", makeCurves(0, 0));
    SpeechCurveCache cache;
    SpeechPlayback p;
    EXPECT_GT(p.update(cache, f.reader(), sVoice, 0.1).mTalk, 0.9f);
    const SpeechFrame b = p.update(cache, f.reader(), "sound/b.wav", 0.1);
    EXPECT_TRUE(b.mActive);
    EXPECT_EQ(b.mTalk, 0.f);
}

TEST(FaceSpeechPlayback, a_negative_position_counts_as_zero)
{
    Files f;
    f.putBinary(sBin, makeCurves(255, 0));
    SpeechCurveCache cache;
    SpeechPlayback p;
    EXPECT_GT(p.update(cache, f.reader(), sVoice, -0.2).mTalk, 0.9f);
}

TEST(FaceSpeechPlayback, the_same_input_gives_the_same_output)
{
    Files f;
    f.putBinary(sBin, makeCurves(100, 200));
    SpeechCurveCache c1, c2;
    SpeechPlayback p1, p2;
    for (double t = 0.0; t < 1.2; t += 0.033)
    {
        const SpeechFrame a = p1.update(c1, f.reader(), sVoice, t);
        const SpeechFrame b = p2.update(c2, f.reader(), sVoice, t);
        EXPECT_EQ(a.mTalk, b.mTalk);
        EXPECT_EQ(a.mBlink, b.mBlink);
    }
}

TEST(FaceSpeechPlayback, steady_state_reads_no_files)
{
    Files f;
    f.putBinary(sBin, makeCurves(100, 0));
    SpeechCurveCache cache;
    SpeechPlayback p;
    p.update(cache, f.reader(), sVoice, 0.0);
    const int reads = f.mReads;
    for (int i = 0; i < 100; ++i)
        p.update(cache, f.reader(), sVoice, i * 0.01);
    EXPECT_EQ(f.mReads, reads);
}

TEST(FaceSpeechAnimTime, talk_maps_to_the_talk_range)
{
    SpeechFrame fr{ true, 0.5f, 0.f };
    EXPECT_FLOAT_EQ(speechAnimTime(fr, 2.f, 4.f, 10.f, 12.f), 3.f);
    fr.mTalk = 0.f;
    EXPECT_FLOAT_EQ(speechAnimTime(fr, 2.f, 4.f, 10.f, 12.f), 2.f);
    fr.mTalk = 1.f;
    EXPECT_FLOAT_EQ(speechAnimTime(fr, 2.f, 4.f, 10.f, 12.f), 4.f);
}

TEST(FaceSpeechAnimTime, a_blink_takes_over_and_maps_to_the_first_half_of_its_range)
{
    SpeechFrame fr{ true, 1.f, 1.f };
    EXPECT_FLOAT_EQ(speechAnimTime(fr, 2.f, 4.f, 10.f, 12.f), 11.f);
    fr.mBlink = 0.5f;
    EXPECT_FLOAT_EQ(speechAnimTime(fr, 2.f, 4.f, 10.f, 12.f), 10.5f);
}

TEST(FaceSpeechAnimTime, a_small_blink_does_not_override_talk)
{
    SpeechFrame fr{ true, 1.f, 0.05f };
    EXPECT_FLOAT_EQ(speechAnimTime(fr, 2.f, 4.f, 10.f, 12.f), 4.f);
}

TEST(FaceSpeechAnimTime, values_are_clamped)
{
    SpeechFrame fr{ true, 3.f, 0.f };
    EXPECT_FLOAT_EQ(speechAnimTime(fr, 2.f, 4.f, 10.f, 12.f), 4.f);
    fr = { true, 0.f, 7.f };
    EXPECT_FLOAT_EQ(speechAnimTime(fr, 2.f, 4.f, 10.f, 12.f), 11.f);
}

TEST(FaceSampleVisemes, rhubarb_shapes_drive_talk_when_there_is_no_jaw_channel)
{
    auto one = [](const char* name) {
        Curves c;
        c.mAudioName = "a";
        c.mAudioDurationSeconds = 0.1;
        c.mAudioSha256 = std::string(64, 'd');
        c.mGeneratorName = "g";
        c.mGeneratorVersion = "1";
        c.mFrameRate = 30;
        c.mFrameCount = 3;
        c.mChannels.push_back({ name, ChannelKind::Weight, { 255, 255, 255 } });
        return c;
    };
    auto talk = [&](const char* name) {
        const Curves c = one(name);
        return Driver(c).update(0.0)->mTalk;
    };
    EXPECT_EQ(talk("viseme_a"), 0.f);
    EXPECT_EQ(talk("viseme_x"), 0.f);
    EXPECT_FLOAT_EQ(talk("viseme_d"), 1.f);
    EXPECT_GT(talk("viseme_c"), talk("viseme_b"));
    EXPECT_LT(talk("viseme_c"), 1.f);
    EXPECT_GT(talk("viseme_b"), 0.f);
}
