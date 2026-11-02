// Eden Archive unit tests (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// The reader: specification sections 7, 8, 9, 13.3, 14 and 15. Written from the specification and the public headers.
#include <gtest/gtest.h>

#include <atomic>
#include <limits>
#include <thread>

#include <edenarchive/archive.hpp>

#include "testhelp.hpp"

namespace EdenTestSuite
{
    namespace
    {
        using EdenArchive::Archive;
        using EdenArchive::DecodeBudget;
        using EdenArchive::Error;
        using EdenArchive::ErrorClass;
        using EdenArchive::OpenOptions;
        using EdenTest::CorpusCase;
        using EdenTest::Outcome;

        constexpr std::uint64_t maxU64 = std::numeric_limits<std::uint64_t>::max();

        const CorpusCase& caseById(const std::string& id)
        {
            for (const CorpusCase& c : EdenTest::corpusCases())
            {
                if (c.mId == id)
                    return c;
            }
            ADD_FAILURE() << "no corpus case " << id;
            static const CorpusCase empty;
            return empty;
        }

        /// readRange into a buffer with guard bytes on both sides; the guards must never change.
        Error readGuarded(const Archive& archive, std::size_t entry, std::uint64_t offset, std::size_t size, Bytes& out,
            DecodeBudget* budget = nullptr)
        {
            constexpr std::size_t guard = 16;
            constexpr std::uint8_t guardByte = 0xAB;
            Bytes buffer(size + 2 * guard, guardByte);
            const Error error = archive.readRange(entry, offset, size, buffer.data() + guard, budget);
            for (std::size_t i = 0; i < guard; ++i)
            {
                EXPECT_EQ(buffer[i], guardByte)
                    << "write before the destination, offset " << offset << " size " << size;
                EXPECT_EQ(buffer[guard + size + i], guardByte)
                    << "write after the destination, offset " << offset << " size " << size;
            }
            out.assign(buffer.begin() + guard, buffer.begin() + guard + static_cast<std::ptrdiff_t>(size));
            return error;
        }

        /// Reads [offset, offset + size) of every entry the table lists and compares with the content.
        void checkRangeRules(const Archive& archive, std::size_t entry, const Bytes& content,
            const std::vector<std::uint64_t>& offsets, const std::vector<std::uint64_t>& sizes)
        {
            for (const std::uint64_t offset : offsets)
            {
                for (const std::uint64_t size : sizes)
                {
                    SCOPED_TRACE("entry " + std::to_string(entry) + " offset " + std::to_string(offset) + " size "
                        + std::to_string(size));
                    Bytes got;
                    const Error error = readGuarded(archive, entry, offset, static_cast<std::size_t>(size), got);
                    const bool valid = offset <= content.size() && size <= content.size() - offset;
                    if (valid)
                    {
                        ASSERT_TRUE(error.ok()) << error.describe();
                        const Bytes want(content.begin() + static_cast<std::ptrdiff_t>(offset),
                            content.begin() + static_cast<std::ptrdiff_t>(offset + size));
                        EXPECT_EQ(got, want);
                    }
                    else
                        EXPECT_FALSE(error.ok()) << "a read past the end of the content must fail";
                }
            }
        }

        // ---- every corpus case ----

        class CorpusReaderTest : public testing::TestWithParam<std::size_t>
        {
        };

        TEST_P(CorpusReaderTest, openAndReadShouldFollowTheExpectation)
        {
            const CorpusCase& c = EdenTest::corpusCases()[GetParam()];
            SCOPED_TRACE(c.mId + ": " + c.mDescription);
            Error error;
            const std::unique_ptr<Archive> archive = openBytes(c.mBytes, error);

            switch (c.mOutcome)
            {
                case Outcome::Valid:
                    if (archive == nullptr && c.mReaderMayReject)
                    {
                        // Section 7.3.2: a reader MAY accept a file that is also a directory; this one did not.
                        EXPECT_TRUE(error.mClass == ErrorClass::Name || c.mCanonicalClasses.empty())
                            << error.describe();
                        return;
                    }
                    ASSERT_NE(archive, nullptr) << error.describe();
                    EXPECT_TRUE(error.ok());
                    checkEntries(*archive, c.mEntries);
                    break;

                case Outcome::InvalidAtOpen:
                    ASSERT_EQ(archive, nullptr) << "the pack must be rejected at open";
                    EXPECT_TRUE(classIs(error, c.mClasses))
                        << "got " << className(error) << ", want " << joined(c.mClasses) << ": " << error.describe();
                    if (c.mOffset.has_value())
                    {
                            EXPECT_EQ(error.mOffset, *c.mOffset) << error.describe();
                    }
                    break;

                case Outcome::InvalidAtRead:
                {
                    ASSERT_NE(archive, nullptr) << error.describe();
                    checkEntries(*archive, c.mEntries);
                    const std::size_t entry = c.mFailingEntry;
                    ASSERT_LT(entry, c.mEntries.size());
                    std::vector<std::uint8_t> content;
                    const Error readError = archive->readEntry(entry, content);
                    EXPECT_TRUE(classIs(readError, c.mClasses))
                        << "got " << className(readError) << ", want " << joined(c.mClasses);
                    EXPECT_FALSE(archive->verifyEntry(entry).ok());
                    const std::size_t size = c.mEntries[entry].mContent.size();
                    Bytes got;
                    const Error rangeError = readGuarded(*archive, entry, 0, size, got);
                    if (c.mRangeFails)
                        EXPECT_TRUE(classIs(rangeError, c.mClasses)) << className(rangeError);
                    else
                    {
                        EXPECT_TRUE(rangeError.ok()) << rangeError.describe();
                        EXPECT_EQ(got, c.mEntries[entry].mContent);
                    }
                    break;
                }
            }
        }

        INSTANTIATE_TEST_SUITE_P(Corpus, CorpusReaderTest,
            testing::Range<std::size_t>(0, EdenTest::corpusCases().size()),
            [](const testing::TestParamInfo<std::size_t>& item) { return EdenTest::corpusCases()[item.param].mId; });

        // ---- lookups ----

        TEST(EdenArchiveReader, lookupsShouldHitEveryEntryAndMissWhatIsNotThere)
        {
            for (const CorpusCase& c : EdenTest::corpusCases())
            {
                if (c.mOutcome != Outcome::Valid || c.mEntries.size() > 100)
                    continue;
                SCOPED_TRACE(c.mId);
                const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
                if (archive == nullptr)
                    continue;
                for (std::size_t i = 0; i < c.mEntries.size(); ++i)
                {
                    const EdenTest::ExpectedEntry& want = c.mEntries[i];
                    if (want.mPath.has_value())
                    {
                        EXPECT_EQ(archive->findPath(*want.mPath), std::optional<std::size_t>(i));
                        // Names next to a name are found only if they are entries themselves.
                        const auto exists = [&](const std::string& path) {
                            return std::any_of(
                                c.mEntries.begin(), c.mEntries.end(), [&](const EdenTest::ExpectedEntry& e) {
                                    return e.mPath == std::optional<std::string>(path);
                                });
                        };
                        for (const std::string& other :
                            { *want.mPath + "x", *want.mPath + "/", want.mPath->substr(0, want.mPath->size() - 1) })
                            EXPECT_EQ(archive->findPath(other).has_value(), exists(other)) << other;
                        std::string upper = *want.mPath;
                        for (char& ch : upper)
                            ch = (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 'a' + 'A') : ch;
                        if (upper != *want.mPath)
                        {
                                EXPECT_FALSE(archive->findPath(upper).has_value()) << "lookup is on normalized names only";
                        }
                    }
                    if (want.mKey.has_value())
                    {
                            EXPECT_EQ(archive->findKey(*want.mKey), std::optional<std::size_t>(i));
                    }
                }
                EXPECT_FALSE(archive->findPath("").has_value());
                EXPECT_FALSE(archive->findPath("no/such/entry.bin").has_value());
                EXPECT_FALSE(archive->findKey(0).has_value());
                EXPECT_FALSE(archive->findKey(maxU64).has_value());
                // A key that is not used: one above or below each used key, when that is not itself used.
                for (const EdenTest::ExpectedEntry& want : c.mEntries)
                {
                    if (!want.mKey.has_value())
                        continue;
                    for (const std::uint64_t other : { *want.mKey + 1, *want.mKey - 1 })
                    {
                        const bool used
                            = std::any_of(c.mEntries.begin(), c.mEntries.end(), [&](const EdenTest::ExpectedEntry& e) {
                                  return e.mKey == std::optional<std::uint64_t>(other);
                              });
                        if (!used)
                        {
                                EXPECT_FALSE(archive->findKey(other).has_value()) << other;
                        }
                    }
                }
            }
        }

        TEST(EdenArchiveReader, lookupShouldWorkInAPackWithHundredsOfEntries)
        {
            const CorpusCase& c = caseById("v31many");
            const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
            ASSERT_NE(archive, nullptr);
            for (std::size_t i = 0; i < c.mEntries.size(); ++i)
            {
                const EdenTest::ExpectedEntry& want = c.mEntries[i];
                if (want.mPath.has_value())
                {
                    EXPECT_EQ(archive->findPath(*want.mPath), std::optional<std::size_t>(i)) << *want.mPath;
                }
                if (want.mKey.has_value())
                {
                    EXPECT_EQ(archive->findKey(*want.mKey), std::optional<std::size_t>(i)) << *want.mKey;
                }
            }
            EXPECT_FALSE(archive->findPath("many/e9999").has_value());
            EXPECT_FALSE(archive->findKey(978).has_value());
        }

        // ---- the header and records of Annex B ----

        TEST(EdenArchiveReader, annexBHeaderShouldBeDecodedAsDescribedInTheAnnex)
        {
            const Bytes golden = EdenTest::annexBGolden();
            const std::unique_ptr<Archive> archive = openOrFail(golden);
            ASSERT_NE(archive, nullptr);
            const EdenArchive::PackInfo& info = archive->info();
            EXPECT_EQ(info.mVersionMajor, 0);
            EXPECT_EQ(info.mVersionMinor, 1);
            EXPECT_EQ(info.mPackFlags, 0u);
            EXPECT_EQ(info.mFileSize, 0x42C0u);
            EXPECT_EQ(info.mChunkSize, 0x20000u);
            EXPECT_EQ(info.mEntryRecordSize, 112u);
            EXPECT_EQ(info.mChunkRecordSize, 56u);
            EXPECT_EQ(info.mCompressorVersion, 10507u);
            EXPECT_EQ(info.mCompressorLevel, 3);
            EXPECT_EQ(info.mEntryCount, 3u);
            EXPECT_EQ(info.mPathEntryCount, 2u);
            EXPECT_EQ(info.mKeyEntryCount, 1u);
            EXPECT_EQ(info.mDataOffset, 0x1000u);
            EXPECT_EQ(info.mDataSize, 0x2004u);
            EXPECT_EQ(info.mMetadataOffset, 0x4000u);
            EXPECT_EQ(info.mMetadataSize, 0x240u);
            EXPECT_EQ(info.mStringTableOffset, 0x4000u);
            EXPECT_EQ(info.mStringTableSize, 0x18u);
            EXPECT_EQ(info.mEntryTableOffset, 0x4018u);
            EXPECT_EQ(info.mPathIndexOffset, 0x4168u);
            EXPECT_EQ(info.mKeyIndexOffset, 0x4188u);
            EXPECT_EQ(info.mChunkTablesOffset, 0x4198u);
            EXPECT_EQ(info.mChunkTablesSize, 0xA8u);
            EXPECT_EQ(info.mProvenanceOffset, 0x4240u);
            EXPECT_EQ(info.mProvenanceSize, 0u);
            EXPECT_EQ(info.mExtensionOffset, 0x4240u);
            EXPECT_EQ(info.mExtensionSize, 0u);
            EXPECT_EQ(info.mGeneration, 0u);
            EXPECT_EQ(toDigest(info.mSourceDigest), EdenTest::Digest{});
            EXPECT_EQ(EdenArchive::toHex(info.mManifestDigest),
                "6250f496c3639c22e6a6a9a9e4b2396cab02cf56513f7022495d5ed0bc73038c");
            EXPECT_EQ(EdenArchive::toHex(info.mMetadataDigest),
                "a5fd5acb7a035f3879235b0b8fddd205e21bc14409a7c96a98833c0e5b6b9729");
            EXPECT_EQ(EdenArchive::toHex(info.mHeaderChecksum),
                "e470c02b697d9a5c115428e327a65a4cc87d6e4a8e40e96642a76d0b1108a7aa");
            EXPECT_EQ(archive->source().size(), golden.size());
        }

        TEST(EdenArchiveReader, annexBChunkRecordsShouldBeDecodedAsDescribedInTheAnnex)
        {
            const Bytes golden = EdenTest::annexBGolden();
            const std::unique_ptr<Archive> archive = openOrFail(golden);
            ASSERT_NE(archive, nullptr);
            struct Want
            {
                std::uint64_t mOffset;
                std::uint32_t mStored;
                std::uint32_t mSize;
                std::uint16_t mCodec;
            };
            const Want wants[3] = { { 0x1000, 10, 64, 1 }, { 0x2000, 13, 13, 0 }, { 0x3000, 4, 4, 0 } };
            for (std::size_t i = 0; i < 3; ++i)
            {
                const EdenArchive::ChunkInfo chunk = archive->chunk(i, 0);
                EXPECT_EQ(chunk.mOffset, wants[i].mOffset);
                EXPECT_EQ(chunk.mStoredSize, wants[i].mStored);
                EXPECT_EQ(chunk.mSize, wants[i].mSize);
                EXPECT_EQ(chunk.mCodec, wants[i].mCodec);
                EXPECT_EQ(chunk.mFlags & 1u, 1u);
                // One chunk per entry, so the chunk hash is the entry hash.
                EXPECT_EQ(chunk.mHash, archive->entry(i).mContentHash);
            }
            const EdenArchive::EntryInfo tile = archive->entry(2);
            EXPECT_TRUE(tile.mHasKey);
            EXPECT_FALSE(tile.mHasPath);
            EXPECT_EQ(tile.mKey, 0x1000000000000001ull);
            EXPECT_EQ(tile.mType, 0x0010);
            EXPECT_EQ(tile.mFlags & EdenArchive::entryFlagPinned, EdenArchive::entryFlagPinned);
            EXPECT_EQ(tile.mFlags & EdenArchive::entryFlagHasKey, EdenArchive::entryFlagHasKey);
        }

        // ---- ranged reads ----

        TEST(EdenArchiveReader, everyRangeOfTheAnnexBEntriesShouldReadOrFailAsTheRulesSay)
        {
            const CorpusCase& c = caseById("v01annexb");
            const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
            ASSERT_NE(archive, nullptr);
            for (std::size_t entry = 0; entry < c.mEntries.size(); ++entry)
            {
                const Bytes& content = c.mEntries[entry].mContent;
                std::vector<std::uint64_t> numbers;
                for (std::uint64_t n = 0; n <= content.size() + 2; ++n)
                    numbers.push_back(n);
                checkRangeRules(*archive, entry, content, numbers, numbers);
            }
        }

        TEST(EdenArchiveReader, rangesAcrossChunkBoundariesShouldMatchTheContent)
        {
            for (const char* id : { "v05chunkplus1", "v06threechunks", "v22zstdmulti", "v25gaps" })
            {
                SCOPED_TRACE(id);
                const CorpusCase& c = caseById(id);
                const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
                ASSERT_NE(archive, nullptr);
                for (std::size_t entry = 0; entry < c.mEntries.size(); ++entry)
                {
                    const std::uint64_t total = c.mEntries[entry].mContent.size();
                    // Around every multiple of the chunk size 4096, at the start, in the middle and at the end.
                    std::vector<std::uint64_t> offsets
                        = { 0, 1, 7, total / 2, total > 0 ? total - 1 : 0, total, total + 1 };
                    for (std::uint64_t boundary = 4096; boundary <= total + 4096; boundary += 4096)
                    {
                        offsets.push_back(boundary - 1);
                        offsets.push_back(boundary);
                        offsets.push_back(boundary + 1);
                    }
                    std::vector<std::uint64_t> sizes
                        = { 0, 1, 2, 4095, 4096, 4097, 8191, 8192, 8193, total, total + 1 };
                    checkRangeRules(*archive, entry, c.mEntries[entry].mContent, offsets, sizes);
                }
            }
        }

        TEST(EdenArchiveReader, aZeroLengthReadShouldBeValidAtEveryOffsetUpToTheSizeAndNotBeyond)
        {
            const CorpusCase& c = caseById("v06threechunks");
            const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
            ASSERT_NE(archive, nullptr);
            const std::uint64_t size = c.mEntries[0].mContent.size();
            for (const std::uint64_t offset : { std::uint64_t{ 0 }, std::uint64_t{ 4096 }, size - 1, size })
            {
                Bytes got;
                EXPECT_TRUE(readGuarded(*archive, 0, offset, 0, got).ok()) << offset;
            }
            for (const std::uint64_t offset : { size + 1, size + 4096, maxU64 })
            {
                Bytes got;
                EXPECT_FALSE(readGuarded(*archive, 0, offset, 0, got).ok()) << offset;
            }
        }

        TEST(EdenArchiveReader, sumsThatOverflowShouldFailAndNotWrap)
        {
            const CorpusCase& c = caseById("v06threechunks");
            const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
            ASSERT_NE(archive, nullptr);
            std::vector<std::uint8_t> small(16, 0xAB);
            // offset + size wraps around 2^64 to a small number; size_t may be narrower than 64 bits.
            EXPECT_FALSE(archive->readRange(0, maxU64, 16, small.data()).ok());
            EXPECT_FALSE(archive->readRange(0, maxU64 - 7, 16, small.data()).ok());
            EXPECT_FALSE(archive->readRange(0, 1, std::numeric_limits<std::size_t>::max(), small.data()).ok());
            EXPECT_FALSE(archive->readRange(0, 8, std::numeric_limits<std::size_t>::max() - 4, small.data()).ok());
            for (const std::uint8_t byte : small)
                EXPECT_EQ(byte, 0xAB) << "the destination must be untouched by a rejected range";
        }

        TEST(EdenArchiveReader, readEntryShouldReturnTheWholeContentAndVerifyEntryShouldAgree)
        {
            for (const CorpusCase& c : EdenTest::corpusCases())
            {
                if (c.mOutcome != Outcome::Valid)
                    continue;
                const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
                if (archive == nullptr)
                    continue;
                for (std::size_t i = 0; i < c.mEntries.size() && i < 8; ++i)
                {
                    std::vector<std::uint8_t> content;
                    ASSERT_TRUE(archive->readEntry(i, content).ok()) << c.mId << " " << i;
                    EXPECT_EQ(content, c.mEntries[i].mContent) << c.mId << " " << i;
                    EXPECT_TRUE(archive->verifyEntry(i).ok()) << c.mId << " " << i;
                }
            }
        }

        // ---- damage is local ----

        TEST(EdenArchiveReader, aDamagedChunkShouldFailOnlyTheReadsThatTouchIt)
        {
            const CorpusCase& c = caseById("r18damagedmiddle");
            const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
            ASSERT_NE(archive, nullptr);
            const Bytes& content = c.mEntries[0].mContent; // three chunks of 4096; chunk 1 is damaged
            ASSERT_EQ(content.size(), 3u * 4096);
            const auto slice = [&](std::size_t from, std::size_t size) {
                return Bytes(content.begin() + static_cast<std::ptrdiff_t>(from),
                    content.begin() + static_cast<std::ptrdiff_t>(from + size));
            };

            for (int round = 0; round < 3; ++round) // a failure must not poison later reads
            {
                Bytes got;
                ASSERT_TRUE(readGuarded(*archive, 0, 0, 4096, got).ok());
                EXPECT_EQ(got, slice(0, 4096));
                ASSERT_TRUE(readGuarded(*archive, 0, 8192, 4096, got).ok());
                EXPECT_EQ(got, slice(8192, 4096));
                ASSERT_TRUE(readGuarded(*archive, 0, 100, 3000, got).ok());
                EXPECT_EQ(got, slice(100, 3000));

                EXPECT_EQ(className(readGuarded(*archive, 0, 4096, 4096, got)), "E_HASH");
                EXPECT_EQ(className(readGuarded(*archive, 0, 5000, 1, got)), "E_HASH");
                EXPECT_EQ(className(readGuarded(*archive, 0, 4000, 200, got)), "E_HASH"); // spans chunks 0 and 1
                EXPECT_EQ(className(readGuarded(*archive, 0, 0, 3 * 4096, got)), "E_HASH");
                EXPECT_EQ(className(archive->verifyEntry(0)), "E_HASH");

                std::vector<std::uint8_t> other;
                EXPECT_TRUE(archive->readEntry(1, other).ok());
                EXPECT_EQ(other, c.mEntries[1].mContent);
            }
        }

        TEST(EdenArchiveReader, anUnknownCodecShouldIsolateItsEntryAndNeverReturnStoredBytesAsContent)
        {
            // Annex C I15 (codec 7 on entry 1) and the experimental codec 0xFF00 of r13, whose stored bytes are the
            // content.
            for (const char* id : { "i15codec7", "r13codecff00", "f71codec256", "f72codecff00" })
            {
                SCOPED_TRACE(id);
                const CorpusCase& c = caseById(id);
                const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
                ASSERT_NE(archive, nullptr);
                for (int round = 0; round < 2; ++round)
                {
                    for (std::size_t i = 0; i < c.mEntries.size(); ++i)
                    {
                        std::vector<std::uint8_t> content;
                        const Error error = archive->readEntry(i, content);
                        if (i == c.mFailingEntry)
                        {
                            EXPECT_EQ(className(error), "E_UNSUPPORTED");
                            EXPECT_NE(content, c.mEntries[i].mContent) << "the stored bytes were returned as content";
                        }
                        else
                        {
                            EXPECT_TRUE(error.ok()) << error.describe();
                            EXPECT_EQ(content, c.mEntries[i].mContent);
                        }
                    }
                }
            }
        }

        // ---- budgets and options ----

        TEST(EdenArchiveReader, aDecodeBudgetThatIsTooSmallShouldGiveLimitAndALargeOneShouldAllowTheRead)
        {
            const CorpusCase& bomb = caseById("v28bomb8m");
            const std::unique_ptr<Archive> archive = openOrFail(bomb.mBytes);
            ASSERT_NE(archive, nullptr);
            ASSERT_EQ(bomb.mEntries[0].mContent.size(), 8u << 20);

            std::vector<std::uint8_t> content;
            DecodeBudget small{ 1u << 20 };
            EXPECT_EQ(className(archive->readEntry(0, content, &small)), "E_LIMIT");
            DecodeBudget justShort{ (8u << 20) - 1 };
            EXPECT_EQ(className(archive->readEntry(0, content, &justShort)), "E_LIMIT");
            DecodeBudget zero{ 0 };
            EXPECT_EQ(className(archive->verifyEntry(0, &zero)), "E_LIMIT");
            DecodeBudget enough{ 16u << 20 };
            EXPECT_TRUE(archive->readEntry(0, content, &enough).ok());
            EXPECT_EQ(content, bomb.mEntries[0].mContent);
            // No budget means no limit.
            EXPECT_TRUE(archive->verifyEntry(0).ok());
        }

        TEST(EdenArchiveReader, theBudgetShouldBeSharedByTheReadsThatUseIt)
        {
            const CorpusCase& c = caseById("v06threechunks"); // 8292 bytes
            const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
            ASSERT_NE(archive, nullptr);
            DecodeBudget budget{ 10000 };
            std::vector<std::uint8_t> content;
            EXPECT_TRUE(archive->readEntry(0, content, &budget).ok());
            EXPECT_EQ(content, c.mEntries[0].mContent);
            EXPECT_EQ(className(archive->readEntry(0, content, &budget)), "E_LIMIT");
            EXPECT_EQ(className(archive->verifyEntry(0, &budget)), "E_LIMIT");
            Bytes got;
            EXPECT_EQ(className(readGuarded(*archive, 0, 0, 8292, got, &budget)), "E_LIMIT");
        }

        TEST(EdenArchiveReader, skippingTheMetadataDigestShouldSkipOnlyTheDigest)
        {
            // Change a reserved byte of an entry record: nothing but the metadata digest covers it.
            Bytes stale = EdenTest::annexBPack();
            stale[0x4018 + 22] = 0x5A;
            {
                Error error;
                EXPECT_EQ(openBytes(stale, error), nullptr);
                EXPECT_EQ(className(error), "E_CHECKSUM");
            }
            OpenOptions skip;
            skip.mVerifyMetadataDigest = false;
            {
                const std::unique_ptr<Archive> archive = openOrFail(stale, skip);
                ASSERT_NE(archive, nullptr);
                checkEntries(*archive, caseById("v01annexb").mEntries);
            }
            // Everything else is still checked: bad chunk offsets, bad header checksum, bad names, bad indexes.
            for (const char* id : { "i06chunkoffset", "i07overlap", "i08traversal", "i16order", "i18pathhash",
                     "i03checksum", "i14critflag", "f01version02", "f11pathcount", "f52stringpad" })
            {
                SCOPED_TRACE(id);
                const CorpusCase& c = caseById(id);
                Error error;
                EXPECT_EQ(openBytes(c.mBytes, error, skip), nullptr);
                EXPECT_TRUE(classIs(error, c.mClasses)) << className(error);
            }
        }

        TEST(EdenArchiveReader, aLowerMetadataCapShouldGiveLimitAndTheSpecificationsCapShouldNeverBeRaised)
        {
            const Bytes pack = EdenTest::annexBPack(); // metadataSize 0x240 = 576
            OpenOptions exact;
            exact.mMaxMetadataSize = 576;
            EXPECT_NE(openOrFail(pack, exact), nullptr);
            OpenOptions below;
            below.mMaxMetadataSize = 575;
            Error error;
            EXPECT_EQ(openBytes(pack, error, below), nullptr);
            EXPECT_EQ(className(error), "E_LIMIT");
            OpenOptions zero;
            zero.mMaxMetadataSize = 0;
            EXPECT_EQ(openBytes(pack, error, zero), nullptr);
            EXPECT_EQ(className(error), "E_LIMIT");

            // Asking for more than 2^32 does not allow a larger metadata region (section 12).
            OpenOptions huge;
            huge.mMaxMetadataSize = maxU64;
            const CorpusCase& tooBig = caseById("f28metadatasize");
            EXPECT_EQ(openBytes(tooBig.mBytes, error, huge), nullptr);
            EXPECT_EQ(className(error), "E_LIMIT");
        }

        // ---- sources ----

        TEST(EdenArchiveReader, aMemorySourceShouldReadInsideItsRangeAndRefuseOutside)
        {
            const Bytes data = EdenTest::patternBytes(1000, 3);
            const auto source = EdenArchive::makeMemorySource(data);
            ASSERT_NE(source, nullptr);
            EXPECT_EQ(source->size(), 1000u);
            Bytes got(1000);
            EXPECT_TRUE(source->read(0, got.data(), 1000));
            EXPECT_EQ(got, data);
            Bytes part(10);
            EXPECT_TRUE(source->read(990, part.data(), 10));
            EXPECT_TRUE(std::equal(part.begin(), part.end(), data.begin() + 990));
            EXPECT_FALSE(source->read(991, part.data(), 10));
            EXPECT_FALSE(source->read(1000, part.data(), 1));
            EXPECT_FALSE(source->read(1001, part.data(), 1));
            EXPECT_FALSE(source->read(maxU64, part.data(), 10));
            EXPECT_FALSE(source->read(maxU64 - 3, part.data(), 10));
            EXPECT_FALSE(source->read(5, part.data(), std::numeric_limits<std::size_t>::max()));
        }

        TEST(EdenArchiveReader, aFileSourceShouldReadLikeTheFileAndReportMissingFilesAsIo)
        {
            TempDir dir;
            const Bytes pack = EdenTest::annexBPack();
            const auto path = dir.path() / "annexb.edenpak";
            ASSERT_TRUE(writeFileBytes(path, pack));

            Error error;
            const auto source = EdenArchive::openFileSource(path, error);
            ASSERT_NE(source, nullptr) << error.describe();
            EXPECT_EQ(source->size(), pack.size());
            Bytes got(100);
            EXPECT_TRUE(source->read(0x4000, got.data(), 100));
            EXPECT_TRUE(std::equal(got.begin(), got.end(), pack.begin() + 0x4000));
            EXPECT_FALSE(source->read(pack.size() - 10, got.data(), 11));

            const auto missing = EdenArchive::openFileSource(dir.path() / "missing.edenpak", error);
            EXPECT_EQ(missing, nullptr);
            EXPECT_EQ(className(error), "E_IO");

            // Archive::open(path) works the same way, and for a missing file it reports Io.
            Error openError;
            const std::unique_ptr<Archive> archive = Archive::open(path, OpenOptions{}, openError);
            ASSERT_NE(archive, nullptr) << openError.describe();
            checkEntries(*archive, caseById("v01annexb").mEntries);
            EXPECT_EQ(Archive::open(dir.path() / "missing.edenpak", OpenOptions{}, openError), nullptr);
            EXPECT_EQ(className(openError), "E_IO");
        }

        TEST(EdenArchiveReader, anEmptyOrTinyFileShouldBeRejectedAsNotAPack)
        {
            TempDir dir;
            for (const std::size_t size : { std::size_t{ 0 }, std::size_t{ 3 }, std::size_t{ 6 } })
            {
                const auto path = dir.path() / ("tiny" + std::to_string(size));
                ASSERT_TRUE(writeFileBytes(path, Bytes(size, 'x')));
                Error error;
                EXPECT_EQ(Archive::open(path, OpenOptions{}, error), nullptr);
                EXPECT_EQ(className(error), "E_MAGIC") << size;
            }
        }

        TEST(EdenArchiveReader, openingTheSameSourceTwiceShouldGiveTwoIndependentArchives)
        {
            const Bytes pack = EdenTest::annexBPack();
            const auto source = EdenArchive::makeMemorySource(pack);
            Error error;
            const std::unique_ptr<Archive> first = Archive::open(source, OpenOptions{}, error);
            const std::unique_ptr<Archive> second = Archive::open(source, OpenOptions{}, error);
            ASSERT_NE(first, nullptr);
            ASSERT_NE(second, nullptr);
            std::vector<std::uint8_t> a;
            std::vector<std::uint8_t> b;
            EXPECT_TRUE(first->readEntry(1, a).ok());
            EXPECT_TRUE(second->readEntry(1, b).ok());
            EXPECT_EQ(a, b);
            EXPECT_EQ(EdenTest::textOf(a), "Hello, Eden!\n");
        }

        // ---- threads ----

        TEST(EdenArchiveReader, concurrentRangeReadsOfOneArchiveShouldAllSucceedAndAgree)
        {
            const CorpusCase& c = caseById("v22zstdmulti");
            const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
            ASSERT_NE(archive, nullptr);
            const CorpusCase& many = caseById("v06threechunks");
            const std::unique_ptr<Archive> second = openOrFail(many.mBytes);
            ASSERT_NE(second, nullptr);

            std::atomic<unsigned> failures{ 0 };
            std::vector<std::thread> threads;
            for (unsigned t = 0; t < 8; ++t)
            {
                threads.emplace_back([&, t] {
                    EdenTest::SplitMix gen(0xC0FFEE00ull + t);
                    for (unsigned i = 0; i < 400; ++i)
                    {
                        const bool useFirst = (gen.next() & 1) != 0;
                        const Archive& a = useFirst ? *archive : *second;
                        const CorpusCase& cc = useFirst ? c : many;
                        const std::size_t entry = gen.below(cc.mEntries.size());
                        const Bytes& content = cc.mEntries[entry].mContent;
                        const std::uint64_t offset = gen.below(content.size() + 1);
                        const std::size_t size = gen.below(content.size() - offset + 1);
                        Bytes got(size);
                        const Error error = a.readRange(entry, offset, size, got.data());
                        const bool same = size == 0
                            || std::equal(
                                got.begin(), got.end(), content.begin() + static_cast<std::ptrdiff_t>(offset));
                        if (!error.ok() || !same)
                            ++failures;
                        if (i % 50 == 0)
                        {
                            std::vector<std::uint8_t> whole;
                            if (!a.readEntry(entry, whole).ok() || whole != content)
                                ++failures;
                        }
                    }
                });
            }
            for (std::thread& thread : threads)
                thread.join();
            EXPECT_EQ(failures.load(), 0u);
        }

        TEST(EdenArchiveReader, concurrentReadsShouldNotHideADamagedChunkFromAnyThread)
        {
            const CorpusCase& c = caseById("r18damagedmiddle");
            const std::unique_ptr<Archive> archive = openOrFail(c.mBytes);
            ASSERT_NE(archive, nullptr);
            std::atomic<unsigned> wrong{ 0 };
            std::vector<std::thread> threads;
            for (unsigned t = 0; t < 8; ++t)
            {
                threads.emplace_back([&] {
                    Bytes got(4096);
                    for (unsigned i = 0; i < 200; ++i)
                    {
                        if (archive->readRange(0, 4096, 4096, got.data()).mClass != ErrorClass::Hash)
                            ++wrong;
                        if (!archive->readRange(0, 0, 4096, got.data()).ok())
                            ++wrong;
                        if (!archive->readRange(0, 8192, 4096, got.data()).ok())
                            ++wrong;
                    }
                });
            }
            for (std::thread& thread : threads)
                thread.join();
            EXPECT_EQ(wrong.load(), 0u);
        }
    }
}
