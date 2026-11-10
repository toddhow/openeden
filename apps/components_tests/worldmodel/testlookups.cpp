// Task 0102, stage N2: the core store forwards the 1.0 lookups of the source interface (cell by editor id, cell by
// grid, world by editor id, units per metre) and answers "none" for a game it does not know. Scripted source, no game
// data.
#include <components/worldmodel/store.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using NeutralWorld::NeutralWorldStore;

    class LookupSource final : public WorldCellSource
    {
    public:
        explicit LookupSource(std::string game, double units)
            : mGame(std::move(game))
            , mUnits(units)
        {
        }
        std::string_view gameId() const override { return mGame; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<WorldId>&) const override { return 0; }
        bool buildCell(WorldId, WorldCell&, std::string& error) override
        {
            error = "none";
            return false;
        }
        WorldId findCellByEditorId(std::string_view editorId) const override
        {
            return editorId == mGame + "Inn" ? 42 : kNoWorldId;
        }
        WorldId findCellByGrid(WorldId world, std::int32_t x, std::int32_t y) const override
        {
            return world == 7 && x == -3 && y == 9 ? 77 : kNoWorldId;
        }
        WorldId findWorldByEditorId(std::string_view editorId) const override
        {
            return editorId == "Tamriel" ? 7 : kNoWorldId;
        }
        double unitsPerMetre() const override { return mUnits; }

    private:
        std::string mGame;
        double mUnits;
    };

    TEST(NeutralStoreLookups, theStoreForwardsEveryLookupToTheRightSource)
    {
        NeutralWorldStore store;
        LookupSource a("alpha", 70.0), b("beta", 64.0);
        std::string error;
        ASSERT_TRUE(store.addSource(a, error));
        ASSERT_TRUE(store.addSource(b, error));
        EXPECT_EQ(store.findCellByEditorId("alpha", "alphaInn"), 42u);
        EXPECT_EQ(store.findCellByEditorId("beta", "alphaInn"), kNoWorldId);
        EXPECT_EQ(store.findWorldByEditorId("alpha", "Tamriel"), 7u);
        EXPECT_EQ(store.findCellByGrid("beta", 7, -3, 9), 77u);
        EXPECT_EQ(store.findCellByGrid("beta", 7, -3, 8), kNoWorldId);
        EXPECT_EQ(store.unitsPerMetre("alpha"), 70.0);
        EXPECT_EQ(store.unitsPerMetre("beta"), 64.0);
    }

    TEST(NeutralStoreLookups, anUnknownGameAnswersNoneAndZero)
    {
        NeutralWorldStore store;
        EXPECT_EQ(store.findCellByEditorId("nogame", "x"), kNoWorldId);
        EXPECT_EQ(store.findCellByGrid("nogame", kNoWorldId, 0, 0), kNoWorldId);
        EXPECT_EQ(store.findWorldByEditorId("nogame", "x"), kNoWorldId);
        EXPECT_EQ(store.unitsPerMetre("nogame"), 0.0);
    }

    TEST(NeutralStoreLookups, aRemovedSourceIsNoLongerAsked)
    {
        NeutralWorldStore store;
        LookupSource a("alpha", 70.0);
        std::string error;
        ASSERT_TRUE(store.addSource(a, error));
        store.removeSource("alpha");
        EXPECT_EQ(store.findCellByEditorId("alpha", "alphaInn"), kNoWorldId);
        EXPECT_EQ(store.unitsPerMetre("alpha"), 0.0);
    }

    // A source that throws from a lookup must not take the caller down: the store answers none.
    class ThrowingSource final : public WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "throws"; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<WorldId>&) const override { return 0; }
        bool buildCell(WorldId, WorldCell&, std::string&) override { return false; }
        WorldId findCellByEditorId(std::string_view) const override { throw std::runtime_error("boom"); }
        double unitsPerMetre() const override { throw std::runtime_error("boom"); }
    };

    TEST(NeutralStoreLookups, aThrowingLookupAnswersNone)
    {
        NeutralWorldStore store;
        ThrowingSource source;
        std::string error;
        ASSERT_TRUE(store.addSource(source, error));
        EXPECT_EQ(store.findCellByEditorId("throws", "x"), kNoWorldId);
        EXPECT_EQ(store.unitsPerMetre("throws"), 0.0);
    }
}
