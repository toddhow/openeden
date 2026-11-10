// Task 0100 stage N1: the session identity table of the neutral world model (components/worldmodel/idtable.*). Pure
// values, no game data.
#include <gtest/gtest.h>

#include <components/worldmodel/idtable.hpp>

#include <cstdint>
#include <iterator>
#include <string>

namespace
{
    using NeutralWorld::IdTable;
    using openeden::sdk::game::kNoWorldId;
    using openeden::sdk::game::WorldId;

    TEST(NeutralIdTable, numbersStartAtOneAndFollowTheOrderOfFirstRequest)
    {
        IdTable table;
        EXPECT_EQ(table.idFor(1, "alpha"), 1u);
        EXPECT_EQ(table.idFor(1, "beta"), 2u);
        EXPECT_EQ(table.idFor(1, "gamma"), 3u);
        EXPECT_EQ(table.size(), 3u);
    }

    TEST(NeutralIdTable, theSameKeyGivesTheSameIdForTheWholeSession)
    {
        IdTable table;
        const WorldId a = table.idFor(1, "alpha");
        const WorldId b = table.idFor(1, "beta");
        EXPECT_EQ(table.idFor(1, "alpha"), a);
        EXPECT_EQ(table.idFor(1, "beta"), b);
        EXPECT_EQ(table.idFor(1, "alpha"), a);
        EXPECT_EQ(table.size(), 2u);
    }

    TEST(NeutralIdTable, theSameBytesInAnotherSpaceAreAnotherKey)
    {
        IdTable table;
        const WorldId a = table.idFor(1, "same");
        const WorldId b = table.idFor(2, "same");
        EXPECT_NE(a, b);
        EXPECT_EQ(table.keyOf(a)->space, 1);
        EXPECT_EQ(table.keyOf(b)->space, 2);
    }

    TEST(NeutralIdTable, findNeverMintsAndAnUnknownKeyIsNoId)
    {
        IdTable table;
        EXPECT_EQ(table.find(1, "missing"), kNoWorldId);
        EXPECT_EQ(table.findNumber(1, 7), kNoWorldId);
        EXPECT_EQ(table.size(), 0u);
        const WorldId a = table.idFor(1, "present");
        EXPECT_EQ(table.find(1, "present"), a);
        EXPECT_EQ(table.size(), 1u);
    }

    TEST(NeutralIdTable, keyOfGivesBackTheKeyAndRefusesIdsThatWereNeverMinted)
    {
        IdTable table;
        const WorldId a = table.idFor(3, "hello");
        const IdTable::Key* key = table.keyOf(a);
        ASSERT_NE(key, nullptr);
        EXPECT_EQ(key->space, 3);
        EXPECT_EQ(key->bytes, "hello");
        EXPECT_EQ(table.keyOf(kNoWorldId), nullptr);
        EXPECT_EQ(table.keyOf(a + 1), nullptr);
        EXPECT_EQ(table.keyOf(0xffffffffu), nullptr);
    }

    TEST(NeutralIdTable, numberKeysRoundTripIncludingTheExtremes)
    {
        IdTable table;
        const std::uint64_t numbers[] = { 0, 1, 0xffu, 0x100u, 0xffffffffull, 0x100000000ull, 0xffffffffffffffffull };
        for (const std::uint64_t n : numbers)
        {
            const WorldId id = table.idForNumber(5, n);
            ASSERT_NE(id, kNoWorldId);
            EXPECT_EQ(table.idForNumber(5, n), id);
            EXPECT_EQ(table.findNumber(5, n), id);
            std::uint64_t back = 12345;
            ASSERT_TRUE(table.numberOf(id, 5, back));
            EXPECT_EQ(back, n);
        }
        EXPECT_EQ(table.size(), std::size(numbers));
    }

    TEST(NeutralIdTable, numberOfRefusesAnotherSpaceAnUnknownIdAndAKeyThatIsNotANumber)
    {
        IdTable table;
        const WorldId n = table.idForNumber(5, 9);
        const WorldId s = table.idFor(6, "text");
        std::uint64_t out = 77;
        EXPECT_FALSE(table.numberOf(n, 6, out));
        EXPECT_FALSE(table.numberOf(s, 6, out));
        EXPECT_FALSE(table.numberOf(kNoWorldId, 5, out));
        EXPECT_FALSE(table.numberOf(999, 5, out));
        EXPECT_EQ(out, 77u);
    }

    TEST(NeutralIdTable, aKeyWithEmbeddedNulBytesIsKeptWhole)
    {
        IdTable table;
        const std::string a("ab\0cd", 5);
        const std::string b("ab\0ce", 5);
        EXPECT_NE(table.idFor(1, a), table.idFor(1, b));
        EXPECT_EQ(table.keyOf(table.idFor(1, a))->bytes, a);
    }

    TEST(NeutralIdTable, aFullTableRefusesNewKeysCountsThemAndStillAnswersOldOnes)
    {
        IdTable table(3);
        const WorldId a = table.idFor(1, "a");
        (void)table.idFor(1, "b");
        (void)table.idFor(1, "c");
        EXPECT_EQ(table.idFor(1, "d"), kNoWorldId);
        EXPECT_EQ(table.idForNumber(2, 1), kNoWorldId);
        EXPECT_EQ(table.refused(), 2u);
        EXPECT_EQ(table.size(), 3u);
        EXPECT_EQ(table.idFor(1, "a"), a);
        EXPECT_EQ(table.refused(), 2u);
    }

    TEST(NeutralIdTable, twoTablesAskedInTheSameOrderAgree)
    {
        IdTable first;
        IdTable second;
        for (int i = 0; i < 200; ++i)
        {
            const std::string key = "k" + std::to_string((i * 37) % 53);
            EXPECT_EQ(first.idFor(static_cast<std::uint8_t>(i % 3), key),
                second.idFor(static_cast<std::uint8_t>(i % 3), key));
        }
        EXPECT_EQ(first.size(), second.size());
    }
}
