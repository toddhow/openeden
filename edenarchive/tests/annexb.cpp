// Eden Archive unit tests (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
//
// Checks of the test side itself: the independent builder must reproduce Annex B of the specification byte for byte,
// and the patch helpers must change nothing when asked to recompute an unchanged pack. If these fail, the corpus is
// not to be trusted, whatever the library does.
#include <gtest/gtest.h>

#include <set>

#include "testhelp.hpp"

namespace EdenTestSuite
{
    namespace
    {
        TEST(EdenArchiveTestSide, theBuilderShouldReproduceAnnexBByteForByte)
        {
            const Bytes golden = EdenTest::annexBGolden();
            const Bytes built = EdenTest::annexBPack();
            ASSERT_EQ(golden.size(), 0x42C0u);
            ASSERT_EQ(built.size(), golden.size());
            for (std::size_t i = 0; i < golden.size(); ++i)
                ASSERT_EQ(built[i], golden[i]) << "first difference at offset 0x" << std::hex << i;
        }

        TEST(EdenArchiveTestSide, theBuildersDigestsShouldBeThoseOfAnnexB)
        {
            const Bytes built = EdenTest::annexBPack();
            const auto hexAt = [&](std::size_t offset) {
                EdenTest::Digest digest{};
                std::copy(built.begin() + static_cast<std::ptrdiff_t>(offset),
                    built.begin() + static_cast<std::ptrdiff_t>(offset + 32), digest.begin());
                return EdenTest::hexOf(digest);
            };
            EXPECT_EQ(hexAt(EdenTest::Fmt::hManifestDigest),
                "6250f496c3639c22e6a6a9a9e4b2396cab02cf56513f7022495d5ed0bc73038c");
            EXPECT_EQ(hexAt(EdenTest::Fmt::hMetadataDigest),
                "a5fd5acb7a035f3879235b0b8fddd205e21bc14409a7c96a98833c0e5b6b9729");
            EXPECT_EQ(hexAt(EdenTest::Fmt::hHeaderChecksum),
                "e470c02b697d9a5c115428e327a65a4cc87d6e4a8e40e96642a76d0b1108a7aa");
        }

        TEST(EdenArchiveTestSide, theHandBuiltFrameShouldBeTheOneOfAnnexBAndLibzstdShouldDecodeIt)
        {
            const Bytes frame = EdenTest::makeRleFrame(0x41, 64);
            EXPECT_EQ(frame, (Bytes{ 0x28, 0xB5, 0x2F, 0xFD, 0x20, 0x40, 0x03, 0x02, 0x00, 0x41 }));
            Bytes decoded;
            ASSERT_TRUE(EdenTest::zstdDecompress(frame, 64, decoded));
            EXPECT_EQ(decoded, Bytes(64, 0x41));
            // Larger run-length frames decode too: two-byte and four-byte content sizes, several blocks.
            for (const std::uint64_t count :
                { 255ull, 256ull, 1000ull, 65791ull, 65792ull, 131072ull, 131073ull, 300000ull })
            {
                Bytes big;
                ASSERT_TRUE(EdenTest::zstdDecompress(EdenTest::makeRleFrame(0x5A, count), count, big)) << count;
                EXPECT_EQ(big, Bytes(count, 0x5A)) << count;
            }
        }

        TEST(EdenArchiveTestSide, realZstdFramesShouldRoundTripWithAndWithoutAChecksum)
        {
            const Bytes text = EdenTest::textBytes(10000);
            for (const bool checksum : { false, true })
            {
                const Bytes frame = EdenTest::zstdCompress(text.data(), text.size(), 3, checksum);
                ASSERT_FALSE(frame.empty());
                EXPECT_LT(frame.size(), text.size() / 4);
                Bytes back;
                ASSERT_TRUE(EdenTest::zstdDecompress(frame, text.size(), back));
                EXPECT_EQ(back, text);
            }
            // The checksum costs four bytes.
            EXPECT_EQ(EdenTest::zstdCompress(text.data(), text.size(), 3, true).size(),
                EdenTest::zstdCompress(text.data(), text.size(), 3, false).size() + 4);
        }

        TEST(EdenArchiveTestSide, recomputingAnUnchangedPackShouldChangeNothing)
        {
            Bytes pack = EdenTest::annexBPack();
            const Bytes before = pack;
            EdenTest::Recompute all;
            all.mPathIndex = true;
            EdenTest::recompute(pack, all);
            EXPECT_EQ(pack, before);
        }

        TEST(EdenArchiveTestSide, recomputingAfterAPatchShouldGiveWhatTheBuilderWouldWrite)
        {
            // Change a hint value in the builder's image and the same value in the bytes, then recompute: the two
            // routes agree everywhere but the manifest digest (which the byte route does not recompute) and the
            // checksums that cover it.
            EdenTest::PackSpec spec = EdenTest::annexBSpec();
            spec.mMutate = [](EdenTest::PackImage& image) { image.mEntries[0].mHintGroup = 5; };
            Bytes viaImage = EdenTest::buildPack(spec);
            Bytes viaBytes = EdenTest::annexBPack();
            EdenTest::patch(viaBytes, 0x4018 + 92, 5, 4);
            EdenTest::recompute(viaBytes);
            for (Bytes* pack : { &viaImage, &viaBytes })
            {
                std::fill(pack->begin() + 240, pack->begin() + 272, std::uint8_t{ 0 });
                std::fill(pack->begin() + 480, pack->begin() + 512, std::uint8_t{ 0 });
                std::fill(pack->end() - 128 + 72, pack->end() - 128 + 104, std::uint8_t{ 0 });
            }
            EXPECT_EQ(viaImage, viaBytes);
        }

        TEST(EdenArchiveTestSide, theBuilderShouldPutEntriesInTheCanonicalOrder)
        {
            EdenTest::PackSpec spec;
            for (const char* path : { "b", "a/b", "a-x", "a0", "ab", "abc", "\xC3\xA4" })
            {
                EdenTest::EntrySpec e;
                e.mPath = path;
                spec.mEntries.push_back(e);
            }
            for (const std::uint64_t key :
                { std::uint64_t{ 0x8000000000000000ull }, std::uint64_t{ 5 }, std::uint64_t{ 9 } })
            {
                EdenTest::EntrySpec e;
                e.mKey = key;
                spec.mEntries.push_back(e);
            }
            const auto order = EdenTest::entryOrder(spec);
            std::vector<std::string> names;
            for (const auto& e : order)
                names.push_back(e.mPath.has_value() ? *e.mPath : "key" + std::to_string(*e.mKey));
            EXPECT_EQ(names,
                (std::vector<std::string>{
                    "a-x", "a/b", "a0", "ab", "abc", "b", "\xC3\xA4", "key5", "key9", "key9223372036854775808" }));
        }

        TEST(EdenArchiveTestSide, everyCorpusCaseShouldHaveAUniqueFileSafeIdAndATidyDescription)
        {
            std::set<std::string> ids;
            for (const EdenTest::CorpusCase& c : EdenTest::corpusCases())
            {
                EXPECT_TRUE(ids.insert(c.mId).second) << "duplicate id " << c.mId;
                EXPECT_FALSE(c.mId.empty());
                for (const char ch : c.mId)
                    EXPECT_TRUE((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) << c.mId;
                EXPECT_EQ(c.mDescription.find(' '), std::string::npos) << c.mId;
                EXPECT_EQ(c.mDescription.find('\n'), std::string::npos) << c.mId;
                EXPECT_FALSE(c.mDescription.empty()) << c.mId;
                EXPECT_LE(c.mBytes.size(), 600u * 1024) << c.mId << " is not a small file";
                if (c.mOutcome == EdenTest::Outcome::Valid)
                {
                        EXPECT_FALSE(c.mEntries.empty() && c.mId != "v02empty") << c.mId;
                }
            }
        }

        TEST(EdenArchiveTestSide, theCorpusShouldCoverEveryErrorClassOfSection141AtOpenOrRead)
        {
            std::set<std::string> seen;
            for (const EdenTest::CorpusCase& c : EdenTest::corpusCases())
            {
                if (c.mOutcome != EdenTest::Outcome::Valid && c.mClasses.size() == 1)
                    seen.insert(c.mClasses[0]);
            }
            for (const char* name : { "E_MAGIC", "E_VERSION", "E_TRUNCATED", "E_CHECKSUM", "E_RANGE", "E_OVERLAP",
                     "E_ORDER", "E_DUPLICATE", "E_NAME", "E_LIMIT", "E_INDEX", "E_UNSUPPORTED", "E_CODEC", "E_HASH" })
                EXPECT_TRUE(seen.count(name) > 0) << name << " is not exercised by any case";
        }

        TEST(EdenArchiveTestSide, theCorpusShouldContainTheCasesTheTaskNames)
        {
            std::size_t valid = 0;
            std::size_t invalidOpen = 0;
            std::size_t invalidRead = 0;
            for (const EdenTest::CorpusCase& c : EdenTest::corpusCases())
            {
                valid += c.mOutcome == EdenTest::Outcome::Valid ? 1 : 0;
                invalidOpen += c.mOutcome == EdenTest::Outcome::InvalidAtOpen ? 1 : 0;
                invalidRead += c.mOutcome == EdenTest::Outcome::InvalidAtRead ? 1 : 0;
            }
            RecordProperty("valid", static_cast<int>(valid));
            RecordProperty("invalid_at_open", static_cast<int>(invalidOpen));
            RecordProperty("invalid_at_read", static_cast<int>(invalidRead));
            EXPECT_GE(valid, 30u);
            EXPECT_GE(invalidOpen, 600u); // 641 truncations alone
            EXPECT_GE(invalidRead, 20u);
        }
    }
}
