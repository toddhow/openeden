// Eden Archive unit tests (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// The validator: specification sections 13.5, 14 and 17.
#include <gtest/gtest.h>

#include <edenarchive/validator.hpp>
#include <edenarchive/writer.hpp>

#include "testhelp.hpp"

namespace EdenTestSuite
{
    namespace
    {
        using EdenArchive::ErrorClass;
        using EdenArchive::ValidateOptions;
        using EdenArchive::ValidationReport;
        using EdenTest::CanonicalResult;
        using EdenTest::CorpusCase;
        using EdenTest::Outcome;

        ValidationReport run(const Bytes& bytes, bool canonical = true, bool content = true)
        {
            ValidateOptions options;
            options.mCanonical = canonical;
            options.mVerifyContent = content;
            return EdenArchive::validate(EdenArchive::makeMemorySource(bytes), options);
        }

        bool hasClass(const ValidationReport& report, const std::vector<std::string>& classes)
        {
            for (const EdenArchive::Error& finding : report.mFindings)
            {
                if (classIs(finding, classes))
                    return true;
            }
            return false;
        }

        std::string describeAll(const ValidationReport& report)
        {
            std::string text;
            for (const EdenArchive::Error& finding : report.mFindings)
                text += finding.describe() + "\n";
            return text.empty() ? "(no findings)" : text;
        }

        class CorpusValidatorTest : public testing::TestWithParam<std::size_t>
        {
        };

        TEST_P(CorpusValidatorTest, everyCaseShouldGetTheVerdictOfTheSpecification)
        {
            const CorpusCase& c = EdenTest::corpusCases()[GetParam()];
            SCOPED_TRACE(c.mId + ": " + c.mDescription);

            switch (c.mOutcome)
            {
                case Outcome::Valid:
                {
                    // The structure and content of a valid pack are always fine, in either mode.
                    const ValidationReport relaxed = run(c.mBytes, false);
                    if (!relaxed.mOpened && c.mReaderMayReject)
                        break;
                    EXPECT_TRUE(relaxed.valid()) << describeAll(relaxed);
                    const ValidationReport canonical = run(c.mBytes, true);
                    EXPECT_TRUE(canonical.mOpened);
                    if (c.mCanonical == CanonicalResult::Pass)
                        EXPECT_TRUE(canonical.valid()) << describeAll(canonical);
                    else if (c.mCanonical == CanonicalResult::Fail)
                    {
                        EXPECT_FALSE(canonical.mFindings.empty()) << "the canonical validator must report this pack";
                        if (!c.mCanonicalClasses.empty())
                        {
                            EXPECT_TRUE(hasClass(canonical, c.mCanonicalClasses)) << describeAll(canonical);
                        }
                    }
                    break;
                }

                case Outcome::InvalidAtOpen:
                    for (const bool canonical : { true, false })
                    {
                        const ValidationReport report = run(c.mBytes, canonical);
                        EXPECT_FALSE(report.mOpened);
                        EXPECT_FALSE(report.valid());
                        ASSERT_EQ(report.mFindings.size(), 1u) << "the open error is the only finding\n"
                                                               << describeAll(report);
                        EXPECT_TRUE(classIs(report.mFindings[0], c.mClasses))
                            << "got " << className(report.mFindings[0]) << ", want " << joined(c.mClasses);
                        if (c.mOffset.has_value())
                        {
                            EXPECT_EQ(report.mFindings[0].mOffset, *c.mOffset);
                        }
                    }
                    break;

                case Outcome::InvalidAtRead:
                {
                    const ValidationReport report = run(c.mBytes, false, true);
                    EXPECT_TRUE(report.mOpened);
                    EXPECT_TRUE(hasClass(report, c.mClasses)) << "wanted " << joined(c.mClasses) << "\n"
                                                              << describeAll(report);
                    // Without content checking only the structure is looked at, and that is fine.
                    const ValidationReport structure = run(c.mBytes, false, false);
                    EXPECT_TRUE(structure.valid()) << describeAll(structure);
                    break;
                }
            }
        }

        INSTANTIATE_TEST_SUITE_P(Corpus, CorpusValidatorTest,
            testing::Range<std::size_t>(0, EdenTest::corpusCases().size()),
            [](const testing::TestParamInfo<std::size_t>& item) { return EdenTest::corpusCases()[item.param].mId; });

        // ---- more detailed behaviour ----

        TEST(EdenArchiveValidator, aCorruptChunkShouldBeReportedWithHashAndOnlyThatChunk)
        {
            const Bytes pack = EdenTest::buildPack([] {
                EdenTest::PackSpec spec;
                spec.mChunkSize = 4096;
                EdenTest::EntrySpec a;
                a.mPath = "a.bin";
                a.mContent = EdenTest::patternBytes(3 * 4096, 1);
                EdenTest::EntrySpec b;
                b.mPath = "b.bin";
                b.mContent = EdenTest::patternBytes(100, 2);
                spec.mEntries = { a, b };
                return spec;
            }());
            Bytes damaged = pack;
            damaged[0x1000 + 4096 + 5] ^= 0x80;
            const ValidationReport report = run(damaged);
            EXPECT_TRUE(report.mOpened);
            ASSERT_FALSE(report.mFindings.empty());
            EXPECT_TRUE(hasClass(report, { "E_HASH" })) << describeAll(report);
            // The intact pack validates.
            EXPECT_TRUE(run(pack).valid());
        }

        TEST(EdenArchiveValidator, aDecodeBudgetThatIsTooSmallShouldBeAFindingOfClassLimit)
        {
            const CorpusCase* bomb = nullptr;
            for (const CorpusCase& c : EdenTest::corpusCases())
            {
                if (c.mId == "v28bomb8m")
                    bomb = &c;
            }
            ASSERT_NE(bomb, nullptr);
            ValidateOptions options;
            options.mCanonical = false;
            options.mDecodeBudget = 1u << 20;
            const ValidationReport report = EdenArchive::validate(EdenArchive::makeMemorySource(bomb->mBytes), options);
            EXPECT_TRUE(report.mOpened);
            EXPECT_TRUE(hasClass(report, { "E_LIMIT" })) << describeAll(report);

            options.mDecodeBudget = 64u << 20;
            const ValidationReport enough = EdenArchive::validate(EdenArchive::makeMemorySource(bomb->mBytes), options);
            EXPECT_TRUE(enough.valid()) << describeAll(enough);
            EXPECT_GE(enough.mBytesDecoded, 8u << 20) << "the whole 8 MiB chunk is decoded to be verified";
        }

        TEST(EdenArchiveValidator, maxFindingsShouldStopTheCollection)
        {
            // Ten entries whose single chunk does not match its hash.
            EdenTest::PackSpec spec;
            for (int i = 0; i < 10; ++i)
            {
                EdenTest::EntrySpec entry;
                entry.mPath = "bad/e" + std::to_string(i);
                EdenTest::ChunkSpec chunk;
                chunk.mRaw = EdenTest::patternBytes(64, 100 + i);
                chunk.mStored = EdenTest::patternBytes(64, 200 + i);
                entry.mChunks = { chunk };
                spec.mEntries.push_back(entry);
            }
            const Bytes pack = EdenTest::buildPack(spec);
            ValidateOptions options;
            options.mCanonical = false;
            options.mMaxFindings = 100;
            const ValidationReport all = EdenArchive::validate(EdenArchive::makeMemorySource(pack), options);
            EXPECT_GE(all.mFindings.size(), 10u) << describeAll(all);
            options.mMaxFindings = 3;
            const ValidationReport three = EdenArchive::validate(EdenArchive::makeMemorySource(pack), options);
            EXPECT_TRUE(three.mOpened);
            EXPECT_GE(three.mFindings.size(), 1u);
            EXPECT_LE(three.mFindings.size(), 3u);
        }

        TEST(EdenArchiveValidator, aStaleManifestDigestShouldBeReportedAsChecksumInCanonicalMode)
        {
            // Change a hint value after the manifest digest was computed: the metadata digest and header checksum are
            // right, only the logical description of 13.5 no longer matches.
            EdenTest::PackSpec spec = EdenTest::annexBSpec();
            EdenTest::PackImage original = EdenTest::layoutPack(EdenTest::annexBSpec());
            const EdenTest::Digest manifest = EdenTest::manifestDigestOf(original);
            spec.mMutate = [manifest](EdenTest::PackImage& image) {
                image.mEntries[1].mLayoutParamA = 77;
                std::copy(manifest.begin(), manifest.end(), image.mHeader.begin() + EdenTest::Fmt::hManifestDigest);
                image.mKeepManifest = true;
            };
            const Bytes pack = EdenTest::buildPack(spec);
            const ValidationReport report = run(pack, true);
            if (report.mOpened) // a reader MAY check the manifest digest at open (13.5); then it is the open error
                EXPECT_TRUE(hasClass(report, { "E_CHECKSUM" })) << describeAll(report);
            else
                EXPECT_EQ(className(report.mFindings.at(0)), "E_CHECKSUM");
        }

        TEST(EdenArchiveValidator, canonicalOnlyRulesShouldBeReportedOnlyInCanonicalMode)
        {
            struct Case
            {
                const char* mName;
                std::function<void(EdenTest::PackSpec&)> mChange;
            };
            const Case cases[] = {
                { "codec1 chunk that saves less than 1/16",
                    [](EdenTest::PackSpec& spec) {
                        EdenTest::EntrySpec entry;
                        entry.mPath = "poor.bin";
                        entry.mContent = EdenTest::patternBytes(4096, 5);
                        entry.mCodec
                            = EdenTest::CodecChoice::Zstd; // random data: the frame is larger than the 1/16 rule allows
                        spec.mEntries = { entry };
                    } },
                { "generation without the derived flag",
                    [](EdenTest::PackSpec& spec) {
                        spec.mEntries = { [] {
                            EdenTest::EntrySpec e;
                            e.mPath = "a";
                            e.mContent = EdenTest::bytesOf("x");
                            return e;
                        }() };
                        spec.mGeneration = 3;
                    } },
                { "source digest without the derived flag",
                    [](EdenTest::PackSpec& spec) {
                        spec.mEntries = { [] {
                            EdenTest::EntrySpec e;
                            e.mPath = "a";
                            e.mContent = EdenTest::bytesOf("x");
                            return e;
                        }() };
                        spec.mSourceDigest[7] = 1;
                    } },
                { "layout parameter on a generic entry",
                    [](EdenTest::PackSpec& spec) {
                        EdenTest::EntrySpec e;
                        e.mPath = "a";
                        e.mContent = EdenTest::bytesOf("x");
                        e.mLayoutParamB = 4;
                        spec.mEntries = { e };
                    } },
                { "an extension block",
                    [](EdenTest::PackSpec& spec) {
                        EdenTest::EntrySpec e;
                        e.mPath = "a";
                        e.mContent = EdenTest::bytesOf("x");
                        spec.mEntries = { e };
                        spec.mExtensions.push_back({ 0x80000009u, 0, EdenTest::bytesOf("payload") });
                    } },
                { "an entry cut into chunks of 3000 bytes",
                    [](EdenTest::PackSpec& spec) {
                        EdenTest::EntrySpec e;
                        e.mPath = "a";
                        for (int i = 0; i < 2; ++i)
                        {
                            EdenTest::ChunkSpec chunk;
                            chunk.mRaw = EdenTest::patternBytes(3000, 20 + i);
                            e.mChunks.push_back(chunk);
                        }
                        spec.mChunkSize = 4096;
                        spec.mEntries = { e };
                    } },
            };
            for (const Case& c : cases)
            {
                SCOPED_TRACE(c.mName);
                EdenTest::PackSpec spec;
                c.mChange(spec);
                const Bytes pack = EdenTest::buildPack(spec);
                // The reader accepts the pack; the canonical validator reports it; the relaxed validator does not.
                EXPECT_NE(openOrFail(pack), nullptr);
                EXPECT_TRUE(run(pack, false).valid()) << describeAll(run(pack, false));
                const ValidationReport canonical = run(pack, true);
                EXPECT_TRUE(canonical.mOpened);
                EXPECT_FALSE(canonical.mFindings.empty());
            }
        }

        TEST(EdenArchiveValidator, everyPackOfTheWriterShouldBeCanonical)
        {
            for (const std::uint32_t chunkSize : { 4096u, 8192u, 131072u })
            {
                for (const bool store : { true, false })
                {
                    SCOPED_TRACE("chunk size " + std::to_string(chunkSize) + (store ? " store" : " optimise"));
                    std::vector<EdenArchive::InputEntry> entries;
                    const auto add
                        = [&](std::optional<std::string> path, std::optional<std::uint64_t> key, Bytes data) {
                              EdenArchive::InputEntry entry;
                              entry.mPath = std::move(path);
                              entry.mKey = key;
                              entry.mData = std::move(data);
                              entries.push_back(std::move(entry));
                          };
                    add("a/text.txt", std::nullopt, EdenTest::textBytes(20000));
                    add("a/random.bin", std::nullopt, EdenTest::patternBytes(9000, 3));
                    add("a/empty", std::nullopt, {});
                    add("a/zero.bin", std::nullopt, Bytes(300000, 0));
                    add(std::nullopt, 9, EdenTest::textBytes(5000));
                    add("b.txt", 0x77, EdenTest::bytesOf("both"));
                    EdenArchive::WriteOptions options;
                    options.mChunkSize = chunkSize;
                    options.mStoreOnly = store;
                    options.mThreads = 2;
                    Bytes pack;
                    ASSERT_TRUE(EdenArchive::writePackToMemory(entries, pack, options).ok());
                    const ValidationReport report = run(pack, true);
                    EXPECT_TRUE(report.valid()) << describeAll(report);
                    EXPECT_TRUE(run(pack, false).valid());
                }
            }
        }

        TEST(EdenArchiveValidator, aFindingShouldCarryAnOffsetInsideTheFileAndAFieldName)
        {
            const CorpusCase* c = nullptr;
            for (const CorpusCase& candidate : EdenTest::corpusCases())
            {
                if (candidate.mId == "i06chunkoffset")
                    c = &candidate;
            }
            ASSERT_NE(c, nullptr);
            const ValidationReport report = run(c->mBytes);
            ASSERT_EQ(report.mFindings.size(), 1u);
            EXPECT_EQ(report.mFindings[0].mOffset, 0x4198u);
            EXPECT_FALSE(report.mFindings[0].mField.empty());
            EXPECT_FALSE(report.mFindings[0].mMessage.empty());
            const std::string text = report.mFindings[0].describe("pack.edenpak");
            EXPECT_NE(text.find("pack.edenpak"), std::string::npos);
            EXPECT_NE(text.find("E_RANGE"), std::string::npos);
            EXPECT_NE(text.find("0x4198"), std::string::npos);
        }
    }
}
