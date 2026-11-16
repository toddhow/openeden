// Task 0224: the pure mining and woodcutting rules (mining.hpp): reading the resource data from a VMAD, the strike
// counting, depletion and regrowth, the save chunk. Written from the spec before the code and run red against a stub.
#include <game_skyrimse/mining.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::mining;

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

    template <class T>
    void put(std::vector<unsigned char>& out, T v)
    {
        unsigned char b[sizeof(T)];
        std::memcpy(b, &v, sizeof(T));
        out.insert(out.end(), b, b + sizeof(T));
    }

    vmad::Property object(const char* name, std::uint32_t form)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        return p;
    }

    vmad::Property integer(const char* name, std::int32_t v)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 3;
        p.mValue.mKind = vmad::ValueKind::Int;
        p.mValue.mInteger = v;
        return p;
    }

    vmad::Script oreScript()
    {
        vmad::Script s;
        s.mName = "MineOreScript";
        s.mProperties.push_back(object("Ore", 0x5ACE4));
        s.mProperties.push_back(object("mineOreToolsList", 0xE3C16));
        s.mProperties.push_back(object("lItemGems10", 0xF0000));
        s.mProperties.push_back(object("FailureMessage", 0xA2BDF));
        s.mProperties.push_back(object("DepletedMessage", 0xA2BE0));
        return s;
    }

    ResourceInfo ore(int total = 3, int perDrop = 1, int strikes = 1)
    {
        ResourceInfo info;
        info.mKind = ResourceKind::Ore;
        info.mTotal = total;
        info.mPerDrop = perDrop;
        info.mStrikes = strikes;
        return info;
    }

    TEST(SkyrimMiningScripts, aVeinScriptGivesTheOreAndItsProperties)
    {
        const ResourceInfo info = resourceFromScripts({ oreScript() });
        EXPECT_EQ(info.mKind, ResourceKind::Ore);
        EXPECT_EQ(info.mItem, 0x5ACE4u);
        EXPECT_EQ(info.mToolList, 0xE3C16u);
        EXPECT_EQ(info.mGems, 0xF0000u);
        EXPECT_EQ(info.mFailureMessage, 0xA2BDFu);
        EXPECT_EQ(info.mDepletedMessage, 0xA2BE0u);
    }

    TEST(SkyrimMiningScripts, theScriptsOwnDefaultsApplyWhenNoCountIsSet)
    {
        const ResourceInfo info = resourceFromScripts({ oreScript() });
        EXPECT_EQ(info.mTotal, 3);
        EXPECT_EQ(info.mPerDrop, 1);
        EXPECT_EQ(info.mStrikes, 1);
    }

    TEST(SkyrimMiningScripts, countsSetOnTheScriptAreTaken)
    {
        vmad::Script s = oreScript();
        s.mProperties.push_back(integer("ResourceCountTotal", 5));
        s.mProperties.push_back(integer("ResourceCount", 2));
        s.mProperties.push_back(integer("StrikesBeforeCollection", 4));
        const ResourceInfo info = resourceFromScripts({ s });
        EXPECT_EQ(info.mTotal, 5);
        EXPECT_EQ(info.mPerDrop, 2);
        EXPECT_EQ(info.mStrikes, 4);
    }

    TEST(SkyrimMiningScripts, propertyNamesMatchIgnoringCaseAndHostileCountsAreClamped)
    {
        vmad::Script s;
        s.mName = "mineorescript";
        s.mProperties.push_back(object("ORE", 7));
        s.mProperties.push_back(integer("resourcecounttotal", 2000000000));
        s.mProperties.push_back(integer("ResourceCount", -4));
        const ResourceInfo info = resourceFromScripts({ s });
        EXPECT_EQ(info.mKind, ResourceKind::Ore);
        EXPECT_EQ(info.mItem, 7u);
        EXPECT_EQ(info.mTotal, kMaxCount);
        EXPECT_EQ(info.mPerDrop, 1);
    }

    TEST(SkyrimMiningScripts, aPropertyOfTheWrongTypeIsIgnored)
    {
        vmad::Script s = oreScript();
        s.mProperties.push_back(integer("Ore", 9)); // an int where a form is expected
        s.mProperties.push_back(object("ResourceCountTotal", 4)); // a form where an int is expected
        const ResourceInfo info = resourceFromScripts({ s });
        EXPECT_EQ(info.mItem, 0x5ACE4u);
        EXPECT_EQ(info.mTotal, 3);
    }

    TEST(SkyrimMiningScripts, theChoppingBlockScriptIsWood)
    {
        vmad::Script s;
        s.mName = "ResourceFurnitureScript";
        s.mProperties.push_back(object("Resource", 0x6F993));
        s.mProperties.push_back(object("requiredItemList", 0x70230));
        s.mProperties.push_back(object("FailureMessage", 0x7022D));
        const ResourceInfo info = resourceFromScripts({ s });
        EXPECT_EQ(info.mKind, ResourceKind::Wood);
        EXPECT_EQ(info.mItem, 0x6F993u);
        EXPECT_EQ(info.mToolList, 0x70230u);
        EXPECT_EQ(info.mFailureMessage, 0x7022Du);
        EXPECT_EQ(info.mPerDrop, 1);
        EXPECT_EQ(info.mMaxPerUse, 6);
    }

    TEST(SkyrimMiningScripts, maxPerActivationIsTaken)
    {
        vmad::Script s;
        s.mName = "ResourceFurnitureScript";
        s.mProperties.push_back(object("Resource", 1));
        s.mProperties.push_back(integer("MaxResourcePerActivation", 3));
        s.mProperties.push_back(integer("ResourceCount", 2));
        const ResourceInfo info = resourceFromScripts({ s });
        EXPECT_EQ(info.mMaxPerUse, 3);
        EXPECT_EQ(info.mPerDrop, 2);
    }

    TEST(SkyrimMiningScripts, otherScriptsAreNoResource)
    {
        vmad::Script s;
        s.mName = "defaultActivateScript";
        s.mProperties.push_back(object("Ore", 5));
        EXPECT_EQ(resourceFromScripts({ s }).mKind, ResourceKind::None);
        EXPECT_EQ(resourceFromScripts({}).mKind, ResourceKind::None);
    }

    TEST(SkyrimMiningScripts, theResourceScriptIsFoundAmongOthers)
    {
        vmad::Script other;
        other.mName = "somethingElse";
        EXPECT_EQ(resourceFromScripts({ other, oreScript() }).mKind, ResourceKind::Ore);
    }

    TEST(SkyrimMiningText, theOrePromptIsTheVerbAndTheName)
    {
        EXPECT_EQ(orePrompt("Mine", "Iron Ore Vein"), "Mine Iron Ore Vein");
        EXPECT_EQ(orePrompt("", "Iron Ore Vein"), "Mine Iron Ore Vein");
        EXPECT_EQ(orePrompt("Dig", "Gold Ore Vein"), "Dig Gold Ore Vein");
        EXPECT_EQ(orePrompt("", ""), "Mine");
    }

    TEST(SkyrimMiningText, aDepletedNameSaysSo)
    {
        EXPECT_EQ(depletedName("Iron Ore Vein"), "Iron Ore Vein (depleted)");
    }

    TEST(SkyrimMiningText, gemSeedsDifferByPlacementAndDropAndAreStable)
    {
        EXPECT_EQ(gemSeed(10, 1), gemSeed(10, 1));
        EXPECT_NE(gemSeed(10, 1), gemSeed(10, 2));
        EXPECT_NE(gemSeed(10, 1), gemSeed(11, 1));
    }

    TEST(SkyrimMiningBook, eachStrikeCountsAndTheLastDropDepletes)
    {
        VeinBook book;
        const ResourceInfo info = ore(3, 1, 1);
        StrikeOutcome a = book.strike(100, info, 5000.0);
        EXPECT_TRUE(a.mDrop);
        EXPECT_FALSE(a.mDepleted);
        EXPECT_FALSE(book.depleted(100, 1.0));
        EXPECT_TRUE(book.strike(100, info, 5000.0).mDrop);
        const StrikeOutcome last = book.strike(100, info, 5000.0);
        EXPECT_TRUE(last.mDrop);
        EXPECT_TRUE(last.mDepleted);
        EXPECT_TRUE(book.depleted(100, 1.0));
        EXPECT_EQ(book.state(100).mDue, 5000.0);
        EXPECT_EQ(book.state(100).mDrops, 3);
    }

    TEST(SkyrimMiningBook, severalStrikesMakeOneDropWhenTheScriptSaysSo)
    {
        VeinBook book;
        const ResourceInfo info = ore(2, 1, 3);
        EXPECT_FALSE(book.strike(7, info, 100.0).mDrop);
        EXPECT_FALSE(book.strike(7, info, 100.0).mDrop);
        EXPECT_EQ(book.state(7).mStrikes, 2);
        EXPECT_TRUE(book.strike(7, info, 100.0).mDrop);
        EXPECT_EQ(book.state(7).mStrikes, 0);
        EXPECT_EQ(book.state(7).mDrops, 1);
    }

    TEST(SkyrimMiningBook, aDepletedVeinTakesNoMoreStrikes)
    {
        VeinBook book;
        const ResourceInfo info = ore(1);
        EXPECT_TRUE(book.strike(5, info, 100.0).mDepleted);
        const StrikeOutcome again = book.strike(5, info, 999.0);
        EXPECT_FALSE(again.mDrop);
        EXPECT_FALSE(again.mDepleted);
        EXPECT_EQ(book.state(5).mDue, 100.0);
    }

    TEST(SkyrimMiningBook, theNullPlacementIsRefused)
    {
        VeinBook book;
        EXPECT_FALSE(book.strike(0, ore(), 1.0).mDrop);
        EXPECT_TRUE(book.entries().empty());
    }

    TEST(SkyrimMiningBook, aVeinGrowsBackAtItsTimeAndTheNegativeClockKeepsItDepleted)
    {
        VeinBook book;
        (void)book.strike(9, ore(1), 500.0);
        std::vector<FormId> grown;
        book.collectGrown(-1.0, grown);
        EXPECT_TRUE(grown.empty());
        EXPECT_TRUE(book.depleted(9, -1.0));
        book.collectGrown(499.0, grown);
        EXPECT_TRUE(grown.empty());
        EXPECT_TRUE(book.depleted(9, 499.0));
        EXPECT_FALSE(book.depleted(9, 500.0)); // due: not depleted any more even before collection
        book.collectGrown(500.0, grown);
        ASSERT_EQ(grown.size(), 1u);
        EXPECT_EQ(grown[0], 9u);
        EXPECT_EQ(book.state(9).mDrops, 0);
        EXPECT_TRUE(book.entries().empty());
    }

    TEST(SkyrimMiningBook, grownVeinsAreListedAscending)
    {
        VeinBook book;
        (void)book.strike(30, ore(1), 10.0);
        (void)book.strike(20, ore(1), 10.0);
        (void)book.strike(25, ore(1), 99.0);
        std::vector<FormId> grown;
        book.collectGrown(10.0, grown);
        ASSERT_EQ(grown.size(), 2u);
        EXPECT_EQ(grown[0], 20u);
        EXPECT_EQ(grown[1], 30u);
    }

    TEST(SkyrimMiningBook, theRevisionMovesWithEveryChange)
    {
        VeinBook book;
        const std::uint32_t r0 = book.revision();
        (void)book.strike(1, ore(), 10.0);
        EXPECT_GT(book.revision(), r0);
    }

    TEST(SkyrimMiningChunk, aBookRoundTripsAndKeepsStrikesDropsAndDueTime)
    {
        VeinBook book;
        (void)book.strike(300, ore(3, 1, 2), 77.5);
        (void)book.strike(100, ore(1), 42.25);
        MemWriter out;
        book.write(out);
        VeinBook loaded;
        MemReader reader(out.mBytes);
        std::string error;
        ASSERT_TRUE(loaded.read(reader, error)) << error;
        ASSERT_EQ(loaded.entries().size(), 2u);
        EXPECT_EQ(loaded.state(300).mStrikes, 1);
        EXPECT_EQ(loaded.state(300).mDue, -1.0);
        EXPECT_EQ(loaded.state(100).mDrops, 1);
        EXPECT_EQ(loaded.state(100).mDue, 42.25);
        MemWriter again;
        loaded.write(again);
        EXPECT_EQ(again.mBytes, out.mBytes);
    }

    TEST(SkyrimMiningChunk, theWrittenBytesAreTheDocumentedLayout)
    {
        VeinBook book;
        (void)book.strike(5, ore(1), 8.0);
        MemWriter out;
        book.write(out);
        std::vector<unsigned char> expect;
        put<std::uint32_t>(expect, 1);
        put<std::uint32_t>(expect, 5);
        put<std::uint16_t>(expect, 0);
        put<std::uint16_t>(expect, 1);
        put<double>(expect, 8.0);
        EXPECT_EQ(out.mBytes, expect);
    }

    std::vector<unsigned char> oneEntry(std::uint32_t id, double due)
    {
        std::vector<unsigned char> b;
        put<std::uint32_t>(b, 1);
        put<std::uint32_t>(b, id);
        put<std::uint16_t>(b, 0);
        put<std::uint16_t>(b, 1);
        put<double>(b, due);
        return b;
    }

    TEST(SkyrimMiningChunk, aDamagedReadChangesNothing)
    {
        VeinBook book;
        (void)book.strike(1, ore(1), 10.0);
        const auto before = book.entries().size();
        std::vector<std::vector<unsigned char>> bad;
        bad.push_back({}); // empty
        bad.push_back(oneEntry(0, 1.0)); // the null placement
        bad.push_back(oneEntry(5, std::numeric_limits<double>::infinity()));
        bad.push_back(oneEntry(5, std::nan("")));
        auto trailing = oneEntry(5, 1.0);
        trailing.push_back(1);
        bad.push_back(trailing);
        auto cut = oneEntry(5, 1.0);
        cut.pop_back();
        bad.push_back(cut);
        std::vector<unsigned char> descending;
        put<std::uint32_t>(descending, 2);
        for (const std::uint32_t id : { 9u, 4u })
        {
            put<std::uint32_t>(descending, id);
            put<std::uint16_t>(descending, 0);
            put<std::uint16_t>(descending, 1);
            put<double>(descending, 1.0);
        }
        bad.push_back(descending);
        std::vector<unsigned char> huge;
        put<std::uint32_t>(huge, 0x7fffffffu);
        bad.push_back(huge);
        for (const auto& bytes : bad)
        {
            MemReader reader(bytes);
            std::string error;
            EXPECT_FALSE(book.read(reader, error));
            EXPECT_FALSE(error.empty());
            EXPECT_EQ(book.entries().size(), before);
            EXPECT_EQ(book.state(1).mDue, 10.0);
        }
    }

    TEST(SkyrimMiningChunk, theChunkHandlerAcceptsVersionOneAndRefusesOthers)
    {
        VeinBook book;
        MiningChunk chunk(book);
        EXPECT_EQ(chunk.chunkName(), "mining");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
        std::string sunk;
        chunk.setRefusalSink([&](const std::string& why) { sunk = why; });
        std::size_t restored = 99;
        chunk.setRestoredSink([&](std::size_t n) { restored = n; });
        MemReader wrongVersion(oneEntry(5, 1.0));
        EXPECT_FALSE(chunk.read(wrongVersion, 2));
        EXPECT_FALSE(sunk.empty());
        MemReader good(oneEntry(5, 1.0));
        EXPECT_TRUE(chunk.read(good, 1));
        EXPECT_EQ(restored, 1u);
        EXPECT_EQ(book.entries().size(), 1u);
    }
}
