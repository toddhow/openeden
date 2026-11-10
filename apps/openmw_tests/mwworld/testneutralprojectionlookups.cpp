// Task 0102 stage N2: the Morrowind source answers the lookups of world_source.hpp 1.0 (cell by name, exterior cell by
// grid, the single exterior world, units per metre) from the ESMStore, and a cell it found can be built afterwards.
#include <gtest/gtest.h>

#include <components/esm3/loadcell.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/misc/constants.hpp>

#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralprojection.hpp"
#include "apps/openmw/mwworld/store.hpp"

namespace
{
    using namespace openeden::sdk::game;

    class LookupTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            ESM::Cell interior;
            interior.blank();
            interior.mName = "Seyda Neen, Census and Excise Office";
            interior.mData.mFlags = ESM::Cell::Interior;
            interior.mId = ESM::RefId::stringRefId(interior.mName);
            ESM::Cell exterior;
            exterior.blank();
            exterior.mData.mFlags = 0;
            exterior.mData.mX = -2;
            exterior.mData.mY = -9;
            exterior.mId = ESM::RefId::esm3ExteriorCell(-2, -9);
            mStore.getWritable<ESM::Cell>().insert(interior);
            mStore.getWritable<ESM::Cell>().insert(exterior);
        }

        MWWorld::ESMStore mStore;
        NeutralWorld::IdTable mIds;
    };

    TEST_F(LookupTest, anInteriorIsFoundByItsNameWithoutCaseAndAnExteriorByItsGrid)
    {
        MWWorld::MorrowindCellSource source(
            mStore, mIds, [](const ESM::RefId&) -> MWWorld::CellStore* { return nullptr; });
        const WorldId inn = source.findCellByEditorId("seyda neen, census and excise office");
        ASSERT_NE(inn, kNoWorldId);
        const WorldId land = source.findCellByGrid(kNoWorldId, -2, -9);
        ASSERT_NE(land, kNoWorldId);
        EXPECT_NE(inn, land);
        EXPECT_EQ(source.findCellByEditorId("nowhere"), kNoWorldId);
        EXPECT_EQ(source.findCellByGrid(kNoWorldId, 0, 0), kNoWorldId);
        // A cell the lookup found has the id the listing gives it.
        std::vector<WorldId> listed;
        source.listCells(0, 10, listed);
        EXPECT_NE(std::find(listed.begin(), listed.end(), inn), listed.end());
        EXPECT_NE(std::find(listed.begin(), listed.end(), land), listed.end());
    }

    TEST_F(LookupTest, aFoundCellIsKnownToTheSourceEvenBeforeAnyListing)
    {
        MWWorld::MorrowindCellSource source(
            mStore, mIds, [](const ESM::RefId&) -> MWWorld::CellStore* { return nullptr; });
        const WorldId inn = source.findCellByEditorId("Seyda Neen, Census and Excise Office");
        ASSERT_NE(inn, kNoWorldId);
        WorldCell cell;
        std::string error;
        EXPECT_FALSE(source.buildCell(inn, cell, error));
        EXPECT_EQ(error.find("never listed"), std::string::npos) << "the lookup registered the id: " << error;
    }

    TEST_F(LookupTest, morrowindHasNoWorldspacesToNameAndOneUnit)
    {
        MWWorld::MorrowindCellSource source(
            mStore, mIds, [](const ESM::RefId&) -> MWWorld::CellStore* { return nullptr; });
        EXPECT_EQ(source.findWorldByEditorId("Tamriel"), kNoWorldId);
        EXPECT_NEAR(source.unitsPerMetre(), static_cast<double>(Constants::UnitsPerMeter), 1e-6);
    }
}
