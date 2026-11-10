// Task 0086, Eden Studio's read-only cell viewer: the pure model that turns the objects of a cell (the WorldView
// contract's plain values) into a SceneModel. No Qt, no GPU, no game data.
#include <apps/opencs/model/mesh/cellscene.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>

namespace
{
    namespace sdkgame = openeden::sdk::game;
    using namespace CSMMesh;

    constexpr double kPi = 3.14159265358979323846;

    sdkgame::PlacedObject object(std::uint32_t id, const char* model, float x = 0, float y = 0, float z = 0)
    {
        sdkgame::PlacedObject o;
        o.id = id;
        o.base = id + 1000;
        o.baseKind = sdkgame::BaseKind::Static;
        o.baseEditorId = "Thing";
        o.modelPath = model;
        o.position = { x, y, z };
        return o;
    }

    void withBounds(sdkgame::PlacedObject& o, float hx = 10, float hy = 20, float hz = 30)
    {
        o.hasBounds = true;
        o.boundsMin = { -hx, -hy, 0 };
        o.boundsMax = { hx, hy, hz };
    }

    // A single triangle mesh in the model's own frame.
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

    sdkgame::WorldCellContent cellOf(std::vector<sdkgame::PlacedObject> objects)
    {
        sdkgame::WorldCellContent c;
        c.cell.editorId = "TestCell";
        c.objects = std::move(objects);
        return c;
    }

    TEST(CellScenePlacement, identityTranslationAndScale)
    {
        const auto m = placementMatrix({ 1, 2, 3 }, { 0, 0, 0 }, 2.0);
        EXPECT_DOUBLE_EQ(m[0], 2.0);
        EXPECT_DOUBLE_EQ(m[5], 2.0);
        EXPECT_DOUBLE_EQ(m[10], 2.0);
        EXPECT_DOUBLE_EQ(m[12], 1.0);
        EXPECT_DOUBLE_EQ(m[13], 2.0);
        EXPECT_DOUBLE_EQ(m[14], 3.0);
        EXPECT_DOUBLE_EQ(m[15], 1.0);
    }

    TEST(CellScenePlacement, aQuarterTurnAboutZMovesXToY)
    {
        // The settled convention (task 0086 stage log): a positive Z angle turns clockwise seen from above, +X to -Y.
        const auto m = placementMatrix({ 0, 0, 0 }, { 0, 0, static_cast<float>(kPi / 2) }, 1.0);
        EXPECT_NEAR(m[0], 0.0, 1e-6);
        EXPECT_NEAR(m[1], -1.0, 1e-6); // column 0 is where X goes
        EXPECT_NEAR(m[4], 1.0, 1e-6); // column 1 is where Y goes
        EXPECT_NEAR(m[5], 0.0, 1e-6);
    }

    TEST(CellScenePlacement, rotationsComposeAsZYX)
    {
        // Clockwise angles: Rx(-90) takes +Y to -Z, then Rz(-90) leaves Z alone: with Rz * Ry * Rx a vector turns about
        // X first.
        const auto m
            = placementMatrix({ 0, 0, 0 }, { static_cast<float>(kPi / 2), 0, static_cast<float>(kPi / 2) }, 1.0);
        // +Y goes to -Z under Rx, -Z stays under Rz.
        EXPECT_NEAR(m[4], 0.0, 1e-6);
        EXPECT_NEAR(m[5], 0.0, 1e-6);
        EXPECT_NEAR(m[6], -1.0, 1e-6);
        // +X stays under Rx, then goes to -Y under Rz.
        EXPECT_NEAR(m[0], 0.0, 1e-6);
        EXPECT_NEAR(m[1], -1.0, 1e-6);
        EXPECT_NEAR(m[2], 0.0, 1e-6);
    }

    TEST(CellScenePlacement, positiveXAndYAnglesTipTheUpAxisTowardPlusYAndMinusX)
    {
        // Settled by terrain (task 0086 stage log, rotation convention): on sloped ground the placements that follow
        // the slope are best explained when a positive X angle tips the object's up axis toward +Y and a positive Y
        // angle tips it toward -X (every angle clockwise), applied X first, then Y, then Z. 79,340 placements in
        // Skyrim.esm's Tamriel: mean angle to the terrain normal 0.203 rad with this convention, 0.357 rad untilted,
        // 0.455 rad with both signs flipped. Column 2 of the matrix is where local +Z goes.
        const float a = 0.5f;
        const auto mx = placementMatrix({ 0, 0, 0 }, { a, 0, 0 }, 1.0);
        EXPECT_NEAR(mx[8], 0.0, 1e-6);
        EXPECT_NEAR(mx[9], std::sin(a), 1e-6); // up gains +Y
        EXPECT_NEAR(mx[10], std::cos(a), 1e-6);
        const auto my = placementMatrix({ 0, 0, 0 }, { 0, a, 0 }, 1.0);
        EXPECT_NEAR(my[8], -std::sin(a), 1e-6); // up gains -X
        EXPECT_NEAR(my[9], 0.0, 1e-6);
        EXPECT_NEAR(my[10], std::cos(a), 1e-6);
    }

    TEST(CellScene, aMeshIsPlacedByTheObjectsMatrix)
    {
        const SceneModel tri = triangleModel();
        auto o = object(1, "a\\tri.nif", 100, 200, 300);
        o.scale = 2.0f;
        const CellScene scene = buildCellScene(
            cellOf({ o }), [&](const std::string& path) { return path == "a\\tri.nif" ? &tri : nullptr; });
        ASSERT_EQ(scene.model.shapes.size(), 1u);
        const auto& p = scene.model.shapes[0].mesh.positions;
        ASSERT_EQ(p.size(), 3u);
        EXPECT_FLOAT_EQ(p[0][0], 100.f);
        EXPECT_FLOAT_EQ(p[1][0], 102.f);
        EXPECT_FLOAT_EQ(p[2][1], 202.f);
        EXPECT_FLOAT_EQ(p[0][2], 300.f);
        EXPECT_EQ(scene.stats.drawnAsMesh, 1u);
        EXPECT_EQ(scene.stats.drawnAsBox, 0u);
        EXPECT_EQ(scene.stats.triangles, 1u);
        EXPECT_EQ(scene.stats.vertices, 3u);
        EXPECT_TRUE(scene.model.bounds.valid);
        EXPECT_DOUBLE_EQ(scene.model.bounds.max[0], 102.0);
        // Normals stay unit length and follow the rotation.
        const auto& n = scene.model.shapes[0].mesh.normals[0];
        EXPECT_NEAR(n[2], 1.f, 1e-6);
    }

    TEST(CellScene, aRotatedMeshHasRotatedNormalsAndPositions)
    {
        const SceneModel tri = triangleModel();
        auto o = object(1, "m.nif");
        o.rotation = { 0, 0, static_cast<float>(kPi / 2) };
        const CellScene scene = buildCellScene(cellOf({ o }), [&](const std::string&) { return &tri; });
        const auto& p = scene.model.shapes[0].mesh.positions;
        EXPECT_NEAR(p[1][0], 0.f, 1e-5); // the point (1, 0, 0) turned to (0, -1, 0)
        EXPECT_NEAR(p[1][1], -1.f, 1e-5);
    }

    TEST(CellScene, aMissingMeshBecomesABoxOfTheBounds)
    {
        auto o = object(1, "gone.nif", 50, 0, 0);
        withBounds(o);
        const CellScene scene
            = buildCellScene(cellOf({ o }), [](const std::string&) -> const SceneModel* { return nullptr; });
        EXPECT_EQ(scene.stats.drawnAsBox, 1u);
        EXPECT_EQ(scene.stats.meshMisses, 1u);
        ASSERT_EQ(scene.model.shapes.size(), 1u);
        EXPECT_EQ(scene.model.shapes[0].mesh.positions.size(), 24u);
        EXPECT_EQ(scene.model.shapes[0].mesh.indices.size(), 36u);
        EXPECT_EQ(scene.stats.triangles, 12u);
        EXPECT_DOUBLE_EQ(scene.model.bounds.min[0], 40.0);
        EXPECT_DOUBLE_EQ(scene.model.bounds.max[0], 60.0);
        EXPECT_DOUBLE_EQ(scene.model.bounds.max[2], 30.0);
        // Faces point outwards: the first face's normal is +X and its first triangle's winding agrees.
        const auto& mesh = scene.model.shapes[0].mesh;
        const auto n = faceNormal(
            mesh.positions[mesh.indices[0]], mesh.positions[mesh.indices[1]], mesh.positions[mesh.indices[2]]);
        EXPECT_NEAR(n[0], mesh.normals[mesh.indices[0]][0], 1e-6);
        EXPECT_NEAR(n[1], mesh.normals[mesh.indices[0]][1], 1e-6);
        EXPECT_NEAR(n[2], mesh.normals[mesh.indices[0]][2], 1e-6);
    }

    TEST(CellScene, everyBoxFaceWindsOutwards)
    {
        auto o = object(1, "", 0, 0, 0);
        withBounds(o, 10, 20, 30);
        const CellScene scene = buildCellScene(cellOf({ o }), nullptr);
        ASSERT_EQ(scene.stats.drawnAsBox, 1u);
        const auto& mesh = scene.model.shapes[0].mesh;
        for (std::size_t t = 0; t < mesh.indices.size(); t += 3)
        {
            const auto& a = mesh.positions[mesh.indices[t]];
            const auto& b = mesh.positions[mesh.indices[t + 1]];
            const auto& c = mesh.positions[mesh.indices[t + 2]];
            const auto n = faceNormal(a, b, c);
            // The centre of the box is at (0, 0, 15); the face normal points away from it.
            const float cx = (a[0] + b[0] + c[0]) / 3 - 0.f, cy = (a[1] + b[1] + c[1]) / 3 - 0.f,
                        cz = (a[2] + b[2] + c[2]) / 3 - 15.f;
            EXPECT_GT(n[0] * cx + n[1] * cy + n[2] * cz, 0.f) << t;
        }
    }

    TEST(CellScene, nothingToDrawIsCountedNotDropped)
    {
        auto disabled = object(1, "a.nif");
        disabled.initiallyDisabled = true;
        sdkgame::PlacedObject actor;
        actor.id = 2;
        actor.baseKind = sdkgame::BaseKind::Actor;
        sdkgame::PlacedObject modelless = object(3, "");
        const SceneModel tri = triangleModel();
        const CellScene scene = buildCellScene(
            cellOf({ disabled, actor, modelless, object(4, "a.nif") }), [&](const std::string&) { return &tri; });
        EXPECT_EQ(scene.stats.objects, 4u);
        EXPECT_EQ(scene.stats.skippedDisabled, 1u);
        EXPECT_EQ(scene.stats.skippedNoGeometry, 2u);
        EXPECT_EQ(scene.stats.drawnAsMesh, 1u);
        EXPECT_EQ(scene.stats.skippedInvalid, 0u);
    }

    TEST(CellScene, hostileValuesAreSkipped)
    {
        const SceneModel tri = triangleModel();
        const float inf = std::numeric_limits<float>::infinity();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        auto nanPos = object(1, "a.nif", nan, 0, 0);
        auto farAway = object(2, "a.nif", 1e9f, 0, 0);
        auto badScale = object(3, "a.nif");
        badScale.scale = inf;
        auto zeroScale = object(4, "a.nif");
        zeroScale.scale = 0.f;
        auto hugeScale = object(5, "a.nif");
        hugeScale.scale = 1e6f;
        auto badRot = object(6, "a.nif");
        badRot.rotation = { 0, nan, 0 };
        auto badBounds = object(7, "gone.nif");
        withBounds(badBounds);
        badBounds.boundsMax = { inf, 0, 0 };
        const CellScene scene
            = buildCellScene(cellOf({ nanPos, farAway, badScale, zeroScale, hugeScale, badRot, badBounds }),
                [&](const std::string& p) { return p == "a.nif" ? &tri : nullptr; });
        EXPECT_EQ(scene.stats.skippedInvalid, 7u);
        EXPECT_TRUE(scene.model.shapes.empty());
        EXPECT_FALSE(scene.model.bounds.valid);
    }

    TEST(CellScene, theBudgetTurnsMeshesIntoBoxesThenDropsThem)
    {
        SceneModel big;
        SceneModelShape s;
        for (int i = 0; i < 300; ++i)
            s.mesh.positions.push_back({ float(i), 0, 0 });
        for (std::uint32_t i = 0; i + 2 < 300; ++i)
            s.mesh.indices.insert(s.mesh.indices.end(), { i, i + 1, i + 2 });
        big.shapes.push_back(s); // 298 triangles, 300 vertices
        std::vector<sdkgame::PlacedObject> objects;
        for (std::uint32_t i = 0; i < 10; ++i)
        {
            auto o = object(i + 1, "big.nif", float(i) * 100, 0, 0);
            withBounds(o);
            objects.push_back(o);
        }
        CellSceneLimits limits;
        limits.maxTriangles = 700; // two meshes (596) and then boxes (12 each) until 700
        const CellScene scene = buildCellScene(
            cellOf(objects), [&](const std::string&) { return &big; }, limits);
        EXPECT_EQ(scene.stats.drawnAsMesh, 2u);
        EXPECT_GT(scene.stats.drawnAsBox, 0u);
        EXPECT_LE(scene.stats.triangles, 700u);
        EXPECT_EQ(scene.stats.drawnAsMesh + scene.stats.drawnAsBox + scene.stats.skippedByBudget, 10u);

        CellSceneLimits objectCap;
        objectCap.maxObjects = 3;
        const CellScene capped = buildCellScene(
            cellOf(objects), [&](const std::string&) { return &big; }, objectCap);
        EXPECT_EQ(capped.stats.drawnAsMesh, 3u);
        EXPECT_EQ(capped.stats.skippedByBudget, 7u);

        CellSceneLimits perMesh;
        perMesh.maxVerticesPerMesh = 100;
        const CellScene boxes = buildCellScene(
            cellOf(objects), [&](const std::string&) { return &big; }, perMesh);
        EXPECT_EQ(boxes.stats.drawnAsMesh, 0u);
        EXPECT_EQ(boxes.stats.drawnAsBox, 10u);
    }

    TEST(CellScene, theNodeTreeIsParentsFirstAndShapesPointAtTheirNodes)
    {
        const SceneModel tri = triangleModel();
        auto a = object(1, "a.nif", 0, 0, 0);
        auto b = object(2, "gone.nif", 10, 0, 0);
        withBounds(b);
        const CellScene scene
            = buildCellScene(cellOf({ a, b }), [&](const std::string& p) { return p == "a.nif" ? &tri : nullptr; });
        ASSERT_EQ(scene.model.shapes.size(), 2u);
        EXPECT_EQ(scene.model.nodes[0].name, "TestCell");
        for (std::size_t i = 0; i < scene.model.nodes.size(); ++i)
            EXPECT_LT(scene.model.nodes[i].parent, static_cast<int>(i));
        for (std::size_t s = 0; s < scene.model.shapes.size(); ++s)
        {
            const int node = scene.model.shapes[s].node;
            ASSERT_GE(node, 0);
            ASSERT_LT(node, static_cast<int>(scene.model.nodes.size()));
            EXPECT_EQ(scene.model.nodes[node].shape, static_cast<int>(s));
            EXPECT_EQ(scene.model.nodes[node].type, "Geometry");
        }
    }

    TEST(CellScene, theKindTintsTheMeshColour)
    {
        SceneModel tri = triangleModel();
        tri.shapes[0].colour = { 0.72f, 0.72f, 0.72f, 1.f };
        auto furniture = object(1, "a.nif");
        furniture.baseKind = sdkgame::BaseKind::Furniture;
        auto stone = object(2, "a.nif");
        const CellScene scene = buildCellScene(cellOf({ furniture, stone }), [&](const std::string&) { return &tri; });
        ASSERT_EQ(scene.model.shapes.size(), 2u);
        EXPECT_NE(scene.model.shapes[0].colour, scene.model.shapes[1].colour);
        EXPECT_FLOAT_EQ(scene.model.shapes[0].colour[3], 1.f);
    }

    TEST(CellScene, aTexturedShapeKeepsMostOfItsTextureColour)
    {
        // Task 0086, textures: a shape coloured by its texture takes a trace of the kind's tint, a flat grey takes a
        // lot.
        SceneModel tri = triangleModel();
        tri.shapes[0].colour = { 1.f, 0.f, 0.f, 1.f };
        tri.shapes[0].textured = true;
        tri.shapes[0].diffuse = "textures\a.dds";
        SceneModel flat = triangleModel();
        flat.shapes[0].colour = { 1.f, 0.f, 0.f, 1.f };
        auto a = object(1, "a.nif");
        auto b = object(2, "b.nif");
        const CellScene scene
            = buildCellScene(cellOf({ a, b }), [&](const std::string& path) { return path == "a.nif" ? &tri : &flat; });
        ASSERT_EQ(scene.model.shapes.size(), 2u);
        const auto& textured = scene.model.shapes[0];
        const auto& grey = scene.model.shapes[1];
        EXPECT_TRUE(textured.textured);
        EXPECT_EQ(textured.diffuse, "textures\a.dds");
        EXPECT_FALSE(grey.textured);
        EXPECT_GT(textured.colour[0], grey.colour[0]);
        EXPECT_GT(textured.colour[0], 0.85f);
        EXPECT_LT(textured.colour[1], 0.2f);
    }

    TEST(CellScene, aLightWithNoModelHasNoBox)
    {
        auto light = object(1, "");
        light.baseKind = sdkgame::BaseKind::Light;
        withBounds(light, 400, 400, 400);
        auto lamp = object(2, "gone.nif");
        lamp.baseKind = sdkgame::BaseKind::Light;
        withBounds(lamp, 400, 400, 400);
        const CellScene scene = buildCellScene(cellOf({ light, lamp }), nullptr);
        EXPECT_EQ(scene.stats.drawnAsBox, 0u);
        EXPECT_EQ(scene.stats.skippedNoGeometry + scene.stats.skippedByBudget, 2u);
        EXPECT_TRUE(scene.model.shapes.empty());
    }

    TEST(CellScene, anEmptyCellGivesJustTheRoot)
    {
        const CellScene scene = buildCellScene(cellOf({}), nullptr);
        EXPECT_EQ(scene.model.nodes.size(), 1u);
        EXPECT_TRUE(scene.model.shapes.empty());
        EXPECT_FALSE(scene.model.bounds.valid);
    }
}
