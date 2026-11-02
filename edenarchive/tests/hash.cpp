// Eden Archive unit tests (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// Hashing: specification sections 7.3.3, 13.1 and Annex B.
#include <gtest/gtest.h>

#include <edenarchive/hash.hpp>

#include EDENARCHIVE_ZSTD_H

#include "eacodec.hpp"
#include "testhelp.hpp"

namespace EdenTestSuite
{
    namespace
    {
        // Section 13.1: the BLAKE3 digest of the empty input, from the project's first test vector.
        constexpr const char* emptyHex = "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262";

        TEST(EdenArchiveHash, emptyHashShouldBeTheBlake3ConstantOfTheSpecification)
        {
            EdenTest::Digest want{};
            ASSERT_TRUE(EdenTest::digestFromHex(emptyHex, want));
            EXPECT_EQ(toDigest(EdenArchive::emptyHash()), want);
            EXPECT_EQ(EdenArchive::toHex(EdenArchive::emptyHash()), emptyHex);
            EXPECT_EQ(toDigest(EdenArchive::blake3(nullptr, 0)), want);
            EXPECT_EQ(EdenTest::hexOf(EdenTest::emptyDigest()), emptyHex);
        }

        TEST(EdenArchiveHash, oneShotShouldMatchTheIndependentImplementationAtTheTreeBoundaries)
        {
            // BLAKE3 works in 64-byte blocks and 1024-byte chunks, so test the sizes on both sides of each boundary.
            for (const std::size_t size : { 0, 1, 2, 63, 64, 65, 127, 128, 129, 1023, 1024, 1025, 2047, 2048, 2049,
                     3072, 4095, 4096, 4097, 8191, 8192, 8193, 100000 })
            {
                const Bytes data = EdenTest::patternBytes(size, 1000 + size);
                EXPECT_EQ(toDigest(EdenArchive::blake3(data.data(), data.size())), EdenTest::blake3Of(data)) << size;
            }
        }

        TEST(EdenArchiveHash, incrementalShouldEqualOneShotForAnySplit)
        {
            const Bytes data = EdenTest::patternBytes(20000, 77);
            const EdenTest::Digest want = EdenTest::blake3Of(data);
            for (const std::size_t step : { 1, 3, 63, 64, 65, 1000, 1024, 1025, 4096, 19999, 20000 })
            {
                EdenArchive::Hasher hasher;
                for (std::size_t at = 0; at < data.size(); at += step)
                    hasher.update(data.data() + at, std::min(step, data.size() - at));
                EXPECT_EQ(toDigest(hasher.finalize()), want) << "step " << step;
            }
        }

        TEST(EdenArchiveHash, emptyHasherShouldGiveTheEmptyHashAndFinalizeShouldBeRepeatable)
        {
            EdenArchive::Hasher hasher;
            EXPECT_EQ(hasher.finalize(), EdenArchive::emptyHash());
            hasher.update("abc", 3);
            const EdenArchive::Hash first = hasher.finalize();
            EXPECT_EQ(hasher.finalize(), first);
            hasher.update("def", 3);
            EXPECT_EQ(toDigest(hasher.finalize()), EdenTest::blake3Of("abcdef", 6));
            hasher.update(nullptr, 0);
            EXPECT_EQ(toDigest(hasher.finalize()), EdenTest::blake3Of("abcdef", 6));
        }

        TEST(EdenArchiveHash, pathHashShouldBeTheFirstEightDigestBytesLittleEndianAsInAnnexB)
        {
            // Annex B: the path index holds 0x681ba1e397996e28 for data/blob.bin and 0xb99969dbea6b958a for readme.txt.
            EXPECT_EQ(EdenArchive::pathHash("data/blob.bin"), 0x681ba1e397996e28ull);
            EXPECT_EQ(EdenArchive::pathHash("readme.txt"), 0xb99969dbea6b958aull);
            for (const char* path : { "a", "", "dir/file.txt", "\xC3\xA4/\xE6\x97\xA5" })
                EXPECT_EQ(EdenArchive::pathHash(path), EdenTest::pathHashOf(path)) << path;
        }

        TEST(EdenArchiveHash, toHexShouldBeLowerCaseTwoDigitsPerByte)
        {
            EdenArchive::Hash hash{};
            for (std::size_t i = 0; i < hash.size(); ++i)
                hash[i] = static_cast<std::uint8_t>(i * 9 + 0xA0);
            EXPECT_EQ(EdenArchive::toHex(hash), EdenTest::hexOf(toDigest(hash)));
            EXPECT_EQ(EdenArchive::toHex(hash).size(), 64u);
            for (const char c : EdenArchive::toHex(hash))
                EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << c;
        }

        TEST(EdenArchiveHash, theLinkedZstdShouldBeTheOneThatWasPinned)
        {
            // The library and these tests must be built against one zstd (extern/CMakeLists.txt pins v1.5.7).
            EXPECT_EQ(static_cast<unsigned>(ZSTD_versionNumber()), static_cast<unsigned>(ZSTD_VERSION_NUMBER));
        }
    }
}
