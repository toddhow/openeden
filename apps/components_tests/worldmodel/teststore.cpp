// Task 0100 stage N1: the core's neutral world store (components/worldmodel/store.*), over a scripted source. No game
// data.
#include <gtest/gtest.h>

#include <components/worldmodel/store.hpp>

#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using NeutralWorld::NeutralWorldStore;
    using NeutralWorld::StoreLimits;

    WorldObject object(WorldId id)
    {
        WorldObject o;
        o.id = id;
        o.kind = ObjectKind::Static;
        o.modelPath = "m.nif";
        o.collision.source = CollisionSource::FromModel;
        return o;
    }

    WorldCell cell(WorldId id, std::size_t objects)
    {
        WorldCell c;
        c.id = id;
        c.editorId = "cell" + std::to_string(id);
        for (std::size_t i = 0; i < objects; ++i)
            c.objects.push_back(object(static_cast<WorldId>(i + 1)));
        return c;
    }

    class ScriptedSource final : public WorldCellSource
    {
    public:
        explicit ScriptedSource(std::string game = "test")
            : mGame(std::move(game))
        {
        }

        std::string_view gameId() const override { return mGame; }
        std::size_t cellCount() const override { return mCells.size(); }
        std::size_t listCells(std::size_t first, std::size_t maxCount, std::vector<WorldId>& out) const override
        {
            std::size_t n = 0;
            std::size_t i = 0;
            for (const auto& [id, c] : mCells)
            {
                if (i++ < first || n >= maxCount)
                    continue;
                out.push_back(id);
                ++n;
            }
            return n;
        }
        bool buildCell(WorldId id, WorldCell& out, std::string& error) override
        {
            ++mBuilds;
            if (mThrow)
                throw std::runtime_error("scripted throw");
            const auto it = mCells.find(id);
            if (it == mCells.end())
            {
                error = "no such cell";
                return false;
            }
            out = it->second;
            if (mAnswerOtherId)
                out.id = id + 1000;
            return true;
        }

        std::string mGame;
        std::map<WorldId, WorldCell> mCells;
        int mBuilds = 0;
        bool mThrow = false;
        bool mAnswerOtherId = false;
    };

    std::string error;

    TEST(NeutralStore, nothingIsBuiltUntilACellIsRequested)
    {
        ScriptedSource source;
        source.mCells[1] = cell(1, 3);
        NeutralWorldStore store;
        ASSERT_TRUE(store.addSource(source, error)) << error;
        EXPECT_EQ(source.mBuilds, 0);
        EXPECT_EQ(store.stats().mResidentCells, 0u);
        EXPECT_FALSE(store.isResident("test", 1));
    }

    TEST(NeutralStore, aRequestedCellIsBuiltOnceValidatedAndKept)
    {
        ScriptedSource source;
        source.mCells[1] = cell(1, 3);
        NeutralWorldStore store;
        ASSERT_TRUE(store.addSource(source, error));
        const WorldCell* first = store.cell("test", 1, error);
        ASSERT_NE(first, nullptr) << error;
        EXPECT_EQ(*first, source.mCells[1]);
        const WorldCell* second = store.cell("test", 1, error);
        EXPECT_EQ(second, first);
        EXPECT_EQ(source.mBuilds, 1);
        EXPECT_EQ(store.stats().mBuilds, 1u);
        EXPECT_EQ(store.stats().mHits, 1u);
        EXPECT_EQ(store.stats().mResidentCells, 1u);
        EXPECT_EQ(store.stats().mResidentObjects, 3u);
        EXPECT_TRUE(store.isResident("test", 1));
    }

    TEST(NeutralStore, anUnknownGameOrCellGivesNullAndAMessage)
    {
        ScriptedSource source;
        NeutralWorldStore store;
        ASSERT_TRUE(store.addSource(source, error));
        std::string e;
        EXPECT_EQ(store.cell("other", 1, e), nullptr);
        EXPECT_NE(e.find("other"), std::string::npos) << e;
        e.clear();
        EXPECT_EQ(store.cell("test", 77, e), nullptr);
        EXPECT_NE(e.find("no such cell"), std::string::npos) << e;
        EXPECT_EQ(store.stats().mSourceFailures, 1u);
        EXPECT_EQ(store.stats().mResidentCells, 0u);
    }

    TEST(NeutralStore, aSourceThatThrowsIsContainedAndLeavesNothingBehind)
    {
        ScriptedSource source;
        source.mCells[1] = cell(1, 1);
        source.mThrow = true;
        NeutralWorldStore store;
        ASSERT_TRUE(store.addSource(source, error));
        std::string e;
        EXPECT_EQ(store.cell("test", 1, e), nullptr);
        EXPECT_NE(e.find("threw"), std::string::npos) << e;
        EXPECT_EQ(store.stats().mSourceFailures, 1u);
        EXPECT_FALSE(store.isResident("test", 1));
        source.mThrow = false;
        EXPECT_NE(store.cell("test", 1, e), nullptr) << e;
    }

    TEST(NeutralStore, aHostileSnapshotIsRefusedNotKeptAndCounted)
    {
        ScriptedSource source;
        source.mCells[1] = cell(1, 3);
        source.mCells[1].objects[1].transform.scale = std::numeric_limits<double>::quiet_NaN();
        source.mCells[2] = cell(2, 2);
        source.mCells[2].objects[0].transform.position.x = 5.0e9;
        NeutralWorldStore store;
        ASSERT_TRUE(store.addSource(source, error));
        std::string e;
        EXPECT_EQ(store.cell("test", 1, e), nullptr);
        EXPECT_NE(e.find("refused"), std::string::npos) << e;
        EXPECT_NE(e.find("object 1"), std::string::npos) << e;
        EXPECT_EQ(store.cell("test", 2, e), nullptr);
        EXPECT_EQ(store.stats().mRefusedByValidation, 2u);
        EXPECT_EQ(store.stats().mResidentCells, 0u);
        EXPECT_EQ(store.stats().mBuilds, 0u);
    }

    TEST(NeutralStore, aSourceThatAnswersAnotherCellThanAskedIsRefused)
    {
        ScriptedSource source;
        source.mCells[1] = cell(1, 1);
        source.mAnswerOtherId = true;
        NeutralWorldStore store;
        ASSERT_TRUE(store.addSource(source, error));
        std::string e;
        EXPECT_EQ(store.cell("test", 1, e), nullptr);
        EXPECT_NE(e.find("answered cell"), std::string::npos) << e;
        EXPECT_EQ(store.stats().mRefusedByValidation, 1u);
    }

    TEST(NeutralStore, theCellCapDropsTheLeastRecentlyRequestedCell)
    {
        ScriptedSource source;
        for (WorldId id = 1; id <= 4; ++id)
            source.mCells[id] = cell(id, 1);
        StoreLimits limits;
        limits.mMaxResidentCells = 2;
        NeutralWorldStore store(limits);
        ASSERT_TRUE(store.addSource(source, error));
        std::string e;
        ASSERT_NE(store.cell("test", 1, e), nullptr);
        ASSERT_NE(store.cell("test", 2, e), nullptr);
        ASSERT_NE(store.cell("test", 1, e), nullptr); // 1 is now more recent than 2
        ASSERT_NE(store.cell("test", 3, e), nullptr); // drops 2
        EXPECT_TRUE(store.isResident("test", 1));
        EXPECT_FALSE(store.isResident("test", 2));
        EXPECT_TRUE(store.isResident("test", 3));
        EXPECT_EQ(store.stats().mEvictions, 1u);
        EXPECT_EQ(store.stats().mResidentCells, 2u);
        // 2 comes back by a fresh build, equal to the first.
        const WorldCell* again = store.cell("test", 2, e);
        ASSERT_NE(again, nullptr);
        EXPECT_EQ(*again, source.mCells[2]);
        EXPECT_FALSE(store.isResident("test", 1)); // 1 was the least recent after 3
        EXPECT_EQ(source.mBuilds, 4);
    }

    TEST(NeutralStore, theObjectCapDropsOldCellsUntilTheNewOneFits)
    {
        ScriptedSource source;
        source.mCells[1] = cell(1, 40);
        source.mCells[2] = cell(2, 40);
        source.mCells[3] = cell(3, 60);
        StoreLimits limits;
        limits.mMaxResidentObjects = 100;
        NeutralWorldStore store(limits);
        ASSERT_TRUE(store.addSource(source, error));
        std::string e;
        ASSERT_NE(store.cell("test", 1, e), nullptr);
        ASSERT_NE(store.cell("test", 2, e), nullptr);
        EXPECT_EQ(store.stats().mResidentObjects, 80u);
        ASSERT_NE(store.cell("test", 3, e), nullptr) << e; // 60 + 40 fits only if the older one goes
        EXPECT_FALSE(store.isResident("test", 1));
        EXPECT_TRUE(store.isResident("test", 2));
        EXPECT_TRUE(store.isResident("test", 3));
        EXPECT_LE(store.stats().mResidentObjects, 100u);
    }

    TEST(NeutralStore, aCellLargerThanTheObjectCapIsRefusedAndTheResidentCellsStay)
    {
        ScriptedSource source;
        source.mCells[1] = cell(1, 10);
        source.mCells[2] = cell(2, 150);
        StoreLimits limits;
        limits.mMaxResidentObjects = 100;
        NeutralWorldStore store(limits);
        ASSERT_TRUE(store.addSource(source, error));
        std::string e;
        ASSERT_NE(store.cell("test", 1, e), nullptr);
        EXPECT_EQ(store.cell("test", 2, e), nullptr);
        EXPECT_NE(e.find("more objects"), std::string::npos) << e;
        EXPECT_EQ(store.stats().mRefusedTooBig, 1u);
        EXPECT_TRUE(store.isResident("test", 1));
    }

    TEST(NeutralStore, theValidationLimitsCanBeTightened)
    {
        ScriptedSource source;
        source.mCells[1] = cell(1, 5);
        StoreLimits limits;
        limits.mCell.mMaxObjectsPerCell = 4;
        NeutralWorldStore store(limits);
        ASSERT_TRUE(store.addSource(source, error));
        std::string e;
        EXPECT_EQ(store.cell("test", 1, e), nullptr);
        EXPECT_EQ(store.stats().mRefusedByValidation, 1u);
    }

    TEST(NeutralStore, sourcesAreKeyedByGameAndDuplicatesAreRefused)
    {
        ScriptedSource a("alpha");
        ScriptedSource b("beta");
        ScriptedSource again("alpha");
        ScriptedSource unnamed("");
        NeutralWorldStore store;
        EXPECT_TRUE(store.addSource(a, error));
        EXPECT_TRUE(store.addSource(b, error));
        EXPECT_FALSE(store.addSource(again, error));
        EXPECT_NE(error.find("alpha"), std::string::npos) << error;
        EXPECT_FALSE(store.addSource(unnamed, error));
        EXPECT_EQ(store.sourceCount(), 2u);
    }

    TEST(NeutralStore, theSourceCapIsEnforced)
    {
        StoreLimits limits;
        limits.mMaxSources = 2;
        NeutralWorldStore store(limits);
        ScriptedSource a("a"), b("b"), c("c");
        EXPECT_TRUE(store.addSource(a, error));
        EXPECT_TRUE(store.addSource(b, error));
        EXPECT_FALSE(store.addSource(c, error));
    }

    TEST(NeutralStore, twoGamesKeepTheirCellsApartEvenWithTheSameCellId)
    {
        ScriptedSource a("alpha");
        ScriptedSource b("beta");
        a.mCells[1] = cell(1, 1);
        b.mCells[1] = cell(1, 2);
        NeutralWorldStore store;
        ASSERT_TRUE(store.addSource(a, error));
        ASSERT_TRUE(store.addSource(b, error));
        std::string e;
        EXPECT_EQ(store.cell("alpha", 1, e)->objects.size(), 1u);
        EXPECT_EQ(store.cell("beta", 1, e)->objects.size(), 2u);
        EXPECT_TRUE(store.isResident("alpha", 1));
        EXPECT_TRUE(store.isResident("beta", 1));
    }

    TEST(NeutralStore, removingASourceDropsItsCellsAndNeverTouchesItAgain)
    {
        ScriptedSource a("alpha");
        ScriptedSource b("beta");
        a.mCells[1] = cell(1, 1);
        b.mCells[1] = cell(1, 1);
        NeutralWorldStore store;
        ASSERT_TRUE(store.addSource(a, error));
        ASSERT_TRUE(store.addSource(b, error));
        std::string e;
        ASSERT_NE(store.cell("alpha", 1, e), nullptr);
        ASSERT_NE(store.cell("beta", 1, e), nullptr);
        store.removeSource("alpha");
        EXPECT_FALSE(store.isResident("alpha", 1));
        EXPECT_TRUE(store.isResident("beta", 1));
        EXPECT_EQ(store.stats().mResidentCells, 1u);
        const int builds = a.mBuilds;
        EXPECT_EQ(store.cell("alpha", 1, e), nullptr);
        EXPECT_EQ(a.mBuilds, builds);
        store.removeSource("alpha"); // a second removal is harmless
        EXPECT_EQ(store.sourceCount(), 1u);
    }

    TEST(NeutralStore, clearDropsEveryCellButKeepsTheSources)
    {
        ScriptedSource source;
        source.mCells[1] = cell(1, 2);
        NeutralWorldStore store;
        ASSERT_TRUE(store.addSource(source, error));
        std::string e;
        ASSERT_NE(store.cell("test", 1, e), nullptr);
        store.clear();
        EXPECT_EQ(store.stats().mResidentCells, 0u);
        EXPECT_EQ(store.stats().mResidentObjects, 0u);
        EXPECT_EQ(store.sourceCount(), 1u);
        EXPECT_NE(store.cell("test", 1, e), nullptr);
        EXPECT_EQ(source.mBuilds, 2);
    }

    TEST(NeutralStore, theSameRequestsGiveTheSameResidentSetAndStatistics)
    {
        auto run = [](std::vector<WorldId>& resident, NeutralWorld::StoreStats& stats) {
            ScriptedSource source;
            for (WorldId id = 1; id <= 8; ++id)
                source.mCells[id] = cell(id, id);
            StoreLimits limits;
            limits.mMaxResidentCells = 3;
            limits.mMaxResidentObjects = 14;
            NeutralWorldStore store(limits);
            std::string e;
            EXPECT_TRUE(store.addSource(source, e));
            for (const WorldId id : { 1u, 2u, 3u, 1u, 4u, 8u, 2u, 5u, 5u, 7u, 3u, 6u })
                (void)store.cell("test", id, e);
            for (WorldId id = 1; id <= 8; ++id)
                if (store.isResident("test", id))
                    resident.push_back(id);
            stats = store.stats();
        };
        std::vector<WorldId> r1, r2;
        NeutralWorld::StoreStats s1, s2;
        run(r1, s1);
        run(r2, s2);
        EXPECT_EQ(r1, r2);
        EXPECT_EQ(s1.mBuilds, s2.mBuilds);
        EXPECT_EQ(s1.mHits, s2.mHits);
        EXPECT_EQ(s1.mEvictions, s2.mEvictions);
        EXPECT_LE(s1.mResidentCells, 3u);
        EXPECT_LE(s1.mResidentObjects, 14u);
    }

    TEST(NeutralStore, aSourceListsItsCellsInAFixedOrder)
    {
        ScriptedSource source;
        for (const WorldId id : { 9u, 3u, 5u })
            source.mCells[id] = cell(id, 0);
        std::vector<WorldId> all;
        EXPECT_EQ(source.listCells(0, 100, all), 3u);
        EXPECT_EQ(all, (std::vector<WorldId>{ 3, 5, 9 }));
        std::vector<WorldId> page;
        EXPECT_EQ(source.listCells(1, 1, page), 1u);
        EXPECT_EQ(page, (std::vector<WorldId>{ 5 }));
    }
}
