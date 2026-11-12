// Task 0142: where the 35 TREE records whose model has a BSTreeNode (task 0136) are placed in the exterior worlds, on the
// user's own plugins. Prints the 12 exterior cells with the most such placements (lines starting SKYTREE-PLACED:
// world, grid square, count, one placement's position in units); aggregates only, no path or name. Skipped when the
// environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the game's five masters are loaded in order
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/world_model.hpp>
#include <openeden/game/world_source.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace sdkgame = openeden::sdk::game;
    using namespace game_skyrimse;

    TEST(SkyrimTreePlacementsRealData, listsTheCellsWithMostBstreeNodeTrees)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = env;
        std::vector<fs::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
            if (fs::exists(data / name))
                paths.push_back(data / name);
        ASSERT_FALSE(paths.empty());

        DataFolderFiles files(data);
        SkyrimWorldView view({}, &files);
        std::vector<std::string> problems;
        ASSERT_GT(view.load(paths, problems), 0u);
        SkyrimCellSource source(view);

        // The 35 TREE records of Skyrim.esm with a BSTreeNode model (docs/tasks/0136-skyrim-tree-sway.md).
        const std::set<std::uint32_t> trees = { 980389, 980388, 980387, 980386, 980385, 980384, 980383, 980382, 980381,
            980380, 758398, 758397, 758396, 758395, 758394, 483659, 483658, 444885, 444884, 436710, 381659, 381658,
            381657, 381656, 381655, 376946, 376945, 376944, 376943, 376942, 332070, 326576, 307222, 100866, 77933 };

        struct Hit
        {
            std::string world;
            int x, y;
            std::size_t count;
            double px, py, pz;
        };
        std::vector<Hit> hits;
        std::uint64_t cells = 0, placements = 0;
        std::vector<sdkgame::WorldId> ids;
        source.listCells(0, source.cellCount(), ids);
        for (const sdkgame::WorldId id : ids)
        {
            sdkgame::WorldCell cell;
            std::string error;
            if (!source.buildCell(id, cell, error) || cell.interior || !cell.hasGrid)
                continue;
            ++cells;
            std::size_t n = 0;
            const sdkgame::WorldObject* sample = nullptr;
            for (const sdkgame::WorldObject& o : cell.objects)
                if (trees.count(static_cast<std::uint32_t>(o.base)) != 0)
                {
                    ++n;
                    if (sample == nullptr)
                        sample = &o;
                }
            if (n == 0)
                continue;
            placements += n;
            hits.push_back({ std::to_string(cell.world), cell.gridX, cell.gridY, n, sample->transform.position.x,
                sample->transform.position.y, sample->transform.position.z });
        }
        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.count > b.count; });
        std::cout << "SKYTREE-PLACED exterior cells read " << cells << ", cells with such trees " << hits.size()
                  << ", placements " << placements << "\n";
        for (std::size_t i = 0; i < hits.size() && i < 12; ++i)
            std::cout << "SKYTREE-PLACED world " << hits[i].world << " cell (" << hits[i].x << ", " << hits[i].y << ") count " << hits[i].count
                      << " sample at " << hits[i].px << ", " << hits[i].py << ", " << hits[i].pz << "\n";
        SUCCEED();
    }
}
