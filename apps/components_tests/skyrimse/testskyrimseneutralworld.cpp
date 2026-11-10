// Decision 0016, task 0099 stage N0: the Skyrim SE adapter into the neutral world model
// (game_skyrimse::loadNeutralCell, toNeutral, eulerToQuat) over openeden/game/world_model.hpp. Generated plugins and
// hand-built placed objects only; the aggregate check on the user's own plugins is in testskyrimseneutralworldreal.cpp.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/world_model.hpp>
#include <openeden/game/world_view.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace sdkgame = openeden::sdk::game;
    namespace fs = std::filesystem;

    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTolerance = 1e-12;

    // ---- a reference rotation, built independently of the adapter: R = Rz * Ry * Rx, every angle negated
    // -------------

    using Mat3 = std::array<std::array<double, 3>, 3>;

    Mat3 mul(const Mat3& a, const Mat3& b)
    {
        Mat3 r{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    r[i][j] += a[i][k] * b[k][j];
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

    Mat3 reference(double x, double y, double z)
    {
        return mul(rotZ(-z), mul(rotY(-y), rotX(-x)));
    }

    // Rotates v by q (q v q^-1), written out so that the test does not share code with the adapter.
    sdkgame::Vec3d rotate(const sdkgame::Quatd& q, const sdkgame::Vec3d& v)
    {
        const double tx = 2 * (q.y * v.z - q.z * v.y);
        const double ty = 2 * (q.z * v.x - q.x * v.z);
        const double tz = 2 * (q.x * v.y - q.y * v.x);
        return { v.x + q.w * tx + (q.y * tz - q.z * ty), v.y + q.w * ty + (q.z * tx - q.x * tz),
            v.z + q.w * tz + (q.x * ty - q.y * tx) };
    }

    sdkgame::Vec3d apply(const Mat3& m, const sdkgame::Vec3d& v)
    {
        return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z };
    }

    void expectNear(const sdkgame::Vec3d& a, const sdkgame::Vec3d& b, double tolerance = kTolerance)
    {
        EXPECT_NEAR(a.x, b.x, tolerance);
        EXPECT_NEAR(a.y, b.y, tolerance);
        EXPECT_NEAR(a.z, b.z, tolerance);
    }

    TEST(SkyrimNeutralEuler, zeroAnglesAreTheIdentity)
    {
        const sdkgame::Quatd q = eulerToQuat({ 0, 0, 0 });
        EXPECT_EQ(q, (sdkgame::Quatd{ 0, 0, 0, 1 }));
    }

    TEST(SkyrimNeutralEuler, aPositiveAngleTurnsClockwiseAboutZ)
    {
        // The Gamebryo family turns clockwise for a positive angle (cellscene.cpp, kAngleSign): +90 degrees about Z
        // takes +X to -Y, and +Y to +X.
        const sdkgame::Quatd q = eulerToQuat({ 0, 0, static_cast<float>(kPi / 2) });
        expectNear(rotate(q, { 1, 0, 0 }), { 0, -1, 0 }, 1e-6);
        expectNear(rotate(q, { 0, 1, 0 }), { 1, 0, 0 }, 1e-6);
        expectNear(rotate(q, { 0, 0, 1 }), { 0, 0, 1 }, 1e-6);
    }

    TEST(SkyrimNeutralEuler, matchesTheReferenceRotationForCombinedAnglesAndIsAUnitQuaternion)
    {
        const float angles[] = { 0.0f, 0.5f, -1.25f, 3.0f, 6.2f, -3.1f };
        for (float x : angles)
            for (float y : angles)
                for (float z : angles)
                {
                    const sdkgame::Quatd q = eulerToQuat({ x, y, z });
                    EXPECT_NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0, 1e-12);
                    const Mat3 m = reference(x, y, z);
                    for (const sdkgame::Vec3d v : { sdkgame::Vec3d{ 1, 0, 0 }, sdkgame::Vec3d{ 0, 1, 0 },
                             sdkgame::Vec3d{ 0, 0, 1 }, sdkgame::Vec3d{ 3, -4, 5 } })
                        expectNear(rotate(q, v), apply(m, v), 1e-9);
                }
    }

    TEST(SkyrimNeutralEuler, aNonFiniteAngleGivesTheIdentity)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(eulerToQuat({ nan, 0, 0 }), (sdkgame::Quatd{}));
        EXPECT_EQ(eulerToQuat({ 0, std::numeric_limits<float>::infinity(), 0 }), (sdkgame::Quatd{}));
    }

    // ---- one placed object ----------------------------------------------------------------------------------------

    sdkgame::PlacedObject placed()
    {
        sdkgame::PlacedObject o;
        o.id = 0x00000A01;
        o.base = 0x00000801;
        o.baseKind = sdkgame::BaseKind::Static;
        o.baseEditorId = "StoneWall";
        o.modelPath = "architecture\\wall.nif";
        o.position = { 100.5f, -200.25f, 3.0f };
        o.rotation = { 0.5f, 0.25f, 0.125f };
        o.scale = 2.0f;
        o.hasBounds = true;
        o.boundsMin = { -10, -10, 0 };
        o.boundsMax = { 10, 10, 20 };
        o.persistent = true;
        o.initiallyDisabled = true;
        return o;
    }

    TEST(SkyrimNeutralObject, copiesTheFieldsWidensTheNumbersAndKeepsTheBaseAsPayload)
    {
        sdkgame::WorldObject out;
        std::string problem;
        ASSERT_TRUE(toNeutral(placed(), out, problem)) << problem;
        EXPECT_EQ(out.id, 0x00000A01u);
        EXPECT_EQ(out.base, 0x00000801u);
        EXPECT_EQ(out.kind, sdkgame::ObjectKind::Static);
        EXPECT_EQ(out.baseEditorId, "StoneWall");
        EXPECT_EQ(out.modelPath, "architecture\\wall.nif");
        EXPECT_EQ(out.transform.position, (sdkgame::Vec3d{ 100.5, -200.25, 3.0 }));
        EXPECT_EQ(out.transform.scale, 2.0);
        EXPECT_EQ(out.transform.rotation, eulerToQuat({ 0.5f, 0.25f, 0.125f }));
        EXPECT_TRUE(out.persistent);
        EXPECT_TRUE(out.initiallyDisabled);
        EXPECT_EQ(out.payload.handle, 0x00000801u);
        // Skyrim SE's store decodes no owner, lock or teleport yet: the components stay empty.
        EXPECT_FALSE(out.owner.has_value());
        EXPECT_FALSE(out.lock.has_value());
        EXPECT_FALSE(out.teleport.has_value());
    }

    TEST(SkyrimNeutralObject, collisionComesFromTheModelElseFromTheBoundsElseIsNone)
    {
        sdkgame::WorldObject out;
        std::string problem;
        sdkgame::PlacedObject withModel = placed();
        ASSERT_TRUE(toNeutral(withModel, out, problem));
        EXPECT_EQ(out.collision.source, sdkgame::CollisionSource::FromModel);
        EXPECT_TRUE(out.collision.hasBounds);
        EXPECT_EQ(out.collision.boundsMin, (sdkgame::Vec3d{ -10, -10, 0 }));
        EXPECT_EQ(out.collision.boundsMax, (sdkgame::Vec3d{ 10, 10, 20 }));

        sdkgame::PlacedObject boundsOnly = placed();
        boundsOnly.modelPath.clear();
        ASSERT_TRUE(toNeutral(boundsOnly, out, problem));
        EXPECT_EQ(out.collision.source, sdkgame::CollisionSource::Box);
        EXPECT_TRUE(out.collision.hasBounds);

        sdkgame::PlacedObject neither = placed();
        neither.modelPath.clear();
        neither.hasBounds = false;
        ASSERT_TRUE(toNeutral(neither, out, problem));
        EXPECT_EQ(out.collision.source, sdkgame::CollisionSource::None);
        EXPECT_FALSE(out.collision.hasBounds);

        sdkgame::PlacedObject modelNoBounds = placed();
        modelNoBounds.hasBounds = false;
        ASSERT_TRUE(toNeutral(modelNoBounds, out, problem));
        EXPECT_EQ(out.collision.source, sdkgame::CollisionSource::FromModel);
        EXPECT_FALSE(out.collision.hasBounds);
    }

    TEST(SkyrimNeutralObject, everyBaseKindSurvivesTheMapping)
    {
        using K = sdkgame::BaseKind;
        for (K k : { K::Unknown, K::Static, K::MovableStatic, K::Furniture, K::Door, K::Activator, K::Container,
                 K::Flora, K::Tree, K::Light, K::Item, K::Actor, K::LeveledList, K::Other })
        {
            sdkgame::PlacedObject o = placed();
            o.baseKind = k;
            sdkgame::WorldObject out;
            std::string problem;
            ASSERT_TRUE(toNeutral(o, out, problem));
            EXPECT_EQ(out.kind, k);
        }
    }

    TEST(SkyrimNeutralObject, hostileValuesAreRefusedWithAReasonAndLeaveTheOutputAlone)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        struct Case
        {
            const char* name;
            void (*change)(sdkgame::PlacedObject&, float nan, float inf);
        };
        const Case cases[] = {
            { "nan position", [](sdkgame::PlacedObject& o, float n, float) { o.position.x = n; } },
            { "inf position", [](sdkgame::PlacedObject& o, float, float i) { o.position.z = i; } },
            { "huge position", [](sdkgame::PlacedObject& o, float, float) { o.position.y = 2.0e7f; } },
            { "nan rotation", [](sdkgame::PlacedObject& o, float n, float) { o.rotation.y = n; } },
            { "inf rotation", [](sdkgame::PlacedObject& o, float, float i) { o.rotation.z = -i; } },
            { "zero scale", [](sdkgame::PlacedObject& o, float, float) { o.scale = 0.f; } },
            { "negative scale", [](sdkgame::PlacedObject& o, float, float) { o.scale = -1.f; } },
            { "nan scale", [](sdkgame::PlacedObject& o, float n, float) { o.scale = n; } },
            { "huge scale", [](sdkgame::PlacedObject& o, float, float) { o.scale = 5.0e3f; } },
            { "nan bounds", [](sdkgame::PlacedObject& o, float n, float) { o.boundsMax.x = n; } },
            { "huge bounds", [](sdkgame::PlacedObject& o, float, float) { o.boundsMin.z = -3.0e7f; } },
        };
        for (const Case& c : cases)
        {
            sdkgame::PlacedObject o = placed();
            c.change(o, nan, inf);
            sdkgame::WorldObject out;
            out.baseEditorId = "untouched";
            std::string problem;
            EXPECT_FALSE(toNeutral(o, out, problem)) << c.name;
            EXPECT_FALSE(problem.empty()) << c.name;
            EXPECT_EQ(out.baseEditorId, "untouched") << c.name;
        }
        // Bounds that are not wanted (hasBounds false) may hold anything.
        sdkgame::PlacedObject o = placed();
        o.hasBounds = false;
        o.boundsMax.x = nan;
        sdkgame::WorldObject out;
        std::string problem;
        EXPECT_TRUE(toNeutral(o, out, problem)) << problem;
    }

    // ---- a cell through a view ------------------------------------------------------------------------------------

    // A view that answers with scripted content (the adapter works over any WorldView).
    class ScriptedView final : public sdkgame::WorldView
    {
    public:
        sdkgame::WorldCellContent mContent;
        bool mFail = false;
        std::string_view gameId() const override { return "scripted"; }
        std::size_t cellCount() const override { return 1; }
        std::size_t worldCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<sdkgame::WorldCellInfo>&) const override
        {
            return 0;
        }
        std::size_t listWorlds(std::vector<sdkgame::WorldInfo>&) const override { return 0; }
        std::optional<sdkgame::WorldCellInfo> findCellByEditorId(std::string_view) const override { return {}; }
        std::optional<sdkgame::WorldCellInfo> findCellByGrid(
            sdkgame::WorldId, std::int32_t, std::int32_t) const override
        {
            return {};
        }
        std::optional<sdkgame::WorldInfo> findWorldByEditorId(std::string_view) const override { return {}; }
        bool loadCell(sdkgame::WorldId cell, sdkgame::WorldCellContent& out, std::string& error) override
        {
            if (mFail || cell != mContent.cell.id)
            {
                error = "scripted: no such cell";
                out = {};
                return false;
            }
            out = mContent;
            return true;
        }
    };

    TEST(SkyrimNeutralCell, copiesTheCellAndKeepsTheObjectOrderAndCounts)
    {
        ScriptedView view;
        view.mContent.cell.id = 0x900;
        view.mContent.cell.editorId = "TestHall";
        view.mContent.cell.name = "The Hall";
        view.mContent.cell.interior = true;
        view.mContent.cell.hasGrid = true;
        view.mContent.cell.gridX = -3;
        view.mContent.cell.gridY = 7;
        view.mContent.cell.world = 0x3C;
        view.mContent.unresolvedBases = 2;
        for (std::uint32_t id : { 5u, 9u, 12u })
        {
            sdkgame::PlacedObject o = placed();
            o.id = id;
            view.mContent.objects.push_back(o);
        }
        // A fourth, hostile, object is dropped and counted.
        sdkgame::PlacedObject bad = placed();
        bad.id = 20;
        bad.position.x = std::numeric_limits<float>::quiet_NaN();
        view.mContent.objects.push_back(bad);

        sdkgame::WorldCell cell;
        std::string error;
        ASSERT_TRUE(loadNeutralCell(view, 0x900, cell, error)) << error;
        EXPECT_EQ(cell.id, 0x900u);
        EXPECT_EQ(cell.editorId, "TestHall");
        EXPECT_EQ(cell.name, "The Hall");
        EXPECT_TRUE(cell.interior);
        EXPECT_TRUE(cell.hasGrid);
        EXPECT_EQ(cell.gridX, -3);
        EXPECT_EQ(cell.gridY, 7);
        EXPECT_EQ(cell.world, 0x3Cu);
        EXPECT_EQ(cell.unresolvedBases, 2u);
        EXPECT_EQ(cell.droppedObjects, 1u);
        ASSERT_EQ(cell.objects.size(), 3u);
        EXPECT_EQ(cell.objects[0].id, 5u);
        EXPECT_EQ(cell.objects[1].id, 9u);
        EXPECT_EQ(cell.objects[2].id, 12u);
    }

    TEST(SkyrimNeutralCell, aViewThatCannotLoadTheCellGivesAnEmptyResultAndItsMessage)
    {
        ScriptedView view;
        view.mContent.cell.id = 0x900;
        view.mFail = true;
        sdkgame::WorldCell cell;
        cell.editorId = "stale";
        cell.objects.push_back({});
        std::string error;
        EXPECT_FALSE(loadNeutralCell(view, 0x900, cell, error));
        EXPECT_NE(error.find("no such cell"), std::string::npos) << error;
        EXPECT_EQ(cell, sdkgame::WorldCell{});

        EXPECT_FALSE(loadNeutralCell(view, sdkgame::kNoWorldId, cell, error));
        EXPECT_FALSE(error.empty());
    }

    // ---- over the real Skyrim store with generated plugins --------------------------------------------------------

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6,
                            kGrupPersistent = 8, kGrupTemporary = 9;
    constexpr std::uint32_t kStatA = 0x00000801, kFurn = 0x00000802, kCellInterior = 0x00000900, kRefA = 0x00000A01,
                            kRefB = 0x00000A02, kRefMissing = 0x00000A03;

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    Bytes refr(std::uint32_t id, std::uint32_t base, std::uint32_t flags, float x, float y, float scale = -1.0f)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(base)));
        if (scale > 0)
            subs.append(sub4("XSCL", Bytes().f32(scale)));
        subs.append(sub4("DATA", Bytes().f32(x).f32(y).f32(3).f32(0.5f).f32(0.25f).f32(0.125f)));
        return record4("REFR", flags, id, subs);
    }

    Bytes modelRecord(const char (&type)[5], std::uint32_t id, const std::string& editorId, const std::string& model)
    {
        Bytes subs;
        subs.append(edid(editorId));
        subs.append(sub4("OBND", Bytes().u16(0xFFF6).u16(0xFFF6).u16(0).u16(10).u16(10).u16(20)));
        subs.append(sub4("MODL", Bytes().zstring(model)));
        return record4(type, 0, id, subs);
    }

    Bytes topGroup(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), kGrupTop, records);
    }

    Bytes baseEsm()
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "A", "B", 1, {}, 24, 44);
        bytes.append(topGroup("STAT", modelRecord("STAT", kStatA, "StoneWall", "architecture\\wall.nif")));
        bytes.append(topGroup("FURN", modelRecord("FURN", kFurn, "WoodChair", "furniture\\chair.nif")));
        Bytes cellSubs;
        cellSubs.append(edid("TestHall"));
        cellSubs.append(sub4("FULL", Bytes().zstring("The Hall")));
        cellSubs.append(sub4("DATA", Bytes().u16(CellFlags::Interior)));
        Bytes cellRec = record4("CELL", 0, kCellInterior, cellSubs);
        Bytes children;
        children.append(group4(kCellInterior, kGrupPersistent, refr(kRefA, kStatA, 0x400, 100, 200)));
        children.append(group4(kCellInterior, kGrupTemporary,
            Bytes().append(refr(kRefB, kFurn, 0, 5, 6, 2.0f)).append(refr(kRefMissing, 0x00000FFF, 0, 1, 1))));
        Bytes sub = cellRec;
        sub.append(group4(kCellInterior, kGrupCellChildren, children));
        bytes.append(topGroup("CELL",
            group4(kCellInterior % 10, kGrupInteriorBlock, group4((kCellInterior / 10) % 10, kGrupInteriorSub, sub))));
        return bytes;
    }

    struct TempPlugin
    {
        fs::path mDir;
        fs::path mFile;
        TempPlugin()
        {
            mDir = fs::temp_directory_path()
                / ("oe_skyrim_neutral_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mDir);
            mFile = mDir / "base.esm";
            std::ofstream out(mFile, std::ios::binary);
            const std::string s = baseEsm().str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
        }
        ~TempPlugin()
        {
            std::error_code ec;
            fs::remove_all(mDir, ec);
        }
    };

    TEST(SkyrimNeutralCell, aRealStoreCellEqualsTheContractsObjectsMappedOneByOne)
    {
        TempPlugin plugin;
        SkyrimWorldView view;
        std::vector<std::string> problems;
        view.load({ plugin.mFile }, problems);
        ASSERT_TRUE(problems.empty());
        const auto info = view.findCellByEditorId("TestHall");
        ASSERT_TRUE(info.has_value());

        sdkgame::WorldCellContent content;
        std::string error;
        ASSERT_TRUE(view.loadCell(info->id, content, error)) << error;

        sdkgame::WorldCell cell;
        ASSERT_TRUE(loadNeutralCell(view, info->id, cell, error)) << error;
        EXPECT_EQ(cell.id, info->id);
        EXPECT_EQ(cell.editorId, "TestHall");
        EXPECT_EQ(cell.name, "The Hall");
        EXPECT_TRUE(cell.interior);
        EXPECT_EQ(cell.unresolvedBases, 1u);
        EXPECT_EQ(cell.droppedObjects, 0u);
        ASSERT_EQ(cell.objects.size(), content.objects.size());
        ASSERT_EQ(cell.objects.size(), 3u);
        for (std::size_t i = 0; i < cell.objects.size(); ++i)
        {
            sdkgame::WorldObject expected;
            std::string problem;
            ASSERT_TRUE(toNeutral(content.objects[i], expected, problem)) << problem;
            EXPECT_EQ(cell.objects[i], expected) << i;
            if (i > 0)
                EXPECT_LT(cell.objects[i - 1].id, cell.objects[i].id);
        }
        // The furniture reference was scaled by 2 and the missing base keeps kind Unknown with its id as payload.
        EXPECT_EQ(cell.objects[1].kind, sdkgame::ObjectKind::Furniture);
        EXPECT_EQ(cell.objects[1].transform.scale, 2.0);
        EXPECT_EQ(cell.objects[2].kind, sdkgame::ObjectKind::Unknown);
        EXPECT_EQ(cell.objects[2].payload.handle, 0x00000FFFu);
    }

    TEST(SkyrimNeutralCell, twoLoadsAndTwoFreshViewsGiveEqualCells)
    {
        TempPlugin plugin;
        sdkgame::WorldCell first, second, third;
        std::string error;
        {
            SkyrimWorldView view;
            std::vector<std::string> problems;
            view.load({ plugin.mFile }, problems);
            const auto info = view.findCellByEditorId("TestHall");
            ASSERT_TRUE(info.has_value());
            ASSERT_TRUE(loadNeutralCell(view, info->id, first, error)) << error;
            ASSERT_TRUE(loadNeutralCell(view, info->id, second, error)) << error;
        }
        {
            SkyrimWorldView view;
            std::vector<std::string> problems;
            view.load({ plugin.mFile }, problems);
            const auto info = view.findCellByEditorId("TestHall");
            ASSERT_TRUE(info.has_value());
            ASSERT_TRUE(loadNeutralCell(view, info->id, third, error)) << error;
        }
        EXPECT_EQ(first, second);
        EXPECT_EQ(first, third);
    }

    TEST(SkyrimNeutralCell, anUnknownCellIdFailsWithAMessage)
    {
        TempPlugin plugin;
        SkyrimWorldView view;
        std::vector<std::string> problems;
        view.load({ plugin.mFile }, problems);
        sdkgame::WorldCell cell;
        std::string error;
        EXPECT_FALSE(loadNeutralCell(view, 0x00DEAD00, cell, error));
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(cell, sdkgame::WorldCell{});
    }
}
