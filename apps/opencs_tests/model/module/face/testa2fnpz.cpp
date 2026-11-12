// Task 0147 slice 2: the .npz reader for the Audio2Face downloads. Generated archives only (the real files are read by
// the gated test in testa2fgenerate.cpp).
#include "editor_modules/face_editor/a2f_npz.hpp"

#include <gtest/gtest.h>

#include <fstream>

namespace
{
    using namespace face_editor;
    namespace fs = std::filesystem;

    fs::path scratch(const char* name)
    {
        fs::path p = fs::temp_directory_path() / ("oe_a2f_npz_" + std::string(name));
        fs::remove_all(p);
        fs::create_directories(p);
        return p;
    }

    fs::path spit(const fs::path& dir, const char* name, const std::string& bytes)
    {
        const fs::path p = dir / name;
        std::ofstream out(p, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return p;
    }

    std::string sample()
    {
        return buildNpz({ { "pose", { 2, 3 }, { 1.f, 2.f, 3.f, 4.f, 5.f, -6.5f } }, { "empty", { 0, 3 }, {} } },
            { { "start", { 3 }, { 7, -8, 9 } } }, { { "poseNames", { "neutral", "jawOpen", "mouthClose" } } });
    }

    TEST(A2FNpz, readsFloatsIntsAndStringsBackFromAGeneratedArchive)
    {
        const fs::path dir = scratch("roundtrip");
        const fs::path file = spit(dir, "a.npz", sample());
        NpzFile npz;
        std::string error;
        ASSERT_TRUE(npz.open(file, error)) << error;
        EXPECT_EQ(npz.names(), (std::vector<std::string>{ "pose", "empty", "start", "poseNames" }));
        EXPECT_TRUE(npz.has("pose"));
        EXPECT_FALSE(npz.has("pose.npy"));
        EXPECT_FALSE(npz.has("nothing"));

        std::vector<float> f;
        NpyInfo info;
        ASSERT_TRUE(npz.readFloats("pose", f, info, error)) << error;
        EXPECT_EQ(f, (std::vector<float>{ 1.f, 2.f, 3.f, 4.f, 5.f, -6.5f }));
        EXPECT_EQ(info.mShape, (std::vector<std::size_t>{ 2, 3 }));
        EXPECT_EQ(info.mDescr, "<f4");
        EXPECT_EQ(info.itemSize(), 4u);
        EXPECT_EQ(info.elementCount(), 6u);

        std::vector<std::int32_t> i;
        ASSERT_TRUE(npz.readInt32s("start", i, info, error)) << error;
        EXPECT_EQ(i, (std::vector<std::int32_t>{ 7, -8, 9 }));

        std::vector<std::string> s;
        ASSERT_TRUE(npz.readStrings("poseNames", s, error)) << error;
        EXPECT_EQ(s, (std::vector<std::string>{ "neutral", "jawOpen", "mouthClose" }));

        ASSERT_TRUE(npz.readFloats("empty", f, info, error)) << error;
        EXPECT_TRUE(f.empty());
    }

    TEST(A2FNpz, readBytesReadsARangeAndRefusesOneBeyondTheData)
    {
        const fs::path dir = scratch("range");
        NpzFile npz;
        std::string error;
        ASSERT_TRUE(npz.open(spit(dir, "a.npz", sample()), error)) << error;
        NpyInfo info;
        ASSERT_TRUE(npz.info("pose", info, error)) << error;
        float two[2] = {};
        ASSERT_TRUE(npz.readBytes(info, 8, 8, two, error)) << error; // the third and fourth floats
        EXPECT_EQ(two[0], 3.f);
        EXPECT_EQ(two[1], 4.f);
        EXPECT_FALSE(npz.readBytes(info, 20, 8, two, error));
        EXPECT_FALSE(error.empty());
        EXPECT_FALSE(npz.readBytes(info, ~0ull - 3, 8, two, error)); // overflow
    }

    TEST(A2FNpz, aWrongTypeIsRefusedNotReinterpreted)
    {
        const fs::path dir = scratch("type");
        NpzFile npz;
        std::string error;
        ASSERT_TRUE(npz.open(spit(dir, "a.npz", sample()), error)) << error;
        std::vector<std::int32_t> i;
        std::vector<float> f;
        NpyInfo info;
        EXPECT_FALSE(npz.readInt32s("pose", i, info, error));
        EXPECT_FALSE(npz.readFloats("start", f, info, error));
        std::vector<std::string> s;
        EXPECT_FALSE(npz.readStrings("pose", s, error));
        EXPECT_FALSE(npz.readFloats("missing", f, info, error));
        EXPECT_NE(error.find("missing"), std::string::npos);
    }

    TEST(A2FNpz, notAnArchiveTruncatedAndEmptyFilesAreRefusedWithoutThrowing)
    {
        const fs::path dir = scratch("bad");
        NpzFile npz;
        std::string error;
        EXPECT_FALSE(npz.open(dir / "missing.npz", error));
        EXPECT_FALSE(error.empty());
        EXPECT_FALSE(npz.open(spit(dir, "empty.npz", ""), error));
        EXPECT_FALSE(npz.open(spit(dir, "text.npz", "this is not a zip file at all, just text"), error));
        const std::string full = sample();
        for (std::size_t cut : { full.size() / 2, full.size() - 5, std::size_t{ 30 } })
        {
            NpzFile again;
            EXPECT_FALSE(again.open(spit(dir, "cut.npz", full.substr(0, cut)), error)) << cut;
        }
    }

    TEST(A2FNpz, aCompressedEntryIsRefusedByName)
    {
        std::string bytes = sample();
        // The central directory's first entry: signature 0x02014b50, the method is the u16 at +10.
        const std::string sig("PK\x01\x02", 4);
        const std::size_t central = bytes.find(sig);
        ASSERT_NE(central, std::string::npos);
        bytes[central + 10] = 8; // deflate
        const fs::path dir = scratch("deflate");
        NpzFile npz;
        std::string error;
        EXPECT_FALSE(npz.open(spit(dir, "a.npz", bytes), error));
        EXPECT_NE(error.find("stored"), std::string::npos) << error;
    }

    TEST(A2FNpz, anOversizedNpyHeaderLengthIsRefused)
    {
        std::string bytes = sample();
        // The first entry's data starts after its 30 byte local header and its name "pose.npy" (8 bytes): the .npy
        // magic is 6 bytes, the version 2, then the u16 header length.
        const std::size_t npy = 30 + 8;
        ASSERT_EQ(bytes.compare(npy, 6, "\x93NUMPY", 6), 0);
        bytes[npy + 8] = static_cast<char>(0xFF);
        bytes[npy + 9] = static_cast<char>(0xFF);
        const fs::path dir = scratch("hdr");
        NpzFile npz;
        std::string error;
        ASSERT_TRUE(npz.open(spit(dir, "a.npz", bytes), error)) << error; // the directory itself is fine
        NpyInfo info;
        EXPECT_FALSE(npz.info("pose", info, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(A2FNpz, aShapeThatDisagreesWithTheDataSizeIsRefused)
    {
        // Rewrite "(2, 3)" to "(9, 3)" in the first header: the data holds 6 floats, not 27.
        std::string bytes = sample();
        const std::size_t at = bytes.find("(2, 3)");
        ASSERT_NE(at, std::string::npos);
        bytes[at + 1] = '9';
        const fs::path dir = scratch("shape");
        NpzFile npz;
        std::string error;
        ASSERT_TRUE(npz.open(spit(dir, "a.npz", bytes), error)) << error;
        NpyInfo info;
        EXPECT_FALSE(npz.info("pose", info, error));
        EXPECT_NE(error.find("size"), std::string::npos) << error;
    }
}
