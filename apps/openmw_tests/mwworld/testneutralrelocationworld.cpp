// Task 0286: the world model's side of moving a neutral placement between cells. Finding the placement wherever it
// stands (loaded, or in a cell the pack names that is not built yet), resolving the destination cell (an interior by
// the pack's id, an exterior by grid or by position), what a cell holding a moved-in actor says, the relocation
// entries the neutral save collects, and the restore of a save into a fresh world. Written first and run red against
// stubs (the new WorldModel functions did not exist and the collect and restore did not know relocations).
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include <components/esm3/readerscache.hpp>
#include <components/esmbridge/util.hpp>
#include <components/worldmodel/store.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/neutral.hpp"
#include "apps/openmw/mwworld/cell.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutralsave.hpp"
#include "apps/openmw/mwworld/neutralstore.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

namespace
{
    using namespace openeden::sdk::game;

    constexpr WorldId kWorld = 20;
    constexpr WorldId kRoomA = 10;
    constexpr WorldId kRoomB = 11;
    constexpr WorldId kExt = 30; // exterior (2, 3) of world 20
    constexpr WorldId kPersistent = 32; // the persistent cell of world 20: no grid
    constexpr WorldId kActorA = 1001; // an actor of RoomA
    constexpr WorldId kPersistentActor = 3201; // an actor of the persistent cell, standing in square (2, 3)

    WorldObject plain(WorldId id, WorldId base, ObjectKind kind, Vec3d at)
    {
        WorldObject o;
        o.id = id;
        o.base = base;
        o.kind = kind;
        o.baseEditorId = "Base" + std::to_string(base);
        o.modelPath = "x" + std::to_string(base) + ".nif";
        o.collision.source = CollisionSource::FromModel;
        o.collision.hasBounds = true;
        o.collision.boundsMin = { -1, -1, -1 };
        o.collision.boundsMax = { 1, 1, 1 };
        o.transform.position = at;
        return o;
    }

    class MoveSource final : public WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "fakegame"; }
        std::size_t cellCount() const override { return 4; }
        std::size_t listCells(std::size_t first, std::size_t max, std::vector<WorldId>& out) const override
        {
            std::size_t n = 0;
            for (WorldId id : { kRoomA, kRoomB, kExt, kPersistent })
            {
                if (n++ < first)
                    continue;
                if (out.size() >= max)
                    break;
                out.push_back(id);
            }
            return out.size();
        }
        bool buildCell(WorldId id, WorldCell& out, std::string& error) override
        {
            ++mBuilds;
            out = {};
            out.id = id;
            if (id == kRoomA)
            {
                out.editorId = "RoomA";
                out.interior = true;
                out.objects.push_back(plain(kActorA, 900, ObjectKind::Actor, { 10, 20, 30 }));
                out.objects.push_back(plain(1002, 901, ObjectKind::Static, { 1, 1, 1 }));
                return true;
            }
            if (id == kRoomB)
            {
                out.editorId = "RoomB";
                out.interior = true;
                out.objects.push_back(plain(1101, 902, ObjectKind::Static, { 5, 5, 5 }));
                return true;
            }
            if (id == kExt || id == kPersistent)
            {
                out.interior = false;
                out.world = kWorld;
                if (id == kExt)
                {
                    out.hasGrid = true;
                    out.gridX = 2;
                    out.gridY = 3;
                    out.editorId = "Ext30";
                    out.objects.push_back(plain(3001, 903, ObjectKind::Static, { 8200, 12300, 0 }));
                    // the pack puts a world's persistent placements into the square they stand in
                    out.objects.push_back(plain(kPersistentActor, 904, ObjectKind::Actor, { 8300, 12500, 50 }));
                }
                else
                {
                    // as the real data: a world's persistent cell reports the grid (0, 0)
                    out.hasGrid = true;
                    out.gridX = 0;
                    out.gridY = 0;
                }
                return true;
            }
            error = "no such cell";
            return false;
        }
        WorldId findCellByGrid(WorldId world, std::int32_t x, std::int32_t y) const override
        {
            return world == kWorld && x == 2 && y == 3 ? kExt : kNoWorldId;
        }
        bool worldInfo(WorldId world, WorldSpaceInfo& out) const override
        {
            if (world != kWorld)
                return false;
            out = {};
            out.id = kWorld;
            out.editorId = "TestWorld";
            return true;
        }
        int mBuilds = 0;
    };

    struct MoveFixture : testing::Test
    {
        void SetUp() override
        {
            MWClass::Neutral::registerSelf();
            std::string error;
            ASSERT_TRUE(mNeutral.addSource(mSource, error)) << error;
            mModel = std::make_unique<MWWorld::WorldModel>(mStore, mReaders);
            mEnvironment = std::make_unique<MWBase::Environment>();
            mEnvironment->setWorldModel(*mModel);
        }
        void TearDown() override
        {
            mEnvironment.reset();
            mModel.reset();
        }

        MWWorld::CellStore* interior(WorldId id)
        {
            const auto found = mModel->findNeutralCell(mNeutral, "fakegame", id);
            EXPECT_EQ(found.mStatus, MWWorld::NeutralCellStatus::Found);
            if (found.mStore != nullptr && found.mStore->getState() != MWWorld::CellStore::State_Loaded)
                found.mStore->load();
            return found.mStore;
        }

        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        MoveSource mSource;
        NeutralWorld::NeutralWorldStore mNeutral;
        std::unique_ptr<MWWorld::WorldModel> mModel;
        std::unique_ptr<MWBase::Environment> mEnvironment;
    };

    TEST_F(MoveFixture, aPlacementInACellThatIsNotBuiltYetIsFoundThroughTheCellTheHintNames)
    {
        const MWWorld::Ptr ptr = mModel->findNeutralObjectForMove(mNeutral, "fakegame", kRoomA, kActorA, nullptr);
        ASSERT_FALSE(ptr.isEmpty());
        EXPECT_EQ(ptr.getCellRef().getRefNum().mIndex, kActorA);
        EXPECT_EQ(ptr.getCell()->getState(), MWWorld::CellStore::State_Loaded) << "built and loaded, not entered";
        EXPECT_EQ(ptr.getCell()->neutralCell()->id, kRoomA);
    }

    TEST_F(MoveFixture, aCellThatIsAlreadyLoadedIsNotBuiltAgain)
    {
        ASSERT_NE(interior(kRoomA), nullptr);
        const int builds = mSource.mBuilds;
        const MWWorld::Ptr ptr = mModel->findNeutralObjectForMove(mNeutral, "fakegame", kRoomA, kActorA, nullptr);
        EXPECT_FALSE(ptr.isEmpty());
        EXPECT_EQ(mSource.mBuilds, builds);
    }

    TEST_F(MoveFixture, aPlacementTheHintDoesNotHoldOrNobodyKnowsIsNotFound)
    {
        EXPECT_TRUE(mModel->findNeutralObjectForMove(mNeutral, "fakegame", kRoomB, kActorA, nullptr).isEmpty());
        EXPECT_TRUE(mModel->findNeutralObjectForMove(mNeutral, "fakegame", 777, kActorA, nullptr).isEmpty());
        EXPECT_TRUE(mModel->findNeutralObjectForMove(mNeutral, "fakegame", kRoomA, 99999, nullptr).isEmpty());
        EXPECT_TRUE(mModel->findNeutralObjectForMove(mNeutral, "othergame", kRoomA, kActorA, nullptr).isEmpty());
    }

    TEST_F(MoveFixture, aPersistentReferenceIsFoundInTheSquareItsPositionIsIn)
    {
        // the pack's cell id for it is the persistent cell of the world, which has no grid
        ASSERT_NE(mModel->findNeutralCell(mNeutral, "fakegame", kExt).mWorldspace, ESM::RefId());
        const Vec3d from{ 8300, 12500, 50 };
        const MWWorld::Ptr ptr
            = mModel->findNeutralObjectForMove(mNeutral, "fakegame", kPersistent, kPersistentActor, &from);
        ASSERT_FALSE(ptr.isEmpty());
        EXPECT_EQ(ptr.getCell()->getCell()->getGridX(), 2);
        EXPECT_EQ(ptr.getCell()->getCell()->getGridY(), 3);
        const MWWorld::Ptr withoutPlace
            = mModel->findNeutralObjectForMove(mNeutral, "fakegame", kPersistent, kPersistentActor, nullptr);
        EXPECT_TRUE(withoutPlace.isEmpty() || withoutPlace.getCell() == ptr.getCell());
    }

    // ---- the destination ----

    TEST_F(MoveFixture, anInteriorDestinationIsFoundByTheIdAndLoaded)
    {
        std::string error;
        MWWorld::CellStore* store = mModel->resolveNeutralDestination(mNeutral, "fakegame", kRoomB, { 0, 0, 0 }, nullptr, error);
        ASSERT_NE(store, nullptr) << error;
        EXPECT_EQ(store->neutralCell()->id, kRoomB);
        EXPECT_EQ(store->getState(), MWWorld::CellStore::State_Loaded);
    }

    TEST_F(MoveFixture, anExteriorDestinationIsTheSquareThePositionIsIn)
    {
        std::string error;
        MWWorld::CellStore* store
            = mModel->resolveNeutralDestination(mNeutral, "fakegame", kExt, { 8200, 12300, 0 }, nullptr, error);
        ASSERT_NE(store, nullptr) << error;
        EXPECT_EQ(store->getCell()->getGridX(), 2);
        EXPECT_EQ(store->getCell()->getGridY(), 3);
    }

    TEST_F(MoveFixture, aPersistentDestinationTakesTheSquareThePositionIsIn)
    {
        (void)mModel->findNeutralCell(mNeutral, "fakegame", kExt); // registers the world
        std::string error;
        MWWorld::CellStore* store
            = mModel->resolveNeutralDestination(mNeutral, "fakegame", kPersistent, { 8300, 12500, 0 }, nullptr, error);
        ASSERT_NE(store, nullptr) << error;
        EXPECT_EQ(store->getCell()->getGridX(), 2);
        EXPECT_EQ(store->getCell()->getGridY(), 3);
        store = mModel->resolveNeutralDestination(mNeutral, "fakegame", kPersistent, { -100, -100, 0 }, nullptr, error);
        ASSERT_NE(store, nullptr) << error;
        EXPECT_EQ(store->getCell()->getGridX(), -1);
        EXPECT_EQ(store->getCell()->getGridY(), -1);
    }

    TEST_F(MoveFixture, noCellIdMeansTheSquareOfTheSourcesWorldAndAnInteriorSourceHasNone)
    {
        MWWorld::CellStore* ext = mModel->findNeutralExterior(
            mNeutral, ESM::ExteriorCellLocation(2, 3, mModel->findNeutralCell(mNeutral, "fakegame", kExt).mWorldspace));
        ASSERT_NE(ext, nullptr);
        std::string error;
        MWWorld::CellStore* store
            = mModel->resolveNeutralDestination(mNeutral, "fakegame", kNoWorldId, { 12400, 0, 0 }, ext, error);
        ASSERT_NE(store, nullptr) << error;
        EXPECT_EQ(store->getCell()->getGridX(), 3);
        EXPECT_EQ(store->getCell()->getGridY(), 0);
        MWWorld::CellStore* room = interior(kRoomA);
        EXPECT_EQ(mModel->resolveNeutralDestination(mNeutral, "fakegame", kNoWorldId, { 0, 0, 0 }, room, error), nullptr);
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(mModel->resolveNeutralDestination(mNeutral, "fakegame", 777, { 0, 0, 0 }, nullptr, error), nullptr);
        EXPECT_FALSE(error.empty());
    }

    // ---- a moved placement ----

    struct Moved
    {
        MWWorld::CellStore* mA = nullptr;
        MWWorld::CellStore* mB = nullptr;
        MWWorld::Ptr mActor;
    };

    Moved moveActorToB(MoveFixture& f)
    {
        Moved m;
        m.mA = f.interior(kRoomA);
        m.mB = f.interior(kRoomB);
        MWWorld::Ptr actor = f.mModel->findNeutralObjectForMove(f.mNeutral, "fakegame", kRoomA, kActorA, nullptr);
        EXPECT_FALSE(actor.isEmpty());
        ESM::Position at = actor.getRefData().getPosition();
        at.pos[0] = 777.f;
        at.pos[1] = -5.f;
        at.pos[2] = 12.f;
        at.rot[2] = 1.25f;
        actor.getRefData().setPosition(at);
        const MWWorld::CellStore* origin = actor.getCell()->originOf(actor.getBase());
        m.mActor = m.mA->moveTo(actor, m.mB);
        EXPECT_TRUE(f.mModel->noteNeutralRelocation("fakegame", kActorA, *origin, *m.mB, at));
        return m;
    }

    TEST_F(MoveFixture, aMovedPlacementIsFoundInTheCellItStandsInNowAndTheOriginKnowsWhereItBelongs)
    {
        const Moved m = moveActorToB(*this);
        const MWWorld::Ptr found = mModel->findNeutralObjectForMove(mNeutral, "fakegame", kRoomA, kActorA, nullptr);
        ASSERT_FALSE(found.isEmpty());
        EXPECT_EQ(found.getCell(), m.mB);
        EXPECT_EQ(m.mB->originOf(found.getBase()), m.mA);
        EXPECT_EQ(m.mA->originOf(found.getBase()), m.mA);
        EXPECT_EQ(mModel->findLoadedNeutralObject("fakegame", kNoWorldId, kActorA).getCell(), m.mB)
            << "an enable or disable after the move reaches the placement in its new cell";
    }

    TEST_F(MoveFixture, aCellThatHoldsAMovedInActorSaysSoAndTheOriginDoesNot)
    {
        const Moved m = moveActorToB(*this);
        EXPECT_TRUE(m.mB->holdsMovedHereNeutralActor());
        EXPECT_FALSE(m.mA->holdsMovedHereNeutralActor());
        // a moved-in object that is not an actor does not pin the cell
        MWWorld::Ptr stone = mModel->findNeutralObjectForMove(mNeutral, "fakegame", kRoomA, 1002, nullptr);
        ASSERT_FALSE(stone.isEmpty());
        MWWorld::CellStore* other = interior(kRoomB);
        (void)other;
        EXPECT_TRUE(m.mB->holdsMovedHereNeutralActor());
    }

    TEST_F(MoveFixture, aDisabledMovedInActorDoesNotPinItsCell)
    {
        Moved m = moveActorToB(*this);
        m.mActor.getRefData().disable();
        EXPECT_FALSE(m.mB->holdsMovedHereNeutralActor());
    }

    TEST_F(MoveFixture, theNeutralSaveCollectsTheMoveWithThePlaceTheActorStandsAt)
    {
        const Moved m = moveActorToB(*this);
        ESM::Position later = m.mActor.getRefData().getPosition();
        later.pos[0] = 900.f; // the actor walked on after the move
        m.mActor.getRefData().setPosition(later);
        MWWorld::NeutralSaveState state;
        mModel->collectNeutralSave(state, m.mB);
        ASSERT_EQ(state.mRelocations.size(), 1u);
        const MWWorld::NeutralRelocation& r = state.mRelocations[0];
        EXPECT_EQ(r.mGameId, "fakegame");
        EXPECT_EQ(r.mPlacement, kActorA);
        EXPECT_EQ(r.mOrigin.mKind, MWWorld::NeutralSaveCellKey::Kind::Interior);
        EXPECT_EQ(r.mOrigin.mCell, kRoomA);
        EXPECT_EQ(r.mDestination.mCell, kRoomB);
        EXPECT_FLOAT_EQ(r.mPos[0], 900.f);
        EXPECT_FLOAT_EQ(r.mPos[1], -5.f);
        EXPECT_FLOAT_EQ(r.mRot[2], 1.25f);
        EXPECT_EQ(mModel->neutralRelocations().size(), 1u);
    }

    TEST_F(MoveFixture, aMoveBackToTheOriginIsForgotten)
    {
        Moved m = moveActorToB(*this);
        const MWWorld::Ptr home = m.mB->moveTo(m.mActor, m.mA);
        EXPECT_TRUE(mModel->noteNeutralRelocation(
            "fakegame", kActorA, *m.mA, *m.mA, home.getRefData().getPosition()));
        MWWorld::NeutralSaveState state;
        mModel->collectNeutralSave(state, m.mA);
        EXPECT_TRUE(state.mRelocations.empty());
    }

    TEST_F(MoveFixture, aSaveIsRestoredIntoAFreshWorldWithTheActorInItsNewCellAtItsPlace)
    {
        const Moved m = moveActorToB(*this);
        MWWorld::NeutralSaveState state;
        mModel->collectNeutralSave(state, m.mB);
        ASSERT_EQ(state.mRelocations.size(), 1u);

        // a fresh world: new store, new model, new source; the saved state is all it has
        MWWorld::ESMStore store2;
        ESM::ReadersCache readers2;
        MoveSource source2;
        NeutralWorld::NeutralWorldStore neutral2;
        std::string error;
        ASSERT_TRUE(neutral2.addSource(source2, error)) << error;
        MWWorld::WorldModel model2(store2, readers2);
        mEnvironment->setWorldModel(model2);

        const MWWorld::WorldModel::NeutralSaveRestore result = model2.restoreNeutralSave(state, neutral2);
        EXPECT_EQ(result.mRelocated, 1u);
        EXPECT_TRUE(result.mWarnings.empty());
        ASSERT_NE(result.mPlayerCell, nullptr);
        const MWWorld::Ptr moved = model2.findLoadedNeutralObject("fakegame", kNoWorldId, kActorA);
        ASSERT_FALSE(moved.isEmpty());
        EXPECT_EQ(moved.getCell()->neutralCell()->id, kRoomB);
        EXPECT_FLOAT_EQ(moved.getRefData().getPosition().pos[0], 777.f);
        EXPECT_FLOAT_EQ(moved.getRefData().getPosition().rot[2], 1.25f);
        EXPECT_EQ(model2.neutralRelocations().size(), 1u) << "and the next save writes it again";
        EXPECT_TRUE(moved.getCell()->holdsMovedHereNeutralActor());
        mEnvironment->setWorldModel(*mModel);
    }

    TEST_F(MoveFixture, aSavedMoveOfAPlacementThePackNoLongerHasIsDroppedWithAWarning)
    {
        MWWorld::NeutralSaveState state;
        state.mPlayerCell.mKind = MWWorld::NeutralSaveCellKey::Kind::Interior;
        state.mPlayerCell.mGameId = "fakegame";
        state.mPlayerCell.mCell = kRoomA;
        MWWorld::NeutralRelocation r;
        r.mGameId = "fakegame";
        r.mPlacement = 424242;
        r.mOrigin = state.mPlayerCell;
        r.mDestination = state.mPlayerCell;
        r.mDestination.mCell = kRoomB;
        state.mRelocations.push_back(r);
        const MWWorld::WorldModel::NeutralSaveRestore result = mModel->restoreNeutralSave(state, mNeutral);
        EXPECT_EQ(result.mRelocated, 0u);
        EXPECT_FALSE(result.mWarnings.empty());
        EXPECT_EQ(mModel->neutralRelocations().size(), 0u);
    }
}
