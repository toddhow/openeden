// Eden Archive unit tests (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
//
// A deterministic mutation fuzz test for the reader (stage f of the task). It needs no fuzzing engine: a fixed seed
// and an explicit random mapping make every run the same on every compiler and standard library. Each input goes to
// EdenArchive::fuzzOneInput, which must neither crash, hang, throw nor allocate without bound. Run it under the
// address and undefined-behaviour sanitizers in CI for the real value of this test.
#include <gtest/gtest.h>

#include <cstdlib>
#include <random>

#include <edenarchive/fuzz.hpp>

#include "testhelp.hpp"

namespace EdenTestSuite
{
    namespace
    {
        // A fixed 64-bit seed (the digits of nothing in particular). The generator is std::mt19937_64, whose output is
        // fixed by the C++ standard; the mapping to ranges below is ours, because the standard distributions differ
        // between library implementations.
        constexpr std::uint64_t fuzzSeed = 0xED3A5EEDC0FFEE11ull;
        constexpr unsigned defaultIterations = 20000;

        unsigned iterationsFromEnvironment()
        {
            std::string text;
#ifdef _MSC_VER
            char* value = nullptr;
            std::size_t length = 0;
            if (_dupenv_s(&value, &length, "EDENARCHIVE_FUZZ_ITERATIONS") == 0 && value != nullptr)
            {
                text = value;
                std::free(value);
            }
#else
            if (const char* value = std::getenv("EDENARCHIVE_FUZZ_ITERATIONS"))
                text = value;
#endif
            if (text.empty())
                return defaultIterations;
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
            return (end != nullptr && *end == '\0' && parsed > 0) ? static_cast<unsigned>(parsed) : defaultIterations;
        }

        class Rng
        {
        public:
            explicit Rng(std::uint64_t seed)
                : mEngine(seed)
            {
            }

            std::uint64_t next() { return mEngine(); }
            /// A number in [0, bound); bound must be non-zero.
            std::uint64_t below(std::uint64_t bound) { return next() % bound; }
            bool chance(unsigned percent) { return below(100) < percent; }

        private:
            std::mt19937_64 mEngine;
        };

        const std::vector<Bytes>& seeds()
        {
            static const std::vector<Bytes> list = [] {
                std::vector<Bytes> out;
                for (const EdenTest::CorpusCase& c : EdenTest::corpusCases())
                {
                    // The 641 truncations of Annex B say little that one or two do not; keep a few of them.
                    if (c.mId.size() == 5 && c.mId[0] == 't' && c.mId[1] == '0' && c.mId != "t0007" && c.mId != "t0512"
                        && c.mId != "t0639")
                        continue;
                    out.push_back(c.mBytes);
                }
                return out;
            }();
            return list;
        }

        /// The numbers worth writing into a size, offset or count field.
        std::uint64_t interestingValue(Rng& rng, std::uint64_t fileSize)
        {
            const std::uint64_t fixed[] = { 0, 1, 2, 3, 7, 8, 0xFF, 0x100, 0xFFFF, 0x10000, 0x7FFFFFFFull,
                0x80000000ull, 0xFFFFFFFFull, 1ull << 32, (1ull << 32) + 1, 1ull << 40, 1ull << 44, (1ull << 44) + 1,
                1ull << 63, ~std::uint64_t{ 0 }, ~std::uint64_t{ 0 } - 7, 4096, 8192, 1ull << 22, (1ull << 22) + 1,
                1ull << 20, (1ull << 20) + 1, 1ull << 23, (1ull << 23) + 1, 1ull << 24, 1ull << 25, 60000, 60001 };
            const std::uint64_t relative[] = { fileSize, fileSize - 1, fileSize + 1, fileSize - 8, fileSize + 8,
                fileSize - 128, fileSize - 127, fileSize - 129, fileSize / 2 };
            if (rng.chance(70))
                return fixed[rng.below(sizeof(fixed) / sizeof(fixed[0]))];
            return relative[rng.below(sizeof(relative) / sizeof(relative[0]))];
        }

        struct Field
        {
            std::size_t mOffset;
            unsigned mWidth;
        };

        // Fields of the header (section 5) that hold numbers.
        constexpr Field headerFields[] = { { 8, 2 }, { 10, 2 }, { 12, 4 }, { 16, 8 }, { 24, 8 }, { 32, 4 }, { 36, 4 },
            { 40, 4 }, { 44, 4 }, { 48, 4 }, { 56, 8 }, { 64, 8 }, { 72, 8 }, { 80, 8 }, { 88, 8 }, { 96, 8 },
            { 104, 8 }, { 112, 8 }, { 120, 8 }, { 128, 8 }, { 136, 8 }, { 144, 8 }, { 152, 8 }, { 160, 8 }, { 168, 8 },
            { 176, 8 }, { 184, 8 }, { 192, 8 }, { 200, 8 } };
        // Fields of an entry record (7.4) and of a chunk record (7.7).
        constexpr Field entryFields[] = { { 0, 8 }, { 8, 8 }, { 16, 4 }, { 20, 2 }, { 24, 4 }, { 28, 4 }, { 32, 8 },
            { 40, 8 }, { 80, 8 }, { 88, 4 }, { 92, 4 }, { 96, 4 }, { 100, 4 } };
        constexpr Field chunkFields[] = { { 0, 8 }, { 8, 4 }, { 12, 4 }, { 16, 2 }, { 18, 2 }, { 20, 4 } };

        /// Overwrites one field of a header, entry record, chunk record, index record, provenance record or extension
        /// block with an interesting value. The location is read from the pack's own header, so that most writes land
        /// on a real field of a pack that is otherwise valid.
        void overwriteField(Bytes& pack, Rng& rng)
        {
            if (pack.size() < 512)
                return;
            const std::uint64_t value = interestingValue(rng, pack.size());
            std::size_t where = 0;
            unsigned width = 8;
            switch (rng.below(6))
            {
                case 0:
                {
                    const Field& f = headerFields[rng.below(sizeof(headerFields) / sizeof(headerFields[0]))];
                    where = f.mOffset;
                    width = f.mWidth;
                    break;
                }
                case 1:
                case 2:
                {
                    const std::uint64_t count = std::min<std::uint64_t>(EdenTest::getLe(pack, 56, 8), 64);
                    const std::uint64_t stride = EdenTest::getLe(pack, 36, 4);
                    const Field& f = entryFields[rng.below(sizeof(entryFields) / sizeof(entryFields[0]))];
                    where = EdenTest::getLe(pack, 128, 8) + (count > 0 ? rng.below(count) : 0) * stride + f.mOffset;
                    width = f.mWidth;
                    break;
                }
                case 3:
                {
                    const std::uint64_t total = std::min<std::uint64_t>(EdenTest::getLe(pack, 160, 8) / 56, 64);
                    const std::uint64_t stride = EdenTest::getLe(pack, 40, 4);
                    const Field& f = chunkFields[rng.below(sizeof(chunkFields) / sizeof(chunkFields[0]))];
                    where = EdenTest::getLe(pack, 152, 8) + (total > 0 ? rng.below(total) : 0) * stride + f.mOffset;
                    width = f.mWidth;
                    break;
                }
                case 4: // path index or key index records
                {
                    const bool path = rng.chance(50);
                    const std::uint64_t count = std::min<std::uint64_t>(EdenTest::getLe(pack, path ? 64 : 72, 8), 64);
                    where = EdenTest::getLe(pack, path ? 136 : 144, 8) + (count > 0 ? rng.below(count) : 0) * 16
                        + (rng.chance(50) ? 8 : 0);
                    break;
                }
                default: // provenance record or extension block header
                {
                    const bool provenance = rng.chance(50);
                    where = EdenTest::getLe(pack, provenance ? 168 : 184, 8) + rng.below(provenance ? 48 : 16);
                    width = rng.chance(50) ? 4 : 2;
                    break;
                }
            }
            if (where < pack.size())
                EdenTest::putLe(pack, where, value, width);
        }

        void mutate(Bytes& pack, Rng& rng)
        {
            const unsigned steps = 1 + static_cast<unsigned>(rng.below(3));
            for (unsigned step = 0; step < steps; ++step)
            {
                switch (rng.below(9))
                {
                    case 0: // flip a few bits
                        for (std::uint64_t i = 0, n = 1 + rng.below(4); i < n && !pack.empty(); ++i)
                            pack[rng.below(pack.size())] ^= static_cast<std::uint8_t>(1u << rng.below(8));
                        break;
                    case 1: // set a byte
                        if (!pack.empty())
                            pack[rng.below(pack.size())] = static_cast<std::uint8_t>(rng.next());
                        break;
                    case 2: // truncate
                        if (!pack.empty())
                            pack.resize(rng.below(pack.size()));
                        break;
                    case 3: // extend with random bytes
                    {
                        const std::uint64_t n = 1 + rng.below(64);
                        for (std::uint64_t i = 0; i < n; ++i)
                            pack.push_back(static_cast<std::uint8_t>(rng.next()));
                        break;
                    }
                    case 4: // splice: the start of this pack and the end of another one
                    {
                        const Bytes& other = seeds()[rng.below(seeds().size())];
                        const std::size_t cutA = pack.empty() ? 0 : rng.below(pack.size() + 1);
                        const std::size_t cutB = other.empty() ? 0 : rng.below(other.size() + 1);
                        pack.resize(cutA);
                        pack.insert(pack.end(), other.begin() + static_cast<std::ptrdiff_t>(cutB), other.end());
                        break;
                    }
                    case 5: // copy a block over another place
                        if (pack.size() > 16)
                        {
                            const std::size_t length = 1 + rng.below(std::min<std::size_t>(pack.size() / 2, 256));
                            const std::size_t from = rng.below(pack.size() - length + 1);
                            const std::size_t to = rng.below(pack.size() - length + 1);
                            const Bytes block(pack.begin() + static_cast<std::ptrdiff_t>(from),
                                pack.begin() + static_cast<std::ptrdiff_t>(from + length));
                            std::copy(block.begin(), block.end(), pack.begin() + static_cast<std::ptrdiff_t>(to));
                        }
                        break;
                    case 6: // zero a block
                        if (!pack.empty())
                        {
                            const std::size_t length = 1 + rng.below(std::min<std::size_t>(pack.size(), 128));
                            const std::size_t at = rng.below(pack.size() - length + 1);
                            std::fill(pack.begin() + static_cast<std::ptrdiff_t>(at),
                                pack.begin() + static_cast<std::ptrdiff_t>(at + length), std::uint8_t{ 0 });
                        }
                        break;
                    default: // the field-aware overwrite is the most useful one, so it has three of nine
                        overwriteField(pack, rng);
                        break;
                }
            }
            // Half of the time, make the digests right again so that the mutation is judged by the structural checks
            // and not stopped at the checksum.
            if (rng.chance(50))
            {
                EdenTest::Recompute what;
                what.mPathIndex = rng.chance(30);
                EdenTest::recompute(pack, what);
            }
        }

        TEST(EdenArchiveFuzz, mutatedCorpusPacksShouldNeverCrashOrHangTheReader)
        {
            const unsigned iterations = iterationsFromEnvironment();
            Rng rng(fuzzSeed);
            unsigned opened = 0;
            for (unsigned i = 0; i < iterations; ++i)
            {
                Bytes pack = seeds()[rng.below(seeds().size())];
                mutate(pack, rng);
                if (EdenArchive::fuzzOneInput(pack.data(), pack.size()))
                    ++opened;
            }
            // A mutation that leaves a pack valid is common (a hint bit, a padding byte of a record); if none ever
            // opens, the digest repair is not reaching the structural checks.
            if (iterations >= 1000)
            {
                    EXPECT_GT(opened, 0u) << "no mutated input opened in " << iterations << " iterations";
            }
            RecordProperty("opened", static_cast<int>(opened));
        }

        TEST(EdenArchiveFuzz, theSameSeedShouldGiveTheSameResults)
        {
            std::vector<bool> first;
            std::vector<bool> second;
            for (std::vector<bool>* results : { &first, &second })
            {
                Rng rng(fuzzSeed);
                for (unsigned i = 0; i < 400; ++i)
                {
                    Bytes pack = seeds()[rng.below(seeds().size())];
                    mutate(pack, rng);
                    results->push_back(EdenArchive::fuzzOneInput(pack.data(), pack.size()));
                }
            }
            EXPECT_EQ(first, second);
        }

        TEST(EdenArchiveFuzz, fuzzOneInputShouldReturnTrueExactlyForThePacksThatOpen)
        {
            for (const EdenTest::CorpusCase& c : EdenTest::corpusCases())
            {
                if (c.mReaderMayReject)
                    continue;
                EXPECT_EQ(EdenArchive::fuzzOneInput(c.mBytes.data(), c.mBytes.size()), c.opens()) << c.mId;
            }
        }

        TEST(EdenArchiveFuzz, randomBytesShouldBeRejectedWithoutTroubleWithAndWithoutAMagic)
        {
            Rng rng(fuzzSeed ^ 0x1234);
            const Bytes magic = EdenTest::bytesOf("EDENPAK");
            for (unsigned i = 0; i < 2000; ++i)
            {
                Bytes data(rng.below(3000));
                for (std::uint8_t& byte : data)
                    byte = static_cast<std::uint8_t>(rng.next());
                if (i % 2 == 1 && data.size() >= 8)
                {
                    std::copy(magic.begin(), magic.end(), data.begin());
                    data[7] = 1;
                }
                EXPECT_FALSE(EdenArchive::fuzzOneInput(data.data(), data.size())) << i;
            }
            EXPECT_FALSE(EdenArchive::fuzzOneInput(nullptr, 0));
        }

        TEST(EdenArchiveFuzz, everyTruncationOfTheAnnexBPackShouldBeRejected)
        {
            const Bytes pack = EdenTest::annexBPack();
            for (std::size_t length = 0; length < pack.size(); ++length)
                ASSERT_FALSE(EdenArchive::fuzzOneInput(pack.data(), length)) << length;
            EXPECT_TRUE(EdenArchive::fuzzOneInput(pack.data(), pack.size()));
        }

        TEST(EdenArchiveFuzz, everyBitOfTheHeaderAndFooterShouldMatterOrBeReserved)
        {
            const Bytes pack = EdenTest::annexBPack();
            // The header checksum covers every header bit and the footer repeats the checksum, so any flip in the
            // header is caught. In the footer only the 24 reserved bytes (offset 104 on) may change without notice.
            for (std::size_t bit = 0; bit < 512 * 8; ++bit)
            {
                Bytes changed = pack;
                changed[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
                EXPECT_FALSE(EdenArchive::fuzzOneInput(changed.data(), changed.size())) << "header bit " << bit;
            }
            const std::size_t footer = pack.size() - 128;
            for (std::size_t bit = 0; bit < 128 * 8; ++bit)
            {
                Bytes changed = pack;
                changed[footer + bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
                const bool reserved = bit / 8 >= 104;
                EXPECT_EQ(EdenArchive::fuzzOneInput(changed.data(), changed.size()), reserved) << "footer bit " << bit;
            }
            // And flips anywhere in the metadata region must not crash it (a stale digest should stop most of them).
            for (std::size_t bit = 0x4000 * 8; bit < (pack.size() - 128) * 8; ++bit)
            {
                Bytes changed = pack;
                changed[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
                (void)EdenArchive::fuzzOneInput(changed.data(), changed.size());
            }
        }
    }
}
