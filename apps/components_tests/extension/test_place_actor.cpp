// Task 0250: WorldSourceService::placeActor, the additive call that creates an actor (a conjured creature, a raised
// corpse). It shares the ordered queue and the id range with placeObject, which keeps refusing actors.
#include <components/extension/game_services.hpp>
#include <openeden/game/world_source.hpp>
#include <openeden/game/world_source_service.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using openeden::extension::ObjectOp;
    using openeden::extension::WorldSourceServiceImpl;

    class ActorSource final : public WorldCellSource
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

    WorldObject wolf()
    {
        WorldObject o;
        o.base = 0x1234;
        o.kind = ObjectKind::Actor;
        o.baseEditorId = "Familiar";
        o.transform.position = { 1.0, 2.0, 3.0 };
        return o;
    }

    struct PlacedActorTest : ::testing::Test
    {
        WorldSourceServiceImpl mService;
        ActorSource mSource;
        void SetUp() override
        {
            std::string error;
            ASSERT_TRUE(mService.registerSource(mSource, error)) << error;
        }
    };

    TEST_F(PlacedActorTest, anActorGetsAnIdAtOnceAndIsQueuedAsAPlacement)
    {
        const WorldId id = mService.placeActor(mSource, 77, wolf());
        EXPECT_GE(id, kCreatedObjectBase);
        std::vector<ObjectOp> ops;
        ASSERT_EQ(mService.takeObjectOps(ops), 1u);
        EXPECT_EQ(ops[0].kind, ObjectOp::Kind::Place);
        EXPECT_EQ(ops[0].cell, 77u);
        EXPECT_EQ(ops[0].object.id, id);
        EXPECT_EQ(ops[0].object.kind, ObjectKind::Actor);
        EXPECT_EQ(ops[0].gameId, "skyrimse");
    }

    TEST_F(PlacedActorTest, anExteriorNeedsNoCellHint)
    {
        EXPECT_NE(mService.placeActor(mSource, kNoWorldId, wolf()), kNoWorldId);
    }

    TEST_F(PlacedActorTest, itSharesTheIdCounterWithPlaceObject)
    {
        const WorldId a = mService.placeActor(mSource, 5, wolf());
        WorldObject barrel = wolf();
        barrel.kind = ObjectKind::Static;
        const WorldId b = mService.placeObject(mSource, 5, barrel);
        EXPECT_EQ(b, a + 1);
    }

    TEST_F(PlacedActorTest, onlyAnActorIsAcceptedAndPlaceObjectStillRefusesOne)
    {
        WorldObject notActor = wolf();
        notActor.kind = ObjectKind::Static;
        EXPECT_EQ(mService.placeActor(mSource, 5, notActor), kNoWorldId);
        EXPECT_EQ(mService.placeObject(mSource, 5, wolf()), kNoWorldId);
        std::vector<ObjectOp> ops;
        EXPECT_EQ(mService.takeObjectOps(ops), 0u);
    }

    TEST_F(PlacedActorTest, badInputIsRefusedAndMintsNoId)
    {
        WorldObject noBase = wolf();
        noBase.base = kNoWorldId;
        EXPECT_EQ(mService.placeActor(mSource, 5, noBase), kNoWorldId);
        WorldObject nan = wolf();
        nan.transform.position.x = std::numeric_limits<double>::quiet_NaN();
        EXPECT_EQ(mService.placeActor(mSource, 5, nan), kNoWorldId);
        ActorSource other;
        EXPECT_EQ(mService.placeActor(other, 5, wolf()), kNoWorldId) << "a source that was never registered";
        EXPECT_EQ(mService.nextCreatedIndex(), 1u);
    }

    TEST(PlacedActorDefault, aServiceThatDoesNotOverrideItRefuses)
    {
        class Plain final : public WorldSourceService
        {
        public:
            bool registerSource(WorldCellSource&, std::string&) override { return true; }
            void withdrawSource(WorldCellSource&) override {}
            bool consumerEnabled() const override { return true; }
        } plain;
        ActorSource source;
        EXPECT_EQ(plain.placeActor(source, 5, wolf()), kNoWorldId);
    }
}
