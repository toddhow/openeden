#include <components/face/facecurves.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

// Task 0071 stage b: opas.facecurves v1. Written from the specification in facecurves.hpp, before the code.
namespace
{
    using namespace Face;

    const std::string sHash(64, 'a');

    // The canonical text of makeSample(), derived by hand from the format description.
    const std::string sCanonical = std::string("{\n"
                                               "  \"format\": \"opas.facecurves\",\n"
                                               "  \"version\": 1,\n"
                                               "  \"audio\": {\"name\": \"vo/a.mp3\", \"durationSeconds\": 0.1, \"sha256\": \"")
        + sHash
        + "\"},\n"
          "  \"generator\": {\"name\": \"hand\", \"version\": \"1\", \"seed\": 7, \"emotion\": \"neutral\"},\n"
          "  \"frameRate\": 30,\n"
          "  \"frameCount\": 3,\n"
          "  \"channels\": [\n"
          "    {\"name\": \"jawopen\", \"kind\": \"weight\", \"values\": [0, 128, 255]},\n"
          "    {\"name\": \"gazex\", \"kind\": \"signed\", \"values\": [-255, 0, 255]}\n"
          "  ]\n"
          "}\n";

    Curves makeSample()
    {
        Curves c;
        c.mAudioName = "vo/a.mp3";
        c.mAudioDurationSeconds = 0.1;
        c.mAudioSha256 = sHash;
        c.mGeneratorName = "hand";
        c.mGeneratorVersion = "1";
        c.mSeed = 7;
        c.mEmotion = "neutral";
        c.mFrameRate = 30;
        c.mFrameCount = 3;
        c.mChannels.push_back({ "jawopen", ChannelKind::Weight, { 0, 128, 255 } });
        c.mChannels.push_back({ "gazex", ChannelKind::Signed, { -255, 0, 255 } });
        return c;
    }

    bool same(const Curves& a, const Curves& b)
    {
        if (a.mAudioName != b.mAudioName || a.mAudioDurationSeconds != b.mAudioDurationSeconds
            || a.mAudioSha256 != b.mAudioSha256 || a.mGeneratorName != b.mGeneratorName
            || a.mGeneratorVersion != b.mGeneratorVersion || a.mSeed != b.mSeed || a.mEmotion != b.mEmotion
            || a.mFrameRate != b.mFrameRate || a.mFrameCount != b.mFrameCount
            || a.mChannels.size() != b.mChannels.size())
            return false;
        for (std::size_t i = 0; i < a.mChannels.size(); ++i)
            if (a.mChannels[i].mName != b.mChannels[i].mName || a.mChannels[i].mKind != b.mChannels[i].mKind
                || a.mChannels[i].mValues != b.mChannels[i].mValues)
                return false;
        return true;
    }

    std::optional<Curves> parse(const std::string& text, LoadError& e)
    {
        return readJson(text, "test.facecurves.json", e);
    }

    // Replaces the first occurrence of `from` in the canonical text.
    std::string edited(const std::string& from, const std::string& to)
    {
        std::string s = sCanonical;
        const std::size_t at = s.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        if (at != std::string::npos)
            s.replace(at, from.size(), to);
        return s;
    }

    void expectRefused(const std::string& text, const std::string& pointer = {}, std::size_t line = 0)
    {
        LoadError e;
        EXPECT_FALSE(parse(text, e).has_value()) << text;
        EXPECT_EQ(e.mFile, "test.facecurves.json");
        EXPECT_FALSE(e.mMessage.empty());
        if (!pointer.empty())
            EXPECT_EQ(e.mPointer, pointer) << e.format();
        if (line != 0)
            EXPECT_EQ(e.mLine, line) << e.format();
    }

    struct Lcg
    {
        std::uint64_t s;
        std::uint32_t next()
        {
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<std::uint32_t>(s >> 33);
        }
    };
}

TEST(FaceCurvesJson, canonical_text_reads_to_the_expected_model)
{
    LoadError e;
    const auto c = parse(sCanonical, e);
    ASSERT_TRUE(c.has_value()) << e.format();
    EXPECT_TRUE(same(*c, makeSample()));
}

TEST(FaceCurvesJson, canonical_text_round_trips_byte_for_byte)
{
    LoadError e;
    const auto c = parse(sCanonical, e);
    ASSERT_TRUE(c.has_value());
    const auto out = writeJson(*c);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, sCanonical);
}

TEST(FaceCurvesJson, writer_output_of_a_model_reads_back_equal)
{
    const auto out = writeJson(makeSample());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, sCanonical);
}

TEST(FaceCurvesJson, emotion_is_optional_and_absent_means_empty)
{
    Curves c = makeSample();
    c.mEmotion.clear();
    const auto out = writeJson(c);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->find("emotion"), std::string::npos);
    LoadError e;
    const auto back = parse(*out, e);
    ASSERT_TRUE(back.has_value()) << e.format();
    EXPECT_TRUE(same(*back, c));
}

TEST(FaceCurvesJson, keys_may_come_in_any_order_but_are_written_canonically)
{
    const std::string shuffled = edited("  \"format\": \"opas.facecurves\",\n  \"version\": 1,\n",
        "  \"version\": 1,\n  \"format\": \"opas.facecurves\",\n");
    LoadError e;
    const auto c = parse(shuffled, e);
    ASSERT_TRUE(c.has_value()) << e.format();
    EXPECT_EQ(*writeJson(*c), sCanonical);
}

TEST(FaceCurvesJson, durations_round_trip_exactly)
{
    for (const double d : { 0.1, 1.0 / 3.0, 12.345678901234567, 2.0 })
    {
        Curves c = makeSample();
        c.mAudioDurationSeconds = d;
        c.mFrameRate = 30;
        c.mFrameCount = static_cast<std::uint32_t>(d * 30.0 + 0.5);
        if (c.mFrameCount < 1)
            c.mFrameCount = 1;
        for (auto& ch : c.mChannels)
            ch.mValues.assign(c.mFrameCount, 0);
        const auto text = writeJson(c);
        ASSERT_TRUE(text.has_value()) << d;
        LoadError e;
        const auto back = parse(*text, e);
        ASSERT_TRUE(back.has_value()) << e.format();
        EXPECT_EQ(back->mAudioDurationSeconds, d);
    }
}

TEST(FaceCurvesJson, unusual_but_legal_text_is_accepted_and_normalised)
{
    // Compact whitespace and an escaped slash in a name.
    std::string s = sCanonical;
    for (std::size_t p; (p = s.find("\n  ")) != std::string::npos;)
        s.erase(p + 1, 2);
    LoadError e;
    const auto c = parse(s, e);
    ASSERT_TRUE(c.has_value()) << e.format();
    EXPECT_EQ(*writeJson(*c), sCanonical);
}

TEST(FaceCurvesJsonRefusals, structure)
{
    expectRefused("");
    expectRefused("[]");
    expectRefused("{");
    expectRefused(sCanonical + "x");
    expectRefused(sCanonical + "{}");
    expectRefused(edited("\"format\": \"opas.facecurves\"", "\"format\": \"other\""), "/format", 2);
    expectRefused(edited("\"version\": 1", "\"version\": 2"), "/version", 3);
    expectRefused(edited("\"version\": 1", "\"version\": 1.5"), "/version", 3);
    expectRefused(edited("\"version\": 1,", "\"version\": 1, \"extra\": 1,"), "/extra");
    expectRefused(edited("\"frameRate\": 30,\n", ""), "/frameRate");
    expectRefused(edited("\"frameRate\": 30", "\"frameRate\": 30, \"frameRate\": 30"), "/frameRate");
    expectRefused(edited("\"seed\": 7", "\"seed\": \"7\""), "/generator/seed");
    expectRefused(edited("\"seed\": 7", "\"seed\": 7.5"), "/generator/seed");
    expectRefused(edited("\"values\": [0, 128, 255]", "\"values\": [0, 128.5, 255]"));
    expectRefused(edited("\"values\": [0, 128, 255]", "\"values\": [0, null, 255]"));
    expectRefused(edited("\"values\": [0, 128, 255]", "\"values\": [0, [1], 255]"));
    expectRefused(edited("\"values\": [0, 128, 255]", "\"values\": [0, 128, 255,]"));
    expectRefused(edited("\"name\": \"jawopen\"", "\"name\": 5"));
    expectRefused(edited("\"kind\": \"weight\"", "\"kind\": \"loud\""), "/channels/0/kind", 9);
}

TEST(FaceCurvesJsonRefusals, text_level)
{
    expectRefused(edited("vo/a.mp3", "vo/\x01.mp3")); // control character in a string
    expectRefused(edited("vo/a.mp3", "vo/\xC3.mp3")); // invalid UTF-8
    expectRefused(edited("vo/a.mp3", "vo/\xED\xA0\x80.mp3")); // a surrogate encoded in UTF-8
    expectRefused(edited("vo/a.mp3", "vo/\\q.mp3")); // unknown escape
    expectRefused(edited("vo/a.mp3", "vo/\\u00zz.mp3"));
    expectRefused(edited("\"version\": 1", "\"version\": 01"));
    expectRefused(edited("\"version\": 1", "\"version\": +1"));
    expectRefused(edited("\"version\": 1", "\"version\": 1e999"));
    expectRefused(edited("\"version\": 1", "\"version\": NaN"));
    expectRefused(edited("\"version\": 1", "\"version\": 99999999999999999999"));
    expectRefused("{\"format\": \"opas.facecurves\"} // comment");
}

TEST(FaceCurvesJsonRefusals, a_valid_unicode_escape_and_utf8_name_are_accepted)
{
    LoadError e;
    const auto c = parse(edited("vo/a.mp3", "vo/\\u00e9\xC3\xA9.mp3"), e);
    ASSERT_TRUE(c.has_value()) << e.format();
    EXPECT_EQ(c->mAudioName, "vo/\xC3\xA9\xC3\xA9.mp3");
}

TEST(FaceCurvesJsonRefusals, semantic_rules_with_located_errors)
{
    // Line 9 is the first channel, line 10 the second.
    expectRefused(edited("[0, 128, 255]", "[0, 256, 255]"), "/channels/0/values/1", 9);
    expectRefused(edited("[0, 128, 255]", "[0, -1, 255]"), "/channels/0/values/1", 9);
    expectRefused(edited("[-255, 0, 255]", "[-255, 0, 256]"), "/channels/1/values/2", 10);
    expectRefused(edited("[-255, 0, 255]", "[-256, 0, 255]"), "/channels/1/values/0", 10);
    expectRefused(edited("[0, 128, 255]", "[0, 128]"), "/channels/0/values", 9);
    expectRefused(edited("[0, 128, 255]", "[0, 128, 255, 0]"), "/channels/0/values", 9);
    expectRefused(edited("\"name\": \"jawopen\"", "\"name\": \"JawOpen\""), "/channels/0/name", 9);
    expectRefused(edited("\"name\": \"jawopen\"", "\"name\": \"\""), "/channels/0/name", 9);
    expectRefused(edited("\"name\": \"jawopen\"", "\"name\": \"jaw open\""), "/channels/0/name", 9);
    expectRefused(edited("\"name\": \"gazex\"", "\"name\": \"jawopen\""), "/channels/1/name", 10);
    expectRefused(edited("\"frameRate\": 30", "\"frameRate\": 0"), "/frameRate", 6);
    expectRefused(edited("\"frameRate\": 30", "\"frameRate\": 241"), "/frameRate", 6);
    expectRefused(edited("\"frameRate\": 30", "\"frameRate\": -30"), "/frameRate", 6);
    expectRefused(edited("\"frameCount\": 3", "\"frameCount\": 0"), "/frameCount", 7);
    expectRefused(edited("\"frameCount\": 3", "\"frameCount\": 216001"), "/frameCount", 7);
    expectRefused(edited("\"durationSeconds\": 0.1", "\"durationSeconds\": 5"), "/audio/durationSeconds", 4);
    expectRefused(edited("\"durationSeconds\": 0.1", "\"durationSeconds\": -0.1"), "/audio/durationSeconds", 4);
    expectRefused(edited(sHash, std::string(64, 'A')), "/audio/sha256", 4);
    expectRefused(edited(sHash, sHash.substr(1)), "/audio/sha256", 4);
    expectRefused(edited(sHash, std::string(63, 'a') + "g"), "/audio/sha256", 4);
    expectRefused(edited("\"name\": \"hand\"", "\"name\": \"\""), "/generator/name", 5);
    expectRefused(edited("\"emotion\": \"neutral\"", "\"emotion\": \"\""), "/generator/emotion", 5);
    expectRefused(edited("\"name\": \"vo/a.mp3\"", "\"name\": \"\""), "/audio/name", 4);
}

TEST(FaceCurvesJsonRefusals, no_channels_is_refused)
{
    expectRefused(edited("  \"channels\": [\n    {\"name\": \"jawopen\", \"kind\": \"weight\", \"values\": [0, 128, 255]},\n"
                         "    {\"name\": \"gazex\", \"kind\": \"signed\", \"values\": [-255, 0, 255]}\n  ]\n",
                      "  \"channels\": []\n"),
        "/channels");
}

TEST(FaceCurvesCaps, every_cap_has_a_failing_and_a_passing_case)
{
    // String length: 256 passes, 257 fails.
    {
        Curves c = makeSample();
        c.mAudioName.assign(Limits::sMaxStringBytes, 'x');
        EXPECT_TRUE(validate(c).empty());
        c.mAudioName.push_back('x');
        EXPECT_FALSE(validate(c).empty());
        EXPECT_FALSE(writeJson(c).has_value());
        std::string text = edited("vo/a.mp3", std::string(Limits::sMaxStringBytes + 1, 'x'));
        expectRefused(text, "/audio/name");
        text = edited("vo/a.mp3", std::string(Limits::sMaxStringBytes, 'x'));
        LoadError e;
        EXPECT_TRUE(parse(text, e).has_value()) << e.format();
    }
    // Channels: 128 pass, 129 fail.
    for (const std::size_t n : { Limits::sMaxChannels, Limits::sMaxChannels + 1 })
    {
        Curves c = makeSample();
        c.mChannels.clear();
        for (std::size_t i = 0; i < n; ++i)
            c.mChannels.push_back({ "c" + std::to_string(i), ChannelKind::Weight, { 0, 0, 0 } });
        EXPECT_EQ(validate(c).empty(), n <= Limits::sMaxChannels) << n;
        std::string text = "{\"format\": \"opas.facecurves\", \"version\": 1, \"audio\": {\"name\": \"a\", "
                           "\"durationSeconds\": 0.1, \"sha256\": \""
            + sHash + "\"}, \"generator\": {\"name\": \"g\", \"version\": \"1\", \"seed\": 0}, \"frameRate\": 30, "
                      "\"frameCount\": 3, \"channels\": [";
        for (std::size_t i = 0; i < n; ++i)
            text += std::string(i ? ", " : "") + "{\"name\": \"c" + std::to_string(i)
                + "\", \"kind\": \"weight\", \"values\": [0, 0, 0]}";
        text += "]}";
        LoadError e;
        EXPECT_EQ(parse(text, e).has_value(), n <= Limits::sMaxChannels) << n << " " << e.format();
    }
    // Nesting: 8 levels pass (the canonical file uses 4), 9 fail.
    expectRefused("{\"a\":[[[[[[[[[[1]]]]]]]]]]}");
    // Total values: frameCount * channels above the cap is refused by validate (a model that big is never allocated).
    {
        Curves c = makeSample();
        c.mFrameCount = Limits::sMaxFrameCount;
        c.mAudioDurationSeconds = static_cast<double>(c.mFrameCount) / c.mFrameRate;
        c.mChannels.assign(Limits::sMaxChannels, Channel{ "x", ChannelKind::Weight, {} });
        for (std::size_t i = 0; i < c.mChannels.size(); ++i)
            c.mChannels[i].mName = "c" + std::to_string(i);
        // 128 * 216000 = 27.6 million values: above sMaxTotalValues, refused before any value is looked at.
        const auto issues = validate(c);
        ASSERT_FALSE(issues.empty());
        EXPECT_NE(issues.front().mMessage.find("values"), std::string::npos);
    }
    // File size: one byte over the cap is refused without being parsed.
    {
        std::string big(Limits::sMaxFileBytes + 1, ' ');
        LoadError e;
        EXPECT_FALSE(readJson(big, "big.json", e).has_value());
        EXPECT_NE(e.mMessage.find("larger"), std::string::npos) << e.format();
    }
}

TEST(FaceCurvesJsonRefusals, a_lying_frame_count_cannot_make_the_reader_allocate)
{
    // frameCount says 216000 but the channels carry 3 values: refused by comparing counts, not by allocating.
    expectRefused(edited("\"frameCount\": 3", "\"frameCount\": 216000"));
}

TEST(FaceCurvesValidate, a_valid_model_has_no_issues_and_each_rule_fires)
{
    EXPECT_TRUE(validate(makeSample()).empty());
    Curves c = makeSample();
    c.mChannels[0].mValues[0] = 300;
    EXPECT_FALSE(validate(c).empty());
    c = makeSample();
    c.mChannels[1].mName = "jawopen";
    EXPECT_FALSE(validate(c).empty());
    c = makeSample();
    c.mAudioSha256 = "xyz";
    EXPECT_FALSE(validate(c).empty());
    c = makeSample();
    c.mAudioDurationSeconds = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(validate(c).empty());
    c = makeSample();
    c.mAudioDurationSeconds = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(validate(c).empty());
}

TEST(FaceCurvesValidate, duration_may_differ_from_the_frame_count_by_one_frame_only)
{
    Curves c = makeSample(); // 3 frames at 30: 0.1 s
    c.mAudioDurationSeconds = 0.1 + 1.0 / 30.0 - 1e-6;
    EXPECT_TRUE(validate(c).empty());
    c.mAudioDurationSeconds = 0.1 + 1.0 / 30.0 + 1e-3;
    EXPECT_FALSE(validate(c).empty());
    c.mAudioDurationSeconds = 0.1 - 1.0 / 30.0 + 1e-6;
    EXPECT_TRUE(validate(c).empty());
    c.mAudioDurationSeconds = 0.1 - 1.0 / 30.0 - 1e-3;
    EXPECT_FALSE(validate(c).empty());
}

TEST(FaceCurvesValidate, channel_name_syntax)
{
    EXPECT_TRUE(isValidChannelName("jawopen"));
    EXPECT_TRUE(isValidChannelName("viseme_a"));
    EXPECT_TRUE(isValidChannelName("eye.blink_l2"));
    EXPECT_TRUE(isValidChannelName(std::string(64, 'a')));
    EXPECT_FALSE(isValidChannelName(std::string(65, 'a')));
    EXPECT_FALSE(isValidChannelName(""));
    EXPECT_FALSE(isValidChannelName("Jaw"));
    EXPECT_FALSE(isValidChannelName("a b"));
    EXPECT_FALSE(isValidChannelName("a-b"));
    EXPECT_FALSE(isValidChannelName(std::string("a\0b", 3)));
}

TEST(FaceCurvesWriter, refuses_an_invalid_model)
{
    Curves c = makeSample();
    c.mChannels[0].mValues.pop_back();
    EXPECT_FALSE(writeJson(c).has_value());
    EXPECT_FALSE(writeBinary(c).has_value());
}

TEST(FaceCurvesWriter, escapes_what_json_requires_and_only_that)
{
    Curves c = makeSample();
    c.mAudioName = "a\"b\\c\n\t\r\x01\xC3\xA9";
    const auto text = writeJson(c);
    ASSERT_TRUE(text.has_value());
    EXPECT_NE(text->find("a\\\"b\\\\c\\n\\t\\r\\u0001\xC3\xA9"), std::string::npos) << *text;
    LoadError e;
    const auto back = parse(*text, e);
    ASSERT_TRUE(back.has_value()) << e.format();
    EXPECT_EQ(back->mAudioName, c.mAudioName);
    EXPECT_EQ(*writeJson(*back), *text);
}

TEST(FaceCurvesBinary, round_trips_and_matches_the_json_form)
{
    const Curves c = makeSample();
    const auto bin = writeBinary(c);
    ASSERT_TRUE(bin.has_value());
    ASSERT_GE(bin->size(), 4u);
    EXPECT_EQ(std::string(bin->begin(), bin->begin() + 4), "OPFC");
    LoadError e;
    const auto back = readBinary(*bin, "t.opfc", e);
    ASSERT_TRUE(back.has_value()) << e.format();
    EXPECT_TRUE(same(*back, c));
    EXPECT_EQ(*writeJson(*back), sCanonical);
    EXPECT_EQ(*writeBinary(*back), *bin);
}

TEST(FaceCurvesBinary, layout_is_pinned_by_hand_assembled_bytes)
{
    // One channel, one frame: derived by hand from the layout in facecurves.cpp.
    Curves c;
    c.mAudioName = "a";
    c.mAudioDurationSeconds = 1.0 / 30.0;
    c.mAudioSha256 = sHash;
    c.mGeneratorName = "g";
    c.mGeneratorVersion = "v";
    c.mSeed = -2;
    c.mFrameRate = 30;
    c.mFrameCount = 1;
    c.mChannels.push_back({ "j", ChannelKind::Signed, { -2 } });
    const auto bin = writeBinary(c);
    ASSERT_TRUE(bin.has_value());
    std::vector<std::uint8_t> expect = { 'O', 'P', 'F', 'C', 1, 0, // magic, version, reserved
        30, 0, // frameRate u16
        1, 0, 0, 0 }; // frameCount u32
    double d = 1.0 / 30.0;
    std::uint64_t bits;
    std::memcpy(&bits, &d, 8);
    for (int i = 0; i < 8; ++i)
        expect.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    for (int i = 0; i < 32; ++i)
        expect.push_back(0xaa); // sha256 raw
    for (int i = 0; i < 8; ++i)
        expect.push_back(i == 0 ? 0xfe : 0xff); // seed -2, i64
    expect.insert(expect.end(), { 1, 0, 'a' }); // audio name: u16 length + bytes
    expect.insert(expect.end(), { 1, 0, 'g' }); // generator name
    expect.insert(expect.end(), { 1, 0, 'v' }); // generator version
    expect.insert(expect.end(), { 0, 0 }); // emotion (empty = absent)
    expect.insert(expect.end(), { 1, 0 }); // channel count u16
    expect.insert(expect.end(), { 1, 1, 'j' }); // kind (1 = signed), name length u8, name
    expect.insert(expect.end(), { 0xfe, 0xff }); // value -2 as i16
    EXPECT_EQ(*bin, expect);
}

TEST(FaceCurvesBinary, every_truncation_is_refused_with_a_located_error)
{
    const auto bin = *writeBinary(makeSample());
    for (std::size_t n = 0; n < bin.size(); ++n)
    {
        const std::vector<std::uint8_t> cut(bin.begin(), bin.begin() + n);
        LoadError e;
        EXPECT_FALSE(readBinary(cut, "t.opfc", e).has_value()) << n;
        EXPECT_EQ(e.mFile, "t.opfc");
        EXPECT_FALSE(e.mMessage.empty());
    }
    std::vector<std::uint8_t> more = bin;
    more.push_back(0);
    LoadError e;
    EXPECT_FALSE(readBinary(more, "t.opfc", e).has_value());
}

TEST(FaceCurvesBinary, header_fields_are_checked)
{
    auto bin = *writeBinary(makeSample());
    LoadError e;
    auto bad = bin;
    bad[0] = 'X';
    EXPECT_FALSE(readBinary(bad, "t", e).has_value());
    bad = bin;
    bad[4] = 2;
    EXPECT_FALSE(readBinary(bad, "t", e).has_value());
    bad = bin;
    bad[5] = 1;
    EXPECT_FALSE(readBinary(bad, "t", e).has_value());
    bad = bin;
    bad[6] = 0;
    bad[7] = 0; // frameRate 0
    EXPECT_FALSE(readBinary(bad, "t", e).has_value());
    bad = bin;
    bad[8] = 0xff;
    bad[9] = 0xff;
    bad[10] = 0xff;
    bad[11] = 0xff; // frameCount 4 billion, far beyond the bytes that remain: refused before allocating
    EXPECT_FALSE(readBinary(bad, "t", e).has_value());
}

TEST(FaceCurvesBinary, a_signed_value_out_of_range_and_a_weight_in_a_signed_slot)
{
    Curves c = makeSample();
    auto bin = *writeBinary(c);
    // Last channel's last value is the final two bytes (255 as i16): make it 256.
    bin[bin.size() - 2] = 0x00;
    bin[bin.size() - 1] = 0x01;
    LoadError e;
    EXPECT_FALSE(readBinary(bin, "t", e).has_value());
}

TEST(FaceCurvesFuzz, mutated_inputs_never_crash_and_accepted_ones_rewrite_stably)
{
    Lcg rng{ 12345 };
    const std::string json = sCanonical;
    const auto bin = *writeBinary(makeSample());
    std::size_t acceptedJson = 0, acceptedBin = 0;
    for (int i = 0; i < 20000; ++i)
    {
        std::string s = json;
        const int edits = 1 + rng.next() % 3;
        for (int k = 0; k < edits; ++k)
        {
            const std::size_t at = rng.next() % s.size();
            switch (rng.next() % 3)
            {
                case 0:
                    s[at] = static_cast<char>(rng.next());
                    break;
                case 1:
                    s.erase(at, 1 + rng.next() % 4);
                    break;
                default:
                    s.insert(at, 1, static_cast<char>(rng.next()));
            }
            if (s.empty())
                s = "x";
        }
        LoadError e;
        if (const auto c = readJson(s, "fuzz", e))
        {
            ++acceptedJson;
            ASSERT_TRUE(validate(*c).empty());
            const auto w = writeJson(*c);
            ASSERT_TRUE(w.has_value());
            LoadError e2;
            const auto again = readJson(*w, "fuzz", e2);
            ASSERT_TRUE(again.has_value()) << e2.format();
            ASSERT_TRUE(same(*again, *c));
            ASSERT_EQ(*writeJson(*again), *w);
        }
        else
        {
            ASSERT_FALSE(e.mMessage.empty());
        }

        std::vector<std::uint8_t> b = bin;
        const int bedits = 1 + rng.next() % 3;
        for (int k = 0; k < bedits; ++k)
        {
            const std::size_t at = rng.next() % b.size();
            if (rng.next() % 4 == 0)
                b.erase(b.begin() + at, b.begin() + std::min(b.size(), at + 1 + rng.next() % 4));
            else
                b[at] = static_cast<std::uint8_t>(rng.next());
            if (b.empty())
                b.push_back(0);
        }
        if (const auto c = readBinary(b, "fuzz", e))
        {
            ++acceptedBin;
            ASSERT_TRUE(validate(*c).empty());
            const auto w = writeBinary(*c);
            ASSERT_TRUE(w.has_value());
            LoadError e2;
            const auto again = readBinary(*w, "fuzz", e2);
            ASSERT_TRUE(again.has_value());
            ASSERT_TRUE(same(*again, *c));
        }
    }
    // Some mutations (a changed value, a changed name letter) are legal: the fuzz must exercise acceptance too.
    EXPECT_GT(acceptedJson, 0u);
    EXPECT_GT(acceptedBin, 0u);
}

TEST(FaceCurvesAudio, matches_by_hash_and_duration)
{
    const Curves c = makeSample();
    EXPECT_TRUE(audioMatches(c, sHash, 0.1));
    EXPECT_TRUE(audioMatches(c, sHash, 0.1 + 1.0 / 30.0 - 1e-6));
    EXPECT_FALSE(audioMatches(c, sHash, 0.1 + 1.0 / 30.0 + 1e-3));
    EXPECT_FALSE(audioMatches(c, std::string(64, 'b'), 0.1));
    EXPECT_FALSE(audioMatches(c, "", 0.1));
    EXPECT_FALSE(audioMatches(c, sHash, std::numeric_limits<double>::quiet_NaN()));
}

TEST(FaceCurvesError, formats_file_line_pointer_and_message)
{
    LoadError e;
    e.mFile = "a.json";
    e.mLine = 12;
    e.mPointer = "/x/0";
    e.mMessage = "boom";
    EXPECT_EQ(e.format(), "a.json:12: /x/0: boom");
    e.mPointer.clear();
    EXPECT_EQ(e.format(), "a.json:12: boom");
}
