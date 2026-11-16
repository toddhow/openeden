// Task 0208: the pure pickup and pickpocket rules (pickup.hpp). Written from the spec before the code and run red against
// a stub.
#include <game_skyrimse/pickup.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::pickup;

    struct MemWriter final : openeden::sdk::ChunkWriter
    {
        void write(const void* d, std::size_t n) override
        {
            const auto* p = static_cast<const unsigned char*>(d);
            mBytes.insert(mBytes.end(), p, p + n);
        }
        std::vector<unsigned char> mBytes;
    };

    struct MemReader final : openeden::sdk::ChunkReader
    {
        explicit MemReader(std::vector<unsigned char> bytes)
            : mBytes(std::move(bytes))
        {
        }
        bool read(void* d, std::size_t n) override
        {
            if (n > remaining())
                return false;
            std::memcpy(d, mBytes.data() + mPos, n);
            mPos += n;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }
        std::vector<unsigned char> mBytes;
        std::size_t mPos = 0;
    };

    std::vector<unsigned char> bytesOf(std::initializer_list<std::uint32_t> words)
    {
        std::vector<unsigned char> out;
        for (const std::uint32_t w : words)
        {
            unsigned char b[4];
            std::memcpy(b, &w, 4);
            out.insert(out.end(), b, b + 4);
        }
        return out;
    }

    TEST(SkyrimPickupBook, marksOncePerPlacementAndRefusesTheNullForm)
    {
        PickupBook book;
        EXPECT_TRUE(book.markTaken(0x9cafb));
        EXPECT_TRUE(book.taken(0x9cafb));
        EXPECT_FALSE(book.markTaken(0x9cafb));
        EXPECT_FALSE(book.markTaken(kNullFormId));
        EXPECT_EQ(book.placements().size(), 1u);
        EXPECT_FALSE(book.taken(0x9cafc));
    }

    TEST(SkyrimPickupBook, revisionMovesOnEveryChange)
    {
        PickupBook book;
        const std::uint32_t r0 = book.revision();
        EXPECT_TRUE(book.markTaken(5));
        EXPECT_NE(book.revision(), r0);
        const std::uint32_t r1 = book.revision();
        EXPECT_FALSE(book.markTaken(5));
        EXPECT_EQ(book.revision(), r1);
    }

    TEST(SkyrimPickupBook, theSetIsCapped)
    {
        PickupBook book;
        for (std::uint32_t i = 1; i <= kMaxTaken; ++i)
            ASSERT_TRUE(book.markTaken(i));
        EXPECT_FALSE(book.markTaken(kMaxTaken + 1));
        EXPECT_EQ(book.placements().size(), kMaxTaken);
    }

    TEST(SkyrimPickupBook, attemptsCountUp)
    {
        PickupBook book;
        EXPECT_EQ(book.takeAttempt(), 0u);
        EXPECT_EQ(book.takeAttempt(), 1u);
        EXPECT_EQ(book.attempts(), 2u);
    }

    TEST(SkyrimPickupBook, theChunkRoundTripsAndIsCanonical)
    {
        PickupBook a;
        a.markTaken(30);
        a.markTaken(10);
        a.markTaken(20);
        a.takeAttempt();
        a.takeAttempt();
        a.takeAttempt();
        MemWriter w;
        a.write(w);
        EXPECT_EQ(w.mBytes, bytesOf({ 3, 10, 20, 30, 3 }));
        PickupBook b;
        MemReader r(w.mBytes);
        std::string error;
        ASSERT_TRUE(b.read(r, error)) << error;
        EXPECT_EQ(b.placements(), a.placements());
        EXPECT_EQ(b.attempts(), 3u);
        MemWriter w2;
        b.write(w2);
        EXPECT_EQ(w2.mBytes, w.mBytes);
    }

    TEST(SkyrimPickupBook, aDamagedChunkChangesNothing)
    {
        const std::vector<std::vector<unsigned char>> bad = {
            {}, // no count
            bytesOf({ 2, 10 }), // cut short
            bytesOf({ 2, 20, 10, 0 }), // not ascending
            bytesOf({ 2, 10, 10, 0 }), // a repeat
            bytesOf({ 1, 0, 0 }), // the null placement
            bytesOf({ 1, 10, 0, 7 }), // bytes follow
            bytesOf({ static_cast<std::uint32_t>(kMaxTaken + 1), 1 }), // over the cap
        };
        for (const auto& bytes : bad)
        {
            PickupBook b;
            b.markTaken(99);
            MemReader r(bytes);
            std::string error;
            EXPECT_FALSE(b.read(r, error));
            EXPECT_FALSE(error.empty());
            EXPECT_TRUE(b.taken(99));
            EXPECT_EQ(b.placements().size(), 1u);
        }
    }

    TEST(SkyrimPickupBook, aGoodReadReplacesWhatWasThere)
    {
        PickupBook b;
        b.markTaken(99);
        MemReader r(bytesOf({ 1, 10, 4 }));
        std::string error;
        ASSERT_TRUE(b.read(r, error));
        EXPECT_FALSE(b.taken(99));
        EXPECT_TRUE(b.taken(10));
        EXPECT_EQ(b.attempts(), 4u);
    }

    TEST(SkyrimPickupChunk, checksTheVersionAndReports)
    {
        PickupBook a;
        a.markTaken(7);
        PickupChunk chunk(a);
        EXPECT_EQ(chunk.chunkName(), "pickups");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
        MemWriter w;
        chunk.write(w);
        PickupBook b;
        PickupChunk other(b);
        std::string reported;
        other.setRefusalSink([&](const std::string& s) { reported = s; });
        MemReader ok(w.mBytes);
        EXPECT_TRUE(other.read(ok, 1));
        EXPECT_TRUE(b.taken(7));
        MemReader bad(w.mBytes);
        EXPECT_FALSE(other.read(bad, 2));
        EXPECT_FALSE(other.refusal().empty());
        EXPECT_EQ(reported, other.refusal());
    }

    TEST(SkyrimPickupCount, noneAndHostileAreOneUnit)
    {
        EXPECT_EQ(placedCount(0), 1);
        EXPECT_EQ(placedCount(-5), 1);
        EXPECT_EQ(placedCount(1), 1);
        EXPECT_EQ(placedCount(25), 25);
        EXPECT_EQ(placedCount(std::numeric_limits<std::int32_t>::max()), kMaxPickupCount);
    }

    TEST(SkyrimPickpocketChance, followsTheFormula)
    {
        const PickpocketSettings s;
        // 25 + 0.6 * 50 - 3 * sqrt(100) - 1.5 * 2 = 25 + 30 - 30 - 3 = 22
        EXPECT_NEAR(pickpocketChance(s, 50.f, 100, 2.f, 1), 22.f, 1e-3f);
        // two units: sqrt(200) and double the weight: 25 + 30 - 42.426 - 6 = 6.574
        EXPECT_NEAR(pickpocketChance(s, 50.f, 100, 2.f, 2), 6.574f, 1e-2f);
    }

    TEST(SkyrimPickpocketChance, risesWithSkillAndFallsWithValueAndWeight)
    {
        const PickpocketSettings s;
        EXPECT_LT(pickpocketChance(s, 20.f, 50, 1.f, 1), pickpocketChance(s, 60.f, 50, 1.f, 1));
        EXPECT_GT(pickpocketChance(s, 40.f, 10, 1.f, 1), pickpocketChance(s, 40.f, 400, 1.f, 1));
        EXPECT_GT(pickpocketChance(s, 40.f, 10, 0.f, 1), pickpocketChance(s, 40.f, 10, 20.f, 1));
    }

    TEST(SkyrimPickpocketChance, isClamped)
    {
        const PickpocketSettings s;
        EXPECT_FLOAT_EQ(pickpocketChance(s, 200.f, 0, 0.f, 1), s.mMax);
        EXPECT_FLOAT_EQ(pickpocketChance(s, 0.f, 100000, 100.f, 1), s.mMin);
    }

    TEST(SkyrimPickpocketChance, nonFiniteAndNegativeInputsAreSafe)
    {
        const PickpocketSettings s;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        for (const float v : { pickpocketChance(s, nan, 10, 1.f, 1), pickpocketChance(s, inf, 10, 1.f, 1),
                 pickpocketChance(s, 10.f, 10, nan, 1), pickpocketChance(s, 10.f, 10, -inf, 1),
                 pickpocketChance(s, 10.f, -50, -3.f, -2), pickpocketChance(s, -5.f, 10, 1.f, 0) })
        {
            EXPECT_TRUE(std::isfinite(v));
            EXPECT_GE(v, s.mMin);
            EXPECT_LE(v, s.mMax);
        }
        // a non-finite skill counts as 0 skill
        EXPECT_FLOAT_EQ(pickpocketChance(s, nan, 10, 1.f, 1), pickpocketChance(s, 0.f, 10, 1.f, 1));
    }

    TEST(SkyrimPickpocketRoll, isDeterministic)
    {
        for (std::uint32_t attempt = 0; attempt < 50; ++attempt)
            EXPECT_EQ(pickpocketRoll(0xb80803, 0x2f, attempt, 40.f), pickpocketRoll(0xb80803, 0x2f, attempt, 40.f));
    }

    TEST(SkyrimPickpocketRoll, theEdgesAreExact)
    {
        for (std::uint32_t attempt = 0; attempt < 200; ++attempt)
        {
            EXPECT_FALSE(pickpocketRoll(1, 2, attempt, 0.f));
            EXPECT_FALSE(pickpocketRoll(1, 2, attempt, -10.f));
            EXPECT_TRUE(pickpocketRoll(1, 2, attempt, 100.f));
            EXPECT_TRUE(pickpocketRoll(1, 2, attempt, 250.f));
        }
    }

    TEST(SkyrimPickpocketRoll, theRateMatchesTheChance)
    {
        for (const float chance : { 10.f, 35.f, 50.f, 80.f })
        {
            int hits = 0;
            constexpr int n = 4000;
            for (int attempt = 0; attempt < n; ++attempt)
                if (pickpocketRoll(0xb80803, 0x2f, static_cast<std::uint32_t>(attempt), chance))
                    ++hits;
            EXPECT_NEAR(100.0 * hits / n, chance, 3.0) << chance;
        }
    }

    TEST(SkyrimPickpocketRoll, theSeedUsesOwnerItemAndAttempt)
    {
        int differByOwner = 0, differByItem = 0, differByAttempt = 0;
        for (std::uint32_t i = 0; i < 200; ++i)
        {
            differByOwner += pickpocketRoll(1, 5, i, 50.f) != pickpocketRoll(2, 5, i, 50.f);
            differByItem += pickpocketRoll(1, 5, i, 50.f) != pickpocketRoll(1, 6, i, 50.f);
            differByAttempt += pickpocketRoll(1, 5, i, 50.f) != pickpocketRoll(1, 5, i + 1, 50.f);
        }
        EXPECT_GT(differByOwner, 20);
        EXPECT_GT(differByItem, 20);
        EXPECT_GT(differByAttempt, 20);
    }

    TEST(SkyrimPickpocketTraining, isTheValueOfWhatWasTriedFor)
    {
        EXPECT_FLOAT_EQ(pickpocketTraining(50, 2), 100.f);
        EXPECT_FLOAT_EQ(pickpocketTraining(0, 1), 1.f);
        EXPECT_FLOAT_EQ(pickpocketTraining(-4, 3), 1.f);
        EXPECT_FLOAT_EQ(pickpocketTraining(5, 0), 1.f);
    }
}
