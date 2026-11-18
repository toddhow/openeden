// Task 0286: a move that carries a destination cell (ObjectMove::hasCell) goes through the host's ordered queue like
// every other move and is validated: a destination needs a position, a "from" place must be finite. Written first and
// run red against a service that drops the new fields.
#include <components/extension/game_services.hpp>
#include <openeden/game/world_source.hpp>
#include <openeden/game/world_source_service.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using openeden::extension::ObjectOp;
    using openeden::extension::WorldSourceServiceImpl;

    class NamedSource final : public WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "skyrimse"; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<WorldId>&) const override { return 0; }
        bool buildCell(WorldId, WorldCell&, std::string& error) override
        {
            error = "none";
            return false;
        }
    };

    struct Registered
    {
        Registered()
        {
            std::string error;
            EXPECT_TRUE(mService.registerSource(mSource, error)) << error;
        }
        WorldSourceServiceImpl mService;
        NamedSource mSource;
    };

    ObjectMove toCell(WorldId cell)
    {
        ObjectMove m;
        m.hasPosition = true;
        m.position = { 1.0, 2.0, 3.0 };
        m.hasCell = true;
        m.cell = cell;
        return m;
    }

    TEST(ObjectMoveCell, aMoveIntoAnotherCellIsQueuedWithItsDestination)
    {
        Registered r;
        ObjectMove m = toCell(0x1234);
        m.hasFrom = true;
        m.from = { 9.0, 8.0, 7.0 };
        ASSERT_TRUE(r.mService.moveObject(r.mSource, 0x55, 0xA0, m));
        std::vector<ObjectOp> ops;
        ASSERT_EQ(r.mService.takeObjectOps(ops), 1u);
        EXPECT_EQ(ops[0].kind, ObjectOp::Kind::Move);
        EXPECT_EQ(ops[0].placement, 0xA0u);
        EXPECT_EQ(ops[0].cell, 0x55u);
        EXPECT_TRUE(ops[0].move.hasCell);
        EXPECT_EQ(ops[0].move.cell, 0x1234u);
        EXPECT_TRUE(ops[0].move.hasFrom);
        EXPECT_DOUBLE_EQ(ops[0].move.from.y, 8.0);
        EXPECT_DOUBLE_EQ(ops[0].move.position.z, 3.0);
    }

    TEST(ObjectMoveCell, aDestinationWithoutAPositionIsRefused)
    {
        Registered r;
        ObjectMove m;
        m.hasCell = true;
        m.cell = 5;
        m.hasRotation = true;
        m.rotation = { 0.0, 0.0, 0.0, 1.0 };
        EXPECT_FALSE(r.mService.moveObject(r.mSource, kNoWorldId, 0xA0, m));
        std::vector<ObjectOp> ops;
        EXPECT_EQ(r.mService.takeObjectOps(ops), 0u);
    }

    TEST(ObjectMoveCell, aFromPlaceThatIsNotFiniteIsRefused)
    {
        Registered r;
        ObjectMove m = toCell(5);
        m.hasFrom = true;
        m.from = { std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0 };
        EXPECT_FALSE(r.mService.moveObject(r.mSource, kNoWorldId, 0xA0, m));
    }

    TEST(ObjectMoveCell, aMoveBackToTheRecordHasNoDestination)
    {
        Registered r;
        ObjectMove m;
        m.toOriginal = true;
        m.hasCell = true;
        m.cell = 5;
        m.hasPosition = true;
        m.position = { 1.0, 2.0, 3.0 };
        EXPECT_FALSE(r.mService.moveObject(r.mSource, kNoWorldId, 0xA0, m));
    }

    TEST(ObjectMoveCell, aMoveWithAnExteriorDestinationNeedsNoCellId)
    {
        Registered r;
        ObjectMove m = toCell(kNoWorldId);
        EXPECT_TRUE(r.mService.moveObject(r.mSource, kNoWorldId, 0xA0, m));
        std::vector<ObjectOp> ops;
        ASSERT_EQ(r.mService.takeObjectOps(ops), 1u);
        EXPECT_TRUE(ops[0].move.hasCell);
        EXPECT_EQ(ops[0].move.cell, kNoWorldId);
    }
}
