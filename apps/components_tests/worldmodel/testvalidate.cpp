// Task 0100 stage N1: the core re-validates every snapshot a pack hands over (components/worldmodel/validate.*).
#include <gtest/gtest.h>

#include <components/worldmodel/validate.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace
{
    using namespace openeden::sdk::game;
    using NeutralWorld::Limits;

    WorldObject object(WorldId id)
    {
        WorldObject o;
        o.id = id;
        o.base = 7;
        o.kind = ObjectKind::Static;
        o.baseEditorId = "wall";
        o.modelPath = "architecture\\wall.nif";
        o.collision.source = CollisionSource::FromModel;
        o.transform.position = { 1.0, 2.0, 3.0 };
        o.transform.scale = 1.5;
        return o;
    }

    WorldCell cell()
    {
        WorldCell c;
        c.id = 40;
        c.editorId = "Hall";
        c.name = "The Hall";
        c.objects = { object(1), object(2), object(5) };
        return c;
    }

    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    constexpr double kInf = std::numeric_limits<double>::infinity();

    // The problem text of the first thing wrong; empty means valid.
    std::string problemOf(const WorldCell& c, const Limits& limits = {})
    {
        std::string problem;
        return NeutralWorld::validateCell(c, limits, problem) ? std::string() : problem;
    }

    TEST(NeutralValidate, aGoodCellPasses)
    {
        EXPECT_EQ(problemOf(cell()), "");
        EXPECT_EQ(problemOf(WorldCell{ .id = 1 }), "");
    }

    TEST(NeutralValidate, aCellWithNoIdIsRefused)
    {
        WorldCell c = cell();
        c.id = kNoWorldId;
        EXPECT_NE(problemOf(c), "");
    }

    TEST(NeutralValidate, theProblemNamesTheCellAndTheObjectIndex)
    {
        WorldCell c = cell();
        c.objects[1].transform.scale = 0.0;
        const std::string p = problemOf(c);
        EXPECT_NE(p.find("cell 40"), std::string::npos) << p;
        EXPECT_NE(p.find("object 1"), std::string::npos) << p;
        EXPECT_NE(p.find("scale"), std::string::npos) << p;
    }

    TEST(NeutralValidate, nonFiniteAndHugePositionsAreRefused)
    {
        for (const double bad : { kNaN, kInf, -kInf, 1.0e7 + 1.0, -2.0e9 })
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                WorldCell c = cell();
                Vec3d& p = c.objects[2].transform.position;
                (axis == 0 ? p.x : axis == 1 ? p.y : p.z) = bad;
                EXPECT_NE(problemOf(c), "") << bad << " on axis " << axis;
            }
        }
        WorldCell edge = cell();
        edge.objects[0].transform.position = { 1.0e7, -1.0e7, 0.0 };
        EXPECT_EQ(problemOf(edge), "");
    }

    TEST(NeutralValidate, scalesThatAreNotPositiveFiniteAndWithinTheLimitAreRefused)
    {
        for (const double bad : { 0.0, -1.0, kNaN, kInf, 1.0e3 + 1.0 })
        {
            WorldCell c = cell();
            c.objects[0].transform.scale = bad;
            EXPECT_NE(problemOf(c), "") << bad;
        }
        WorldCell edge = cell();
        edge.objects[0].transform.scale = 1.0e3;
        EXPECT_EQ(problemOf(edge), "");
    }

    TEST(NeutralValidate, rotationsThatAreNotFiniteUnitQuaternionsAreRefused)
    {
        WorldCell zero = cell();
        zero.objects[0].transform.rotation = { 0, 0, 0, 0 };
        EXPECT_NE(problemOf(zero), "");
        WorldCell big = cell();
        big.objects[0].transform.rotation = { 0, 0, 0, 2 };
        EXPECT_NE(problemOf(big), "");
        WorldCell nan = cell();
        nan.objects[0].transform.rotation = { kNaN, 0, 0, 1 };
        EXPECT_NE(problemOf(nan), "");
        WorldCell inf = cell();
        inf.objects[0].transform.rotation = { 0, 0, kInf, 1 };
        EXPECT_NE(problemOf(inf), "");
        WorldCell ok = cell();
        ok.objects[0].transform.rotation = { 0, 0, std::sqrt(0.5), std::sqrt(0.5) };
        EXPECT_EQ(problemOf(ok), "");
    }

    TEST(NeutralValidate, anObjectWithNoIdIsRefused)
    {
        WorldCell c = cell();
        c.objects[0].id = kNoWorldId;
        EXPECT_NE(problemOf(c), "");
    }

    TEST(NeutralValidate, objectIdsMustBeStrictlyAscending)
    {
        WorldCell descending = cell();
        std::swap(descending.objects[0], descending.objects[2]);
        EXPECT_NE(problemOf(descending), "");
        WorldCell duplicate = cell();
        duplicate.objects[1].id = duplicate.objects[0].id;
        EXPECT_NE(problemOf(duplicate), "");
    }

    TEST(NeutralValidate, enumerationsOutOfRangeAreRefused)
    {
        WorldCell kind = cell();
        kind.objects[0].kind = static_cast<ObjectKind>(200);
        EXPECT_NE(problemOf(kind), "");
        WorldCell source = cell();
        source.objects[0].collision.source = static_cast<CollisionSource>(9);
        EXPECT_NE(problemOf(source), "");
    }

    TEST(NeutralValidate, collisionBoundsMustBeFiniteOrderedAndPresentForABox)
    {
        WorldCell box = cell();
        box.objects[0].collision.source = CollisionSource::Box;
        EXPECT_NE(problemOf(box), "") << "a box without bounds";
        box.objects[0].collision.hasBounds = true;
        box.objects[0].collision.boundsMin = { -1, -1, -1 };
        box.objects[0].collision.boundsMax = { 1, 1, 1 };
        EXPECT_EQ(problemOf(box), "");
        box.objects[0].collision.boundsMax = { 1, -2, 1 };
        EXPECT_NE(problemOf(box), "") << "inverted";
        box.objects[0].collision.boundsMax = { 1, kNaN, 1 };
        EXPECT_NE(problemOf(box), "") << "NaN";
        box.objects[0].collision.boundsMax = { 1, 1, 2.0e7 };
        EXPECT_NE(problemOf(box), "") << "past the limit";
    }

    TEST(NeutralValidate, stringsWithNulBytesOrPastTheLimitAreRefused)
    {
        for (int field = 0; field < 4; ++field)
        {
            WorldCell nul = cell();
            WorldCell longer = cell();
            const std::string withNul("a\0b", 3);
            const std::string tooLong(5000, 'x');
            switch (field)
            {
                case 0:
                    nul.editorId = withNul;
                    longer.editorId = tooLong;
                    break;
                case 1:
                    nul.name = withNul;
                    longer.name = tooLong;
                    break;
                case 2:
                    nul.objects[0].modelPath = withNul;
                    longer.objects[0].modelPath = tooLong;
                    break;
                default:
                    nul.objects[0].baseEditorId = withNul;
                    longer.objects[0].baseEditorId = tooLong;
                    break;
            }
            EXPECT_NE(problemOf(nul), "") << field;
            EXPECT_NE(problemOf(longer), "") << field;
        }
        WorldCell exact = cell();
        exact.objects[0].modelPath = std::string(4096, 'x');
        EXPECT_EQ(problemOf(exact), "");
    }

    TEST(NeutralValidate, thePerCellObjectLimitIsEnforced)
    {
        Limits limits;
        limits.mMaxObjectsPerCell = 3;
        EXPECT_EQ(problemOf(cell(), limits), "");
        WorldCell c = cell();
        c.objects.push_back(object(9));
        EXPECT_NE(problemOf(c, limits), "");
    }

    TEST(NeutralValidate, gridCoordinatesPastTheLimitAreRefused)
    {
        WorldCell c = cell();
        c.gridX = (1 << 20) + 1;
        EXPECT_NE(problemOf(c), "");
        c.gridX = -(1 << 20);
        c.gridY = 1 << 20;
        EXPECT_EQ(problemOf(c), "");
    }

    TEST(NeutralValidate, componentsAreChecked)
    {
        WorldCell owner = cell();
        owner.objects[0].owner = OwnerComponent{ 3, -2 };
        EXPECT_NE(problemOf(owner), "");
        owner.objects[0].owner = OwnerComponent{ 3, -1 };
        EXPECT_EQ(problemOf(owner), "");

        WorldCell lock = cell();
        lock.objects[0].lock = LockComponent{ true, -1, 0 };
        EXPECT_NE(problemOf(lock), "");

        WorldCell teleport = cell();
        TeleportComponent t;
        t.destinationCell = 5;
        teleport.objects[0].teleport = t;
        EXPECT_EQ(problemOf(teleport), "");
        teleport.objects[0].teleport->destination.position.x = kInf;
        const std::string p = problemOf(teleport);
        EXPECT_NE(p.find("teleport"), std::string::npos) << p;
    }

    TEST(NeutralValidate, validationNeverChangesTheCell)
    {
        WorldCell c = cell();
        c.objects[1].transform.scale = -4.0;
        const WorldCell before = c;
        EXPECT_NE(problemOf(c), "");
        EXPECT_EQ(c, before);
    }

    TEST(NeutralValidate, findObjectFindsByIdAndOnlyThen)
    {
        const WorldCell c = cell();
        for (const WorldId id : { 1u, 2u, 5u })
        {
            const WorldObject* found = NeutralWorld::findObject(c, id);
            ASSERT_NE(found, nullptr) << id;
            EXPECT_EQ(found->id, id);
        }
        for (const WorldId id : { 0u, 3u, 4u, 6u, 100u })
            EXPECT_EQ(NeutralWorld::findObject(c, id), nullptr) << id;
        EXPECT_EQ(NeutralWorld::findObject(WorldCell{}, 1), nullptr);
    }
}
