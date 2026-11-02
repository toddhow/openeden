// Eden Archive test support (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
//
// The conformance corpus. Every case below is made from the specification alone: the valid packs of Annex D item 1,
// the twenty invalid packs of Annex C (each one change of the Annex B pack), the "further cases" of Annex C, and the
// hostile files of Annex D item 3. The expected class of each invalid case is the class of the FIRST failing check
// in the order of section 14.2; where the specification is ambiguous or contradicts itself the case lists every
// class that a reading allows (see the SPEC QUESTIONS in the task report).
#include "corpuscases.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string_view>

#include "eapackmodel.hpp"

namespace EdenTest
{
    namespace
    {
        constexpr std::uint64_t maxU64 = ~std::uint64_t{ 0 };

        // Annex B offsets (hexadecimal in the specification).
        constexpr std::uint64_t footerAt = 0x4240;
        constexpr std::uint64_t entry0At = 0x4018;
        constexpr std::uint64_t entry1At = 0x4088;
        constexpr std::uint64_t entry2At = 0x40F8;
        constexpr std::uint64_t chunk0At = 0x4198;
        constexpr std::uint64_t chunk1At = 0x41D0;
        constexpr std::uint64_t chunk2At = 0x4208;

        ExpectedEntry expectedOf(const EntrySpec& entry, std::uint32_t chunkSize)
        {
            ExpectedEntry out;
            out.mPath = entry.mPath;
            out.mKey = entry.mKey;
            out.mType = entry.mType;
            if (!entry.mChunks.empty())
            {
                for (const ChunkSpec& chunk : entry.mChunks)
                    appendBytes(out.mContent, chunk.mRaw);
                out.mChunkCount = static_cast<std::uint32_t>(entry.mChunks.size());
            }
            else
            {
                out.mContent = entry.mContent;
                out.mChunkCount = static_cast<std::uint32_t>((entry.mContent.size() + chunkSize - 1) / chunkSize);
            }
            if (entry.mProvenance.has_value())
            {
                out.mHasProvenance = true;
                out.mToolId = entry.mProvenance->mToolId;
                out.mParameters = entry.mProvenance->mParameters;
            }
            return out;
        }

        std::vector<ExpectedEntry> expectedOf(const PackSpec& spec)
        {
            std::vector<ExpectedEntry> out;
            for (const EntrySpec& entry : entryOrder(spec))
                out.push_back(expectedOf(entry, spec.mChunkSize));
            return out;
        }

        EntrySpec pathEntry(std::string path, Bytes content = {}, CodecChoice codec = CodecChoice::Store)
        {
            EntrySpec entry;
            entry.mPath = std::move(path);
            entry.mContent = std::move(content);
            entry.mCodec = codec;
            return entry;
        }

        EntrySpec keyEntry(std::uint64_t key, Bytes content = {})
        {
            EntrySpec entry;
            entry.mKey = key;
            entry.mContent = std::move(content);
            return entry;
        }

        PackSpec specOf(std::vector<EntrySpec> entries, std::uint32_t chunkSize = Fmt::defaultChunkSize)
        {
            PackSpec spec;
            spec.mEntries = std::move(entries);
            spec.mChunkSize = chunkSize;
            return spec;
        }

        std::string hex(std::uint64_t value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%04llx", static_cast<unsigned long long>(value));
            return buffer;
        }

        struct Edit
        {
            std::uint64_t mOffset;
            std::uint64_t mValue;
            unsigned mWidth;
        };

        class Corpus
        {
        public:
            std::vector<CorpusCase> mCases;

            Corpus()
                : mAnnexB(annexBPack())
                , mAnnexSpec(annexBSpec())
            {
            }

            // ---- adders ----

            CorpusCase& addValid(std::string id, std::string description, const PackSpec& spec,
                CanonicalResult canonical = CanonicalResult::Pass)
            {
                CorpusCase c;
                c.mId = std::move(id);
                c.mDescription = std::move(description);
                c.mBytes = buildPack(spec);
                c.mOutcome = Outcome::Valid;
                c.mEntries = expectedOf(spec);
                c.mCanonical = canonical;
                mCases.push_back(std::move(c));
                return mCases.back();
            }

            CorpusCase& addInvalid(std::string id, std::string description, Bytes bytes,
                std::vector<std::string> classes, std::optional<std::uint64_t> offset = std::nullopt)
            {
                CorpusCase c;
                c.mId = std::move(id);
                c.mDescription = std::move(description);
                c.mBytes = std::move(bytes);
                c.mOutcome = Outcome::InvalidAtOpen;
                c.mClasses = std::move(classes);
                c.mOffset = offset;
                mCases.push_back(std::move(c));
                return mCases.back();
            }

            /// A pack that opens, where reading entry `failing` must fail with one of `classes`.
            CorpusCase& addInvalidRead(std::string id, std::string description, Bytes bytes,
                std::vector<ExpectedEntry> entries, std::size_t failing, std::vector<std::string> classes)
            {
                CorpusCase c;
                c.mId = std::move(id);
                c.mDescription = std::move(description);
                c.mBytes = std::move(bytes);
                c.mOutcome = Outcome::InvalidAtRead;
                c.mClasses = std::move(classes);
                c.mFailingEntry = failing;
                c.mEntries = std::move(entries);
                // The failing entry keeps its true content so that tests can compare the ranges that do read.
                if (failing < c.mEntries.size())
                    c.mEntries[failing].mReadable = false;
                mCases.push_back(std::move(c));
                return mCases.back();
            }

            // ---- Annex B variants ----

            Bytes annexVariant(const std::vector<Edit>& edits, const Recompute& recomputeWhat = {}, bool recomputeAtAll = true)
            {
                Bytes pack = mAnnexB;
                for (const Edit& edit : edits)
                    patch(pack, edit.mOffset, edit.mValue, edit.mWidth);
                if (recomputeAtAll)
                    recompute(pack, recomputeWhat);
                return pack;
            }

            /// Changes the Annex B image with `change` before it is written (digests are recomputed).
            Bytes annexImage(const std::function<void(PackImage&)>& change)
            {
                PackSpec spec = mAnnexSpec;
                spec.mMutate = change;
                return buildPack(spec);
            }

            std::vector<ExpectedEntry> annexEntries() const { return expectedOf(mAnnexSpec); }

            void variantOpen(std::string id, std::string description, const std::vector<Edit>& edits,
                std::vector<std::string> classes, std::optional<std::uint64_t> offset = std::nullopt,
                const Recompute& recomputeWhat = {}, bool recomputeAtAll = true)
            {
                addInvalid(std::move(id), std::move(description), annexVariant(edits, recomputeWhat, recomputeAtAll),
                    std::move(classes), offset);
            }

            void variantRead(std::string id, std::string description, const std::vector<Edit>& edits,
                std::size_t failing, std::vector<std::string> classes, const Recompute& recomputeWhat = {},
                bool recomputeAtAll = true)
            {
                addInvalidRead(std::move(id), std::move(description), annexVariant(edits, recomputeWhat, recomputeAtAll),
                    annexEntries(), failing, std::move(classes));
            }

            Bytes mAnnexB;
            PackSpec mAnnexSpec;
        };

        Edit text(std::uint64_t offset, std::string_view value)
        {
            // An Edit holds a number, so text is written as one Edit per byte by the caller; see textEdits().
            return { offset, static_cast<std::uint8_t>(value.empty() ? 0 : value[0]), 1 };
        }

        std::vector<Edit> textEdits(std::uint64_t offset, std::string_view value)
        {
            std::vector<Edit> edits;
            for (std::size_t i = 0; i < value.size(); ++i)
                edits.push_back(text(offset + i, value.substr(i, 1)));
            return edits;
        }

        std::string repeatChar(char c, std::size_t count)
        {
            return std::string(count, c);
        }

        // ------------------------------------------------------------------------------------------------------
        // Valid packs (Annex D item 1)
        // ------------------------------------------------------------------------------------------------------

        void addValidCases(Corpus& corpus)
        {
            {
                CorpusCase& c = corpus.addValid("v01annexb", "annex-b-worked-example", corpus.mAnnexSpec);
                c.mBytes = corpus.mAnnexB;
            }
            corpus.addValid("v02empty", "empty-pack-zero-entries", PackSpec{});
            corpus.addValid("v03oneempty", "one-empty-entry", specOf({ pathEntry("empty.txt") }));
            corpus.addValid("v04exactchunk", "entry-of-exactly-chunksize",
                specOf({ pathEntry("exact.bin", patternBytes(4096, 4)) }, 4096));
            corpus.addValid("v05chunkplus1", "entry-of-chunksize-plus-one",
                specOf({ pathEntry("plus1.bin", patternBytes(4097, 5)) }, 4096));
            corpus.addValid("v06threechunks", "entry-of-three-chunks",
                specOf({ pathEntry("three.bin", patternBytes(2 * 4096 + 100, 6)) }, 4096));

            {
                // All registered entry types (section 11.2): the five plain types and the four tile types.
                std::vector<EntrySpec> entries;
                entries.push_back(pathEntry("types/generic.bin", bytesOf("generic")));
                EntrySpec texture = pathEntry("types/texture.ktx2", bytesOf("texture"));
                texture.mType = 0x0001;
                texture.mLayoutParamA = 5;
                texture.mLayoutParamB = 2;
                entries.push_back(texture);
                EntrySpec mesh = pathEntry("types/mesh.glb", bytesOf("mesh"));
                mesh.mType = 0x0002;
                entries.push_back(mesh);
                EntrySpec audio = pathEntry("types/audio.ogg", bytesOf("audio"));
                audio.mType = 0x0003;
                entries.push_back(audio);
                EntrySpec shader = pathEntry("types/shader.spv", bytesOf("shader"));
                shader.mType = 0x0004;
                entries.push_back(shader);
                for (std::uint16_t tag = 0x10; tag <= 0x13; ++tag)
                {
                    EntrySpec tile = keyEntry((static_cast<std::uint64_t>(tag) << 56) | 0x1234, bytesOf("tile"));
                    tile.mType = tag;
                    entries.push_back(tile);
                }
                corpus.addValid("v07alltypes", "all-registered-entry-types", specOf(std::move(entries)));
            }

            {
                // 5 components of 255, 255, 255, 254 and 1 bytes plus 4 separators: exactly 1024 bytes.
                const std::string path = repeatChar('a', 255) + "/" + repeatChar('b', 255) + "/" + repeatChar('c', 255)
                    + "/" + repeatChar('d', 254) + "/e";
                corpus.addValid("v08path1024", "path-of-1024-bytes", specOf({ pathEntry(path, bytesOf("long")) }));
            }

            corpus.addValid("v09utf8path", "non-ascii-utf8-paths-in-unsigned-byte-order",
                specOf({ pathEntry("a.txt", bytesOf("a")), pathEntry("z.txt", bytesOf("z")),
                    pathEntry("\xC3\xA4/\xE6\x97\xA5.txt", bytesOf("umlaut")) }));

            corpus.addValid("v10keyonly", "keyed-only-pack-unsigned-key-order",
                specOf({ keyEntry(0x8000000000000000ull, bytesOf("high")), keyEntry(5, bytesOf("five")),
                    keyEntry(0xFFFFFFFFFFFFFFFEull, bytesOf("max")), keyEntry(0x7FFFFFFFFFFFFFFFull, bytesOf("mid")),
                    keyEntry(0x00FFFFFFFFFFFFFFull, bytesOf("tag0max")) }));

            {
                EntrySpec both = pathEntry("a.txt", bytesOf("both"));
                both.mKey = 0x42;
                corpus.addValid("v11pathandkey", "entry-with-path-and-key",
                    specOf({ both, pathEntry("b.txt", bytesOf("b")), keyEntry(0x41, bytesOf("keyonly")) }));
            }

            {
                EntrySpec a = pathEntry("p/a.bin", bytesOf("with provenance"));
                a.mProvenance = ProvenanceSpec{ "tilebaker/0.3.1", blake3Of(bytesOf("sources")), { 1, 2, 3, 4, 5 } };
                EntrySpec c = pathEntry("p/c.bin", bytesOf("second"));
                c.mProvenance = ProvenanceSpec{ "t/2", blake3Of(bytesOf("other")), {} };
                EntrySpec k = keyEntry(0x77, bytesOf("keyed"));
                k.mProvenance = ProvenanceSpec{ "keytool/1", blake3Of(bytesOf("key")), patternBytes(40, 12) };
                corpus.addValid("v12provenance", "entries-with-provenance-records",
                    specOf({ a, pathEntry("p/b.bin", bytesOf("without")), c, k }));
            }

            {
                PackSpec spec = specOf({});
                EntrySpec a = pathEntry("derived/tile.bin", bytesOf("derived"));
                a.mProvenance = ProvenanceSpec{ "tilebaker/0.3.1", blake3Of(bytesOf("inputs")), { 9 } };
                spec.mEntries.push_back(a);
                spec.mPackFlags = Fmt::packDerived;
                spec.mGeneration = 7;
                spec.mSourceDigest = blake3Of(bytesOf("source digest"));
                corpus.addValid("v13derived", "derived-pack-with-generation-and-source-digest", spec);
            }

            {
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("ignorable")), pathEntry("b.txt", bytesOf("bits")) });
                spec.mPackFlags = (1ull << 33) | (1ull << 63);
                spec.mEntries[0].mExtraFlags = (1u << 20) | (1u << 31);
                spec.mEntries[1].mExtraFlags = 1u << 25;
                corpus.addValid("v14ignorablebits", "ignorable-unknown-pack-and-entry-flag-bits", spec, CanonicalResult::Fail);
            }

            {
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("reserved")), pathEntry("p.txt", bytesOf("prov")) });
                spec.mEntries[1].mProvenance = ProvenanceSpec{ "t/1", blake3Of(bytesOf("x")), { 1 } };
                spec.mMutate = [](PackImage& image) {
                    putLe(image.mHeader, Fmt::hReserved52, 0xDEADBEEF, 4);
                    putLe(image.mHeader, Fmt::hReserved304 + 3, 0xA5, 1);
                    putLe(image.mHeader, Fmt::hReserved304 + 175, 0x5A, 1);
                    image.mEntries[0].mReserved22 = 0x1234;
                    image.mEntries[0].mReserved104 = 0x1122334455667788ull;
                    image.mChunks[0].mReserved20 = 0x99;
                    putLe(image.mProvenance, 6, 1, 2); // record reserved u16
                    putLe(image.mProvenance, 44, 2, 4); // record reserved u32
                    image.mFooter[Fmt::fReserved] = 0x7F;
                    image.mFooter[Fmt::footerSize - 1] = 0x7F;
                };
                corpus.addValid("v15reservednonzero", "non-zero-reserved-fields-everywhere", spec, CanonicalResult::Fail);
            }

            {
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("hint values without flags")) });
                spec.mEntries[0].mHintMemorySize = 123;
                spec.mEntries[0].mHintGroup = 4;
                spec.mEntries[0].mLayoutParamA = 8;
                corpus.addValid("v16hintsnoflag", "hint-values-while-their-flags-are-clear", spec, CanonicalResult::Fail);
            }

            {
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("hints")) });
                spec.mEntries[0].mExtraFlags
                    = Fmt::entryPinned | Fmt::entryCoarseFirst | Fmt::entryHintGroup | Fmt::entryHintMemSize;
                spec.mEntries[0].mHintMemorySize = 4096;
                spec.mEntries[0].mHintGroup = 3;
                corpus.addValid("v17hintsvalid", "all-four-hints-set", spec);
            }

            {
                EntrySpec unknown = pathEntry("a.bin", bytesOf("unknown type"));
                unknown.mType = 0x1234;
                EntrySpec experimental = pathEntry("b.bin", bytesOf("experimental type"));
                experimental.mType = 0xF001;
                EntrySpec reserved = pathEntry("c.bin", bytesOf("reserved core type"));
                reserved.mType = 0x0007;
                corpus.addValid("v18unknowntype", "unknown-entry-types-read-as-generic",
                    specOf({ unknown, experimental, reserved }), CanonicalResult::Unspecified);
            }

            {
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("extension")) });
                spec.mExtensions.push_back({ 0x80000001u, 0, bytesOf("hello") });
                corpus.addValid("v19extskip", "unknown-extension-block-without-must-understand", spec, CanonicalResult::Fail);
            }

            {
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("extension")) });
                spec.mExtensions.push_back({ 1u, 1u, bytesOf("tool/1.0") });
                corpus.addValid("v20extknownmu", "known-writer-info-block-with-must-understand", spec, CanonicalResult::Fail);
            }

            {
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("extension")) });
                spec.mExtensions.push_back({ 0x80000002u, 0xFFFFFFFEu, {} });
                spec.mExtensions.push_back({ 1u, 0, {} });
                corpus.addValid("v21extflagbits", "extension-blocks-with-ignored-flag-bits-and-no-payload", spec,
                    CanonicalResult::Fail);
            }

            {
                std::vector<EntrySpec> entries;
                entries.push_back(pathEntry("z/text.txt", textBytes(10000), CodecChoice::Zstd));
                entries.push_back(pathEntry("z/rand.bin", patternBytes(5000, 22), CodecChoice::Store));
                entries.push_back(pathEntry("z/zero.bin", Bytes(4096, 0), CodecChoice::Zstd));
                CorpusCase& c = corpus.addValid("v22zstdmulti", "real-zstd-frames-in-several-chunks", specOf(entries, 4096));
                c.mUsesRealZstd = true;
            }

            {
                CorpusCase& c = corpus.addValid("v23zstdchecksum", "real-zstd-frame-with-content-checksum",
                    specOf({ pathEntry("c/text.txt", textBytes(3000), CodecChoice::ZstdChecksum) }, 4096),
                    CanonicalResult::Unspecified);
                c.mUsesRealZstd = true;
            }

            {
                PackSpec spec = specOf({ pathEntry("s/a.bin", bytesOf("stride a")), pathEntry("s/b.bin", patternBytes(5000, 24)) }, 4096);
                spec.mEntryRecordSize = 128;
                spec.mChunkRecordSize = 64;
                spec.mRecordPadByte = 0xAA;
                corpus.addValid("v24stride", "larger-entry-and-chunk-record-strides", spec, CanonicalResult::Fail);
            }

            {
                EntrySpec a = pathEntry("g/a.bin");
                ChunkSpec first;
                first.mRaw = patternBytes(4096, 25);
                ChunkSpec second;
                second.mRaw = patternBytes(4096, 26);
                second.mGapBefore = 16;
                a.mChunks = { first, second };
                EntrySpec b = pathEntry("g/b.bin", patternBytes(100, 27));
                b.mBlockGap = 4096;
                corpus.addValid("v25gaps", "zero-gaps-between-chunks-and-blocks", specOf({ a, b }, 4096),
                    CanonicalResult::Fail);
            }

            {
                EntrySpec a = pathEntry("l/a.bin");
                ChunkSpec first;
                first.mRaw = patternBytes(5000, 28);
                ChunkSpec second;
                second.mRaw = patternBytes(5000, 29);
                a.mChunks = { first, second };
                corpus.addValid("v26nondefaultlayout", "chunks-not-cut-at-chunksize", specOf({ a }, 4096),
                    CanonicalResult::Fail);
            }

            {
                EntrySpec a = pathEntry("l/big.bin");
                ChunkSpec one;
                one.mRaw = patternBytes(8000, 30);
                a.mChunks = { one };
                corpus.addValid("v27bigchunk", "one-chunk-larger-than-the-nominal-chunksize", specOf({ a }, 4096),
                    CanonicalResult::Fail);
            }

            {
                EntrySpec a = pathEntry("bomb.bin");
                ChunkSpec one;
                one.mRaw = Bytes(8u << 20, 0x41);
                one.mChoice = CodecChoice::HandRle;
                a.mChunks = { one };
                corpus.addValid("v28bomb8m", "tiny-run-length-frame-that-declares-8-mib", specOf({ a }, 8u << 20));
            }

            {
                EntrySpec a = pathEntry("d/zero.bin");
                ChunkSpec one;
                one.mRaw = Bytes(64, 0x41);
                one.mChoice = CodecChoice::HandRle;
                FrameOptions options;
                options.mDictionaryId = 0;
                one.mStored = makeRleFrame(0x41, 64, options);
                a.mChunks = { one };
                corpus.addValid("v29dictidzero", "frame-with-a-dictionary-id-field-holding-zero", specOf({ a }, 4096),
                    CanonicalResult::Unspecified);
            }

            {
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("chunk flags")) });
                spec.mMutate = [](PackImage& image) { image.mChunks[0].mFlags = 0x0101; };
                corpus.addValid("v30chunkflagbits", "ignorable-chunk-flag-bits", spec, CanonicalResult::Fail);
            }

            {
                std::vector<EntrySpec> entries;
                for (int i = 0; i < 300; ++i)
                {
                    char name[32];
                    std::snprintf(name, sizeof(name), "many/e%04d", i);
                    entries.push_back(pathEntry(name));
                }
                for (std::uint64_t i = 1; i <= 100; ++i)
                    entries.push_back(keyEntry(i * 977));
                entries.push_back(pathEntry("many/full1.bin", bytesOf("one")));
                entries.push_back(pathEntry("many/full2.bin", bytesOf("two")));
                entries.push_back(keyEntry(5000000, bytesOf("three")));
                corpus.addValid("v31many", "many-entries-mostly-empty", specOf(entries));
            }

            corpus.addValid("v32pathorder", "path-order-does-not-treat-slash-specially",
                specOf({ pathEntry("a0", bytesOf("3")), pathEntry("a/b", bytesOf("2")), pathEntry("a-x", bytesOf("1")),
                    pathEntry("ab", bytesOf("4")), pathEntry("abc", bytesOf("5")) }));

            {
                // A path that is also a directory prefix (7.3.2): the reader may accept it, the canonical validator
                // rejects it with E_NAME.
                CorpusCase& c = corpus.addValid("v33dirprefix", "file-and-directory-with-one-name",
                    specOf({ pathEntry("a", bytesOf("file")), pathEntry("a/b", bytesOf("inside")) }),
                    CanonicalResult::Fail);
                c.mCanonicalClasses = { "E_NAME" };
                c.mReaderMayReject = true;
            }

            {
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("compressor fields")) });
                spec.mCompressorVersion = 10507;
                spec.mCompressorLevel = 19;
                corpus.addValid("v34compressorfields", "informative-compressor-fields-set-without-zstd-chunks", spec,
                    CanonicalResult::Unspecified);
            }
        }

        // ------------------------------------------------------------------------------------------------------
        // The twenty invalid packs of Annex C
        // ------------------------------------------------------------------------------------------------------

        void addAnnexCCases(Corpus& corpus)
        {
            const Recompute none{ false, false, false, false };
            const Recompute headerAndFooter{ false, false, true, true };
            const Recompute withIndex{ true, true, true, true };

            corpus.variantOpen("i01magic", "magic-byte-changed", { { 0, 0x58, 1 } }, { "E_MAGIC" }, 0, none);
            {
                Bytes cut = corpus.mAnnexB;
                cut.resize(cut.size() - 16);
                corpus.addInvalid("i02cut16", "file-cut-by-16-bytes", std::move(cut), { "E_TRUNCATED" });
            }
            corpus.variantOpen("i03checksum", "reserved-header-byte-changed-checksum-stale", { { 0x190, 1, 1 } },
                { "E_CHECKSUM" }, std::nullopt, none);
            corpus.variantOpen("i04filesize", "filesize-in-header-and-footer-plus-8", { { 0x18, 0x42C8, 8 }, { 0x4250, 0x42C8, 8 } },
                { "E_TRUNCATED" }, std::nullopt, headerAndFooter);
            {
                Bytes longer = corpus.mAnnexB;
                longer.resize(longer.size() + 8, 0);
                corpus.addInvalid("i05append8", "eight-zero-bytes-after-the-footer", std::move(longer), { "E_TRUNCATED" });
            }
            corpus.variantOpen("i06chunkoffset", "chunk0-offset-far-outside", { { chunk0At, 0xFFFFFFFFFFFFF000ull, 8 } },
                { "E_RANGE" }, chunk0At);
            corpus.variantOpen("i07overlap", "chunk2-offset-inside-entry1-block", { { chunk2At, 0x2000, 8 } },
                { "E_OVERLAP" }, chunk2At);
            corpus.variantOpen("i08traversal", "entry0-name-with-dotdot", textEdits(0x4000, "data/../b.bin"),
                { "E_NAME" }, std::nullopt, withIndex);
            corpus.variantOpen("i09duplicate", "entry1-name-equals-entry0",
                { { entry1At + 8, 0, 8 }, { entry1At + 16, 13, 4 } }, { "E_DUPLICATE" }, std::nullopt, withIndex);
            corpus.variantOpen("i10size", "entry0-size-max", { { entry0At + 32, maxU64, 8 } }, { "E_LIMIT" }, entry0At + 32);
            corpus.variantOpen("i11chunksize", "chunk0-size-16-mib", { { chunk0At + 12, 0x01000000, 4 } }, { "E_LIMIT" },
                chunk0At + 12);
            corpus.variantRead("i12framesize", "zstd-frame-content-size-changed", { { 0x1005, 0xFF, 1 } }, 0, { "E_CODEC" }, none);
            corpus.variantRead("i13flip", "first-content-byte-flipped", { { 0x2000, 'H' ^ 0xFF, 1 } }, 1, { "E_HASH" }, none);
            corpus.variantOpen("i14critflag", "unknown-critical-pack-flag-bit-5", { { 0x10, 0x20, 8 } }, { "E_UNSUPPORTED" }, 0x10);
            corpus.variantRead("i15codec7", "chunk1-codec-7", { { chunk1At + 16, 7, 2 } }, 1, { "E_UNSUPPORTED" });
            corpus.variantOpen("i16order", "entry1-name-sorts-before-entry0", textEdits(0x400D, "aaa"), { "E_ORDER" },
                std::nullopt, withIndex);
            corpus.variantOpen("i17digest", "string-table-padding-changed-digest-stale", { { 0x4017, 1, 1 } },
                { "E_CHECKSUM" }, std::nullopt, none);
            corpus.variantOpen("i18pathhash", "first-path-index-hash-zero", { { 0x4168, 0, 8 } }, { "E_INDEX" }, 0x4168);
            corpus.variantOpen("i19chunktable", "entry0-chunktable-offset-outside", { { entry0At + 40, 0x4248, 8 } },
                { "E_RANGE" }, entry0At + 40);
            corpus.variantOpen("i20entryflags", "entry2-unknown-critical-flag-bit-5", { { entry2At + 24, 0x00010022, 4 } },
                { "E_UNSUPPORTED" }, entry2At + 24);
        }

        // ------------------------------------------------------------------------------------------------------
        // The further cases of Annex C (header and footer fields)
        // ------------------------------------------------------------------------------------------------------

        void addHeaderCases(Corpus& corpus)
        {
            const Recompute none{ false, false, false, false };
            const Recompute withIndex{ true, true, true, true };

            corpus.variantOpen("f01version02", "version-0-2", { { 0x0A, 2, 2 }, { footerAt + 10, 2, 2 } }, { "E_VERSION" }, 0x0A);
            corpus.variantOpen("f02version10", "version-1-0",
                { { 0x08, 1, 2 }, { 0x0A, 0, 2 }, { footerAt + 8, 1, 2 }, { footerAt + 10, 0, 2 } }, { "E_VERSION" });
            corpus.variantOpen("f03generation", "generation-byte-2", { { 7, 2, 1 } }, { "E_VERSION" }, 7, none);
            corpus.variantOpen("f04footermetaoffset", "footer-metadata-offset-differs", { { footerAt + 24, 0x5000, 8 } },
                { "E_CHECKSUM" }, std::nullopt, none);
            corpus.variantOpen("f05footermetasize", "footer-metadata-size-differs", { { footerAt + 32, 0x248, 8 } },
                { "E_CHECKSUM" }, std::nullopt, none);
            corpus.variantOpen("f06footerdigest", "footer-metadata-digest-byte-differs", { { footerAt + 40, 0x11, 1 } },
                { "E_CHECKSUM" }, std::nullopt, none);
            corpus.variantOpen("f07footerchecksum", "footer-header-checksum-byte-differs", { { footerAt + 72, 0x22, 1 } },
                { "E_CHECKSUM" }, std::nullopt, none);
            corpus.variantOpen("f08footerfilesize", "footer-filesize-differs", { { footerAt + 16, 0x42C8, 8 } },
                { "E_CHECKSUM" }, std::nullopt, none);
            corpus.variantOpen("f09footerversion", "footer-version-differs", { { footerAt + 10, 2, 2 } }, { "E_CHECKSUM" },
                std::nullopt, none);
            corpus.variantOpen("f10footersize", "footer-size-64", { { footerAt + 12, 64, 4 } }, { "E_RANGE" }, footerAt + 12, none);
            corpus.variantOpen("f11pathcount", "path-entry-count-above-entry-count", { { 0x40, 4, 8 } }, { "E_RANGE" }, 0x40);
            corpus.variantOpen("f12keycountmax", "key-entry-count-max", { { 0x48, maxU64, 8 } }, { "E_RANGE" }, 0x48);
            corpus.variantOpen("f13entrycountmax", "entry-count-max", { { 0x38, maxU64, 8 } }, { "E_LIMIT" }, 0x38);
            corpus.variantOpen("f14entrycount2p22p1", "entry-count-2-pow-22-plus-1-in-a-tiny-file", { { 0x38, 4194305, 8 } },
                { "E_LIMIT" }, 0x38);
            corpus.variantOpen("f15entrycountabovesum", "entry-count-above-path-plus-key-count", { { 0x38, 4, 8 } },
                { "E_RANGE" }, 0x38);
            corpus.variantOpen("f16chunksize5000", "chunksize-not-a-power-of-two", { { 0x20, 5000, 4 } }, { "E_RANGE" }, 0x20);
            corpus.variantOpen("f17chunksize2048", "chunksize-below-4096", { { 0x20, 2048, 4 } }, { "E_RANGE" }, 0x20);
            corpus.variantOpen("f18chunksize2p24", "chunksize-above-8-mib", { { 0x20, 1u << 24, 4 } }, { "E_RANGE" }, 0x20);
            corpus.variantOpen("f19entrystride100", "entry-record-size-not-a-multiple-of-8", { { 0x24, 100, 4 } }, { "E_RANGE" }, 0x24);
            corpus.variantOpen("f20entrystride520", "entry-record-size-above-512", { { 0x24, 520, 4 } }, { "E_RANGE" }, 0x24);
            corpus.variantOpen("f21entrystride104", "entry-record-size-below-112", { { 0x24, 104, 4 } }, { "E_RANGE" }, 0x24);
            corpus.variantOpen("f22chunkstride48", "chunk-record-size-below-56", { { 0x28, 48, 4 } }, { "E_RANGE" }, 0x28);
            corpus.variantOpen("f23chunkstride264", "chunk-record-size-above-256", { { 0x28, 264, 4 } }, { "E_RANGE" }, 0x28);
            corpus.variantOpen("f24headersize1024", "header-size-1024", { { 0x0C, 1024, 4 } }, { "E_VERSION" }, 0x0C, none);
            corpus.variantOpen("f25dataoffset", "data-offset-8192", { { 0x50, 0x2000, 8 } }, { "E_RANGE" }, 0x50);
            corpus.variantOpen("f26datasizemax", "data-size-max", { { 0x58, maxU64, 8 } }, { "E_RANGE" }, 0x58);
            corpus.variantOpen("f27metadataoffset", "metadata-offset-not-the-smallest-multiple", { { 0x60, 0x5000, 8 } },
                { "E_RANGE" }, 0x60);
            corpus.variantOpen("f28metadatasize", "metadata-size-above-2-pow-32", { { 0x68, 0x100000008ull, 8 } },
                { "E_LIMIT" }, 0x68);
            corpus.variantOpen("f29datasizegap", "data-size-longer-than-the-last-chunk", { { 0x58, 0x2008, 8 } },
                { "E_RANGE" }, 0x58);
            corpus.variantOpen("f30extensionsizebig", "extension-size-above-16-mib", { { 0xC0, 0x1000008, 8 } },
                { "E_LIMIT" }, 0xC0);
            corpus.variantOpen("f31stringsize", "string-table-size-does-not-tile", { { 0x78, 0x20, 8 } }, { "E_RANGE" });
            corpus.variantOpen("f32chunktablessizemax", "chunk-tables-size-near-max", { { 0xA0, 0xFFFFFFFFFFFFFFF8ull, 8 } },
                { "E_RANGE" });
            corpus.variantOpen("f33tilingoverflow", "chunk-tables-and-provenance-sizes-overflow-the-sum",
                { { 0xA0, 1ull << 63, 8 }, { 0xB0, 1ull << 63, 8 } }, { "E_RANGE" });
            corpus.variantOpen("f34pathindexoverlap", "path-index-starts-where-the-entry-table-starts",
                { { 0x88, entry0At, 8 } }, { "E_RANGE", "E_OVERLAP" });
            corpus.variantOpen("f35stringoffset", "string-table-not-at-metadata-offset", { { 0x70, 0x4008, 8 } },
                { "E_RANGE", "E_OVERLAP" });
            corpus.variantOpen("f36padafterheader", "non-zero-padding-after-the-header", { { 0x800, 1, 1 } }, { "E_RANGE" },
                0x800, none, false);
            corpus.variantOpen("f37padafterdata", "non-zero-padding-before-the-metadata", { { 0x3100, 1, 1 } }, { "E_RANGE" },
                0x3100, none, false);
            corpus.variantOpen("f38noaddress", "entry0-has-neither-path-nor-key", { { entry0At + 24, 0, 4 } }, { "E_RANGE" },
                entry0At + 24);
            corpus.variantOpen("f39keynoflag", "non-zero-key-without-has-key", { { entry0At, 5, 8 } }, { "E_RANGE" }, entry0At);
            corpus.variantOpen("f40keyzero", "has-key-with-key-zero", { { entry2At, 0, 8 } }, { "E_RANGE" }, entry2At);
            corpus.variantOpen("f41typenokey", "tile-type-without-key", { { entry0At + 20, 0x10, 2 } }, { "E_RANGE" }, entry0At + 20);
            corpus.variantOpen("f42tagmismatch", "tile-type-with-another-tag",
                { { entry2At + 20, 0x11, 2 } }, { "E_RANGE" }, entry2At + 20);
            corpus.variantOpen("f43pathlengthnopath", "path-length-without-has-path", { { entry2At + 16, 4, 4 } }, { "E_RANGE" },
                entry2At + 16);
            corpus.variantOpen("f44provoffsetnoflag", "provenance-offset-without-the-flag", { { entry0At + 80, 8, 8 } },
                { "E_RANGE" }, entry0At + 80);
            corpus.variantOpen("f45chunkcounthuge", "chunk-count-above-2-pow-20", { { entry0At + 28, 0x100001, 4 } },
                { "E_LIMIT" }, entry0At + 28);
            corpus.variantOpen("f46sizeabove4g", "entry-size-above-4-gib", { { entry0At + 32, 0x100000001ull, 8 } },
                { "E_LIMIT" }, entry0At + 32);
            corpus.variantOpen("f47chunkcountzero", "chunk-count-zero-with-size", { { entry0At + 28, 0, 4 } }, { "E_RANGE" },
                entry0At + 28);
            corpus.variantOpen("f48sizezero", "size-zero-with-one-chunk", { { entry0At + 32, 0, 8 } }, { "E_RANGE" },
                entry0At + 32);
            corpus.variantOpen("f49pathlength0", "has-path-with-length-zero", { { entry1At + 16, 0, 4 } }, { "E_LIMIT" },
                entry1At + 16);
            corpus.variantOpen("f50pathoutside", "path-longer-than-the-string-table", { { entry1At + 16, 100, 4 } },
                { "E_RANGE" }, entry1At + 16);
            corpus.variantOpen("f51pathgap", "second-path-does-not-follow-the-first",
                { { entry1At + 8, 14, 8 }, { entry1At + 16, 9, 4 } }, { "E_RANGE" }, entry1At + 8, withIndex);
            corpus.variantOpen("f52stringpad", "string-table-padding-not-zero", { { 0x4017, 1, 1 } }, { "E_RANGE" }, 0x4017);
            corpus.addInvalid("f53stringextra", "string-table-longer-than-the-paths",
                corpus.annexImage([](PackImage& image) {
                    image.mStringTable.resize(32, 0);
                    relayoutRegions(image);
                    fixDerivedOffsets(image);
                }),
                { "E_RANGE" });
            corpus.variantOpen("f54pathindexentry", "path-index-entry-index-out-of-range", { { 0x4170, 9, 8 } }, { "E_INDEX" }, 0x4170);
            corpus.variantOpen("f55pathindexnopath", "path-index-names-an-entry-without-path", { { 0x4170, 2, 8 } },
                { "E_INDEX" }, 0x4170);
            {
                // Swap the two path-index records (hash order broken).
                const Bytes& b = corpus.mAnnexB;
                std::vector<Edit> edits;
                for (unsigned i = 0; i < 16; ++i)
                {
                    edits.push_back({ 0x4168 + i, b[0x4178 + i], 1 });
                    edits.push_back({ 0x4178 + i, b[0x4168 + i], 1 });
                }
                corpus.variantOpen("f56pathindexswap", "path-index-records-out-of-order", edits, { "E_ORDER" });
                std::vector<Edit> copy;
                for (unsigned i = 0; i < 16; ++i)
                    copy.push_back({ 0x4178 + i, b[0x4168 + i], 1 });
                corpus.variantOpen("f57pathindexdup", "path-index-record-repeated", copy, { "E_ORDER" });
            }
            corpus.variantOpen("f58keyindexentry", "key-index-entry-index-out-of-range", { { 0x4190, 9, 8 } }, { "E_INDEX" }, 0x4190);
            corpus.variantOpen("f59keyindexnokey", "key-index-names-an-entry-without-key", { { 0x4190, 0, 8 } }, { "E_INDEX" }, 0x4190);
            corpus.variantOpen("f60keyindexkey", "key-index-key-differs-from-the-entry", { { 0x4188, 0x1000000000000002ull, 8 } },
                { "E_INDEX" }, 0x4188);
            corpus.variantOpen("f61chunkhashdiffers", "one-chunk-entry-with-another-chunk-hash", { { chunk1At + 24, 0x11, 1 } },
                { "E_RANGE" });
            corpus.variantOpen("f62chunkunaligned", "first-chunk-not-4096-aligned", { { chunk1At, 0x2008, 8 } }, { "E_RANGE" }, chunk1At);
            corpus.variantOpen("f63chunkpastdata", "chunk-starting-inside-the-last-block-end", { { chunk2At, 0x3002, 8 } },
                { "E_RANGE" }, chunk2At);
            corpus.variantOpen("f64storedbig", "stored-size-above-2-pow-24", { { chunk1At + 8, 0x01000001, 4 } }, { "E_LIMIT" },
                chunk1At + 8);
            corpus.variantOpen("f65zstdbound", "zstd-stored-size-above-the-compress-bound", { { chunk0At + 8, 128, 4 } },
                { "E_LIMIT" }, chunk0At + 8);
            corpus.variantOpen("f66storedneqsize", "stored-codec-with-stored-size-not-equal-to-size", { { chunk1At + 8, 14, 4 } },
                { "E_RANGE" }, chunk1At + 8);
            corpus.variantOpen("f67chunksizezero", "chunk-size-zero", { { chunk1At + 12, 0, 4 } }, { "E_LIMIT" }, chunk1At + 12);
            corpus.variantOpen("f68storedzero", "chunk-stored-size-zero", { { chunk1At + 8, 0, 4 } }, { "E_LIMIT" }, chunk1At + 8);
            corpus.variantOpen("f69hashpresent", "hash-present-clear", { { chunk1At + 18, 0, 2 } }, { "E_RANGE" }, chunk1At + 18);
            corpus.variantRead("f70chunkflagcritical", "unknown-critical-chunk-flag-bit-1", { { chunk1At + 18, 3, 2 } }, 1,
                { "E_UNSUPPORTED" });
            corpus.variantRead("f71codec256", "unallocated-codec-256", { { chunk1At + 16, 256, 2 } }, 1, { "E_UNSUPPORTED" });
            corpus.variantRead("f72codecff00", "experimental-codec-ff00", { { chunk1At + 16, 0xFF00, 2 } }, 1, { "E_UNSUPPORTED" });
            corpus.variantOpen("f73entrycrit3", "entry-unknown-critical-flag-bit-3", { { entry0At + 24, 0x9, 4 } },
                { "E_UNSUPPORTED" }, entry0At + 24);
            corpus.variantOpen("f74packflag31", "pack-flag-bit-31", { { 0x10, 0x80000000ull, 8 } }, { "E_UNSUPPORTED" }, 0x10);
            corpus.variantOpen("f75packflag1", "pack-flag-bit-1", { { 0x10, 2, 8 } }, { "E_UNSUPPORTED" }, 0x10);
            corpus.variantOpen("f76chunk0before", "entry-chunk-tables-shared-by-two-entries",
                { { entry1At + 40, chunk0At, 8 } }, { "E_RANGE" }, entry1At + 40);
            corpus.variantOpen("f77footermagic", "wrong-footer-magic", { { footerAt + 7, 2, 1 } }, { "E_TRUNCATED" },
                std::nullopt, none);
            corpus.variantOpen("f78magicgenerationzero", "generation-byte-0", { { 7, 0, 1 } }, { "E_VERSION" }, 7, none);

            // Empty pack with counts that do not fit "no entries".
            {
                const Bytes empty = buildPack(PackSpec{});
                const auto variant = [&](std::string id, std::string description, std::size_t offset, std::uint64_t value) {
                    Bytes pack = empty;
                    patch(pack, offset, value, 8);
                    recompute(pack);
                    corpus.addInvalid(std::move(id), std::move(description), std::move(pack), { "E_RANGE" }, offset);
                };
                variant("z01emptypathcount", "zero-entries-with-path-count-1", Fmt::hPathEntryCount, 1);
                variant("z02emptykeycount", "zero-entries-with-key-count-1", Fmt::hKeyEntryCount, 1);
                variant("z03emptyentrycount", "zero-path-and-key-entries-with-entry-count-1", Fmt::hEntryCount, 1);
                variant("z04emptydatasize", "zero-entries-with-data-size-8", Fmt::hDataSize, 8);
            }
        }

        // ------------------------------------------------------------------------------------------------------
        // Names, order, keys
        // ------------------------------------------------------------------------------------------------------

        void addNameCases(Corpus& corpus)
        {
            const auto badName = [&](std::string id, std::string description, std::string path, std::vector<std::string> classes) {
                corpus.addInvalid(std::move(id), std::move(description), buildPack(specOf({ pathEntry(std::move(path)) })),
                    std::move(classes));
            };
            badName("n01utf8invalid", "path-with-invalid-utf8", "a\xFF\xFE" "b", { "E_NAME" });
            badName("n02utf8overlong", "path-with-overlong-utf8", "a\xC0\x80" "b", { "E_NAME" });
            badName("n03utf8surrogate", "path-with-utf8-surrogate", "a\xED\xA0\x80", { "E_NAME" });
            badName("n04utf8toohigh", "path-above-u10ffff", "a\xF4\x90\x80\x80", { "E_NAME" });
            badName("n05utf8truncated", "path-ending-in-a-cut-utf8-sequence", "a\xC3", { "E_NAME" });
            badName("n06control", "path-with-control-character", "a\x01" "b", { "E_NAME" });
            badName("n07nul", "path-with-nul", std::string("a\0b", 3), { "E_NAME" });
            badName("n08backslash", "path-with-backslash", "a\\b", { "E_NAME" });
            badName("n09uppercase", "path-with-upper-case", "Abc.txt", { "E_NAME" });
            badName("n10leadingslash", "path-with-leading-slash", "/a", { "E_NAME" });
            badName("n11trailingslash", "path-with-trailing-slash", "a/", { "E_NAME" });
            badName("n12doubleslash", "path-with-empty-component", "a//b", { "E_NAME" });
            badName("n13dotcomponent", "path-with-dot-component", "a/./b", { "E_NAME" });
            badName("n14dotdotcomponent", "path-with-dotdot-component", "a/../b", { "E_NAME" });
            badName("n15dot", "path-is-dot", ".", { "E_NAME" });
            badName("n16dotdot", "path-is-dotdot", "..", { "E_NAME" });
            badName("n17slash", "path-is-a-slash", "/", { "E_NAME" });
            badName("n18empty", "path-is-empty", "", { "E_LIMIT" });
            badName("n19component256", "path-component-of-256-bytes", repeatChar('a', 256), { "E_LIMIT", "E_NAME" });
            // 255 + 255 + 255 + 255 + 1 bytes of components and 4 separators: 1025 bytes.
            badName("n20path1025", "path-of-1025-bytes",
                repeatChar('a', 255) + "/" + repeatChar('b', 255) + "/" + repeatChar('c', 255) + "/" + repeatChar('d', 255) + "/e",
                { "E_LIMIT" });
            badName("n21deletechar", "path-ending-in-control-character-1f", "a\x1F", { "E_NAME" });

            // Order and duplicates.
            const auto unsorted = [&](std::string id, std::string description, std::vector<EntrySpec> entries,
                                      std::vector<std::string> classes) {
                PackSpec spec = specOf(std::move(entries));
                spec.mSortEntries = false;
                corpus.addInvalid(std::move(id), std::move(description), buildPack(spec), std::move(classes));
            };
            unsorted("o01order", "paths-b-then-a", { pathEntry("b"), pathEntry("a") }, { "E_ORDER" });
            unsorted("o02duplicate", "path-a-twice", { pathEntry("a"), pathEntry("a") }, { "E_DUPLICATE" });
            unsorted("o03slashfirst", "slash-sorted-as-if-special", { pathEntry("a/b"), pathEntry("a-x") }, { "E_ORDER" });
            unsorted("o04prefix", "longer-name-before-its-prefix", { pathEntry("abc"), pathEntry("ab") }, { "E_ORDER" });
            unsorted("o05signed", "non-ascii-before-ascii", { pathEntry("\xC3\xA4"), pathEntry("z") }, { "E_ORDER" });
            unsorted("o06keysorder", "keys-5-then-3", { keyEntry(5), keyEntry(3) }, { "E_ORDER" });
            unsorted("o07keysequal", "key-5-twice", { keyEntry(5), keyEntry(5) }, { "E_DUPLICATE" });
            unsorted("o08keysigned", "key-above-2-pow-63-before-a-small-key", { keyEntry(0x8000000000000000ull), keyEntry(5) },
                { "E_ORDER" });
            {
                EntrySpec both = pathEntry("a");
                both.mKey = 7;
                unsorted("o09keyacrossgroups", "path-with-key-7-and-key-only-entry-7", { both, keyEntry(7) }, { "E_DUPLICATE" });
            }
            unsorted("o10pathafterkey", "path-entry-after-a-key-only-entry", { keyEntry(5), pathEntry("a") }, { "E_ORDER" });
            {
                EntrySpec zero;
                zero.mKey = 0;
                unsorted("o11keyzero", "has-key-with-key-zero-built-by-the-builder", { zero }, { "E_RANGE" });
                EntrySpec tile = keyEntry(0x1100000000000001ull);
                tile.mType = 0x10;
                unsorted("o12tilekeytag", "type-0x10-with-key-tag-0x11", { tile }, { "E_RANGE" });
                EntrySpec pathTile = pathEntry("tile");
                pathTile.mType = 0x12;
                unsorted("o13tilepath", "type-0x12-without-key", { pathTile }, { "E_RANGE" });
                EntrySpec none;
                unsorted("o14noaddress", "entry-with-neither-path-nor-key", { none }, { "E_RANGE" });
            }
        }

        // ------------------------------------------------------------------------------------------------------
        // Extension blocks and provenance
        // ------------------------------------------------------------------------------------------------------

        void addExtensionAndProvenanceCases(Corpus& corpus)
        {
            const auto extension = [&](std::string id, std::string description, Bytes raw, std::vector<std::string> classes) {
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("ext")) });
                spec.mMutate = [raw](PackImage& image) {
                    image.mExtension = raw;
                    relayoutRegions(image);
                    fixDerivedOffsets(image);
                };
                corpus.addInvalid(std::move(id), std::move(description), buildPack(spec), std::move(classes));
            };
            const auto block = [](std::uint32_t id, std::uint32_t flags, std::uint64_t payloadSize, std::size_t actualPayload) {
                Bytes out;
                appendLe(out, id, 4);
                appendLe(out, flags, 4);
                appendLe(out, payloadSize, 8);
                out.resize(out.size() + actualPayload, 0);
                return out;
            };
            extension("e01short", "extension-region-of-8-bytes", Bytes(8, 0), { "E_RANGE" });
            extension("e02payloadtoobig", "payload-size-larger-than-the-region", block(0x80000001u, 0, 100, 8), { "E_RANGE" });
            extension("e03payloadmax", "payload-size-max", block(0x80000001u, 0, maxU64, 8), { "E_RANGE" });
            {
                Bytes many;
                for (int i = 0; i < 1025; ++i)
                    appendBytes(many, block(0x80000001u, 0, 0, 0));
                extension("e04manyblocks", "1025-extension-blocks", many, { "E_LIMIT" });
            }
            extension("e05mustunderstand", "unknown-must-understand-block-2", block(2, 1, 0, 0), { "E_UNSUPPORTED" });
            extension("e06mustunderstandexp", "unknown-must-understand-block-80000005", block(0x80000005u, 1, 8, 8),
                { "E_UNSUPPORTED" });
            extension("e07writerinfobig", "writer-info-block-of-1025-bytes", block(1, 0, 1025, 1032), { "E_LIMIT" });
            {
                Bytes trailing = block(0x80000001u, 0, 0, 0);
                trailing.resize(trailing.size() + 8, 0);
                extension("e08trailing", "8-bytes-after-the-last-block", trailing, { "E_RANGE" });
            }
            {
                // Valid: a writer-info block with exactly 1024 bytes.
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("ext")) });
                spec.mExtensions.push_back({ 1, 0, Bytes(1024, 'w') });
                corpus.addValid("v35writerinfo1024", "writer-info-block-of-exactly-1024-bytes", spec, CanonicalResult::Fail);
            }
            {
                PackSpec spec = specOf({ pathEntry("a.txt", bytesOf("ext")) });
                Bytes many;
                for (int i = 0; i < 1024; ++i)
                    appendBytes(many, block(0x80000001u, 0, 0, 0));
                spec.mMutate = [many](PackImage& image) {
                    image.mExtension = many;
                    relayoutRegions(image);
                    fixDerivedOffsets(image);
                };
                corpus.addValid("v36extension1024", "exactly-1024-extension-blocks", spec, CanonicalResult::Fail);
            }

            // Provenance records (7.8 and 14.2 item 9). Base: entry "a" has a record of 56 bytes, "b" has none.
            const auto provenance = [&](std::string id, std::string description,
                                        const std::function<void(PackImage&)>& change, std::vector<std::string> classes) {
                EntrySpec a = pathEntry("a", bytesOf("one"));
                a.mProvenance = ProvenanceSpec{ "t/1", blake3Of(bytesOf("p")), { 1, 2, 3, 4 } };
                PackSpec spec = specOf({ a, pathEntry("b", bytesOf("two")) });
                spec.mMutate = change;
                corpus.addInvalid(std::move(id), std::move(description), buildPack(spec), std::move(classes));
            };
            provenance("p01recordsize", "record-size-64-instead-of-56", [](PackImage& i) { putLe(i.mProvenance, 0, 64, 4); },
                { "E_RANGE" });
            provenance("p02toolid0", "tool-id-length-0", [](PackImage& i) { putLe(i.mProvenance, 4, 0, 2); },
                { "E_LIMIT", "E_RANGE" });
            provenance("p03toolid256", "tool-id-length-256", [](PackImage& i) { putLe(i.mProvenance, 4, 256, 2); }, { "E_LIMIT" });
            provenance("p04params60001", "parameters-length-60001", [](PackImage& i) { putLe(i.mProvenance, 40, 60001, 4); },
                { "E_LIMIT" });
            provenance("p05toolidutf8", "tool-id-invalid-utf8", [](PackImage& i) { putLe(i.mProvenance, 48, 0xFF, 1); },
                { "E_NAME" });
            provenance("p06toolidnul", "tool-id-with-nul", [](PackImage& i) { putLe(i.mProvenance, 48, 0, 1); }, { "E_NAME" });
            provenance("p07emptyregion", "has-provenance-with-provenance-size-0",
                [](PackImage& i) {
                    i.mProvenance.clear();
                    relayoutRegions(i);
                    fixDerivedOffsets(i);
                },
                { "E_RANGE" });
            provenance("p08regionextra", "provenance-region-8-bytes-longer-than-its-records",
                [](PackImage& i) {
                    i.mProvenance.resize(i.mProvenance.size() + 8, 0);
                    relayoutRegions(i);
                    fixDerivedOffsets(i);
                },
                { "E_RANGE" });
            provenance("p09recordsize65544", "record-size-above-65536", [](PackImage& i) { putLe(i.mProvenance, 0, 65544, 4); },
                { "E_LIMIT" });
            provenance("p10recordsize52", "record-size-not-a-multiple-of-8", [](PackImage& i) { putLe(i.mProvenance, 0, 52, 4); },
                { "E_LIMIT", "E_RANGE" });
            provenance("p11offsetmoved", "entry-provenance-offset-moved-by-8", [](PackImage& i) { i.mEntries[0].mProvenanceOffset += 8; },
                { "E_RANGE" });
            provenance("p12params1", "parameters-length-larger-than-the-record", [](PackImage& i) { putLe(i.mProvenance, 40, 12, 4); },
                { "E_RANGE" });
        }

        // ------------------------------------------------------------------------------------------------------
        // Chunks and the data region built with the builder
        // ------------------------------------------------------------------------------------------------------

        PackSpec twoChunkPack()
        {
            EntrySpec a = pathEntry("a.bin");
            ChunkSpec first;
            first.mRaw = patternBytes(4096, 31);
            ChunkSpec second;
            second.mRaw = patternBytes(4096, 32);
            a.mChunks = { first, second };
            return specOf({ a }, 4096);
        }

        void addChunkCases(Corpus& corpus)
        {
            const auto invalid = [&](std::string id, std::string description, PackSpec spec,
                                     const std::function<void(PackImage&)>& change, std::vector<std::string> classes) {
                spec.mMutate = change;
                corpus.addInvalid(std::move(id), std::move(description), buildPack(spec), std::move(classes));
            };
            invalid("c01chunkoverlap", "second-chunk-starts-inside-the-first", twoChunkPack(),
                [](PackImage& i) { i.mChunks[1].mOffset = i.mChunks[0].mOffset + 10; }, { "E_OVERLAP" });
            invalid("c02chunkbeforedata", "chunk-offset-below-the-data-offset", twoChunkPack(),
                [](PackImage& i) { i.mChunks[0].mOffset = 0x800; }, { "E_RANGE" });
            {
                PackSpec spec = specOf({ pathEntry("a", patternBytes(100, 40)), pathEntry("b", patternBytes(100, 41)) });
                invalid("c03chunksswapped", "chunks-of-two-entries-in-descending-order", spec, [](PackImage& i) {
                    std::swap(i.mChunks[0].mOffset, i.mChunks[1].mOffset);
                }, { "E_OVERLAP" });
            }
            {
                EntrySpec a = pathEntry("a");
                ChunkSpec first;
                first.mRaw = patternBytes(60, 42);
                ChunkSpec second;
                second.mRaw = patternBytes(50, 43);
                a.mChunks = { first, second };
                invalid("c04sumtoobig", "chunk-sizes-add-up-to-more-than-the-entry", specOf({ a }, 4096),
                    [](PackImage& i) { i.mEntries[0].mSize = 100; }, { "E_RANGE" });
                invalid("c05sumtoosmall", "chunk-sizes-add-up-to-less-than-the-entry", specOf({ a }, 4096),
                    [](PackImage& i) { i.mEntries[0].mSize = 120; }, { "E_RANGE" });
            }
            invalid("c06emptywithchunk", "entry-size-zero-with-one-chunk", specOf({ pathEntry("a", patternBytes(10, 44)) }),
                [](PackImage& i) { i.mEntries[0].mSize = 0; }, { "E_RANGE" });
            invalid("c07emptywithtable", "empty-entry-with-a-chunk-table-offset", specOf({ pathEntry("a") }),
                [](PackImage& i) { i.mEntries[0].mChunkTableOffset = 8; }, { "E_RANGE" });
            invalid("c08emptywronghash", "empty-entry-with-another-content-hash", specOf({ pathEntry("a") }),
                [](PackImage& i) { i.mEntries[0].mContentHash[0] ^= 1; }, { "E_RANGE" });
            invalid("c09chunkgapnonzero", "chunk-count-3-with-two-chunk-records", twoChunkPack(),
                [](PackImage& i) { i.mEntries[0].mChunkCount = 3; }, { "E_RANGE" });

            // Read-time failures: entry 0 is "bad.bin", entry 1 "good.txt".
            const auto badChunk = [&](std::string id, std::string description, const ChunkSpec& chunk,
                                      const std::function<void(PackImage&)>& change, std::vector<std::string> classes) {
                EntrySpec bad = pathEntry("bad.bin");
                bad.mChunks = { chunk };
                PackSpec spec = specOf({ bad, pathEntry("good.txt", bytesOf("good")) }, 4096);
                spec.mMutate = change;
                corpus.addInvalidRead(std::move(id), std::move(description), buildPack(spec), expectedOf(spec), 0, std::move(classes));
            };
            const auto rle = [](const FrameOptions& options, std::uint64_t count = 64) {
                ChunkSpec chunk;
                chunk.mRaw = Bytes(64, 0x41);
                chunk.mChoice = CodecChoice::HandRle;
                chunk.mStored = makeRleFrame(0x41, count, options);
                return chunk;
            };
            const auto none = [](PackImage&) {};
            {
                FrameOptions options;
                options.mDictionaryId = 5;
                badChunk("r01dictionary", "zstd-frame-with-dictionary-id-5", rle(options), none, { "E_CODEC" });
            }
            {
                ChunkSpec chunk = rle({});
                appendBytes(*chunk.mStored, Bytes{ 0, 0, 0 });
                badChunk("r02trailing", "zstd-frame-with-trailing-bytes", chunk, none, { "E_CODEC" });
            }
            {
                FrameOptions options;
                options.mSingleSegment = false;
                options.mWindowLog = 24;
                badChunk("r03window16m", "zstd-frame-with-a-16-mib-window", rle(options), none, { "E_CODEC" });
            }
            {
                FrameOptions options;
                options.mDeclaredSize = 63;
                badChunk("r04contentsize", "zstd-frame-content-size-differs-from-the-record", rle(options), none, { "E_CODEC" });
            }
            {
                FrameOptions options;
                options.mSingleSegment = false;
                options.mWriteContentSize = false;
                badChunk("r05nocontentsize", "zstd-frame-without-content-size", rle(options), none, { "E_CODEC" });
            }
            {
                ChunkSpec chunk = rle({});
                chunk.mStored = makeSkippableFrame(Bytes{ 1, 2, 3, 4 });
                badChunk("r06skippable", "stored-bytes-are-a-skippable-frame", chunk, none, { "E_CODEC" });
            }
            {
                ChunkSpec chunk = rle({});
                Bytes two = makeRleFrame(0x41, 64);
                appendBytes(two, makeRleFrame(0x41, 64));
                chunk.mStored = two;
                badChunk("r07twoframes", "two-zstd-frames", chunk, none, { "E_CODEC" });
            }
            {
                ChunkSpec chunk = rle({});
                chunk.mStored = patternBytes(10, 45);
                (*chunk.mStored)[0] = 0x11;
                badChunk("r08notaframe", "stored-bytes-without-zstd-magic", chunk, none, { "E_CODEC" });
            }
            {
                FrameOptions options;
                options.mDeclaredSize = 64;
                badChunk("r09declares64decodes4096", "frame-declares-64-bytes-but-holds-4096", rle(options, 4096), none, { "E_CODEC" });
            }
            {
                FrameOptions options;
                options.mDeclaredSize = 8u << 20;
                badChunk("r10declares8m", "frame-declares-8-mib-but-the-record-says-64", rle(options), none, { "E_CODEC" });
            }
            {
                ChunkSpec chunk = rle({});
                chunk.mStored->pop_back();
                badChunk("r11cutframe", "zstd-frame-missing-its-last-byte", chunk, none, { "E_CODEC" });
            }
            {
                ChunkSpec chunk = rle({});
                chunk.mCodecId = std::uint16_t{ 7 };
                badChunk("r12codec7", "unknown-codec-7-on-entry-0", chunk, none, { "E_UNSUPPORTED" });
            }
            {
                ChunkSpec chunk;
                chunk.mRaw = Bytes(64, 0x41);
                chunk.mCodecId = std::uint16_t{ 0xFF00 };
                badChunk("r13codecff00", "experimental-codec-with-the-content-as-stored-bytes", chunk, none, { "E_UNSUPPORTED" });
            }
            badChunk("r14chunkflag", "unknown-critical-chunk-flag", rle({}), [](PackImage& i) { i.mChunks[0].mFlags = 3; },
                { "E_UNSUPPORTED" });
            {
                ChunkSpec chunk;
                chunk.mRaw = Bytes(64, 0x41);
                chunk.mChoice = CodecChoice::Store;
                chunk.mStored = Bytes(64, 0x42); // stored bytes differ from the content the hash was made for
                badChunk("r15storedwrong", "stored-bytes-differ-from-the-hashed-content", chunk, none, { "E_HASH" });
            }
            {
                // A valid zstd frame of the right length that decodes to other bytes.
                ChunkSpec chunk;
                chunk.mRaw = Bytes(64, 0x41);
                chunk.mChoice = CodecChoice::HandRle;
                chunk.mStored = makeRleFrame(0x42, 64);
                badChunk("r16otherbytes", "valid-frame-decoding-to-other-bytes", chunk, none, { "E_HASH" });
            }
            {
                // Content hash wrong in an entry of two chunks: a whole-entry read fails, a range read does not.
                PackSpec spec = twoChunkPack();
                spec.mMutate = [](PackImage& i) { i.mEntries[0].mContentHash[5] ^= 0x40; };
                CorpusCase& c = corpus.addInvalidRead("r17contenthash", "content-hash-wrong-in-a-two-chunk-entry",
                    buildPack(spec), expectedOf(spec), 0, { "E_HASH" });
                c.mRangeFails = false;
            }
            {
                // A damaged byte in the middle chunk of a three-chunk entry; the other entry stays readable.
                PackSpec spec = specOf({ pathEntry("a.bin", patternBytes(3 * 4096, 46)), pathEntry("b.bin", bytesOf("fine")) }, 4096);
                Bytes pack = buildPack(spec);
                pack[0x1000 + 4096 + 100] ^= 0x01; // chunk 1 of entry 0 starts at 0x1000 + 4096
                corpus.addInvalidRead("r18damagedmiddle", "one-damaged-byte-in-the-middle-chunk",
                    std::move(pack), expectedOf(spec), 0, { "E_HASH" });
            }

            // Count products and sums.
            {
                std::vector<EntrySpec> entries;
                for (int i = 0; i < 257; ++i)
                {
                    char name[32];
                    std::snprintf(name, sizeof(name), "big/e%04d", i);
                    entries.push_back(pathEntry(name));
                }
                PackSpec spec = specOf(entries);
                invalid("c10totalsize", "257-entries-of-2-pow-32-bytes-add-up-to-more-than-2-pow-40", spec, [](PackImage& i) {
                    for (EntryRec& e : i.mEntries)
                    {
                        e.mSize = 1ull << 32;
                        e.mChunkCount = 1;
                        e.mContentHash = blake3Of(Bytes{ 1 });
                        ChunkRec chunk;
                        chunk.mOffset = 0;
                        chunk.mStoredSize = 1;
                        chunk.mSize = 1;
                        i.mChunks.push_back(chunk);
                    }
                    relayoutRegions(i);
                    fixDerivedOffsets(i);
                }, { "E_LIMIT" });
            }
        }

        // ------------------------------------------------------------------------------------------------------
        // Hostile files (Annex D item 3)
        // ------------------------------------------------------------------------------------------------------

        void addHostileCases(Corpus& corpus)
        {
            // Truncations of the Annex B pack at every length from 0 to 640 (the header is 512 bytes, the smallest
            // possible pack 640), and at each structure boundary. Section 14.2 item 1: fewer than 7 bytes is E_MAGIC;
            // fewer than 640 bytes (with the magic) or a last 128 bytes that do not begin with the footer magic is
            // E_TRUNCATED. With exactly 7 bytes the eighth byte is absent: the specification does not say which of
            // three classes applies.
            for (std::size_t length = 0; length <= 640; ++length)
            {
                char id[16];
                std::snprintf(id, sizeof(id), "t%04zu", length);
                Bytes cut(corpus.mAnnexB.begin(), corpus.mAnnexB.begin() + static_cast<std::ptrdiff_t>(length));
                std::vector<std::string> classes;
                if (length < 7)
                    classes = { "E_MAGIC" };
                else if (length == 7)
                    classes = { "E_MAGIC", "E_VERSION", "E_TRUNCATED" };
                else
                    classes = { "E_TRUNCATED" };
                corpus.addInvalid(id, "annex-b-cut-to-" + std::to_string(length) + "-bytes", std::move(cut), std::move(classes));
            }
            for (const std::uint64_t length : { 641ull, 700ull, 0x1000ull, 0x2000ull, 0x3000ull, 0x3004ull, 0x4000ull,
                     0x4018ull, 0x4168ull, 0x4188ull, 0x4198ull, 0x4240ull, 0x4241ull, 0x42B0ull, 0x42BFull })
            {
                Bytes cut(corpus.mAnnexB.begin(), corpus.mAnnexB.begin() + static_cast<std::ptrdiff_t>(length));
                corpus.addInvalid("tb" + hex(length), "annex-b-cut-at-a-structure-boundary", std::move(cut), { "E_TRUNCATED" });
            }

            // Random bytes behind a valid magic.
            for (const std::size_t size : { std::size_t{ 100 }, std::size_t{ 639 }, std::size_t{ 700 }, std::size_t{ 5000 } })
            {
                Bytes random = patternBytes(size, 900 + size);
                const Bytes magic(corpus.mAnnexB.begin(), corpus.mAnnexB.begin() + 8);
                std::copy(magic.begin(), magic.end(), random.begin());
                corpus.addInvalid("h" + std::to_string(size) + "random", "random-bytes-behind-a-valid-magic", std::move(random),
                    { "E_TRUNCATED" });
            }
            {
                // Valid magic, version, header size and footer magic around random bytes: only the checksum is wrong.
                Bytes random = patternBytes(4352, 77);
                std::copy(corpus.mAnnexB.begin(), corpus.mAnnexB.begin() + 8, random.begin());
                putLe(random, Fmt::hVersionMajor, 0, 2);
                putLe(random, Fmt::hVersionMinor, 1, 2);
                putLe(random, Fmt::hHeaderSize, 512, 4);
                const std::size_t footer = random.size() - Fmt::footerSize;
                const Bytes footerMagic = { 0x45, 0x44, 0x45, 0x4E, 0x45, 0x4E, 0x44, 0x01 };
                std::copy(footerMagic.begin(), footerMagic.end(), random.begin() + static_cast<std::ptrdiff_t>(footer));
                putLe(random, footer + Fmt::fVersionMajor, 0, 2);
                putLe(random, footer + Fmt::fVersionMinor, 1, 2);
                putLe(random, footer + Fmt::fFooterSize, 128, 4);
                corpus.addInvalid("h4352randombody", "random-body-between-a-valid-magic-and-footer-magic", std::move(random),
                    { "E_CHECKSUM" });
            }
            {
                // The same with a correct checksum and footer copies: it must be rejected, the class depends on the
                // random values.
                Bytes random = patternBytes(4352, 78);
                std::copy(corpus.mAnnexB.begin(), corpus.mAnnexB.begin() + 8, random.begin());
                putLe(random, Fmt::hVersionMajor, 0, 2);
                putLe(random, Fmt::hVersionMinor, 1, 2);
                putLe(random, Fmt::hHeaderSize, 512, 4);
                putLe(random, Fmt::hFileSize, random.size(), 8);
                const std::size_t footer = random.size() - Fmt::footerSize;
                const Bytes footerMagic = { 0x45, 0x44, 0x45, 0x4E, 0x45, 0x4E, 0x44, 0x01 };
                std::copy(footerMagic.begin(), footerMagic.end(), random.begin() + static_cast<std::ptrdiff_t>(footer));
                putLe(random, footer + Fmt::fFooterSize, 128, 4);
                putLe(random, footer + Fmt::fFileSize, random.size(), 8);
                recompute(random);
                corpus.addInvalid("h4352randomchecked", "random-header-with-correct-checksum", std::move(random), {});
            }
        }
    }

    const std::vector<CorpusCase>& corpusCases()
    {
        static const std::vector<CorpusCase> cases = [] {
            Corpus corpus;
            addValidCases(corpus);
            addAnnexCCases(corpus);
            addHeaderCases(corpus);
            addNameCases(corpus);
            addExtensionAndProvenanceCases(corpus);
            addChunkCases(corpus);
            addHostileCases(corpus);
            return std::move(corpus.mCases);
        }();
        return cases;
    }

    std::string expectedLine(const CorpusCase& c)
    {
        std::string line = c.mId;
        line += c.mOutcome == Outcome::Valid ? " valid " : " invalid ";
        if (c.mOutcome == Outcome::Valid || c.mClasses.empty())
            line += "-";
        else
        {
            for (std::size_t i = 0; i < c.mClasses.size(); ++i)
                line += (i == 0 ? "" : "|") + c.mClasses[i];
        }
        line += c.mOutcome == Outcome::InvalidAtRead ? " read " : " open ";
        if (c.mOffset.has_value())
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(*c.mOffset));
            line += buffer;
        }
        else
            line += "-";
        line += " " + c.mDescription;
        if (c.mOutcome == Outcome::Valid)
        {
            line += ";canonical=";
            line += c.mCanonical == CanonicalResult::Pass ? "pass" : (c.mCanonical == CanonicalResult::Fail ? "fail" : "any");
            for (const std::string& name : c.mCanonicalClasses)
                line += ";canonical-class=" + name;
            if (c.mReaderMayReject)
                line += ";reader-may-reject";
        }
        if (c.mOutcome == Outcome::InvalidAtRead)
            line += ";entry=" + std::to_string(c.mFailingEntry) + (c.mRangeFails ? "" : ";range-ok");
        return line;
    }

    std::string expectedText()
    {
        std::string text;
        for (const CorpusCase& c : corpusCases())
            text += expectedLine(c) + "\n";
        return text;
    }
}
