// Task 0265 (the werewolf beast form survives a save and a load): the pure half, the encoding and the chunk handler
// (game_skyrimse/werewolf.hpp). Byte layout by hand: u8 granted, u8 active, f32 seconds, u32 count, count x u64 = 10 +
// 8n.
#include <game_skyrimse/werewolf.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    using namespace game_skyrimse::werewolf;

    class BeastMemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* p = static_cast<const std::uint8_t*>(data);
            mBytes.insert(mBytes.end(), p, p + size);
        }
        std::vector<std::uint8_t> mBytes;
    };

    class BeastMemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit BeastMemoryReader(std::vector<std::uint8_t> bytes)
            : mBytes(std::move(bytes))
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mAt, size);
            mAt += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mAt; }

    private:
        std::vector<std::uint8_t> mBytes;
        std::size_t mAt = 0;
    };

    BeastSaved activeForm(float seconds, std::vector<std::uint64_t> fed = {})
    {
        BeastSaved s;
        s.mGranted = true;
        s.mActive = true;
        s.mRemaining = seconds;
        s.mFed = std::move(fed);
        return s;
    }

    TEST(SkyrimWerewolfSaveCodec, anActiveFormIsTenBytesPlusEightPerCorpse)
    {
        const std::vector<std::uint8_t> bytes = encodeBeastSaved(activeForm(45.f, { 7, 9 }));
        ASSERT_EQ(bytes.size(), 10u + 16u);
        EXPECT_EQ(bytes[0], 1);
        EXPECT_EQ(bytes[1], 1);
        float seconds = 0.f;
        std::memcpy(&seconds, bytes.data() + 2, 4);
        EXPECT_FLOAT_EQ(seconds, 45.f);
        std::uint32_t count = 0;
        std::memcpy(&count, bytes.data() + 6, 4);
        EXPECT_EQ(count, 2u);
        std::uint64_t first = 0;
        std::memcpy(&first, bytes.data() + 10, 8);
        EXPECT_EQ(first, 7u);
    }

    TEST(SkyrimWerewolfSaveCodec, roundTripsGrantedActiveSecondsAndTheFedCorpses)
    {
        BeastSaved out;
        const std::vector<std::uint8_t> bytes = encodeBeastSaved(activeForm(88.5f, { 3, 11, 4000000000000ull }));
        ASSERT_TRUE(decodeBeastSaved(bytes.data(), bytes.size(), out));
        EXPECT_TRUE(out.mGranted);
        EXPECT_TRUE(out.mActive);
        EXPECT_FLOAT_EQ(out.mRemaining, 88.5f);
        EXPECT_EQ(out.mFed, (std::vector<std::uint64_t>{ 3, 11, 4000000000000ull }));
    }

    TEST(SkyrimWerewolfSaveCodec, aGrantedButHumanPlayerKeepsOnlyTheGrant)
    {
        BeastSaved human;
        human.mGranted = true;
        const std::vector<std::uint8_t> bytes = encodeBeastSaved(human);
        ASSERT_EQ(bytes.size(), 10u);
        BeastSaved out;
        out.mActive = true;
        out.mFed = { 1 };
        ASSERT_TRUE(decodeBeastSaved(bytes.data(), bytes.size(), out));
        EXPECT_TRUE(out.mGranted);
        EXPECT_FALSE(out.mActive) << "the decode overwrites every field";
        EXPECT_FLOAT_EQ(out.mRemaining, 0.f);
        EXPECT_TRUE(out.mFed.empty());
    }

    TEST(SkyrimWerewolfSaveCodec, theFedListIsWrittenSortedAndWithoutRepeats)
    {
        const std::vector<std::uint8_t> bytes = encodeBeastSaved(activeForm(10.f, { 9, 3, 9, 5 }));
        BeastSaved out;
        ASSERT_TRUE(decodeBeastSaved(bytes.data(), bytes.size(), out));
        EXPECT_EQ(out.mFed, (std::vector<std::uint64_t>{ 3, 5, 9 }));
    }

    TEST(SkyrimWerewolfSaveCodec, aFormThatIsOffWritesNoTimeAndNoCorpses)
    {
        BeastSaved s = activeForm(30.f, { 1 });
        s.mActive = false;
        const std::vector<std::uint8_t> bytes = encodeBeastSaved(s);
        BeastSaved out;
        ASSERT_TRUE(decodeBeastSaved(bytes.data(), bytes.size(), out));
        EXPECT_FALSE(out.mActive);
        EXPECT_FLOAT_EQ(out.mRemaining, 0.f);
        EXPECT_TRUE(out.mFed.empty());
    }

    TEST(SkyrimWerewolfSaveCodec, theSecondsAreClampedToTheClocksCapOnWriting)
    {
        const std::vector<std::uint8_t> bytes = encodeBeastSaved(activeForm(100000.f));
        BeastSaved out;
        ASSERT_TRUE(decodeBeastSaved(bytes.data(), bytes.size(), out));
        EXPECT_FLOAT_EQ(out.mRemaining, kMaxSeconds);
    }

    TEST(SkyrimWerewolfSaveCodec, aFormOnWithNoTimeLeftIsWrittenAsOff)
    {
        const std::vector<std::uint8_t> bytes = encodeBeastSaved(activeForm(0.f));
        BeastSaved out;
        ASSERT_TRUE(decodeBeastSaved(bytes.data(), bytes.size(), out));
        EXPECT_FALSE(out.mActive) << "a form with no time has ended";
    }

    TEST(SkyrimWerewolfSaveCodec, refusesDamagedBytes)
    {
        const std::vector<std::uint8_t> good = encodeBeastSaved(activeForm(45.f, { 7 }));
        ASSERT_EQ(good.size(), 18u);
        BeastSaved out;
        out.mGranted = true;
        out.mRemaining = 5.f;

        for (std::size_t n = 0; n < good.size(); ++n)
        {
            EXPECT_FALSE(decodeBeastSaved(good.data(), n, out)) << "length " << n;
        }
        std::vector<std::uint8_t> longer = good;
        longer.push_back(0);
        EXPECT_FALSE(decodeBeastSaved(longer.data(), longer.size(), out));
        std::vector<std::uint8_t> bad = good;
        bad[0] = 2;
        EXPECT_FALSE(decodeBeastSaved(bad.data(), bad.size(), out));
        bad = good;
        bad[1] = 2;
        EXPECT_FALSE(decodeBeastSaved(bad.data(), bad.size(), out));
        bad = good;
        const std::uint32_t big = 5;
        std::memcpy(bad.data() + 6, &big, 4);
        EXPECT_FALSE(decodeBeastSaved(bad.data(), bad.size(), out));
        bad = good;
        const std::uint32_t huge = 0xFFFFFFFFu;
        std::memcpy(bad.data() + 6, &huge, 4);
        EXPECT_FALSE(decodeBeastSaved(bad.data(), bad.size(), out));
        for (const float seconds : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                 -1.f, kMaxSeconds + 1.f, 0.f })
        {
            bad = good;
            std::memcpy(bad.data() + 2, &seconds, 4);
            EXPECT_FALSE(decodeBeastSaved(bad.data(), bad.size(), out)) << seconds;
        }
        std::vector<std::uint8_t> off = encodeBeastSaved(BeastSaved{});
        const float some = 5.f;
        std::memcpy(off.data() + 2, &some, 4);
        EXPECT_FALSE(decodeBeastSaved(off.data(), off.size(), out)) << "seconds on a form that is off";
        std::vector<std::uint8_t> two = encodeBeastSaved(activeForm(10.f, { 3, 5 }));
        std::uint64_t value = 5;
        std::memcpy(two.data() + 10, &value, 8);
        value = 3;
        std::memcpy(two.data() + 18, &value, 8);
        EXPECT_FALSE(decodeBeastSaved(two.data(), two.size(), out)) << "unsorted";
        value = 5;
        std::memcpy(two.data() + 18, &value, 8);
        EXPECT_FALSE(decodeBeastSaved(two.data(), two.size(), out)) << "repeat";
        EXPECT_TRUE(out.mGranted) << "a refusal changes nothing";
        EXPECT_FLOAT_EQ(out.mRemaining, 5.f);
    }

    TEST(SkyrimWerewolfSaveCodec, aListOverTheCapIsWrittenShort)
    {
        std::vector<std::uint64_t> many;
        for (std::uint64_t i = 0; i < kMaxSavedFed + 10; ++i)
            many.push_back(i + 1);
        const std::vector<std::uint8_t> bytes = encodeBeastSaved(activeForm(10.f, many));
        BeastSaved out;
        ASSERT_TRUE(decodeBeastSaved(bytes.data(), bytes.size(), out));
        EXPECT_EQ(out.mFed.size(), kMaxSavedFed);
    }

    // ---- the handler ---------------------------------------------------------------------------------------------

    TEST(SkyrimWerewolfSaveChunk, hasItsNameAndVersionAndWritesWhatGetReturns)
    {
        const BeastSaved held = activeForm(61.f, { 2 });
        auto chunk = makeBeastChunk([&] { return held; }, [](const BeastSaved&) {});
        ASSERT_NE(chunk, nullptr);
        EXPECT_EQ(chunk->chunkName(), "werewolf");
        EXPECT_EQ(chunk->chunkVersion(), 1u);
        BeastMemoryWriter writer;
        chunk->write(writer);
        EXPECT_EQ(writer.mBytes, encodeBeastSaved(held));
    }

    TEST(SkyrimWerewolfSaveChunk, aGoodReadReachesSetAndABadOneDoesNot)
    {
        int calls = 0;
        BeastSaved got;
        auto chunk = makeBeastChunk([] { return BeastSaved(); },
            [&](const BeastSaved& s) {
                ++calls;
                got = s;
            });
        ASSERT_NE(chunk, nullptr);
        BeastMemoryReader good(encodeBeastSaved(activeForm(20.f, { 8 })));
        ASSERT_TRUE(chunk->read(good, 1));
        EXPECT_EQ(calls, 1);
        EXPECT_TRUE(got.mActive);
        EXPECT_FLOAT_EQ(got.mRemaining, 20.f);

        BeastMemoryReader wrongVersion(encodeBeastSaved(activeForm(20.f)));
        EXPECT_FALSE(chunk->read(wrongVersion, 2));
        BeastMemoryReader truncated(std::vector<std::uint8_t>(5, 1));
        EXPECT_FALSE(chunk->read(truncated, 1));
        std::vector<std::uint8_t> flag = encodeBeastSaved(activeForm(20.f));
        flag[1] = 3;
        BeastMemoryReader badFlag(flag);
        EXPECT_FALSE(chunk->read(badFlag, 1));
        EXPECT_EQ(calls, 1) << "a refused chunk changes nothing";
    }

    TEST(SkyrimWerewolfSaveChunk, aWriteThenAReadHandsBackTheSameState)
    {
        const BeastSaved held = activeForm(150.f, { 100, 200 });
        BeastSaved restored;
        auto writer = makeBeastChunk([&] { return held; }, [](const BeastSaved&) {});
        auto reader = makeBeastChunk([] { return BeastSaved(); }, [&](const BeastSaved& s) { restored = s; });
        ASSERT_NE(writer, nullptr);
        ASSERT_NE(reader, nullptr);
        BeastMemoryWriter out;
        writer->write(out);
        BeastMemoryReader in(out.mBytes);
        ASSERT_TRUE(reader->read(in, 1));
        EXPECT_EQ(restored.mFed, held.mFed);
        EXPECT_FLOAT_EQ(restored.mRemaining, 150.f);
        EXPECT_TRUE(restored.mGranted);
    }
}
