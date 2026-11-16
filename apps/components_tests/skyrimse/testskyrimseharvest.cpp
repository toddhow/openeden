// Task 0221: the pure harvesting rules (harvest.hpp): seasons, chances, the seeded roll, the prompt text, the book of
// harvested plants and its save chunk. Written from the spec before the code and run red against a stub.
#include <game_skyrimse/harvest.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::harvest;

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

    void putU32(std::vector<unsigned char>& out, std::uint32_t v)
    {
        unsigned char b[4];
        std::memcpy(b, &v, 4);
        out.insert(out.end(), b, b + 4);
    }

    void putF64(std::vector<unsigned char>& out, double v)
    {
        unsigned char b[8];
        std::memcpy(b, &v, 8);
        out.insert(out.end(), b, b + 8);
    }

    TEST(SkyrimHarvestSeason, monthsMapToTheFourSeasons)
    {
        EXPECT_EQ(seasonOfMonth(0), Season::Winter);
        EXPECT_EQ(seasonOfMonth(1), Season::Winter);
        EXPECT_EQ(seasonOfMonth(2), Season::Spring);
        EXPECT_EQ(seasonOfMonth(4), Season::Spring);
        EXPECT_EQ(seasonOfMonth(5), Season::Summer);
        EXPECT_EQ(seasonOfMonth(7), Season::Summer);
        EXPECT_EQ(seasonOfMonth(8), Season::Fall);
        EXPECT_EQ(seasonOfMonth(10), Season::Fall);
        EXPECT_EQ(seasonOfMonth(11), Season::Winter);
    }

    TEST(SkyrimHarvestSeason, aMonthOutsideTheYearIsUnknown)
    {
        EXPECT_EQ(seasonOfMonth(-1), Season::Unknown);
        EXPECT_EQ(seasonOfMonth(12), Season::Unknown);
        EXPECT_EQ(seasonOfMonth(1000), Season::Unknown);
    }

    TEST(SkyrimHarvestChance, theByteOfTheSeasonIsThePercent)
    {
        const std::array<std::uint8_t, 4> chances{ 10, 20, 30, 40 };
        EXPECT_FLOAT_EQ(chanceIn(chances, Season::Spring), 10.f);
        EXPECT_FLOAT_EQ(chanceIn(chances, Season::Summer), 20.f);
        EXPECT_FLOAT_EQ(chanceIn(chances, Season::Fall), 30.f);
        EXPECT_FLOAT_EQ(chanceIn(chances, Season::Winter), 40.f);
    }

    TEST(SkyrimHarvestChance, aSeasonWithZeroGrowsNothingWhenOthersDo)
    {
        const std::array<std::uint8_t, 4> chances{ 0, 100, 100, 0 };
        EXPECT_FLOAT_EQ(chanceIn(chances, Season::Spring), 0.f);
        EXPECT_FLOAT_EQ(chanceIn(chances, Season::Winter), 0.f);
    }

    TEST(SkyrimHarvestChance, fourZerosMeanNoSeasonalData)
    {
        const std::array<std::uint8_t, 4> none{ 0, 0, 0, 0 };
        EXPECT_FLOAT_EQ(chanceIn(none, Season::Winter), kUnseasonalChance);
        EXPECT_FLOAT_EQ(chanceIn(none, Season::Unknown), kUnseasonalChance);
    }

    TEST(SkyrimHarvestChance, anUnknownSeasonTakesTheBestOfTheFour)
    {
        const std::array<std::uint8_t, 4> chances{ 0, 70, 90, 0 };
        EXPECT_FLOAT_EQ(chanceIn(chances, Season::Unknown), 90.f);
    }

    TEST(SkyrimHarvestRoll, theExtremesAreCertain)
    {
        for (std::uint32_t attempt = 0; attempt < 50; ++attempt)
        {
            EXPECT_FALSE(harvestRoll(0x1234, attempt, 0.f));
            EXPECT_FALSE(harvestRoll(0x1234, attempt, -5.f));
            EXPECT_TRUE(harvestRoll(0x1234, attempt, 100.f));
            EXPECT_TRUE(harvestRoll(0x1234, attempt, 250.f));
        }
        EXPECT_FALSE(harvestRoll(0x1234, 0, std::numeric_limits<float>::quiet_NaN()));
    }

    TEST(SkyrimHarvestRoll, isDeterministicAndFollowsTheChance)
    {
        int hits = 0;
        for (std::uint32_t attempt = 0; attempt < 2000; ++attempt)
        {
            const bool a = harvestRoll(0x00042fc1, attempt, 50.f);
            const bool b = harvestRoll(0x00042fc1, attempt, 50.f);
            EXPECT_EQ(a, b);
            hits += a ? 1 : 0;
        }
        EXPECT_GT(hits, 850);
        EXPECT_LT(hits, 1150);
    }

    TEST(SkyrimHarvestRoll, differsByPlacement)
    {
        int differ = 0;
        for (std::uint32_t attempt = 0; attempt < 200; ++attempt)
            differ += harvestRoll(0x100, attempt, 50.f) != harvestRoll(0x101, attempt, 50.f) ? 1 : 0;
        EXPECT_GT(differ, 40);
    }

    TEST(SkyrimHarvestPickSeed, dependsOnThePlacementAndTheAttempt)
    {
        EXPECT_EQ(pickSeed(5, 1), pickSeed(5, 1));
        EXPECT_NE(pickSeed(5, 1), pickSeed(5, 2));
        EXPECT_NE(pickSeed(5, 1), pickSeed(6, 1));
    }

    TEST(SkyrimHarvestPrompt, usesTheRecordsVerbAndName)
    {
        EXPECT_EQ(promptFor("Pick", "Canis Root"), "Pick Canis Root");
        EXPECT_EQ(promptFor("Catch", "Salmon"), "Catch Salmon");
    }

    TEST(SkyrimHarvestPrompt, defaultsToHarvestAndSurvivesAMissingName)
    {
        EXPECT_EQ(promptFor("", "Lavender"), "Harvest Lavender");
        EXPECT_EQ(promptFor("Mine", ""), "Mine");
        EXPECT_EQ(promptFor("", ""), "Harvest");
    }

    TEST(SkyrimHarvestBook, marksOncePerPlantAndRefusesTheNullForm)
    {
        HarvestBook book;
        EXPECT_FALSE(book.mark(kNullFormId, 10.0));
        EXPECT_TRUE(book.mark(0x500, 100.0));
        EXPECT_FALSE(book.mark(0x500, 200.0));
        EXPECT_TRUE(book.has(0x500));
        EXPECT_FALSE(book.has(0x501));
        EXPECT_EQ(book.entries().size(), 1u);
        EXPECT_FALSE(book.mark(0x501, std::numeric_limits<double>::infinity()));
    }

    TEST(SkyrimHarvestBook, aPlantIsHiddenUntilItsTimeComes)
    {
        HarvestBook book;
        book.mark(0x500, 1000.0);
        EXPECT_TRUE(book.hidden(0x500, 999.0));
        EXPECT_FALSE(book.hidden(0x500, 1000.0));
        EXPECT_FALSE(book.hidden(0x500, 5000.0));
        EXPECT_FALSE(book.hidden(0x777, 1.0));
    }

    TEST(SkyrimHarvestBook, anUnknownClockHidesEveryHarvestedPlant)
    {
        HarvestBook book;
        book.mark(0x500, 1000.0);
        EXPECT_TRUE(book.hidden(0x500, -1.0));
    }

    TEST(SkyrimHarvestBook, collectGrownRemovesTheDueEntriesAscending)
    {
        HarvestBook book;
        book.mark(0x30, 50.0);
        book.mark(0x10, 10.0);
        book.mark(0x20, 90.0);
        std::vector<FormId> grown;
        book.collectGrown(60.0, grown);
        ASSERT_EQ(grown.size(), 2u);
        EXPECT_EQ(grown[0], 0x10u);
        EXPECT_EQ(grown[1], 0x30u);
        EXPECT_TRUE(book.has(0x20));
        EXPECT_FALSE(book.has(0x10));
    }

    TEST(SkyrimHarvestBook, collectGrownWaitsForAClock)
    {
        HarvestBook book;
        book.mark(0x10, 0.0);
        std::vector<FormId> grown;
        book.collectGrown(-1.0, grown);
        EXPECT_TRUE(grown.empty());
        EXPECT_TRUE(book.has(0x10));
    }

    TEST(SkyrimHarvestBook, aFullBookRefusesAnotherPlant)
    {
        HarvestBook book;
        for (std::size_t i = 1; i <= kMaxHarvested; ++i)
            ASSERT_TRUE(book.mark(static_cast<FormId>(i), 1.0));
        EXPECT_FALSE(book.mark(static_cast<FormId>(kMaxHarvested + 1), 1.0));
    }

    TEST(SkyrimHarvestBook, countsTheAttempts)
    {
        HarvestBook book;
        EXPECT_EQ(book.takeAttempt(), 0u);
        EXPECT_EQ(book.takeAttempt(), 1u);
        EXPECT_EQ(book.attempts(), 2u);
    }

    TEST(SkyrimHarvestChunk, roundTripsThePlantsAndTheAttempts)
    {
        HarvestBook book;
        book.mark(0x30, 50.5);
        book.mark(0x10, 10.25);
        book.takeAttempt();
        book.takeAttempt();
        MemWriter writer;
        book.write(writer);

        HarvestBook loaded;
        loaded.mark(0x99, 1.0);
        MemReader reader(writer.mBytes);
        std::string error;
        ASSERT_TRUE(loaded.read(reader, error)) << error;
        EXPECT_FALSE(loaded.has(0x99)) << "a restore replaces what was there";
        ASSERT_EQ(loaded.entries().size(), 2u);
        EXPECT_DOUBLE_EQ(loaded.entries().at(0x10), 10.25);
        EXPECT_DOUBLE_EQ(loaded.entries().at(0x30), 50.5);
        EXPECT_EQ(loaded.attempts(), 2u);
    }

    TEST(SkyrimHarvestChunk, aDamagedReadChangesNothing)
    {
        HarvestBook book;
        book.mark(0x11, 1.0);
        const std::vector<std::vector<unsigned char>> bad = [] {
            std::vector<std::vector<unsigned char>> v;
            v.emplace_back(); // nothing at all
            std::vector<unsigned char> huge;
            putU32(huge, 0xFFFFFFFFu);
            v.push_back(huge);
            std::vector<unsigned char> notAscending;
            putU32(notAscending, 2);
            putU32(notAscending, 7);
            putF64(notAscending, 1.0);
            putU32(notAscending, 7);
            putF64(notAscending, 2.0);
            putU32(notAscending, 0);
            v.push_back(notAscending);
            std::vector<unsigned char> nullId;
            putU32(nullId, 1);
            putU32(nullId, 0);
            putF64(nullId, 1.0);
            putU32(nullId, 0);
            v.push_back(nullId);
            std::vector<unsigned char> nan;
            putU32(nan, 1);
            putU32(nan, 5);
            putF64(nan, std::numeric_limits<double>::quiet_NaN());
            putU32(nan, 0);
            v.push_back(nan);
            std::vector<unsigned char> extra;
            putU32(extra, 1);
            putU32(extra, 5);
            putF64(extra, 1.0);
            putU32(extra, 0);
            putU32(extra, 0xDEAD);
            v.push_back(extra);
            std::vector<unsigned char> cut;
            putU32(cut, 1);
            putU32(cut, 5);
            v.push_back(cut);
            return v;
        }();
        for (const auto& bytes : bad)
        {
            MemReader reader(bytes);
            std::string error;
            EXPECT_FALSE(book.read(reader, error));
            EXPECT_FALSE(error.empty());
            EXPECT_TRUE(book.has(0x11));
            EXPECT_EQ(book.entries().size(), 1u);
        }
    }

    TEST(SkyrimHarvestChunk, refusesAnotherVersionAndSaysSo)
    {
        HarvestBook book;
        HarvestChunk chunk(book);
        std::string seen;
        chunk.setRefusalSink([&seen](const std::string& why) { seen = why; });
        MemReader reader(std::vector<unsigned char>{});
        EXPECT_FALSE(chunk.read(reader, 2));
        EXPECT_FALSE(seen.empty());
        EXPECT_EQ(chunk.chunkName(), "harvest");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
    }

    TEST(SkyrimHarvestChunk, aRestoredChunkTellsHowManyPlants)
    {
        HarvestBook book;
        book.mark(0x40, 7.0);
        book.mark(0x41, 8.0);
        HarvestChunk chunk(book);
        MemWriter written;
        chunk.write(written);
        HarvestBook other;
        HarvestChunk otherChunk(other);
        std::size_t told = 0;
        otherChunk.setRestoredSink([&told](std::size_t plants) { told = plants; });
        MemReader reader(written.mBytes);
        ASSERT_TRUE(otherChunk.read(reader, 1));
        EXPECT_EQ(told, 2u);
        // a refused chunk tells nothing
        told = 0;
        MemReader empty(std::vector<unsigned char>{});
        EXPECT_FALSE(otherChunk.read(empty, 1));
        EXPECT_EQ(told, 0u);
    }

    TEST(SkyrimHarvestChunk, theChunkWritesWhatTheBookWrites)
    {
        HarvestBook book;
        book.mark(0x40, 7.0);
        HarvestChunk chunk(book);
        MemWriter fromChunk;
        chunk.write(fromChunk);
        MemWriter fromBook;
        book.write(fromBook);
        EXPECT_EQ(fromChunk.mBytes, fromBook.mBytes);
        MemReader reader(fromChunk.mBytes);
        HarvestBook other;
        HarvestChunk otherChunk(other);
        EXPECT_TRUE(otherChunk.read(reader, 1));
        EXPECT_TRUE(other.has(0x40));
    }
}
