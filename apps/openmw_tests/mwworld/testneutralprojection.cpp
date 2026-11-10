// Task 0100 stage N1: the read-only Morrowind projection into the neutral world model (apps/openmw/mwworld/
// neutralprojection.*). The REAL CellStore, the REAL classes (registered the way the game registers them) and a real
// ESMStore hold generated records; nothing is read from game data. Records and numbers are TEST INPUTS.
#include <gtest/gtest.h>

#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/worldmodel/store.hpp>

#include "apps/openmw/mwclass/activator.hpp"
#include "apps/openmw/mwclass/container.hpp"
#include "apps/openmw/mwclass/creature.hpp"
#include "apps/openmw/mwclass/door.hpp"
#include "apps/openmw/mwclass/light.hpp"
#include "apps/openmw/mwclass/misc.hpp"
#include "apps/openmw/mwclass/static.hpp"
#include "apps/openmw/mwworld/cell.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/neutralprojection.hpp"
#include "apps/openmw/mwworld/store.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using MWWorld::kindOfRecordType;
    using MWWorld::projectCell;

    constexpr double kPi = 3.14159265358979323846;

    // ---- an independent reference rotation: Morrowind's is R = Rx * Ry * Rz (Z first) with every angle negated
    // -------------------------------

    struct Mat3
    {
        double m[3][3];
    };

    Mat3 mul(const Mat3& a, const Mat3& b)
    {
        Mat3 r{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    r.m[i][j] += a.m[i][k] * b.m[k][j];
        return r;
    }

    Mat3 rotX(double a)
    {
        return { { { 1, 0, 0 }, { 0, std::cos(a), -std::sin(a) }, { 0, std::sin(a), std::cos(a) } } };
    }
    Mat3 rotY(double a)
    {
        return { { { std::cos(a), 0, std::sin(a) }, { 0, 1, 0 }, { -std::sin(a), 0, std::cos(a) } } };
    }
    Mat3 rotZ(double a)
    {
        return { { { std::cos(a), -std::sin(a), 0 }, { std::sin(a), std::cos(a), 0 }, { 0, 0, 1 } } };
    }

    Vec3d apply(const Mat3& r, const Vec3d& v)
    {
        return { r.m[0][0] * v.x + r.m[0][1] * v.y + r.m[0][2] * v.z,
            r.m[1][0] * v.x + r.m[1][1] * v.y + r.m[1][2] * v.z, r.m[2][0] * v.x + r.m[2][1] * v.y + r.m[2][2] * v.z };
    }

    // The active rotation of the unit quaternion q applied to v.
    Vec3d rotate(const Quatd& q, const Vec3d& v)
    {
        const Vec3d u{ q.x, q.y, q.z };
        const Vec3d uv{ u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
        const Vec3d uuv{ u.y * uv.z - u.z * uv.y, u.z * uv.x - u.x * uv.z, u.x * uv.y - u.y * uv.x };
        return { v.x + 2.0 * (q.w * uv.x + uuv.x), v.y + 2.0 * (q.w * uv.y + uuv.y), v.z + 2.0 * (q.w * uv.z + uuv.z) };
    }

    void expectNear(const Vec3d& a, const Vec3d& b, double tolerance = 1.0e-6)
    {
        EXPECT_NEAR(a.x, b.x, tolerance);
        EXPECT_NEAR(a.y, b.y, tolerance);
        EXPECT_NEAR(a.z, b.z, tolerance);
    }

    // ---- the fixture -----------------------------------------------------------------------------------------------

    class ProjectionTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            MWClass::Static::registerSelf();
            MWClass::Door::registerSelf();
            MWClass::Activator::registerSelf();
            MWClass::Container::registerSelf();
            MWClass::Light::registerSelf();
            MWClass::Miscellaneous::registerSelf();
            MWClass::Creature::registerSelf();
            makeCell(true);
        }

        static ESM::RefId rid(const std::string& id) { return ESM::RefId::stringRefId(id); }

        void makeCell(bool interior, int x = 0, int y = 0, const std::string& name = "Test Cell")
        {
            ESM::Cell record;
            record.blank();
            record.mName = name;
            if (interior)
            {
                record.mData.mFlags = ESM::Cell::Interior;
                record.mId = rid(name);
            }
            else
            {
                record.mData.mFlags = 0;
                record.mData.mX = x;
                record.mData.mY = y;
                record.mId = ESM::RefId::esm3ExteriorCell(x, y);
            }
            mCell = std::make_unique<MWWorld::CellStore>(MWWorld::Cell(record), mStore, mReaders);
            mCell->load();
        }

        const ESM::Static* staticRecord(const std::string& id, const std::string& model)
        {
            ESM::Static record;
            record.blank();
            record.mId = rid(id);
            record.mModel = model;
            return mStore.insertStatic(record);
        }

        ESM::CellRef cellRef(const ESM::RefId& base, std::uint32_t index, float x = 0.f, float y = 0.f, float z = 0.f)
        {
            ESM::CellRef ref;
            ref.blank();
            ref.mRefID = base;
            ref.mRefNum = ESM::RefNum{ .mIndex = index, .mContentFile = 0 };
            ref.mPos.pos[0] = x;
            ref.mPos.pos[1] = y;
            ref.mPos.pos[2] = z;
            return ref;
        }

        template <class T>
        MWWorld::LiveCellRefBase* place(const T* record, const ESM::CellRef& ref)
        {
            MWWorld::LiveCellRef<T> live(ref, record);
            return mCell->insert(&live);
        }

        MWWorld::LiveCellRefBase* placeStatic(const std::string& id, std::uint32_t index, float x = 0.f, float y = 0.f)
        {
            const ESM::Static* record = mStore.get<ESM::Static>().search(rid(id));
            if (record == nullptr)
                record = staticRecord(id, "f\\" + id + ".nif");
            return place(record, cellRef(record->mId, index, x, y));
        }

        bool project(WorldCell& out, std::string& error, MWWorld::ProjectionStats* stats = nullptr)
        {
            return projectCell(*mCell, mIds, out, error, stats);
        }

        WorldCell projected()
        {
            WorldCell cell;
            std::string error;
            EXPECT_TRUE(project(cell, error)) << error;
            return cell;
        }

        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        std::unique_ptr<MWWorld::CellStore> mCell;
        NeutralWorld::IdTable mIds;
    };

    // ---- kinds -----------------------------------------------------------------------------------------------------

    TEST(NeutralProjectionKinds, everyMorrowindRecordTypeMapsToTheKindTheDesignSays)
    {
        EXPECT_EQ(kindOfRecordType(ESM::REC_STAT), ObjectKind::Static);
        EXPECT_EQ(kindOfRecordType(ESM::REC_DOOR), ObjectKind::Door);
        EXPECT_EQ(kindOfRecordType(ESM::REC_ACTI), ObjectKind::Activator);
        EXPECT_EQ(kindOfRecordType(ESM::REC_CONT), ObjectKind::Container);
        EXPECT_EQ(kindOfRecordType(ESM::REC_LIGH), ObjectKind::Light);
        EXPECT_EQ(kindOfRecordType(ESM::REC_NPC_), ObjectKind::Actor);
        EXPECT_EQ(kindOfRecordType(ESM::REC_CREA), ObjectKind::Actor);
        EXPECT_EQ(kindOfRecordType(ESM::REC_LEVC), ObjectKind::LeveledList);
        EXPECT_EQ(kindOfRecordType(ESM::REC_LEVI), ObjectKind::LeveledList);
        for (const unsigned int item : { ESM::REC_ALCH, ESM::REC_APPA, ESM::REC_ARMO, ESM::REC_BOOK, ESM::REC_CLOT,
                 ESM::REC_INGR, ESM::REC_LOCK, ESM::REC_MISC, ESM::REC_PROB, ESM::REC_REPA, ESM::REC_WEAP })
            EXPECT_EQ(kindOfRecordType(item), ObjectKind::Item);
        EXPECT_EQ(kindOfRecordType(ESM::REC_BODY), ObjectKind::Other);
        EXPECT_EQ(kindOfRecordType(0), ObjectKind::Other);
        EXPECT_EQ(kindOfRecordType(0xffffffffu), ObjectKind::Other);
    }

    // ---- the cell --------------------------------------------------------------------------------------------------

    TEST_F(ProjectionTest, anInteriorCellKeepsItsNamesAndHasNoGrid)
    {
        placeStatic("wall", 1);
        const WorldCell cell = projected();
        EXPECT_NE(cell.id, kNoWorldId);
        EXPECT_EQ(cell.editorId, "Test Cell");
        EXPECT_EQ(cell.name, "Test Cell");
        EXPECT_TRUE(cell.interior);
        EXPECT_FALSE(cell.hasGrid);
        EXPECT_EQ(cell.gridX, 0);
        EXPECT_EQ(cell.gridY, 0);
        EXPECT_EQ(cell.unresolvedBases, 0u);
        EXPECT_EQ(cell.droppedObjects, 0u);
        EXPECT_EQ(cell.objects.size(), 1u);
    }

    TEST_F(ProjectionTest, anExteriorCellHasItsGrid)
    {
        makeCell(false, 3, -7, "");
        placeStatic("wall", 1);
        const WorldCell cell = projected();
        EXPECT_FALSE(cell.interior);
        EXPECT_TRUE(cell.hasGrid);
        EXPECT_EQ(cell.gridX, 3);
        EXPECT_EQ(cell.gridY, -7);
    }

    TEST_F(ProjectionTest, aCellThatIsNotLoadedIsRefusedAndTheOutputIsLeftEmpty)
    {
        ESM::Cell record;
        record.blank();
        record.mName = "Unloaded";
        record.mData.mFlags = ESM::Cell::Interior;
        record.mId = rid("Unloaded");
        MWWorld::CellStore unloaded(MWWorld::Cell(record), mStore, mReaders);
        WorldCell cell;
        cell.editorId = "stale";
        std::string error;
        EXPECT_FALSE(projectCell(unloaded, mIds, cell, error));
        EXPECT_NE(error.find("not loaded"), std::string::npos) << error;
        EXPECT_EQ(cell, WorldCell{});
    }

    // ---- what the engine lists -------------------------------------------------------------------------------------

    TEST_F(ProjectionTest, theProjectionListsExactlyWhatTheEngineListsAndCountsMatchPerType)
    {
        const ESM::Static* wall = staticRecord("wall", "f\\wall.nif");
        ESM::Door door;
        door.blank();
        door.mId = rid("door_a");
        door.mModel = "d\\door.nif";
        const ESM::Door* doorRecord = mStore.insertStatic(door);
        ESM::Miscellaneous misc;
        misc.blank();
        misc.mId = rid("cup");
        misc.mModel = "m\\cup.nif";
        const ESM::Miscellaneous* miscRecord = mStore.insertStatic(misc);

        std::uint32_t index = 1;
        for (int i = 0; i < 5; ++i)
            place(wall, cellRef(wall->mId, index++));
        for (int i = 0; i < 2; ++i)
            place(doorRecord, cellRef(doorRecord->mId, index++));
        for (int i = 0; i < 3; ++i)
            place(miscRecord, cellRef(miscRecord->mId, index++));
        // One deleted by its content file: the engine does not list it, and neither may the projection.
        MWWorld::LiveCellRefBase* deleted = place(wall, cellRef(wall->mId, index++));
        deleted->mData.setDeletedByContentFile(true);

        std::map<std::string, std::size_t> engineCounts;
        std::size_t engineTotal = 0;
        mCell->forEachConst([&](const MWWorld::ConstPtr& ptr) {
            ++engineCounts[std::string(ptr.getTypeDescription())];
            ++engineTotal;
            return true;
        });
        ASSERT_EQ(engineTotal, 10u);

        const WorldCell cell = projected();
        EXPECT_EQ(cell.objects.size(), engineTotal);
        std::size_t statics = 0, doors = 0, items = 0;
        for (const WorldObject& o : cell.objects)
        {
            statics += o.kind == ObjectKind::Static;
            doors += o.kind == ObjectKind::Door;
            items += o.kind == ObjectKind::Item;
        }
        EXPECT_EQ(statics, 5u);
        EXPECT_EQ(doors, 2u);
        EXPECT_EQ(items, 3u);
        EXPECT_EQ(cell.droppedObjects, 0u);
    }

    // ---- the fields of one object ----------------------------------------------------------------------------------

    TEST_F(ProjectionTest, aStaticKeepsItsBaseModelPositionScaleKindAndPayload)
    {
        const ESM::Static* wall = staticRecord("wall", "f\\wall.nif");
        ESM::CellRef ref = cellRef(wall->mId, 5, 100.f, 200.f, 300.f);
        ref.mScale = 1.5f;
        place(wall, ref);
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 1u);
        const WorldObject& o = cell.objects[0];
        EXPECT_EQ(o.kind, ObjectKind::Static);
        EXPECT_EQ(o.baseEditorId, "wall");
        EXPECT_NE(o.base, kNoWorldId);
        EXPECT_EQ(o.modelPath, "f\\wall.nif");
        EXPECT_EQ(o.collision.source, CollisionSource::FromModel);
        EXPECT_EQ(o.transform.position, (Vec3d{ 100.0, 200.0, 300.0 }));
        EXPECT_EQ(o.transform.scale, 1.5);
        EXPECT_EQ(o.transform.rotation, (Quatd{ 0, 0, 0, 1 }));
        EXPECT_FALSE(o.persistent);
        EXPECT_FALSE(o.initiallyDisabled);
        EXPECT_FALSE(o.owner.has_value());
        EXPECT_FALSE(o.lock.has_value());
        EXPECT_FALSE(o.teleport.has_value());
        EXPECT_EQ(o.payload.handle, 5u) << "the reference number: content file 0 in the high half, index 5 in the low";
        EXPECT_NE(o.id, kNoWorldId);
    }

    TEST_F(ProjectionTest, thePayloadCarriesTheContentFileInTheHighHalf)
    {
        const ESM::Static* wall = staticRecord("wall", "f\\wall.nif");
        ESM::CellRef ref = cellRef(wall->mId, 9);
        ref.mRefNum.mContentFile = 3;
        place(wall, ref);
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 1u);
        EXPECT_EQ(cell.objects[0].payload.handle, (std::uint64_t{ 3 } << 32) | 9u);
    }

    TEST_F(ProjectionTest, aTurnAboutZIsAClockwiseQuarterTurnSoPlusXGoesToMinusY)
    {
        const ESM::Static* wall = staticRecord("wall", "f\\wall.nif");
        ESM::CellRef ref = cellRef(wall->mId, 1);
        ref.mPos.rot[2] = static_cast<float>(kPi / 2.0);
        place(wall, ref);
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 1u);
        expectNear(rotate(cell.objects[0].transform.rotation, { 1, 0, 0 }), { 0, -1, 0 }, 1.0e-6);
        expectNear(rotate(cell.objects[0].transform.rotation, { 0, 0, 1 }), { 0, 0, 1 }, 1.0e-6);
    }

    TEST_F(ProjectionTest, combinedAnglesMatchTheIndependentReferenceAndAreUnitQuaternions)
    {
        const ESM::Static* wall = staticRecord("wall", "f\\wall.nif");
        std::vector<std::array<double, 3>> angles;
        std::uint32_t index = 1;
        for (const double x : { -2.5, 0.0, 0.7, 3.0 })
            for (const double y : { -1.2, 0.0, 0.4, 1.5 })
                for (const double z : { -3.0, 0.0, 0.9, 2.2 })
                {
                    ESM::CellRef ref = cellRef(wall->mId, index++);
                    ref.mPos.rot[0] = static_cast<float>(x);
                    ref.mPos.rot[1] = static_cast<float>(y);
                    ref.mPos.rot[2] = static_cast<float>(z);
                    angles.push_back({ static_cast<double>(ref.mPos.rot[0]), static_cast<double>(ref.mPos.rot[1]),
                        static_cast<double>(ref.mPos.rot[2]) });
                    place(wall, ref);
                }
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), angles.size());
        for (std::size_t i = 0; i < angles.size(); ++i)
        {
            // Morrowind's order (OSG's quaternion product, which Misc::VsgMath keeps): Z is applied first, then Y, then
            // X.
            const Mat3 reference = mul(rotX(-angles[i][0]), mul(rotY(-angles[i][1]), rotZ(-angles[i][2])));
            const Quatd& q = cell.objects[i].transform.rotation;
            EXPECT_NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0, 1.0e-9);
            for (const Vec3d probe : { Vec3d{ 1, 0, 0 }, Vec3d{ 0, 1, 0 }, Vec3d{ 0, 0, 1 }, Vec3d{ 0.3, -0.5, 0.8 } })
                expectNear(rotate(q, probe), apply(reference, probe), 1.0e-6);
        }
    }

    TEST_F(ProjectionTest, anActorTurnsAboutZOnlyAsTheEngineTurnsIt)
    {
        ESM::Creature creature;
        creature.blank();
        creature.mId = rid("crea_a");
        creature.mModel = "c\\crea.nif";
        const ESM::Creature* stored = mStore.insertStatic(creature);
        ESM::CellRef ref = cellRef(stored->mId, 1);
        ref.mPos.rot[0] = 0.3f;
        ref.mPos.rot[1] = 0.4f;
        ref.mPos.rot[2] = static_cast<float>(kPi / 2.0);
        place(stored, ref);
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 1u);
        EXPECT_EQ(cell.objects[0].kind, ObjectKind::Actor);
        expectNear(rotate(cell.objects[0].transform.rotation, { 1, 0, 0 }), { 0, -1, 0 }, 1.0e-6);
        expectNear(rotate(cell.objects[0].transform.rotation, { 0, 0, 1 }), { 0, 0, 1 }, 1.0e-6);
    }

    TEST_F(ProjectionTest, aHiddenMarkerHasNoModelAndNoCollisionAsTheEngineDrawsIt)
    {
        const ESM::Static* marker = staticRecord("prisonmarker", "marker.nif");
        place(marker, cellRef(marker->mId, 1));
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 1u);
        EXPECT_EQ(cell.objects[0].modelPath, "");
        EXPECT_EQ(cell.objects[0].collision.source, CollisionSource::None);
        // (RefId text keeps the casing it was first seen with.)
        std::string lowered = cell.objects[0].baseEditorId;
        for (char& c : lowered)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        EXPECT_EQ(lowered, "prisonmarker");
    }

    TEST_F(ProjectionTest, aBaseWithNoModelHasNoCollisionHint)
    {
        const ESM::Static* bare = staticRecord("bare", "");
        place(bare, cellRef(bare->mId, 1));
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 1u);
        EXPECT_EQ(cell.objects[0].modelPath, "");
        EXPECT_EQ(cell.objects[0].collision.source, CollisionSource::None);
    }

    TEST_F(ProjectionTest, aDisabledObjectStartsDisabled)
    {
        MWWorld::LiveCellRefBase* off = placeStatic("wall", 1);
        placeStatic("wall", 2);
        off->mData.disable();
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 2u);
        EXPECT_TRUE(cell.objects[0].initiallyDisabled);
        EXPECT_FALSE(cell.objects[1].initiallyDisabled);
    }

    // ---- components ------------------------------------------------------------------------------------------------

    TEST_F(ProjectionTest, anOwnerIsAnOwnerAndAFactionOwnerCarriesItsRank)
    {
        const ESM::Static* wall = staticRecord("wall", "f\\wall.nif");
        ESM::CellRef owned = cellRef(wall->mId, 1);
        owned.mOwner = rid("bob");
        ESM::CellRef factional = cellRef(wall->mId, 2);
        factional.mFaction = rid("mages guild");
        factional.mFactionRank = 3;
        ESM::CellRef both = cellRef(wall->mId, 3);
        both.mOwner = rid("bob");
        both.mFaction = rid("mages guild");
        both.mFactionRank = 2;
        ESM::CellRef freeRank = cellRef(wall->mId, 4);
        freeRank.mFaction = rid("mages guild");
        freeRank.mFactionRank = -1;
        place(wall, owned);
        place(wall, factional);
        place(wall, both);
        place(wall, freeRank);
        placeStatic("plain", 5);
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 5u);
        ASSERT_TRUE(cell.objects[0].owner.has_value());
        EXPECT_EQ(cell.objects[0].owner->requiredRank, -1);
        ASSERT_TRUE(cell.objects[1].owner.has_value());
        EXPECT_EQ(cell.objects[1].owner->requiredRank, 3);
        EXPECT_NE(cell.objects[0].owner->owner, cell.objects[1].owner->owner);
        ASSERT_TRUE(cell.objects[2].owner.has_value());
        EXPECT_EQ(cell.objects[2].owner->owner, cell.objects[0].owner->owner) << "the owner wins over the faction";
        EXPECT_EQ(cell.objects[2].owner->requiredRank, -1);
        ASSERT_TRUE(cell.objects[3].owner.has_value());
        EXPECT_EQ(cell.objects[3].owner->owner, cell.objects[1].owner->owner);
        EXPECT_EQ(cell.objects[3].owner->requiredRank, -1);
        EXPECT_FALSE(cell.objects[4].owner.has_value());
    }

    TEST_F(ProjectionTest, aLockCarriesItsLevelAndKey)
    {
        ESM::Container chest;
        chest.blank();
        chest.mId = rid("chest");
        chest.mModel = "c\\chest.nif";
        const ESM::Container* stored = mStore.insertStatic(chest);
        MWWorld::LiveCellRefBase* locked = place(stored, cellRef(stored->mId, 1));
        ESM::CellRef keyedRef = cellRef(stored->mId, 2);
        keyedRef.mKey = rid("key_a");
        MWWorld::LiveCellRefBase* withKey = place(stored, keyedRef);
        place(stored, cellRef(stored->mId, 3));
        MWWorld::Ptr(locked, mCell.get()).getCellRef().lock(40);
        MWWorld::Ptr(withKey, mCell.get()).getCellRef().lock(15);
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 3u);
        ASSERT_TRUE(cell.objects[0].lock.has_value());
        EXPECT_TRUE(cell.objects[0].lock->locked);
        EXPECT_EQ(cell.objects[0].lock->level, 40);
        ASSERT_TRUE(cell.objects[1].lock.has_value());
        EXPECT_EQ(cell.objects[1].lock->level, 15);
        EXPECT_FALSE(cell.objects[2].lock.has_value());
        EXPECT_EQ(cell.objects[0].kind, ObjectKind::Container);
        EXPECT_EQ(cell.objects[0].lock->key, kNoWorldId);
        EXPECT_NE(cell.objects[1].lock->key, kNoWorldId);
    }

    TEST_F(ProjectionTest, anUnlockedObjectKeepsItsLevelAndIsNotLocked)
    {
        ESM::Container chest;
        chest.blank();
        chest.mId = rid("chest");
        chest.mModel = "c\\chest.nif";
        const ESM::Container* stored = mStore.insertStatic(chest);
        MWWorld::LiveCellRefBase* base = place(stored, cellRef(stored->mId, 1));
        MWWorld::Ptr ptr(base, mCell.get());
        ptr.getCellRef().lock(25);
        ptr.getCellRef().unlock();
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 1u);
        ASSERT_TRUE(cell.objects[0].lock.has_value());
        EXPECT_FALSE(cell.objects[0].lock->locked);
        EXPECT_EQ(cell.objects[0].lock->level, 25);
    }

    TEST_F(ProjectionTest, aTeleportingDoorCarriesItsDestination)
    {
        ESM::Door door;
        door.blank();
        door.mId = rid("door_a");
        door.mModel = "d\\door.nif";
        const ESM::Door* stored = mStore.insertStatic(door);
        ESM::CellRef toInterior = cellRef(stored->mId, 1);
        toInterior.mTeleport = true;
        toInterior.mDestCell = "Other Cell";
        toInterior.mDoorDest.pos[0] = 5.f;
        toInterior.mDoorDest.pos[1] = 6.f;
        toInterior.mDoorDest.pos[2] = 7.f;
        toInterior.mDoorDest.rot[2] = static_cast<float>(kPi / 2.0);
        ESM::CellRef toExterior = cellRef(stored->mId, 2);
        toExterior.mTeleport = true;
        toExterior.mDoorDest.pos[0] = 4000.f;
        ESM::CellRef plain = cellRef(stored->mId, 3);
        place(stored, toInterior);
        place(stored, toExterior);
        place(stored, plain);
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 3u);
        ASSERT_TRUE(cell.objects[0].teleport.has_value());
        EXPECT_EQ(cell.objects[0].kind, ObjectKind::Door);
        EXPECT_NE(cell.objects[0].teleport->destinationCell, kNoWorldId);
        EXPECT_EQ(cell.objects[0].teleport->destinationDoor, kNoWorldId);
        EXPECT_EQ(cell.objects[0].teleport->destination.position, (Vec3d{ 5.0, 6.0, 7.0 }));
        expectNear(rotate(cell.objects[0].teleport->destination.rotation, { 1, 0, 0 }), { 0, -1, 0 }, 1.0e-6);
        EXPECT_EQ(cell.objects[0].teleport->destination.scale, 1.0);
        ASSERT_TRUE(cell.objects[1].teleport.has_value());
        // The engine names the exterior cell from the destination position (MWWorld::CellRef::getDestCell), so the
        // projection does too: the cell of (4000, 0) is grid (0, 0).
        EXPECT_NE(cell.objects[1].teleport->destinationCell, kNoWorldId);
        EXPECT_NE(cell.objects[1].teleport->destinationCell, cell.objects[0].teleport->destinationCell);
        EXPECT_EQ(cell.objects[1].teleport->destination.position.x, 4000.0);
        EXPECT_FALSE(cell.objects[2].teleport.has_value());
    }

    // ---- identity --------------------------------------------------------------------------------------------------

    TEST_F(ProjectionTest, objectsAreListedByAscendingIdAndTheIdsFollowTheReferenceNumbers)
    {
        placeStatic("wall", 30);
        placeStatic("wall", 10);
        placeStatic("wall", 20);
        const WorldCell cell = projected();
        ASSERT_EQ(cell.objects.size(), 3u);
        EXPECT_LT(cell.objects[0].id, cell.objects[1].id);
        EXPECT_LT(cell.objects[1].id, cell.objects[2].id);
        EXPECT_EQ(cell.objects[0].payload.handle, 10u);
        EXPECT_EQ(cell.objects[1].payload.handle, 20u);
        EXPECT_EQ(cell.objects[2].payload.handle, 30u);
        std::string problem;
        EXPECT_TRUE(NeutralWorld::validateCell(cell, {}, problem)) << problem;
    }

    TEST_F(ProjectionTest, projectingTwiceInOneSessionGivesTheSameCell)
    {
        placeStatic("wall", 1, 10.f, 20.f);
        placeStatic("pillar", 2);
        const WorldCell first = projected();
        const WorldCell second = projected();
        EXPECT_EQ(first, second);
    }

    TEST_F(ProjectionTest, aFreshSessionGivesTheSameCellAndTheOrderTheStoreListsReferencesInDoesNotMatter)
    {
        // The same references inserted in a different order, into a cell of another store, projected with another
        // table.
        placeStatic("wall", 1, 10.f, 20.f);
        placeStatic("pillar", 2, 1.f, 2.f);
        placeStatic("lamp", 3);
        const WorldCell reference = projected();

        MWWorld::ESMStore otherStore;
        ESM::ReadersCache otherReaders;
        ESM::Cell record;
        record.blank();
        record.mName = "Test Cell";
        record.mData.mFlags = ESM::Cell::Interior;
        record.mId = rid("Test Cell");
        MWWorld::CellStore other(MWWorld::Cell(record), otherStore, otherReaders);
        other.load();
        const std::vector<std::tuple<const char*, std::uint32_t, float, float>> order{ { "lamp", 3, 0.f, 0.f },
            { "wall", 1, 10.f, 20.f }, { "pillar", 2, 1.f, 2.f } };
        for (const auto& [name, index, x, y] : order)
        {
            ESM::Static s;
            s.blank();
            s.mId = rid(name);
            s.mModel = std::string("f\\") + name + ".nif";
            const ESM::Static* stored = otherStore.insertStatic(s);
            ESM::CellRef ref = cellRef(stored->mId, index, x, y);
            MWWorld::LiveCellRef<ESM::Static> live(ref, stored);
            other.insert(&live);
        }
        NeutralWorld::IdTable freshIds;
        WorldCell again;
        std::string error;
        ASSERT_TRUE(projectCell(other, freshIds, again, error)) << error;
        EXPECT_EQ(again, reference);
    }

    TEST_F(ProjectionTest, theSameBaseHasTheSameIdAcrossObjectsAndCells)
    {
        placeStatic("wall", 1);
        placeStatic("wall", 2);
        placeStatic("pillar", 3);
        const WorldCell a = projected();
        ASSERT_EQ(a.objects.size(), 3u);
        EXPECT_EQ(a.objects[0].base, a.objects[1].base);
        EXPECT_NE(a.objects[0].base, a.objects[2].base);

        makeCell(true, 0, 0, "Second Cell");
        placeStatic("wall", 4);
        const WorldCell b = projected();
        ASSERT_EQ(b.objects.size(), 1u);
        EXPECT_NE(b.id, a.id);
        EXPECT_EQ(b.objects[0].base, a.objects[0].base);
    }

    TEST_F(ProjectionTest, anObjectMovedBetweenCellsKeepsItsPlacementId)
    {
        placeStatic("wall", 7);
        const WorldCell first = projected();
        makeCell(true, 0, 0, "Second Cell");
        placeStatic("wall", 7);
        const WorldCell second = projected();
        ASSERT_EQ(first.objects.size(), 1u);
        ASSERT_EQ(second.objects.size(), 1u);
        EXPECT_EQ(first.objects[0].id, second.objects[0].id);
    }

    // ---- hostile or odd stores -------------------------------------------------------------------------------------

    TEST_F(ProjectionTest, anObjectWithAValueThatIsNotFiniteIsDroppedAndCounted)
    {
        const ESM::Static* wall = staticRecord("wall", "f\\wall.nif");
        ESM::CellRef bad = cellRef(wall->mId, 1);
        bad.mPos.pos[1] = std::numeric_limits<float>::quiet_NaN();
        ESM::CellRef badAngle = cellRef(wall->mId, 2);
        badAngle.mPos.rot[2] = std::numeric_limits<float>::infinity();
        place(wall, bad);
        place(wall, badAngle);
        placeStatic("wall", 3);
        const WorldCell cell = projected();
        EXPECT_EQ(cell.objects.size(), 1u);
        EXPECT_EQ(cell.droppedObjects, 2u);
        EXPECT_EQ(cell.objects[0].payload.handle, 3u);
    }

    TEST_F(ProjectionTest, anObjectWithNoReferenceNumberIsDroppedAndCounted)
    {
        const ESM::Static* wall = staticRecord("wall", "f\\wall.nif");
        ESM::CellRef none = cellRef(wall->mId, 0);
        none.mRefNum = ESM::RefNum{};
        place(wall, none);
        placeStatic("wall", 2);
        MWWorld::ProjectionStats stats;
        WorldCell cell;
        std::string error;
        ASSERT_TRUE(project(cell, error, &stats)) << error;
        EXPECT_EQ(cell.droppedObjects, 1u);
        EXPECT_EQ(stats.mNoRefNum, 1u);
        EXPECT_EQ(cell.objects.size(), 1u);
    }

    TEST_F(ProjectionTest, aSecondObjectWithTheSameReferenceNumberIsDroppedAndCounted)
    {
        placeStatic("wall", 5, 1.f, 0.f);
        placeStatic("pillar", 5, 2.f, 0.f);
        placeStatic("wall", 6);
        MWWorld::ProjectionStats stats;
        WorldCell cell;
        std::string error;
        ASSERT_TRUE(project(cell, error, &stats)) << error;
        EXPECT_EQ(cell.objects.size(), 2u);
        EXPECT_EQ(cell.droppedObjects, 1u);
        EXPECT_EQ(stats.mDuplicateRefNum, 1u);
    }

    TEST_F(ProjectionTest, aFullIdTableDropsTheObjectsItCannotNameAndStillProjectsTheCell)
    {
        placeStatic("wall", 1);
        placeStatic("wall", 2);
        NeutralWorld::IdTable tiny(1); // room for the cell's own id only
        WorldCell cell;
        std::string error;
        MWWorld::ProjectionStats stats;
        ASSERT_TRUE(projectCell(*mCell, tiny, cell, error, &stats)) << error;
        EXPECT_NE(cell.id, kNoWorldId);
        EXPECT_EQ(cell.objects.size(), 0u);
        EXPECT_EQ(cell.droppedObjects, 2u);
        EXPECT_EQ(stats.mDroppedByIds, 2u);

        NeutralWorld::IdTable none(0);
        EXPECT_FALSE(projectCell(*mCell, none, cell, error));
        EXPECT_EQ(cell, WorldCell{});
    }

    TEST_F(ProjectionTest, aLightWithNoModelHasNoCollisionHintAndNoModelFailureIsCounted)
    {
        ESM::Light light;
        light.blank();
        light.mId = rid("lamp");
        light.mModel = "";
        const ESM::Light* stored = mStore.insertStatic(light);
        place(stored, cellRef(stored->mId, 1));
        MWWorld::ProjectionStats stats;
        WorldCell cell;
        std::string error;
        ASSERT_TRUE(project(cell, error, &stats)) << error;
        EXPECT_EQ(stats.mModelFailures, 0u);
        ASSERT_EQ(cell.objects.size(), 1u);
        EXPECT_EQ(cell.objects[0].kind, ObjectKind::Light);
        EXPECT_EQ(cell.objects[0].collision.source, CollisionSource::None);
    }

    // ---- the source and the core store -----------------------------------------------------------------------------

    TEST_F(ProjectionTest, theMorrowindSourceListsTheStoresCellsAndBuildsThemThroughTheCoreStore)
    {
        // Two cells in the ESMStore; the provider hands out the fixture's cell store for the interior and a second one
        // for the other.
        ESM::Cell interior;
        interior.blank();
        interior.mName = "Test Cell";
        interior.mData.mFlags = ESM::Cell::Interior;
        interior.mId = rid("Test Cell");
        ESM::Cell exterior;
        exterior.blank();
        exterior.mData.mFlags = 0;
        exterior.mData.mX = 2;
        exterior.mData.mY = 3;
        exterior.mId = ESM::RefId::esm3ExteriorCell(2, 3);
        mStore.getWritable<ESM::Cell>().insert(interior);
        mStore.getWritable<ESM::Cell>().insert(exterior);

        placeStatic("wall", 1);
        placeStatic("pillar", 2);
        MWWorld::CellStore outside(MWWorld::Cell(exterior), mStore, mReaders);
        int provided = 0;
        MWWorld::MorrowindCellSource source(mStore, mIds, [&](const ESM::RefId& id) -> MWWorld::CellStore* {
            ++provided;
            if (id == interior.mId)
                return mCell.get();
            if (id == exterior.mId)
                return &outside;
            return nullptr;
        });
        EXPECT_EQ(source.gameId(), "morrowind");
        EXPECT_EQ(source.cellCount(), 2u);
        std::vector<WorldId> ids;
        EXPECT_EQ(source.listCells(0, 100, ids), 2u);
        ASSERT_EQ(ids.size(), 2u);
        EXPECT_NE(ids[0], ids[1]);
        std::vector<WorldId> again;
        EXPECT_EQ(source.listCells(0, 100, again), 2u);
        EXPECT_EQ(again, ids) << "listing twice gives the same ids";
        std::vector<WorldId> page;
        EXPECT_EQ(source.listCells(1, 1, page), 1u);
        EXPECT_EQ(page[0], ids[1]);
        EXPECT_EQ(provided, 0) << "listing builds nothing";

        NeutralWorld::NeutralWorldStore store;
        std::string error;
        ASSERT_TRUE(store.addSource(source, error)) << error;
        EXPECT_EQ(store.stats().mResidentCells, 0u);
        const WorldCell* kept = nullptr;
        for (const WorldId id : ids)
        {
            const WorldCell* cell = store.cell("morrowind", id, error);
            ASSERT_NE(cell, nullptr) << error;
            if (cell->interior)
                kept = cell;
        }
        // (the pointer may have been invalidated by the second request only if the cap were hit; it is not)
        ASSERT_NE(kept, nullptr);
        EXPECT_EQ(kept->objects.size(), 2u);
        EXPECT_EQ(provided, 2);

        // The store's snapshot equals a direct projection.
        WorldCell direct;
        ASSERT_TRUE(project(direct, error)) << error;
        EXPECT_EQ(*kept, direct);

        // A second request is a hit, not a second projection.
        EXPECT_NE(store.cell("morrowind", kept->id, error), nullptr);
        EXPECT_EQ(provided, 2);
    }

    TEST_F(ProjectionTest, theSourceRefusesAnIdItNeverListedAndAProviderThatHasNoCell)
    {
        MWWorld::MorrowindCellSource source(
            mStore, mIds, [](const ESM::RefId&) -> MWWorld::CellStore* { return nullptr; });
        WorldCell cell;
        cell.editorId = "stale";
        std::string error;
        EXPECT_FALSE(source.buildCell(12345, cell, error));
        EXPECT_NE(error.find("never listed"), std::string::npos) << error;
        EXPECT_EQ(cell, WorldCell{});

        ESM::Cell interior;
        interior.blank();
        interior.mName = "Test Cell";
        interior.mData.mFlags = ESM::Cell::Interior;
        interior.mId = rid("Test Cell");
        mStore.getWritable<ESM::Cell>().insert(interior);
        std::vector<WorldId> ids;
        ASSERT_EQ(source.listCells(0, 10, ids), 1u);
        EXPECT_FALSE(source.buildCell(ids[0], cell, error));
        EXPECT_NE(error.find("no cell store"), std::string::npos) << error;
    }

    TEST_F(ProjectionTest, theProjectionDoesNotChangeTheCell)
    {
        placeStatic("wall", 1);
        placeStatic("wall", 2);
        const std::size_t before = mCell->count();
        (void)projected();
        (void)projected();
        EXPECT_EQ(mCell->count(), before);
        std::size_t listed = 0;
        mCell->forEachConst([&](const MWWorld::ConstPtr&) {
            ++listed;
            return true;
        });
        EXPECT_EQ(listed, 2u);
    }
}
