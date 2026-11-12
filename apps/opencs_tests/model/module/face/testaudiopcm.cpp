// Task 0147: WAV decoding for the face editor's waveform track, on generated files, including hostile ones.
#include "editor_modules/face_editor/audio_pcm.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

namespace
{
    using namespace face_editor;

    void put32(std::string& s, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }
    void put16(std::string& s, std::uint16_t v)
    {
        s.push_back(static_cast<char>(v & 0xff));
        s.push_back(static_cast<char>(v >> 8));
    }

    std::string wav(std::uint16_t format, std::uint16_t channels, std::uint32_t rate, std::uint16_t bits,
        const std::string& data)
    {
        std::string s = "RIFF";
        put32(s, static_cast<std::uint32_t>(36 + data.size()));
        s += "WAVEfmt ";
        put32(s, 16);
        put16(s, format);
        put16(s, channels);
        put32(s, rate);
        put32(s, rate * channels * bits / 8);
        put16(s, static_cast<std::uint16_t>(channels * bits / 8));
        put16(s, bits);
        s += "data";
        put32(s, static_cast<std::uint32_t>(data.size()));
        s += data;
        return s;
    }
}

TEST(FaceAudio, sixteenBitMonoDecodesToFloats)
{
    std::string data;
    for (std::int16_t v : { std::int16_t{ 0 }, std::int16_t{ 16384 }, std::int16_t{ -32768 }, std::int16_t{ 32767 } })
        put16(data, static_cast<std::uint16_t>(v));
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(decodeWav(wav(1, 1, 16000, 16, data), pcm, error)) << error;
    EXPECT_EQ(pcm.mRate, 16000u);
    ASSERT_EQ(pcm.mSamples.size(), 4u);
    EXPECT_FLOAT_EQ(pcm.mSamples[0], 0.f);
    EXPECT_FLOAT_EQ(pcm.mSamples[1], 0.5f);
    EXPECT_FLOAT_EQ(pcm.mSamples[2], -1.f);
    EXPECT_NEAR(pcm.mSamples[3], 1.f, 1e-4);
    EXPECT_DOUBLE_EQ(pcm.durationSeconds(), 4.0 / 16000.0);
}

TEST(FaceAudio, stereoIsMixedToMono)
{
    std::string data;
    put16(data, 16384); // L = 0.5
    put16(data, static_cast<std::uint16_t>(-16384)); // R = -0.5
    put16(data, 16384);
    put16(data, 16384);
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(decodeWav(wav(1, 2, 8000, 16, data), pcm, error)) << error;
    ASSERT_EQ(pcm.mSamples.size(), 2u);
    EXPECT_NEAR(pcm.mSamples[0], 0.f, 1e-6);
    EXPECT_NEAR(pcm.mSamples[1], 0.5f, 1e-6);
}

TEST(FaceAudio, eightBitTwentyFourBitAndFloatFormats)
{
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(decodeWav(wav(1, 1, 8000, 8, std::string("\x80\xff\x00", 3)), pcm, error)) << error;
    ASSERT_EQ(pcm.mSamples.size(), 3u);
    EXPECT_NEAR(pcm.mSamples[0], 0.f, 1e-2);
    EXPECT_NEAR(pcm.mSamples[1], 1.f, 1e-2);
    EXPECT_NEAR(pcm.mSamples[2], -1.f, 1e-2);

    std::string d24;
    d24 += std::string("\x00\x00\x40", 3); // 0x400000 = 0.5 of 2^23
    ASSERT_TRUE(decodeWav(wav(1, 1, 8000, 24, d24), pcm, error)) << error;
    EXPECT_NEAR(pcm.mSamples.at(0), 0.5f, 1e-6);

    std::string f32;
    const float f = -0.25f;
    f32.append(reinterpret_cast<const char*>(&f), 4);
    ASSERT_TRUE(decodeWav(wav(3, 1, 8000, 32, f32), pcm, error)) << error;
    EXPECT_FLOAT_EQ(pcm.mSamples.at(0), -0.25f);
}

TEST(FaceAudio, aDataChunkLargerThanTheFileIsTruncatedToWhatIsThere)
{
    std::string data(10, '\x10');
    std::string w = wav(1, 1, 8000, 16, data);
    // Declare 1000 data bytes while only 10 follow.
    const std::size_t sizeAt = w.size() - 10 - 4;
    const std::uint32_t big = 1000;
    std::memcpy(&w[sizeAt], &big, 4);
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(decodeWav(w, pcm, error)) << error;
    EXPECT_EQ(pcm.mSamples.size(), 5u);
}

TEST(FaceAudio, refusesBadFiles)
{
    Pcm pcm;
    std::string error;
    EXPECT_FALSE(decodeWav("", pcm, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(decodeWav("RIFF\x04\0\0\0WAVE", pcm, error));
    EXPECT_FALSE(decodeWav("not a wav file at all, but long enough to have a header", pcm, error));
    EXPECT_FALSE(decodeWav(wav(1, 0, 8000, 16, "ab"), pcm, error)); // zero channels
    EXPECT_FALSE(decodeWav(wav(1, 1, 0, 16, "ab"), pcm, error)); // zero rate
    EXPECT_FALSE(decodeWav(wav(1, 1, 9999999, 16, "ab"), pcm, error)); // rate past the cap
    EXPECT_FALSE(decodeWav(wav(1, 99, 8000, 16, "ab"), pcm, error)); // too many channels
    EXPECT_FALSE(decodeWav(wav(1, 1, 8000, 12, "ab"), pcm, error)); // bit depth we do not read
    EXPECT_FALSE(decodeWav(wav(2, 1, 8000, 4, "ab"), pcm, error)); // ADPCM
    EXPECT_FALSE(decodeWav(wav(1, 1, 8000, 16, ""), pcm, error)); // no samples
    EXPECT_TRUE(pcm.mSamples.empty());
}

TEST(FaceAudio, aFmtChunkWithoutDataIsRefused)
{
    std::string s = "RIFF";
    put32(s, 28);
    s += "WAVEfmt ";
    put32(s, 16);
    put16(s, 1);
    put16(s, 1);
    put32(s, 8000);
    put32(s, 16000);
    put16(s, 2);
    put16(s, 16);
    Pcm pcm;
    std::string error;
    EXPECT_FALSE(decodeWav(s, pcm, error));
}

TEST(FaceAudio, randomMutationsNeverCrash)
{
    std::string data;
    for (int i = 0; i < 64; ++i)
        put16(data, static_cast<std::uint16_t>(i * 300));
    const std::string good = wav(1, 1, 8000, 16, data);
    std::mt19937 rng(7);
    for (int i = 0; i < 4000; ++i)
    {
        std::string bad = good;
        const int edits = 1 + static_cast<int>(rng() % 4);
        for (int e = 0; e < edits; ++e)
            bad[rng() % bad.size()] = static_cast<char>(rng());
        if (rng() % 5 == 0)
            bad.resize(rng() % bad.size());
        Pcm pcm;
        std::string error;
        const bool ok = decodeWav(bad, pcm, error);
        if (ok)
        {
            EXPECT_GT(pcm.mRate, 0u);
            EXPECT_LE(pcm.mSamples.size(), AudioLimits::sMaxSamples);
            for (float v : pcm.mSamples)
                ASSERT_TRUE(std::isfinite(v));
        }
        else
            EXPECT_TRUE(pcm.mSamples.empty());
    }
}

TEST(FaceAudio, encodeWav16RoundTrips)
{
    std::vector<float> src{ 0.f, 0.5f, -0.5f, 0.25f };
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(decodeWav(encodeWav16(src, 22050), pcm, error)) << error;
    EXPECT_EQ(pcm.mRate, 22050u);
    ASSERT_EQ(pcm.mSamples.size(), src.size());
    for (std::size_t i = 0; i < src.size(); ++i)
        EXPECT_NEAR(pcm.mSamples[i], src[i], 1e-4);
}

TEST(FaceAudio, ffmpegDecodesWhatWeEncodeAndRefusesGarbage)
{
    std::vector<float> src(4000);
    for (std::size_t i = 0; i < src.size(); ++i)
        src[i] = 0.4f * std::sin(static_cast<float>(i) * 0.07f);
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(decodeWithFfmpeg(encodeWav16(src, 16000), pcm, error)) << error;
    EXPECT_EQ(pcm.mRate, 16000u);
    ASSERT_EQ(pcm.mSamples.size(), src.size());
    for (std::size_t i = 0; i < src.size(); i += 97)
        EXPECT_NEAR(pcm.mSamples[i], src[i], 2e-3);

    Pcm none;
    EXPECT_FALSE(decodeWithFfmpeg("this is not audio, not at all, just text padding padding padding", none, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(none.mSamples.empty());
    EXPECT_FALSE(decodeWithFfmpeg("", none, error));
}

TEST(FaceAudio, loadVoiceFileReadsAWavFromDiskAndRefusesAMissingFile)
{
    const auto path = std::filesystem::temp_directory_path() / "oe_face_audio_load.wav";
    {
        std::ofstream out(path, std::ios::binary);
        out << encodeWav16(std::vector<float>(800, 0.25f), 8000);
    }
    Pcm pcm;
    std::string error;
    ASSERT_TRUE(loadVoiceFile(path, pcm, error)) << error;
    EXPECT_EQ(pcm.mSamples.size(), 800u);
    EXPECT_FALSE(loadVoiceFile(path.parent_path() / "no_such_voice_file.wav", pcm, error));
    EXPECT_FALSE(error.empty());
}

TEST(FaceAudio, voiceBytesPickTheDecoderByContentNotJustName)
{
    Pcm pcm;
    std::string error;
    EXPECT_TRUE(decodeVoiceBytes(encodeWav16({ 0.f, 0.1f }, 8000), "line.mp3", pcm, error)) << error; // RIFF magic wins
    EXPECT_FALSE(decodeVoiceBytes("", "line.wav", pcm, error));
}
