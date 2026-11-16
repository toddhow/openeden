// Task 0202: WorldSourceService 1.3, moveObject, setObjectEnabledFaded and placeObject. A game pack asks the host to
// move,
// fade or create a placement of its neutral world; the host queues the operations in ONE ordered queue and mints the
// ids of created objects at once. Written first and run red against a stub that refuses everything.
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
        explicit NamedSource(std::string game)
            : mGame(std::move(game))
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

    private:
        std::string mGame;
    };

    struct Registered
    {
        Registered()
            : mSource("skyrimse")
        {
            std::string error;
            EXPECT_TRUE(mService.registerSource(mSource, error)) << error;
        }
        WorldSourceServiceImpl mService;
        NamedSource mSource;
    };

    WorldObject barrel()
    {
        WorldObject o;
        o.base = 0x1234;
        o.kind = ObjectKind::Static;
        o.baseEditorId = "Barrel02";
        o.modelPath = "clutter\\barrel02.nif";
        o.transform.position = { 10.0, 20.0, 30.0 };
        return o;
    }

    ObjectMove positionOnly(double x)
    {
        ObjectMove m;
        m.hasPosition = true;
        m.position = { x, 0.0, 0.0 };
        return m;
    }

    TEST(ObjectOpsQueue, theServiceVersionIsOnePointThree)
    {
        EXPECT_EQ(kWorldSourceServiceVersion.major, 1);
        EXPECT_EQ(kWorldSourceServiceVersion.minor, 3);
        EXPECT_EQ(kCreatedObjectBase, 0xFF800000u);
    }

    TEST(ObjectOpsQueue, aServiceThatDoesNotOverrideTheNewCallsRefusesAndTheFadedEnableForwardsToTheOldOne)
    {
        struct Plain final : WorldSourceService
        {
            bool registerSource(WorldCellSource&, std::string&) override { return true; }
            void withdrawSource(WorldCellSource&) override {}
            bool consumerEnabled() const override { return true; }
            bool setObjectEnabled(WorldCellSource&, WorldId cell, WorldId placement, bool enabled) override
            {
                ++mCalls;
                mCell = cell;
                mPlacement = placement;
                mEnabled = enabled;
                return true;
            }
            int mCalls = 0;
            WorldId mCell = 0;
            WorldId mPlacement = 0;
            bool mEnabled = false;
        } plain;
        NamedSource source("skyrimse");
        EXPECT_FALSE(plain.moveObject(source, 1, 2, positionOnly(1.0)));
        EXPECT_EQ(plain.placeObject(source, 1, barrel()), kNoWorldId);
        EXPECT_TRUE(plain.setObjectEnabledFaded(source, 7, 8, true, 2.5f));
        EXPECT_EQ(plain.mCalls, 1);
        EXPECT_EQ(plain.mCell, 7u);
        EXPECT_EQ(plain.mPlacement, 8u);
        EXPECT_TRUE(plain.mEnabled);
    }

    TEST(ObjectOpsQueue, operationsComeBackInTheOrderTheyWereAsked)
    {
        Registered r;
        const WorldId created = r.mService.placeObject(r.mSource, 5, barrel());
        ASSERT_NE(created, kNoWorldId);
        EXPECT_TRUE(r.mService.moveObject(r.mSource, 5, created, positionOnly(100.0)));
        EXPECT_TRUE(r.mService.setObjectEnabledFaded(r.mSource, 5, created, false, 1.5f));
        EXPECT_TRUE(r.mService.moveObject(r.mSource, 5, 77, positionOnly(5.0)));
        std::vector<ObjectOp> ops;
        ASSERT_EQ(r.mService.takeObjectOps(ops), 4u);
        ASSERT_EQ(ops.size(), 4u);
        EXPECT_EQ(ops[0].kind, ObjectOp::Kind::Place);
        EXPECT_EQ(ops[0].gameId, "skyrimse");
        EXPECT_EQ(ops[0].cell, 5u);
        EXPECT_EQ(ops[0].placement, created);
        EXPECT_EQ(ops[0].object.id, created) << "the prototype carries the minted id";
        EXPECT_EQ(ops[0].object.base, 0x1234u);
        EXPECT_EQ(ops[1].kind, ObjectOp::Kind::Move);
        EXPECT_EQ(ops[1].placement, created);
        EXPECT_TRUE(ops[1].move.hasPosition);
        EXPECT_EQ(ops[1].move.position.x, 100.0);
        EXPECT_EQ(ops[2].kind, ObjectOp::Kind::Enable);
        EXPECT_FALSE(ops[2].enabled);
        EXPECT_FLOAT_EQ(ops[2].fadeSeconds, 1.5f);
        EXPECT_EQ(ops[3].placement, 77u);
        EXPECT_EQ(r.mService.takeObjectOps(ops), 0u) << "taking empties the queue";
    }

    TEST(ObjectOpsQueue, idsOfCreatedObjectsAreMintedAscendingFromTheBase)
    {
        Registered r;
        EXPECT_EQ(r.mService.nextCreatedIndex(), 1u);
        EXPECT_EQ(r.mService.placeObject(r.mSource, 5, barrel()), kCreatedObjectBase + 1);
        EXPECT_EQ(r.mService.placeObject(r.mSource, 5, barrel()), kCreatedObjectBase + 2);
        EXPECT_EQ(r.mService.nextCreatedIndex(), 3u);
        r.mService.setNextCreatedIndex(100);
        EXPECT_EQ(r.mService.placeObject(r.mSource, 5, barrel()), kCreatedObjectBase + 100);
    }

    TEST(ObjectOpsQueue, theIdRangeEndsAndMintsNothingPastIt)
    {
        Registered r;
        r.mService.setNextCreatedIndex(kMaxCreatedObjects);
        EXPECT_EQ(r.mService.placeObject(r.mSource, 5, barrel()), kCreatedObjectBase + kMaxCreatedObjects);
        EXPECT_EQ(r.mService.placeObject(r.mSource, 5, barrel()), kNoWorldId);
        r.mService.setNextCreatedIndex(0);
        EXPECT_EQ(r.mService.nextCreatedIndex(), 1u) << "0 is not an index";
        r.mService.setNextCreatedIndex(0xFFFFFFFFu);
        EXPECT_EQ(r.mService.placeObject(r.mSource, 5, barrel()), kNoWorldId) << "clamped past the range";
    }

    TEST(ObjectOpsQueue, aSourceThatIsNotRegisteredGetsNothing)
    {
        Registered r;
        NamedSource stranger("other");
        EXPECT_FALSE(r.mService.moveObject(stranger, 1, 2, positionOnly(1.0)));
        EXPECT_FALSE(r.mService.setObjectEnabledFaded(stranger, 1, 2, true, 0.f));
        EXPECT_EQ(r.mService.placeObject(stranger, 1, barrel()), kNoWorldId);
        EXPECT_EQ(r.mService.nextCreatedIndex(), 1u) << "no id was minted";
        EXPECT_EQ(r.mService.pendingObjectOps(), 0u);
    }

    TEST(ObjectOpsQueue, aMoveThatMovesNothingOrIsNotFiniteIsRefused)
    {
        Registered r;
        EXPECT_FALSE(r.mService.moveObject(r.mSource, 1, 2, ObjectMove{}));
        ObjectMove nan = positionOnly(std::numeric_limits<double>::quiet_NaN());
        EXPECT_FALSE(r.mService.moveObject(r.mSource, 1, 2, nan));
        ObjectMove inf = positionOnly(std::numeric_limits<double>::infinity());
        EXPECT_FALSE(r.mService.moveObject(r.mSource, 1, 2, inf));
        ObjectMove badRotation;
        badRotation.hasRotation = true;
        badRotation.rotation = { 0.0, 0.0, 0.0, 0.0 };
        EXPECT_FALSE(r.mService.moveObject(r.mSource, 1, 2, badRotation)) << "a zero quaternion is no rotation";
        ObjectMove turn;
        turn.hasRotation = true;
        turn.rotation = { 0.0, 0.0, 0.7071067811865476, 0.7071067811865476 };
        EXPECT_TRUE(r.mService.moveObject(r.mSource, 1, 2, turn));
        EXPECT_EQ(r.mService.pendingObjectOps(), 1u);
    }

    TEST(ObjectOpsQueue, aMoveBackToTheOriginalPlaceNeedsNoNumbers)
    {
        Registered r;
        ObjectMove home;
        home.toOriginal = true;
        EXPECT_TRUE(r.mService.moveObject(r.mSource, 1, 2, home));
        std::vector<ObjectOp> ops;
        ASSERT_EQ(r.mService.takeObjectOps(ops), 1u);
        EXPECT_TRUE(ops[0].move.toOriginal);
    }

    TEST(ObjectOpsQueue, theFadeTimeIsClampedToAUsefulRange)
    {
        Registered r;
        EXPECT_TRUE(r.mService.setObjectEnabledFaded(r.mSource, 1, 2, false, -3.f));
        EXPECT_TRUE(r.mService.setObjectEnabledFaded(r.mSource, 1, 2, false, std::numeric_limits<float>::quiet_NaN()));
        EXPECT_TRUE(r.mService.setObjectEnabledFaded(r.mSource, 1, 2, false, 1e9f));
        std::vector<ObjectOp> ops;
        ASSERT_EQ(r.mService.takeObjectOps(ops), 3u);
        EXPECT_EQ(ops[0].fadeSeconds, 0.f);
        EXPECT_EQ(ops[1].fadeSeconds, 0.f);
        EXPECT_EQ(ops[2].fadeSeconds, 60.f);
    }

    TEST(ObjectOpsQueue, actorsLeveledListsAndObjectsWithNoBaseCannotBePlaced)
    {
        Registered r;
        for (const ObjectKind kind : { ObjectKind::Actor, ObjectKind::LeveledList, ObjectKind::Unknown })
        {
            WorldObject o = barrel();
            o.kind = kind;
            EXPECT_EQ(r.mService.placeObject(r.mSource, 5, o), kNoWorldId);
        }
        WorldObject noBase = barrel();
        noBase.base = kNoWorldId;
        EXPECT_EQ(r.mService.placeObject(r.mSource, 5, noBase), kNoWorldId);
        WorldObject notFinite = barrel();
        notFinite.transform.position.x = std::numeric_limits<double>::quiet_NaN();
        EXPECT_EQ(r.mService.placeObject(r.mSource, 5, notFinite), kNoWorldId);
        EXPECT_EQ(r.mService.placeObject(r.mSource, kNoWorldId, barrel()), kNoWorldId) << "no cell";
        EXPECT_EQ(r.mService.nextCreatedIndex(), 1u) << "a refused creation mints no id";
        EXPECT_EQ(r.mService.pendingObjectOps(), 0u);
    }

    TEST(ObjectOpsQueue, aFullQueueMintsNoIdAndQueuesNothingMore)
    {
        Registered r;
        for (std::size_t i = 0; i < WorldSourceServiceImpl::kMaxPendingObjectChanges; ++i)
            ASSERT_TRUE(r.mService.moveObject(r.mSource, 1, static_cast<WorldId>(i + 1), positionOnly(1.0)));
        EXPECT_FALSE(r.mService.moveObject(r.mSource, 1, 99999, positionOnly(1.0)));
        EXPECT_FALSE(r.mService.setObjectEnabledFaded(r.mSource, 1, 2, true, 0.f));
        EXPECT_EQ(r.mService.placeObject(r.mSource, 5, barrel()), kNoWorldId);
        EXPECT_EQ(r.mService.nextCreatedIndex(), 1u);
        std::vector<ObjectOp> ops;
        EXPECT_EQ(r.mService.takeObjectOps(ops), WorldSourceServiceImpl::kMaxPendingObjectChanges);
    }

    TEST(ObjectOpsQueue, withdrawingTheSourceAndClearingForgetTheQueuedOperations)
    {
        Registered r;
        ASSERT_TRUE(r.mService.moveObject(r.mSource, 1, 2, positionOnly(1.0)));
        ASSERT_NE(r.mService.placeObject(r.mSource, 5, barrel()), kNoWorldId);
        EXPECT_EQ(r.mService.pendingObjectOps(), 2u);
        r.mService.clearObjectOps();
        EXPECT_EQ(r.mService.pendingObjectOps(), 0u);
        EXPECT_EQ(r.mService.nextCreatedIndex(), 2u) << "clearing the queue keeps the counter";
        ASSERT_TRUE(r.mService.moveObject(r.mSource, 1, 2, positionOnly(1.0)));
        r.mService.withdrawSource(r.mSource);
        EXPECT_EQ(r.mService.pendingObjectOps(), 0u);
    }

    TEST(ObjectOpsQueue, theOldEnableQueueIsASeparateQueue)
    {
        Registered r;
        ASSERT_TRUE(r.mService.setObjectEnabled(r.mSource, 1, 2, false));
        ASSERT_TRUE(r.mService.moveObject(r.mSource, 1, 2, positionOnly(1.0)));
        std::vector<openeden::extension::ObjectEnableChange> changes;
        EXPECT_EQ(r.mService.takeObjectEnableChanges(changes), 1u);
        std::vector<ObjectOp> ops;
        EXPECT_EQ(r.mService.takeObjectOps(ops), 1u);
    }
}
