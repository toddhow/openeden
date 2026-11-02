// Eden Archive unit tests (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// The writer: specification sections 7.3, 9.2 and 17. The expected bytes come from the independent builder of
// edenarchive/testsupport, which was written from the specification and does not share code with the library.
#include <gtest/gtest.h>

#include <chrono>
#include <numeric>
#include <optional>

#include <edenarchive/archive.hpp>
#include <edenarchive/writer.hpp>

#include "testhelp.hpp"

namespace EdenTestSuite
{
    namespace
    {
        using EdenArchive::Archive;
        using EdenArchive::Compression;
        using EdenArchive::Error;
        using EdenArchive::InputEntry;
        using EdenArchive::WriteOptions;
        using EdenTest::CodecChoice;
        using EdenTest::EntrySpec;
        using EdenTest::PackSpec;

        /// One file to pack, described once and converted for the library and for the independent builder.
        struct Item
        {
            std::optional<std::string> mPath;
            std::optional<std::uint64_t> mKey;
            std::uint16_t mType = 0;
            std::uint32_t mHintFlags = 0;
            std::uint32_t mHintMemorySize = 0;
            std::uint32_t mHintGroup = 0;
            std::uint32_t mLayoutParamA = 0;
            std::uint32_t mLayoutParamB = 0;
            bool mStore = false; // Compression::Store, else Compression::Auto
            Bytes mData;
            std::optional<EdenTest::ProvenanceSpec> mProvenance;
        };

        InputEntry toInput(const Item& item)
        {
            InputEntry entry;
            entry.mPath = item.mPath;
            entry.mKey = item.mKey;
            entry.mType = item.mType;
            entry.mHintFlags = item.mHintFlags;
            entry.mHintMemorySize = item.mHintMemorySize;
            entry.mHintGroup = item.mHintGroup;
            entry.mLayoutParamA = item.mLayoutParamA;
            entry.mLayoutParamB = item.mLayoutParamB;
            entry.mCompression = item.mStore ? Compression::Store : Compression::Auto;
            entry.mData = item.mData;
            if (item.mProvenance.has_value())
            {
                EdenArchive::ProvenanceInput provenance;
                provenance.mToolId = item.mProvenance->mToolId;
                std::copy(item.mProvenance->mSourceDigest.begin(), item.mProvenance->mSourceDigest.end(),
                    provenance.mSourceDigest.begin());
                provenance.mParameters = item.mProvenance->mParameters;
                entry.mProvenance = provenance;
            }
            return entry;
        }

        std::vector<InputEntry> toInputs(const std::vector<Item>& items)
        {
            std::vector<InputEntry> out;
            for (const Item& item : items)
                out.push_back(toInput(item));
            return out;
        }

        EntrySpec toSpec(const Item& item, bool allStored)
        {
            EntrySpec spec;
            spec.mPath = item.mPath;
            spec.mKey = item.mKey;
            spec.mType = item.mType;
            spec.mExtraFlags = item.mHintFlags;
            spec.mHintMemorySize = item.mHintMemorySize;
            spec.mHintGroup = item.mHintGroup;
            spec.mLayoutParamA = item.mLayoutParamA;
            spec.mLayoutParamB = item.mLayoutParamB;
            spec.mContent = item.mData;
            spec.mCodec = (allStored || item.mStore) ? CodecChoice::Store : CodecChoice::Auto;
            spec.mProvenance = item.mProvenance;
            return spec;
        }

        PackSpec toPackSpec(const std::vector<Item>& items, const WriteOptions& options)
        {
            PackSpec spec;
            for (const Item& item : items)
                spec.mEntries.push_back(toSpec(item, options.mStoreOnly));
            spec.mChunkSize = options.mChunkSize;
            spec.mZstdLevel = options.mZstdLevel;
            spec.mPackFlags = options.mDerived ? EdenTest::Fmt::packDerived : 0;
            spec.mGeneration = options.mGeneration;
            std::copy(options.mSourceDigest.begin(), options.mSourceDigest.end(), spec.mSourceDigest.begin());
            return spec;
        }

        Item pathItem(std::string path, Bytes data, bool store = false)
        {
            Item item;
            item.mPath = std::move(path);
            item.mData = std::move(data);
            item.mStore = store;
            return item;
        }

        Item keyItem(std::uint64_t key, Bytes data)
        {
            Item item;
            item.mKey = key;
            item.mData = std::move(data);
            return item;
        }

        /// A mixed set of files: text, incompressible and constant data, empty, stored, keyed, hinted, with provenance,
        /// and sizes around the small chunk sizes.
        std::vector<Item> standardItems()
        {
            std::vector<Item> items;
            items.push_back(pathItem("docs/readme.txt", EdenTest::textBytes(3000)));
            items.push_back(pathItem("docs/notes/long.txt", EdenTest::textBytes(70000)));
            items.push_back(pathItem("data/random.bin", EdenTest::patternBytes(9000, 11)));
            items.push_back(pathItem("data/zero.bin", Bytes(5000, 0)));
            items.push_back(pathItem("data/stored.bin", EdenTest::textBytes(4097), true));
            items.push_back(pathItem("data/exact4096.bin", EdenTest::patternBytes(4096, 12)));
            items.push_back(pathItem("data/exact8192.bin", EdenTest::textBytes(8192)));
            items.push_back(pathItem("empty.txt", {}));
            items.push_back(pathItem("\xC3\xA4/\xE6\x97\xA5.txt", EdenTest::bytesOf("unicode path")));
            items.push_back(keyItem(0xFFFFFFFFFFFFFFFEull, EdenTest::bytesOf("max key")));
            items.push_back(keyItem(5, EdenTest::textBytes(6000)));
            {
                Item tile = keyItem(0x1000000000000001ull, EdenTest::bytesOf("TILE"));
                tile.mType = 0x0010;
                tile.mHintFlags = EdenTest::Fmt::entryPinned | EdenTest::Fmt::entryCoarseFirst;
                items.push_back(tile);
            }
            {
                Item both = pathItem("both/path.and.key", EdenTest::textBytes(1000));
                both.mKey = 0x42;
                both.mType = 0x0001;
                both.mLayoutParamA = 4;
                both.mLayoutParamB = 2;
                both.mHintFlags = EdenTest::Fmt::entryHintGroup | EdenTest::Fmt::entryHintMemSize;
                both.mHintGroup = 9;
                both.mHintMemorySize = 123456;
                items.push_back(both);
            }
            {
                Item derived = pathItem("derived/tile.bin", EdenTest::textBytes(500));
                derived.mProvenance = EdenTest::ProvenanceSpec{ "tilebaker/0.3.1", EdenTest::blake3Of(EdenTest::bytesOf("src")),
                    { 1, 2, 3, 4, 5 } };
                items.push_back(derived);
            }
            return items;
        }

        Error writeMemory(const std::vector<Item>& items, const WriteOptions& options, Bytes& out)
        {
            out.clear();
            return EdenArchive::writePackToMemory(toInputs(items), out, options);
        }

        Bytes mustWrite(const std::vector<Item>& items, const WriteOptions& options)
        {
            Bytes out;
            const Error error = writeMemory(items, options, out);
            EXPECT_TRUE(error.ok()) << error.describe();
            return out;
        }

        std::vector<Item> shuffled(std::vector<Item> items, std::uint64_t seed)
        {
            EdenTest::SplitMix gen(seed);
            for (std::size_t i = items.size(); i > 1; --i)
                std::swap(items[i - 1], items[gen.below(i)]);
            return items;
        }

        /// The manifest digest (section 13.5) stored in the header of a pack.
        EdenTest::Digest manifestOf(const Bytes& pack)
        {
            EdenTest::Digest digest{};
            std::copy(pack.begin() + EdenTest::Fmt::hManifestDigest, pack.begin() + EdenTest::Fmt::hManifestDigest + 32,
                digest.begin());
            return digest;
        }

        // ---- store mode: byte for byte the canonical pack ----

        class StoreModeTest : public testing::TestWithParam<std::tuple<std::uint32_t, unsigned>>
        {
        };

        TEST_P(StoreModeTest, outputShouldBeByteIdenticalToTheIndependentBuilder)
        {
            WriteOptions options;
            options.mChunkSize = std::get<0>(GetParam());
            options.mThreads = std::get<1>(GetParam());
            options.mStoreOnly = true;
            const std::vector<Item> items = standardItems();
            const Bytes got = mustWrite(items, options);
            const Bytes want = EdenTest::buildPack(toPackSpec(items, options));
            ASSERT_EQ(got.size(), want.size());
            EXPECT_EQ(got, want);
            // Nothing in a store-only pack came from zstd, so the informative fields are zero (17.2 step 9).
            EXPECT_EQ(EdenTest::getLe(got, EdenTest::Fmt::hCompressorVersion, 4), 0u);
            EXPECT_EQ(EdenTest::getLe(got, EdenTest::Fmt::hCompressorLevel, 4), 0u);
        }

        INSTANTIATE_TEST_SUITE_P(Sizes, StoreModeTest,
            testing::Combine(testing::Values(4096u, 8192u, 65536u, 131072u), testing::Values(1u, 4u)));

        TEST(EdenArchiveWriter, anEmptyListShouldGiveTheEmptyPackOfTheBuilder)
        {
            WriteOptions options;
            options.mStoreOnly = true;
            EXPECT_EQ(mustWrite({}, options), EdenTest::buildPack(PackSpec{}));
        }

        TEST(EdenArchiveWriter, derivedFlagGenerationAndSourceDigestShouldBeWrittenAsGiven)
        {
            WriteOptions options;
            options.mStoreOnly = true;
            options.mDerived = true;
            options.mGeneration = 12;
            const EdenTest::Digest digest = EdenTest::blake3Of(EdenTest::bytesOf("inputs"));
            std::copy(digest.begin(), digest.end(), options.mSourceDigest.begin());
            std::vector<Item> items = { pathItem("a.bin", EdenTest::bytesOf("derived")) };
            items[0].mProvenance = EdenTest::ProvenanceSpec{ "t/1", digest, { 7 } };
            const Bytes got = mustWrite(items, options);
            EXPECT_EQ(got, EdenTest::buildPack(toPackSpec(items, options)));
            const std::unique_ptr<Archive> archive = openOrFail(got);
            ASSERT_NE(archive, nullptr);
            EXPECT_EQ(archive->info().mPackFlags & EdenArchive::packFlagDerived, EdenArchive::packFlagDerived);
            EXPECT_EQ(archive->info().mGeneration, 12u);
            EXPECT_EQ(toDigest(archive->info().mSourceDigest), digest);
        }

        // ---- determinism (17.1 and 17.3) ----

        TEST(EdenArchiveWriter, inputOrderShouldNotChangeAByte)
        {
            for (const bool store : { true, false })
            {
                WriteOptions options;
                options.mChunkSize = 4096;
                options.mStoreOnly = store;
                const std::vector<Item> items = standardItems();
                const Bytes want = mustWrite(items, options);
                std::vector<Item> reversed(items.rbegin(), items.rend());
                EXPECT_EQ(mustWrite(reversed, options), want) << "reversed, store " << store;
                std::vector<Item> rotated = items;
                std::rotate(rotated.begin(), rotated.begin() + 5, rotated.end());
                EXPECT_EQ(mustWrite(rotated, options), want) << "rotated, store " << store;
                for (std::uint64_t seed = 1; seed <= 5; ++seed)
                    EXPECT_EQ(mustWrite(shuffled(items, seed), options), want) << "shuffle " << seed << " store " << store;
            }
        }

        TEST(EdenArchiveWriter, threadCountAndWorkWindowShouldNotChangeAByte)
        {
            WriteOptions options;
            options.mChunkSize = 4096;
            options.mThreads = 1;
            const std::vector<Item> items = standardItems();
            const Bytes want = mustWrite(items, options);
            for (const unsigned threads : { 2u, 3u, 4u, 8u, 0u })
            {
                options.mThreads = threads;
                EXPECT_EQ(mustWrite(items, options), want) << threads << " threads";
            }
            options.mThreads = 4;
            for (const std::uint64_t window : { std::uint64_t{ 1 }, std::uint64_t{ 4096 }, std::uint64_t{ 100000 },
                     std::uint64_t{ 1 } << 30 })
            {
                options.mWorkWindow = window;
                EXPECT_EQ(mustWrite(items, options), want) << "work window " << window;
            }
        }

        TEST(EdenArchiveWriter, theSameInputTwiceShouldGiveTheSameBytes)
        {
            WriteOptions options;
            options.mChunkSize = 8192;
            const std::vector<Item> items = standardItems();
            EXPECT_EQ(mustWrite(items, options), mustWrite(items, options));
        }

        // ---- optimise mode: same logical pack as the builder, content intact ----

        TEST(EdenArchiveWriter, optimiseModeShouldGiveTheBuildersManifestDigestAndTheSameContent)
        {
            for (const std::uint32_t chunkSize : { 4096u, 65536u, 131072u })
            {
                for (const int level : { 1, 3, 9 })
                {
                    SCOPED_TRACE("chunk size " + std::to_string(chunkSize) + " level " + std::to_string(level));
                    WriteOptions options;
                    options.mChunkSize = chunkSize;
                    options.mZstdLevel = level;
                    options.mThreads = 3;
                    const std::vector<Item> items = standardItems();
                    const Bytes got = mustWrite(items, options);
                    const Bytes want = EdenTest::buildPack(toPackSpec(items, options));

                    EXPECT_EQ(manifestOf(got), manifestOf(want));

                    const std::unique_ptr<Archive> mine = openOrFail(got);
                    const std::unique_ptr<Archive> reference = openOrFail(want);
                    ASSERT_NE(mine, nullptr);
                    ASSERT_NE(reference, nullptr);
                    ASSERT_EQ(mine->entryCount(), reference->entryCount());

                    bool anyZstd = false;
                    for (std::size_t i = 0; i < mine->entryCount(); ++i)
                    {
                        std::vector<std::uint8_t> a;
                        std::vector<std::uint8_t> b;
                        ASSERT_TRUE(mine->readEntry(i, a).ok());
                        ASSERT_TRUE(reference->readEntry(i, b).ok());
                        EXPECT_EQ(a, b) << "entry " << i;
                        EXPECT_EQ(mine->entry(i).mContentHash, reference->entry(i).mContentHash);
                        EXPECT_EQ(mine->entry(i).mChunkCount, reference->entry(i).mChunkCount);
                        for (std::uint32_t c = 0; c < mine->entry(i).mChunkCount; ++c)
                        {
                            const EdenArchive::ChunkInfo chunk = mine->chunk(i, c);
                            EXPECT_EQ(chunk.mHash, reference->chunk(i, c).mHash);
                            EXPECT_EQ(chunk.mSize, reference->chunk(i, c).mSize);
                            EXPECT_TRUE(chunk.mCodec == 0 || chunk.mCodec == 1);
                            if (chunk.mCodec == 1)
                            {
                                anyZstd = true;
                                // Section 9.2: zstd only when it saves at least 1/16.
                                EXPECT_LE(chunk.mStoredSize, chunk.mSize - (chunk.mSize >> 4));
                            }
                            else
                                EXPECT_EQ(chunk.mStoredSize, chunk.mSize);
                        }
                    }
                    EXPECT_TRUE(anyZstd) << "the standard items contain data that zstd shrinks";
                    EXPECT_EQ(mine->info().mCompressorVersion, static_cast<std::uint32_t>(EdenTest::zstdVersionNumber()));
                    EXPECT_EQ(mine->info().mCompressorLevel, level);
                }
            }
        }

        TEST(EdenArchiveWriter, theCodecChoiceShouldFollowTheRuleOfSection92)
        {
            WriteOptions options;
            options.mChunkSize = 131072;
            const std::vector<Item> items = { pathItem("a/zero.bin", Bytes(100000, 0)),
                pathItem("b/random.bin", EdenTest::patternBytes(100000, 5)),
                pathItem("c/stored.bin", Bytes(100000, 0), true), pathItem("d/text.txt", EdenTest::textBytes(100000)) };
            const std::unique_ptr<Archive> archive = openOrFail(mustWrite(items, options));
            ASSERT_NE(archive, nullptr);
            EXPECT_EQ(archive->chunk(0, 0).mCodec, 1);
            EXPECT_LT(archive->chunk(0, 0).mStoredSize, 1000u);
            EXPECT_EQ(archive->chunk(1, 0).mCodec, 0) << "random data does not shrink by 1/16";
            EXPECT_EQ(archive->chunk(2, 0).mCodec, 0) << "Compression::Store always stores";
            EXPECT_EQ(archive->chunk(3, 0).mCodec, 1);

            // Store mode overrides Compression::Auto.
            options.mStoreOnly = true;
            const std::unique_ptr<Archive> stored = openOrFail(mustWrite(items, options));
            ASSERT_NE(stored, nullptr);
            for (std::size_t i = 0; i < items.size(); ++i)
                EXPECT_EQ(stored->chunk(i, 0).mCodec, 0) << i;
        }

        TEST(EdenArchiveWriter, everyChunkShouldCarryItsHashAndTheFlagThatSaysSo)
        {
            WriteOptions options;
            options.mChunkSize = 4096;
            const std::unique_ptr<Archive> archive = openOrFail(mustWrite(standardItems(), options));
            ASSERT_NE(archive, nullptr);
            for (std::size_t i = 0; i < archive->entryCount(); ++i)
            {
                const EdenArchive::EntryInfo entry = archive->entry(i);
                std::vector<std::uint8_t> content;
                ASSERT_TRUE(archive->readEntry(i, content).ok());
                std::uint64_t at = 0;
                for (std::uint32_t c = 0; c < entry.mChunkCount; ++c)
                {
                    const EdenArchive::ChunkInfo chunk = archive->chunk(i, c);
                    EXPECT_EQ(chunk.mFlags & EdenTest::Fmt::chunkHashPresent, EdenTest::Fmt::chunkHashPresent);
                    EXPECT_EQ(toDigest(chunk.mHash), EdenTest::blake3Of(content.data() + at, chunk.mSize));
                    // The default layout of 8.2: every chunk but the last is exactly chunkSize bytes.
                    if (c + 1 < entry.mChunkCount)
                        EXPECT_EQ(chunk.mSize, 4096u);
                    EXPECT_EQ(chunk.mOffset % 4096 == 0 || c > 0, true);
                    at += chunk.mSize;
                }
                EXPECT_EQ(at, content.size());
            }
        }

        // ---- round trip ----

        TEST(EdenArchiveWriter, writeOpenReadShouldGiveBackEveryEntry)
        {
            WriteOptions options;
            options.mChunkSize = 4096;
            const std::vector<Item> items = standardItems();
            const std::unique_ptr<Archive> archive = openOrFail(mustWrite(items, options));
            ASSERT_NE(archive, nullptr);
            ASSERT_EQ(archive->entryCount(), items.size());
            for (const Item& item : items)
            {
                std::optional<std::size_t> index;
                if (item.mPath.has_value())
                    index = archive->findPath(*item.mPath);
                else
                    index = archive->findKey(*item.mKey);
                ASSERT_TRUE(index.has_value());
                const EdenArchive::EntryInfo entry = archive->entry(*index);
                EXPECT_EQ(entry.mHasPath, item.mPath.has_value());
                EXPECT_EQ(entry.mHasKey, item.mKey.has_value());
                EXPECT_EQ(entry.mType, item.mType);
                EXPECT_EQ(entry.mFlags & 0xFFFF0000u, item.mHintFlags);
                EXPECT_EQ(entry.mHintMemorySize, item.mHintMemorySize);
                EXPECT_EQ(entry.mHintGroup, item.mHintGroup);
                EXPECT_EQ(entry.mLayoutParamA, item.mLayoutParamA);
                EXPECT_EQ(entry.mLayoutParamB, item.mLayoutParamB);
                EXPECT_EQ(entry.mHasProvenance, item.mProvenance.has_value());
                std::vector<std::uint8_t> content;
                ASSERT_TRUE(archive->readEntry(*index, content).ok());
                EXPECT_EQ(content, item.mData);
                if (item.mProvenance.has_value())
                {
                    const auto provenance = archive->provenance(*index);
                    ASSERT_TRUE(provenance.has_value());
                    EXPECT_EQ(std::string(provenance->mToolId), item.mProvenance->mToolId);
                    EXPECT_EQ(toDigest(provenance->mSourceDigest), item.mProvenance->mSourceDigest);
                    EXPECT_EQ(Bytes(provenance->mParameters.begin(), provenance->mParameters.end()),
                        item.mProvenance->mParameters);
                }
            }
        }

        TEST(EdenArchiveWriter, theLargestAcceptedPathAndProvenanceShouldWork)
        {
            const std::string longest = std::string(255, 'a') + "/" + std::string(255, 'b') + "/" + std::string(255, 'c') + "/"
                + std::string(254, 'd') + "/e"; // exactly 1024 bytes, components of at most 255
            ASSERT_EQ(longest.size(), 1024u);
            Item item = pathItem(longest, EdenTest::bytesOf("x"));
            item.mProvenance = EdenTest::ProvenanceSpec{ std::string(255, 't'), EdenTest::Digest{}, Bytes(60000, 0x5A) };
            WriteOptions options;
            options.mStoreOnly = true;
            const Bytes got = mustWrite({ item }, options);
            EXPECT_EQ(got, EdenTest::buildPack(toPackSpec({ item }, options)));
            const std::unique_ptr<Archive> archive = openOrFail(got);
            ASSERT_NE(archive, nullptr);
            EXPECT_TRUE(archive->findPath(longest).has_value());
        }

        // ---- inputs that must be rejected ----

        struct Rejection
        {
            std::string mName;
            std::vector<Item> mItems;
            std::vector<std::string> mClasses; // empty: any class
        };

        std::vector<Rejection> rejections()
        {
            std::vector<Rejection> list;
            const auto name = [&](const char* label, std::string path, std::vector<std::string> classes) {
                list.push_back({ label, { pathItem(std::move(path), EdenTest::bytesOf("x")) }, std::move(classes) });
            };
            name("uppercase", "Abc.txt", { "E_NAME" });
            name("backslash", "a\\b", { "E_NAME" });
            name("doubleslash", "a//b", { "E_NAME" });
            name("leadingslash", "/a", { "E_NAME" });
            name("trailingslash", "a/", { "E_NAME" });
            name("dotdot", "a/../b", { "E_NAME" });
            name("dotcomponent", "a/./b", { "E_NAME" });
            name("dot", ".", { "E_NAME" });
            name("control", "a\x01" "b", { "E_NAME" });
            name("nul", std::string("a\0b", 3), { "E_NAME" });
            name("badutf8", "a\xFF" "b", { "E_NAME" });
            name("overlong", "a\xC0\x80", { "E_NAME" });
            name("empty", "", { "E_LIMIT" });
            name("path1025", std::string(255, 'a') + "/" + std::string(255, 'b') + "/" + std::string(255, 'c') + "/"
                    + std::string(255, 'd') + "/e", { "E_LIMIT" });
            name("component256", std::string(256, 'a'), { "E_LIMIT", "E_NAME" });

            list.push_back({ "duplicatepath", { pathItem("a", {}), pathItem("a", EdenTest::bytesOf("x")) }, { "E_DUPLICATE" } });
            list.push_back({ "duplicatekey", { keyItem(5, {}), keyItem(5, EdenTest::bytesOf("x")) }, { "E_DUPLICATE" } });
            {
                Item both = pathItem("a", {});
                both.mKey = 7;
                list.push_back({ "keyofapathentry", { both, keyItem(7, {}) }, { "E_DUPLICATE" } });
                Item other = pathItem("b", {});
                other.mKey = 7;
                list.push_back({ "samekeytwopaths", { both, other }, { "E_DUPLICATE" } });
            }
            list.push_back({ "noaddress", { Item{} }, { "E_RANGE" } });
            list.push_back({ "keyzero", { keyItem(0, EdenTest::bytesOf("x")) }, { "E_RANGE" } });
            {
                Item tile = pathItem("tile", {});
                tile.mType = 0x10;
                list.push_back({ "tilewithoutkey", { tile }, { "E_RANGE" } });
                Item wrong = keyItem(0x1100000000000001ull, {});
                wrong.mType = 0x10;
                list.push_back({ "tilewrongtag", { wrong }, { "E_RANGE" } });
            }
            const auto provenance = [&](const char* label, std::string tool, Bytes parameters, std::vector<std::string> classes) {
                Item item = pathItem("p", EdenTest::bytesOf("x"));
                item.mProvenance = EdenTest::ProvenanceSpec{ std::move(tool), EdenTest::Digest{}, std::move(parameters) };
                list.push_back({ label, { item }, std::move(classes) });
            };
            provenance("emptytool", "", {}, { "E_LIMIT", "E_RANGE" });
            provenance("tool256", std::string(256, 't'), {}, { "E_LIMIT" });
            provenance("toolnul", std::string("a\0b", 3), {}, { "E_NAME" });
            provenance("toolbadutf8", "\xFF\xFE", {}, { "E_NAME" });
            provenance("params60001", "t", Bytes(60001, 1), { "E_LIMIT" });
            return list;
        }

        TEST(EdenArchiveWriter, invalidInputShouldBeRejectedWithTheRightClassAndWriteNothing)
        {
            for (const Rejection& rejection : rejections())
            {
                SCOPED_TRACE(rejection.mName);
                WriteOptions options;
                options.mStoreOnly = true;
                Bytes out;
                const Error error = writeMemory(rejection.mItems, options, out);
                EXPECT_TRUE(classIs(error, rejection.mClasses))
                    << "got " << className(error) << ", want " << joined(rejection.mClasses);
                EXPECT_TRUE(out.empty()) << "a rejected write must leave the output empty";

                // The same through a file: nothing may appear.
                TempDir dir;
                const auto path = dir.path() / "out.edenpak";
                const Error fileError = EdenArchive::writePack(toInputs(rejection.mItems), path, options);
                EXPECT_TRUE(classIs(fileError, rejection.mClasses)) << className(fileError);
                EXPECT_TRUE(listTree(dir.path()).empty()) << "a rejected write left files behind";
            }
        }

        TEST(EdenArchiveWriter, aBadChunkSizeShouldBeRejected)
        {
            for (const std::uint32_t chunkSize : { 0u, 1u, 1000u, 4095u, 5000u, 2048u, 12288u, (8u << 20) + 4096u, 1u << 24, 1u << 31 })
            {
                SCOPED_TRACE(chunkSize);
                WriteOptions options;
                options.mChunkSize = chunkSize;
                options.mStoreOnly = true;
                Bytes out;
                const Error error = writeMemory({ pathItem("a", EdenTest::bytesOf("x")) }, options, out);
                EXPECT_TRUE(classIs(error, { "E_RANGE", "E_LIMIT" })) << className(error);
                EXPECT_TRUE(out.empty());
            }
            // The smallest and largest allowed sizes work.
            for (const std::uint32_t chunkSize : { 4096u, 8u << 20 })
            {
                WriteOptions options;
                options.mChunkSize = chunkSize;
                options.mStoreOnly = true;
                Bytes out;
                EXPECT_TRUE(writeMemory({ pathItem("a", EdenTest::bytesOf("x")) }, options, out).ok()) << chunkSize;
            }
        }

        // ---- files ----

        TEST(EdenArchiveWriter, writePackShouldCreateTheSameBytesAsWritePackToMemoryAndLeaveNoTemporaryFile)
        {
            TempDir dir;
            WriteOptions options;
            options.mChunkSize = 4096;
            const std::vector<Item> items = standardItems();
            const auto path = dir.path() / "out.edenpak";
            const Error error = EdenArchive::writePack(toInputs(items), path, options);
            ASSERT_TRUE(error.ok()) << error.describe();
            EXPECT_EQ(readFileBytes(path), mustWrite(items, options));
            EXPECT_EQ(listTree(dir.path()), std::vector<std::string>{ "out.edenpak" });
        }

        TEST(EdenArchiveWriter, writePackShouldReplaceAnExistingFileOnlyOnSuccess)
        {
            TempDir dir;
            const auto path = dir.path() / "out.edenpak";
            const Bytes old = EdenTest::bytesOf("old content that is not a pack");
            ASSERT_TRUE(writeFileBytes(path, old));

            WriteOptions options;
            options.mStoreOnly = true;
            // A rejected write leaves the old file exactly as it was.
            const Error bad = EdenArchive::writePack(toInputs({ pathItem("Bad", {}) }), path, options);
            EXPECT_FALSE(bad.ok());
            EXPECT_EQ(readFileBytes(path), old);
            EXPECT_EQ(listTree(dir.path()), std::vector<std::string>{ "out.edenpak" });

            // A good write replaces it.
            const std::vector<Item> items = { pathItem("a", EdenTest::bytesOf("new")) };
            ASSERT_TRUE(EdenArchive::writePack(toInputs(items), path, options).ok());
            EXPECT_EQ(readFileBytes(path), mustWrite(items, options));
            EXPECT_EQ(listTree(dir.path()), std::vector<std::string>{ "out.edenpak" });
        }

        TEST(EdenArchiveWriter, inputFilesShouldBeReadNeverModifiedAndGiveTheSameBytesAsMemory)
        {
            TempDir dir;
            const std::vector<Item> items = { pathItem("a/one.txt", EdenTest::textBytes(9000)),
                pathItem("b/two.bin", EdenTest::patternBytes(5000, 77)), pathItem("c/empty", {}) };
            std::vector<InputEntry> fromFiles;
            std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> before;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const auto file = dir.path() / ("input" + std::to_string(i));
                ASSERT_TRUE(writeFileBytes(file, items[i].mData));
                before.emplace_back(file, std::filesystem::last_write_time(file));
                InputEntry entry = toInput(items[i]);
                entry.mData = EdenTest::bytesOf("ignored: mFile wins"); // mFile is read when it is set
                entry.mFile = file;
                fromFiles.push_back(entry);
            }
            WriteOptions options;
            options.mChunkSize = 4096;
            Bytes out;
            const Error error = EdenArchive::writePackToMemory(fromFiles, out, options);
            ASSERT_TRUE(error.ok()) << error.describe();
            EXPECT_EQ(out, mustWrite(items, options));
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                EXPECT_EQ(readFileBytes(before[i].first), items[i].mData) << "input " << i << " was modified";
                EXPECT_EQ(std::filesystem::last_write_time(before[i].first), before[i].second);
            }
            EXPECT_EQ(listTree(dir.path()).size(), items.size());
        }

        TEST(EdenArchiveWriter, aMissingInputFileShouldBeAnIoErrorAndWriteNothing)
        {
            TempDir dir;
            InputEntry entry = toInput(pathItem("a", {}));
            entry.mFile = dir.path() / "does-not-exist";
            WriteOptions options;
            options.mStoreOnly = true;
            Bytes out;
            const Error memory = EdenArchive::writePackToMemory({ entry }, out, options);
            EXPECT_EQ(className(memory), "E_IO");
            EXPECT_TRUE(out.empty());
            const auto path = dir.path() / "out.edenpak";
            const Error file = EdenArchive::writePack({ entry }, path, options);
            EXPECT_EQ(className(file), "E_IO");
            EXPECT_FALSE(std::filesystem::exists(path));
            EXPECT_EQ(listTree(dir.path()).size(), 0u);
        }

        TEST(EdenArchiveWriter, aLargeInputShouldRoundTripThroughManyChunksAndThreads)
        {
            Bytes big = EdenTest::textBytes(1500000);
            const Bytes noise = EdenTest::patternBytes(500000, 99);
            std::copy(noise.begin(), noise.end(), big.begin() + 300000);
            const std::vector<Item> items = { pathItem("big.bin", big) };
            WriteOptions options;
            options.mChunkSize = 65536;
            options.mThreads = 4;
            const std::unique_ptr<Archive> archive = openOrFail(mustWrite(items, options));
            ASSERT_NE(archive, nullptr);
            EXPECT_EQ(archive->entry(0).mChunkCount, (big.size() + 65535) / 65536);
            std::vector<std::uint8_t> content;
            ASSERT_TRUE(archive->readEntry(0, content).ok());
            EXPECT_EQ(content, big);
        }
    }
}
