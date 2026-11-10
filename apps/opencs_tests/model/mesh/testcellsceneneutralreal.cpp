// Task 0102 stage N2, the real-data check (gated): on the user's own Skyrim SE plugins, every interior cell and an even
// sample of the exterior cells give the SAME scene through the neutral store as through the pack's WorldView (the path
// before stage N2): same counts, same nodes, same vertices to rounding. Aggregates only; no cell or mod name is
// printed. Needs OPENEDEN_SKYRIMSE_PLUGINS (a plugin list file, in load order) and OPENEDEN_SKYRIMSE_DATA; skipped
// without them.
#include <apps/opencs/model/mesh/cellscene.hpp>
#include <apps/opencs/model/mesh/skyrimcellsource.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
    namespace sdkgame = openeden::sdk::game;
    using namespace CSMMesh;

    SceneModel triangleModel()
    {
        SceneModel m;
        SceneModelShape s;
        s.mesh.positions = { { 0, 0, 0 }, { 100, 0, 0 }, { 0, 100, 0 } };
        s.mesh.normals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
        s.mesh.indices = { 0, 1, 2 };
        m.shapes.push_back(s);
        return m;
    }

    struct Totals
    {
        std::size_t cells = 0, objects = 0, meshes = 0, boxes = 0, skipped = 0;
        std::size_t countMismatches = 0, nodeMismatches = 0, vertexMismatches = 0;
        double worstVertex = 0.0;
    };

    void compare(const CellScene& a, const CellScene& b, Totals& t)
    {
        ++t.cells;
        t.objects += a.stats.objects;
        t.meshes += a.stats.drawnAsMesh;
        t.boxes += a.stats.drawnAsBox;
        t.skipped += a.stats.skippedDisabled + a.stats.skippedNoGeometry + a.stats.skippedInvalid;
        const bool same = a.stats.objects == b.stats.objects && a.stats.drawnAsMesh == b.stats.drawnAsMesh
            && a.stats.drawnAsBox == b.stats.drawnAsBox && a.stats.skippedDisabled == b.stats.skippedDisabled
            && a.stats.skippedNoGeometry == b.stats.skippedNoGeometry
            && a.stats.skippedInvalid == b.stats.skippedInvalid && a.stats.skippedByBudget == b.stats.skippedByBudget
            && a.stats.meshMisses == b.stats.meshMisses && a.stats.triangles == b.stats.triangles
            && a.stats.vertices == b.stats.vertices;
        if (!same)
            ++t.countMismatches;
        if (a.model.nodes.size() != b.model.nodes.size() || a.model.shapes.size() != b.model.shapes.size())
        {
            ++t.nodeMismatches;
            return;
        }
        for (std::size_t s = 0; s < a.model.shapes.size(); ++s)
        {
            const auto& pa = a.model.shapes[s].mesh.positions;
            const auto& pb = b.model.shapes[s].mesh.positions;
            if (pa.size() != pb.size() || a.model.shapes[s].mesh.indices != b.model.shapes[s].mesh.indices)
            {
                ++t.vertexMismatches;
                continue;
            }
            for (std::size_t v = 0; v < pa.size(); ++v)
                for (int k = 0; k < 3; ++k)
                    t.worstVertex = std::max(t.worstVertex, static_cast<double>(std::abs(pa[v][k] - pb[v][k])));
        }
    }

    TEST(CellSceneNeutralRealData, theNeutralPathEqualsTheWorldViewPathOnTheInstalledPlugins)
    {
        if (std::getenv("OPENEDEN_SKYRIMSE_PLUGINS") == nullptr || std::getenv("OPENEDEN_SKYRIMSE_DATA") == nullptr)
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_PLUGINS / OPENEDEN_SKYRIMSE_DATA are not set";
        SkyrimCellSource& source = SkyrimCellSource::instance();
        std::string error;
        ASSERT_TRUE(source.ready() || source.configureFromEnvironment(error)) << error;
        sdkgame::WorldView& view = *source.view();
        NeutralWorld::NeutralWorldStore& store = *source.store();

        std::vector<sdkgame::WorldCellInfo> infos;
        view.listCells(0, view.cellCount(), infos);
        ASSERT_FALSE(infos.empty());

        const SceneModel tri = triangleModel();
        const MeshProvider withMeshes = [&](const std::string&) -> const SceneModel* { return &tri; };
        Totals boxesOnly, meshes;
        std::size_t interiors = 0, exteriors = 0, index = 0;
        for (const sdkgame::WorldCellInfo& info : infos)
        {
            const bool wanted = info.interior ? true : (index++ % 37 == 0);
            if (!wanted)
                continue;
            (info.interior ? interiors : exteriors) += 1;
            sdkgame::WorldCellContent content;
            std::string why;
            ASSERT_TRUE(view.loadCell(info.id, content, why)) << why;
            const sdkgame::WorldCell* neutral = store.cell(SkyrimCellSource::kGameId, info.id, why);
            ASSERT_NE(neutral, nullptr) << why;
            // The same object counts per cell: what the view lists is what the neutral cell lists plus what it refused.
            ASSERT_EQ(content.objects.size(), neutral->objects.size() + neutral->droppedObjects);
            compare(buildCellScene(content, nullptr), buildCellScene(*neutral, nullptr), boxesOnly);
            compare(buildCellScene(content, withMeshes), buildCellScene(*neutral, withMeshes), meshes);
        }
        std::printf(
            "[ aggregate ] %zu interior and %zu exterior cells; %zu objects, %zu drawn as meshes, %zu boxes, "
            "%zu skipped; worst vertex difference %.6f units\n",
            interiors, exteriors, meshes.objects, meshes.meshes, boxesOnly.boxes, meshes.skipped,
            std::max(meshes.worstVertex, boxesOnly.worstVertex));
        EXPECT_EQ(boxesOnly.countMismatches + meshes.countMismatches, 0u);
        EXPECT_EQ(boxesOnly.nodeMismatches + meshes.nodeMismatches, 0u);
        EXPECT_EQ(boxesOnly.vertexMismatches + meshes.vertexMismatches, 0u);
        EXPECT_LT(std::max(meshes.worstVertex, boxesOnly.worstVertex), 0.01);
        EXPECT_GT(interiors, 100u);
    }
}
