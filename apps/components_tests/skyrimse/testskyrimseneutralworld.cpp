// Decision 0016, task 0099 stage N0: the Skyrim SE adapter into the neutral world model
// (game_skyrimse::loadNeutralCell, toNeutral, eulerToQuat) over openeden/game/world_model.hpp. Generated plugins and
// hand-built placed objects only; the aggregate check on the user's own plugins is in testskyrimseneutralworldreal.cpp.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <components/worldmodel/store.hpp>

#include <openeden/game/world_model.hpp>
#include <openeden/game/world_view.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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
        // Without a component source (toNeutral called with no PlacementComponents) the components stay empty. Edited
        // in task 0099 N4: this used to say the store decodes no owner, lock or teleport yet; it does now and the store
        // path is tested in SkyrimNeutralDoors.
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

    // ---- Task 0100 stage N1: the Skyrim side of the neutral world store ----

    TEST(SkyrimCellSource, listsTheViewsCellsAndBuildsWhatLoadNeutralCellBuilds)
    {
        TempPlugin plugin;
        SkyrimWorldView view;
        std::vector<std::string> problems;
        view.load({ plugin.mFile }, problems);
        ASSERT_TRUE(problems.empty());
        SkyrimCellSource source(view);
        EXPECT_EQ(source.gameId(), "skyrimse");
        EXPECT_EQ(source.cellCount(), view.cellCount());

        std::vector<sdkgame::WorldId> ids;
        EXPECT_EQ(source.listCells(0, 100, ids), source.cellCount());
        const auto info = view.findCellByEditorId("TestHall");
        ASSERT_TRUE(info.has_value());
        ASSERT_EQ(ids.size(), 1u);
        EXPECT_EQ(ids[0], info->id);

        sdkgame::WorldCell viaSource;
        sdkgame::WorldCell direct;
        std::string error;
        ASSERT_TRUE(source.buildCell(info->id, viaSource, error)) << error;
        ASSERT_TRUE(loadNeutralCell(view, info->id, direct, error)) << error;
        EXPECT_EQ(viaSource, direct);
        EXPECT_FALSE(source.buildCell(0x00DEAD00, viaSource, error));
        EXPECT_EQ(viaSource, sdkgame::WorldCell{});
    }

    TEST(SkyrimCellSource, theCoreStoreBuildsTheCellLazilyAndKeepsAnEqualSnapshot)
    {
        TempPlugin plugin;
        SkyrimWorldView view;
        std::vector<std::string> problems;
        view.load({ plugin.mFile }, problems);
        SkyrimCellSource source(view);
        NeutralWorld::NeutralWorldStore store;
        std::string error;
        ASSERT_TRUE(store.addSource(source, error)) << error;
        EXPECT_EQ(store.stats().mResidentCells, 0u) << "nothing is built before a request";

        const auto info = view.findCellByEditorId("TestHall");
        ASSERT_TRUE(info.has_value());
        const sdkgame::WorldCell* kept = store.cell("skyrimse", info->id, error);
        ASSERT_NE(kept, nullptr) << error;
        sdkgame::WorldCell direct;
        ASSERT_TRUE(loadNeutralCell(view, info->id, direct, error)) << error;
        EXPECT_EQ(*kept, direct);
        EXPECT_EQ(store.stats().mBuilds, 1u);
        EXPECT_EQ(kept->objects.size(), 3u);
        const sdkgame::WorldObject* second = NeutralWorld::findObject(*kept, kept->objects[1].id);
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(second->kind, sdkgame::ObjectKind::Furniture);
        EXPECT_EQ(store.cell("skyrimse", info->id, error), kept);
        EXPECT_EQ(store.stats().mHits, 1u);
    }

    // ---- owner, lock and teleport (task 0099 stage N4, the record-decoding half) -----------------------------------

    constexpr FormId kOwnerNpc = 0x00013BB8, kKeyForm = 0x0001D4A1, kDoorB = 0x00032BDB, kCellOther = 0x00000931;

    PlacedRef doorRef()
    {
        PlacedRef r;
        r.mCommon.mId = 0x00000B01;
        r.mBase = 0x00000801;
        r.mOwner = RefOwner{ kOwnerNpc, 6 };
        r.mLock = RefLock{ 75, kKeyForm, 4 };
        r.mTeleport = RefTeleport{ kDoorB, { 11.5f, -22.25f, 33.75f }, { 0.5f, 0.25f, 0.125f }, 1 };
        return r;
    }

    TEST(SkyrimNeutralComponents, anOwnerWithARankAndWithoutOne)
    {
        ComponentStats stats;
        PlacedRef r = doorRef();
        PlacementComponents c = makeComponents(r, {}, stats, NeutralLimits{});
        ASSERT_TRUE(c.owner.has_value());
        EXPECT_EQ(c.owner->owner, kOwnerNpc);
        EXPECT_EQ(c.owner->requiredRank, 6);
        r.mOwner->mRank.reset();
        c = makeComponents(r, {}, stats, NeutralLimits{});
        ASSERT_TRUE(c.owner.has_value());
        EXPECT_EQ(c.owner->requiredRank, -1);
        EXPECT_EQ(stats.mOwnersDropped, 0u);
    }

    TEST(SkyrimNeutralComponents, anOwnerOutsideTheLoadOrderGivesNoOwnerAndIsCounted)
    {
        ComponentStats stats;
        PlacedRef r = doorRef();
        r.mOwner->mOwner = kUnresolvedFormId;
        const PlacementComponents c = makeComponents(r, {}, stats, NeutralLimits{});
        EXPECT_FALSE(c.owner.has_value());
        EXPECT_EQ(stats.mOwnersDropped, 1u);
        EXPECT_TRUE(c.lock.has_value()); // the rest of the placement is unaffected
        PlacedRef zero = doorRef();
        zero.mOwner->mOwner = kNullFormId;
        EXPECT_FALSE(makeComponents(zero, {}, stats, NeutralLimits{}).owner.has_value());
    }

    TEST(SkyrimNeutralComponents, aLockHasLevelKeyAndIsLocked)
    {
        ComponentStats stats;
        PlacedRef r = doorRef();
        PlacementComponents c = makeComponents(r, {}, stats, NeutralLimits{});
        ASSERT_TRUE(c.lock.has_value());
        EXPECT_TRUE(c.lock->locked);
        EXPECT_EQ(c.lock->level, 75);
        EXPECT_EQ(c.lock->key, kKeyForm);
        r.mLock->mKey = kNullFormId;
        r.mLock->mLevel = 255;
        c = makeComponents(r, {}, stats, NeutralLimits{});
        EXPECT_EQ(c.lock->level, 255);
        EXPECT_EQ(c.lock->key, sdkgame::kNoWorldId);
        EXPECT_EQ(stats.mKeysUnresolved, 0u);
    }

    TEST(SkyrimNeutralComponents, aKeyOutsideTheLoadOrderIsDroppedButTheLockStays)
    {
        ComponentStats stats;
        PlacedRef r = doorRef();
        r.mLock->mKey = kUnresolvedFormId;
        const PlacementComponents c = makeComponents(r, {}, stats, NeutralLimits{});
        ASSERT_TRUE(c.lock.has_value());
        EXPECT_EQ(c.lock->key, sdkgame::kNoWorldId);
        EXPECT_EQ(c.lock->level, 75);
        EXPECT_EQ(stats.mKeysUnresolved, 1u);
    }

    TEST(SkyrimNeutralComponents, aTeleportCarriesDoorCellTransformAndFlags)
    {
        ComponentStats stats;
        const PlacedRef r = doorRef();
        const RefCellIndex cells{ { kDoorB, kCellOther } };
        const PlacementComponents c = makeComponents(r, cells, stats, NeutralLimits{});
        ASSERT_TRUE(c.teleport.has_value());
        EXPECT_EQ(c.teleport->destinationDoor, kDoorB);
        EXPECT_EQ(c.teleport->destinationCell, kCellOther);
        EXPECT_EQ(c.teleport->destination.position, (sdkgame::Vec3d{ 11.5, -22.25, 33.75 }));
        EXPECT_EQ(c.teleport->destination.rotation, eulerToQuat({ 0.5f, 0.25f, 0.125f }));
        EXPECT_EQ(c.teleport->destination.scale, 1.0);
        EXPECT_EQ(c.teleport->flags, 1u);
        EXPECT_EQ(stats.mTeleportCellsUnknown, 0u);
        EXPECT_EQ(stats.mTeleportsDropped, 0u);
    }

    TEST(SkyrimNeutralComponents, aDoorInNoKnownCellKeepsTheTeleportWithNoCellAndCountsIt)
    {
        ComponentStats stats;
        const PlacementComponents c = makeComponents(doorRef(), {}, stats, NeutralLimits{});
        ASSERT_TRUE(c.teleport.has_value());
        EXPECT_EQ(c.teleport->destinationDoor, kDoorB);
        EXPECT_EQ(c.teleport->destinationCell, sdkgame::kNoWorldId);
        EXPECT_EQ(stats.mTeleportCellsUnknown, 1u);
    }

    TEST(SkyrimNeutralComponents, aTeleportWithoutAUsableDoorIsDropped)
    {
        ComponentStats stats;
        PlacedRef r = doorRef();
        r.mTeleport->mDoor = kUnresolvedFormId;
        EXPECT_FALSE(makeComponents(r, {}, stats, NeutralLimits{}).teleport.has_value());
        r.mTeleport->mDoor = kNullFormId;
        EXPECT_FALSE(makeComponents(r, {}, stats, NeutralLimits{}).teleport.has_value());
        EXPECT_EQ(stats.mTeleportsDropped, 2u);
    }

    TEST(SkyrimNeutralComponents, hostileTeleportNumbersDropTheTeleportNotTheRest)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        struct Case
        {
            Vec3 position;
            Vec3 rotation;
        };
        const Case cases[]
            = { { { nan, 0, 0 }, { 0, 0, 0 } }, { { 0, inf, 0 }, { 0, 0, 0 } }, { { 0, 0, 1e8f }, { 0, 0, 0 } },
                  { { 0, 0, -1e8f }, { 0, 0, 0 } }, { { 0, 0, 0 }, { nan, 0, 0 } }, { { 0, 0, 0 }, { 0, -inf, 0 } } };
        ComponentStats stats;
        for (const Case& c : cases)
        {
            PlacedRef r = doorRef();
            r.mTeleport->mPosition = c.position;
            r.mTeleport->mRotation = c.rotation;
            const PlacementComponents out = makeComponents(r, { { kDoorB, kCellOther } }, stats, NeutralLimits{});
            EXPECT_FALSE(out.teleport.has_value());
            EXPECT_TRUE(out.owner.has_value());
            EXPECT_TRUE(out.lock.has_value());
        }
        EXPECT_EQ(stats.mTeleportsDropped, std::size(cases));
    }

    TEST(SkyrimNeutralComponents, aReferenceWithNothingHasNoComponents)
    {
        ComponentStats stats;
        PlacedRef r;
        r.mBase = 0x801;
        EXPECT_TRUE(makeComponents(r, {}, stats, NeutralLimits{}).empty());
    }

    TEST(SkyrimNeutralObject, componentsFromASourceAreCopiedAndWithoutOneTheObjectHasNone)
    {
        ComponentStats stats;
        const PlacementComponents c = makeComponents(doorRef(), { { kDoorB, kCellOther } }, stats, NeutralLimits{});
        sdkgame::WorldObject out;
        std::string problem;
        ASSERT_TRUE(toNeutral(placed(), out, problem, NeutralLimits{}, &c)) << problem;
        EXPECT_EQ(out.owner, c.owner);
        EXPECT_EQ(out.lock, c.lock);
        EXPECT_EQ(out.teleport, c.teleport);
        EXPECT_TRUE(out.owner && out.lock && out.teleport);
        sdkgame::WorldObject plain;
        ASSERT_TRUE(toNeutral(placed(), plain, problem)) << problem;
        EXPECT_FALSE(plain.owner || plain.lock || plain.teleport);
    }

    class ScriptedSource final : public ComponentSource
    {
    public:
        mutable std::vector<std::pair<sdkgame::WorldId, sdkgame::WorldId>> mAsked;
        PlacementComponents mFor0x20;
        [[nodiscard]] const PlacementComponents* find(sdkgame::WorldId cell, sdkgame::WorldId placement) const override
        {
            mAsked.emplace_back(cell, placement);
            return placement == 0x20 ? &mFor0x20 : nullptr;
        }
    };

    void fillScripted(ScriptedView& view)
    {
        view.mContent.cell.id = 0x77;
        for (const sdkgame::WorldId id : { 0x10u, 0x20u })
        {
            sdkgame::PlacedObject o = placed();
            o.id = id;
            view.mContent.objects.push_back(o);
        }
    }

    TEST(SkyrimNeutralCell, anExplicitSourceIsAskedForEachPlacementOfTheCellJustLoaded)
    {
        ScriptedView view;
        fillScripted(view);
        ScriptedSource source;
        ComponentStats stats;
        source.mFor0x20 = makeComponents(doorRef(), {}, stats, NeutralLimits{});
        sdkgame::WorldCell cell;
        std::string error;
        ASSERT_TRUE(loadNeutralCell(view, 0x77, cell, error, NeutralLimits{}, &source)) << error;
        ASSERT_EQ(cell.objects.size(), 2u);
        EXPECT_FALSE(cell.objects[0].owner || cell.objects[0].lock || cell.objects[0].teleport);
        EXPECT_TRUE(cell.objects[1].owner && cell.objects[1].lock && cell.objects[1].teleport);
        ASSERT_EQ(source.mAsked.size(), 2u);
        EXPECT_EQ(source.mAsked[0], (std::pair<sdkgame::WorldId, sdkgame::WorldId>{ 0x77, 0x10 }));
        EXPECT_EQ(source.mAsked[1], (std::pair<sdkgame::WorldId, sdkgame::WorldId>{ 0x77, 0x20 }));
    }

    TEST(SkyrimNeutralCell, aViewThatIsNoSourceGivesEmptyComponents)
    {
        ScriptedView view;
        fillScripted(view);
        sdkgame::WorldCell cell;
        std::string error;
        ASSERT_TRUE(loadNeutralCell(view, 0x77, cell, error)) << error;
        ASSERT_EQ(cell.objects.size(), 2u);
        for (const sdkgame::WorldObject& o : cell.objects)
            EXPECT_FALSE(o.owner || o.lock || o.teleport);
    }

    // ---- over the real store: a pair of doors between two cells -------------------------------------------------

    constexpr std::uint32_t kHallDoor = 0x00000B11, kCellyard = 0x00000910, kYardDoor = 0x00000B21, kChest = 0x00000B12;

    Bytes doorRefr(std::uint32_t id, std::uint32_t base, const Bytes& extra, std::uint32_t flags = 0)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(base)));
        subs.append(sub4("DATA", Bytes().f32(1).f32(2).f32(3).f32(0).f32(0).f32(0)));
        subs.append(extra);
        return record4("REFR", flags, id, subs);
    }

    Bytes xtelSub(std::uint32_t door, float x, float y, float z, std::uint32_t flags)
    {
        return sub4("XTEL", Bytes().u32(door).f32(x).f32(y).f32(z).f32(0).f32(0).f32(1.5f).u32(flags));
    }

    Bytes xlocSub(std::uint8_t level, std::uint32_t key)
    {
        return sub4("XLOC", Bytes().u8(level).u8(9).u8(9).u8(9).u32(key).u8(0).u8(9).u8(9).u8(9).u64(0));
    }

    Bytes interiorCell(std::uint32_t id, const std::string& name, const Bytes& persistent, const Bytes& temporary)
    {
        Bytes cellSubs;
        cellSubs.append(edid(name));
        cellSubs.append(sub4("FULL", Bytes().zstring(name)));
        cellSubs.append(sub4("DATA", Bytes().u16(CellFlags::Interior)));
        Bytes cellRec = record4("CELL", 0, id, cellSubs);
        Bytes children;
        if (persistent.size() > 0)
            children.append(group4(id, kGrupPersistent, persistent));
        if (temporary.size() > 0)
            children.append(group4(id, kGrupTemporary, temporary));
        Bytes sub = cellRec;
        sub.append(group4(id, kGrupCellChildren, children));
        return group4(id % 10, kGrupInteriorBlock, group4((id / 10) % 10, kGrupInteriorSub, sub));
    }

    // Two interiors with a door each that lead to one another, a locked chest and an owned base in the first.
    Bytes doorsEsm()
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "A", "B", 1, {}, 24, 44);
        bytes.append(topGroup("STAT", modelRecord("STAT", kStatA, "StoneWall", "architecture\\wall.nif")));
        const Bytes hall = doorRefr(kHallDoor, kStatA,
            Bytes()
                .append(sub4("XOWN", Bytes().u32(0x00013BB8)))
                .append(sub4("XRNK", Bytes().u32(3)))
                .append(xlocSub(50, 0x0001D4A1))
                .append(xtelSub(kYardDoor, 10, 20, 30, 1)));
        const Bytes chest = doorRefr(kChest, kStatA, xlocSub(255, 0));
        const Bytes yard = doorRefr(kYardDoor, kStatA, xtelSub(kHallDoor, 40, 50, 60, 0));
        Bytes cells = interiorCell(kCellInterior, "TestHall", Bytes(), Bytes().append(hall).append(chest));
        cells.append(interiorCell(kCellyard, "TestYard", yard, Bytes()));
        bytes.append(topGroup("CELL", cells));
        return bytes;
    }

    struct DoorsPlugin
    {
        fs::path mDir;
        fs::path mFile;
        DoorsPlugin()
        {
            mDir = fs::temp_directory_path()
                / ("oe_skyrim_doors_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(mDir);
            mFile = mDir / "doors.esm";
            std::ofstream out(mFile, std::ios::binary);
            const std::string s = doorsEsm().str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
        }
        ~DoorsPlugin()
        {
            std::error_code ec;
            fs::remove_all(mDir, ec);
        }
    };

    const sdkgame::WorldObject* objectById(const sdkgame::WorldCell& c, sdkgame::WorldId id)
    {
        for (const sdkgame::WorldObject& o : c.objects)
            if (o.id == id)
                return &o;
        return nullptr;
    }

    TEST(SkyrimNeutralDoors, aStoreCellCarriesOwnerLockAndTeleportAndTheDestinationNamesTheOtherCell)
    {
        DoorsPlugin plugin;
        SkyrimWorldView view;
        std::vector<std::string> problems;
        view.load({ plugin.mFile }, problems);
        ASSERT_TRUE(problems.empty());
        const auto hallInfo = view.findCellByEditorId("TestHall");
        const auto yardInfo = view.findCellByEditorId("TestYard");
        ASSERT_TRUE(hallInfo && yardInfo);

        sdkgame::WorldCell hall, yard;
        std::string error;
        ASSERT_TRUE(loadNeutralCell(view, hallInfo->id, hall, error)) << error;
        ASSERT_TRUE(loadNeutralCell(view, yardInfo->id, yard, error)) << error;

        const sdkgame::WorldObject* door = objectById(hall, kHallDoor);
        ASSERT_NE(door, nullptr);
        ASSERT_TRUE(door->owner.has_value());
        EXPECT_EQ(door->owner->owner, 0x00013BB8u);
        EXPECT_EQ(door->owner->requiredRank, 3);
        ASSERT_TRUE(door->lock.has_value());
        EXPECT_TRUE(door->lock->locked);
        EXPECT_EQ(door->lock->level, 50);
        EXPECT_EQ(door->lock->key, 0x0001D4A1u);
        ASSERT_TRUE(door->teleport.has_value());
        EXPECT_EQ(door->teleport->destinationDoor, kYardDoor);
        EXPECT_EQ(door->teleport->destinationCell, yardInfo->id); // the door's own cell, from the index pass
        EXPECT_EQ(door->teleport->destination.position, (sdkgame::Vec3d{ 10, 20, 30 }));
        EXPECT_EQ(door->teleport->destination.rotation, eulerToQuat({ 0, 0, 1.5f }));
        EXPECT_EQ(door->teleport->flags, 1u);

        // The other way: the yard door has a teleport only, and its destination is the hall.
        const sdkgame::WorldObject* back = objectById(yard, kYardDoor);
        ASSERT_NE(back, nullptr);
        EXPECT_FALSE(back->owner || back->lock);
        ASSERT_TRUE(back->teleport.has_value());
        EXPECT_EQ(back->teleport->destinationDoor, kHallDoor);
        EXPECT_EQ(back->teleport->destinationCell, hallInfo->id);

        // A chest with a lock and a requires-key level and no key.
        const sdkgame::WorldObject* chest = objectById(hall, kChest);
        ASSERT_TRUE(chest != nullptr && chest->lock.has_value());
        EXPECT_EQ(chest->lock->level, 255);
        EXPECT_EQ(chest->lock->key, sdkgame::kNoWorldId);
        EXPECT_FALSE(chest->teleport.has_value());

        EXPECT_EQ(view.stats().mTeleportDoorsIndexed, 2u);
        EXPECT_EQ(view.stats().mComponents.mTeleportCellsUnknown, 0u);
        EXPECT_EQ(view.stats().mComponents.mTeleportsDropped, 0u);
    }

    TEST(SkyrimNeutralDoors, theSourceKnowsOnlyTheCellLastLoaded)
    {
        DoorsPlugin plugin;
        SkyrimWorldView view;
        std::vector<std::string> problems;
        view.load({ plugin.mFile }, problems);
        const auto hallInfo = view.findCellByEditorId("TestHall");
        const auto yardInfo = view.findCellByEditorId("TestYard");
        ASSERT_TRUE(hallInfo && yardInfo);
        sdkgame::WorldCellContent content;
        std::string error;
        ASSERT_TRUE(view.loadCell(hallInfo->id, content, error)) << error;
        EXPECT_NE(view.find(hallInfo->id, kHallDoor), nullptr);
        EXPECT_EQ(view.find(yardInfo->id, kYardDoor), nullptr); // another cell
        EXPECT_EQ(view.find(hallInfo->id, 0x00000DEAD), nullptr); // an unknown placement
        ASSERT_TRUE(view.loadCell(yardInfo->id, content, error)) << error;
        EXPECT_EQ(view.find(hallInfo->id, kHallDoor), nullptr); // replaced by the later load
        EXPECT_NE(view.find(yardInfo->id, kYardDoor), nullptr);
        EXPECT_FALSE(view.loadCell(0x00DEAD00, content, error));
        EXPECT_EQ(view.find(yardInfo->id, kYardDoor), nullptr); // a failed load clears it
    }

    TEST(SkyrimNeutralDoors, twoLoadsGiveEqualCellsWithComponents)
    {
        DoorsPlugin plugin;
        SkyrimWorldView view;
        std::vector<std::string> problems;
        view.load({ plugin.mFile }, problems);
        const auto hallInfo = view.findCellByEditorId("TestHall");
        ASSERT_TRUE(hallInfo.has_value());
        sdkgame::WorldCell first, second;
        std::string error;
        ASSERT_TRUE(loadNeutralCell(view, hallInfo->id, first, error)) << error;
        ASSERT_TRUE(loadNeutralCell(view, hallInfo->id, second, error)) << error;
        EXPECT_EQ(first, second);
        EXPECT_TRUE(objectById(first, kHallDoor)->teleport.has_value());
    }
}
