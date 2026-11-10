// Task 0102 stage N2: Eden Studio's cell viewer reads the NEUTRAL world model (a WorldCell) instead of the Skyrim
// pack's WorldView content. The neutral overload of buildCellScene must give the same scene as the WorldView one for
// the same objects: same counts, same nodes, same vertices (to rounding), because the quaternion matrix and the Euler
// matrix are the same rotation. No Qt, no GPU, no game data; the installed-data check is the gated test at the end.
#include <apps/opencs/model/mesh/cellscene.hpp>

#include <game_skyrimse/neutral_world.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>

namespace
{
    namespace sdkgame = openeden::sdk::game;
    using namespace CSMMesh;

    sdkgame::PlacedObject object(
        std::uint32_t id, const char* model, sdkgame::BaseKind kind = sdkgame::BaseKind::Static)
    {
        sdkgame::PlacedObject o;
        o.id = id;
        o.base = id + 1000;
        o.baseKind = kind;
        o.baseEditorId = "Thing" + std::to_string(id);
        o.modelPath = model;
        return o;
    }

    SceneModel triangleModel()
    {
        SceneModel m;
        SceneModelShape s;
        s.mesh.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
        s.mesh.normals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
        s.mesh.indices = { 0, 1, 2 };
        m.shapes.push_back(s);
        return m;
    }

    sdkgame::WorldCell neutralOf(const sdkgame::WorldCellContent& content)
    {
        sdkgame::WorldCell cell;
        cell.id = content.cell.id;
        cell.editorId = content.cell.editorId;
        for (const sdkgame::PlacedObject& placed : content.objects)
        {
            sdkgame::WorldObject o;
            std::string problem;
            if (game_skyrimse::toNeutral(placed, o, problem))
                cell.objects.push_back(std::move(o));
            else
                ++cell.droppedObjects;
        }
        return cell;
    }

    void expectSame(const CellScene& a, const CellScene& b)
    {
        EXPECT_EQ(a.stats.objects, b.stats.objects);
        EXPECT_EQ(a.stats.drawnAsMesh, b.stats.drawnAsMesh);
        EXPECT_EQ(a.stats.drawnAsBox, b.stats.drawnAsBox);
        EXPECT_EQ(a.stats.skippedDisabled, b.stats.skippedDisabled);
        EXPECT_EQ(a.stats.skippedNoGeometry, b.stats.skippedNoGeometry);
        EXPECT_EQ(a.stats.skippedInvalid, b.stats.skippedInvalid);
        EXPECT_EQ(a.stats.skippedByBudget, b.stats.skippedByBudget);
        EXPECT_EQ(a.stats.meshMisses, b.stats.meshMisses);
        EXPECT_EQ(a.stats.triangles, b.stats.triangles);
        EXPECT_EQ(a.stats.vertices, b.stats.vertices);
        ASSERT_EQ(a.model.nodes.size(), b.model.nodes.size());
        ASSERT_EQ(a.model.shapes.size(), b.model.shapes.size());
        for (std::size_t i = 0; i < a.model.nodes.size(); ++i)
        {
            EXPECT_EQ(a.model.nodes[i].name, b.model.nodes[i].name);
            EXPECT_EQ(a.model.nodes[i].parent, b.model.nodes[i].parent);
            EXPECT_EQ(a.model.nodes[i].shape, b.model.nodes[i].shape);
            for (int k = 0; k < 16; ++k)
                EXPECT_NEAR(a.model.nodes[i].world[k], b.model.nodes[i].world[k],
                    1e-9 * (1.0 + std::abs(a.model.nodes[i].world[k])));
        }
        for (std::size_t s = 0; s < a.model.shapes.size(); ++s)
        {
            const auto& sa = a.model.shapes[s].mesh;
            const auto& sb = b.model.shapes[s].mesh;
            EXPECT_EQ(sa.indices, sb.indices);
            ASSERT_EQ(sa.positions.size(), sb.positions.size());
            for (std::size_t v = 0; v < sa.positions.size(); ++v)
                for (int k = 0; k < 3; ++k)
                {
                    EXPECT_NEAR(sa.positions[v][k], sb.positions[v][k], 1e-3f);
                    EXPECT_NEAR(sa.normals[v][k], sb.normals[v][k], 1e-5f);
                }
            EXPECT_EQ(a.model.shapes[s].colour, b.model.shapes[s].colour);
        }
        EXPECT_NEAR(a.model.bounds.min[0], b.model.bounds.min[0], 1e-2);
        EXPECT_NEAR(a.model.bounds.max[2], b.model.bounds.max[2], 1e-2);
    }

    TEST(CellSceneNeutral, aPlacementMatrixFromAQuaternionIsTheEulerMatrix)
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> angle(-3.1f, 3.1f);
        for (int i = 0; i < 2000; ++i)
        {
            const sdkgame::WorldVec3 euler{ angle(rng), angle(rng), angle(rng) };
            const sdkgame::WorldVec3 position{ 100.f * i, -5.f, 12.5f };
            const double scale = 0.5 + 0.001 * i;
            sdkgame::WorldObject o;
            std::string problem;
            sdkgame::PlacedObject placed = object(1, "m.nif");
            placed.position = position;
            placed.rotation = euler;
            placed.scale = static_cast<float>(scale);
            ASSERT_TRUE(game_skyrimse::toNeutral(placed, o, problem));
            const auto fromEuler = placementMatrix(position, euler, static_cast<double>(placed.scale));
            const auto fromQuat = placementMatrix(o.transform);
            for (int k = 0; k < 16; ++k)
                EXPECT_NEAR(fromEuler[k], fromQuat[k], 1e-9 * (1.0 + std::abs(fromEuler[k])))
                    << "i=" << i << " k=" << k;
        }
    }

    TEST(CellSceneNeutral, theSameObjectsGiveTheSameSceneAsTheWorldViewPath)
    {
        sdkgame::WorldCellContent content;
        content.cell.editorId = "TestCell";
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> angle(-3.1f, 3.1f), coord(-5000.f, 5000.f);
        const sdkgame::BaseKind kinds[] = { sdkgame::BaseKind::Static, sdkgame::BaseKind::Door,
            sdkgame::BaseKind::Light, sdkgame::BaseKind::Item, sdkgame::BaseKind::Actor, sdkgame::BaseKind::Tree };
        for (std::uint32_t i = 1; i <= 60; ++i)
        {
            sdkgame::PlacedObject o
                = object(i, (i % 7 == 0) ? "" : (i % 5 == 0 ? "missing.nif" : "a.nif"), kinds[i % 6]);
            o.position = { coord(rng), coord(rng), coord(rng) };
            o.rotation = { angle(rng), angle(rng), angle(rng) };
            o.scale = 0.5f + 0.05f * static_cast<float>(i % 9);
            if (i % 3 == 0)
            {
                o.hasBounds = true;
                o.boundsMin = { -10, -20, 0 };
                o.boundsMax = { 10, 20, 30 };
            }
            o.initiallyDisabled = (i % 11 == 0);
            content.objects.push_back(o);
        }
        const SceneModel tri = triangleModel();
        const MeshProvider provider
            = [&](const std::string& path) -> const SceneModel* { return path == "a.nif" ? &tri : nullptr; };
        const CellScene legacy = buildCellScene(content, provider);
        const CellScene neutral = buildCellScene(neutralOf(content), provider);
        EXPECT_GT(legacy.stats.drawnAsMesh, 10u);
        EXPECT_GT(legacy.stats.drawnAsBox, 3u);
        expectSame(legacy, neutral);
    }

    TEST(CellSceneNeutral, objectsTheAdapterRefusedAreCountedAsInvalidLikeTheWorldViewPathDoes)
    {
        sdkgame::WorldCellContent content;
        sdkgame::PlacedObject good = object(1, "a.nif");
        sdkgame::PlacedObject nan = object(2, "a.nif");
        nan.position.x = std::numeric_limits<float>::quiet_NaN();
        sdkgame::PlacedObject farAway = object(3, "a.nif");
        farAway.position.y = 1.0e9f;
        sdkgame::PlacedObject zeroScale = object(4, "a.nif");
        zeroScale.scale = 0.f;
        content.objects = { good, nan, farAway, zeroScale };
        const SceneModel tri = triangleModel();
        const MeshProvider provider = [&](const std::string&) -> const SceneModel* { return &tri; };
        const CellScene legacy = buildCellScene(content, provider);
        const CellScene neutral = buildCellScene(neutralOf(content), provider);
        EXPECT_EQ(legacy.stats.skippedInvalid, 3u);
        expectSame(legacy, neutral);
    }

    TEST(CellSceneNeutral, aBoundsOnlyObjectIsABoxAndALightWithNoModelIsLeftOut)
    {
        sdkgame::WorldCellContent content;
        sdkgame::PlacedObject volume = object(1, "", sdkgame::BaseKind::Activator);
        volume.hasBounds = true;
        volume.boundsMin = { -5, -5, 0 };
        volume.boundsMax = { 5, 5, 10 };
        sdkgame::PlacedObject light = volume;
        light.id = 2;
        light.baseKind = sdkgame::BaseKind::Light;
        content.objects = { volume, light };
        const CellScene neutral = buildCellScene(neutralOf(content), nullptr);
        EXPECT_EQ(neutral.stats.drawnAsBox, 1u);
        EXPECT_EQ(neutral.stats.skippedByBudget + neutral.stats.skippedNoGeometry, 1u);
        expectSame(buildCellScene(content, nullptr), neutral);
    }

    TEST(CellSceneNeutral, anEmptyCellIsJustTheRootNode)
    {
        sdkgame::WorldCell cell;
        cell.editorId = "Empty";
        const CellScene scene = buildCellScene(cell, nullptr);
        EXPECT_EQ(scene.stats.objects, 0u);
        ASSERT_EQ(scene.model.nodes.size(), 1u);
        EXPECT_EQ(scene.model.nodes[0].name, "Empty");
    }
}
