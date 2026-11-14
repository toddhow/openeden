// Task 0138 (milestone M-S2): how the Skyrim SE data marks a teleport door whose destination is an exterior, on the
// user's own plugins. Prints counts only (lines starting SKYRIMSE-EXTDOOR), never a cell, record or mod name except
// the three editor ids the task names; writes nothing. Skipped when the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the game's five masters are loaded in order
//
// What it checks (facts to mark VERIFIED in the task): the destination cell of an exterior-bound teleport is an
// exterior cell of a world the source can describe (worldInfo), carries the grid square, and the square agrees with
// the one the arrival (XTEL) position stands in at 4096 units a square.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/world_model.hpp>
#include <openeden/game/world_source.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace sdkgame = openeden::sdk::game;
    using namespace game_skyrimse;

    TEST(SkyrimExteriorDoorsRealData, exteriorBoundTeleportsPointAtExteriorCellsOfDescribableWorlds)
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

        const sdkgame::WorldId whiterunWorld = source.findWorldByEditorId("WhiterunWorld");
        const sdkgame::WorldId tamriel = source.findWorldByEditorId("Tamriel");
        std::cout << "SKYRIMSE-EXTDOOR worlds: WhiterunWorld " << (whiterunWorld != sdkgame::kNoWorldId) << " Tamriel "
                  << (tamriel != sdkgame::kNoWorldId) << "\n";

        // Every interior cell (the doors out of buildings) and every exterior cell of the two worlds the task names.
        std::uint64_t interiorSamples = 0, cellsRead = 0, teleports = 0, toUnknown = 0, toInterior = 0, toExterior = 0,
                      extNoWorldInfo = 0, extNoGrid = 0, gridDisagrees = 0, gridAgrees = 0, crossWorld = 0,
                      builtFailed = 0;
        std::set<sdkgame::WorldId> destinationsSeen;
        std::map<sdkgame::WorldId, sdkgame::WorldCell> built;
        auto cellOf = [&](sdkgame::WorldId id) -> const sdkgame::WorldCell* {
            auto it = built.find(id);
            if (it != built.end())
                return &it->second;
            sdkgame::WorldCell cell;
            std::string error;
            if (!source.buildCell(id, cell, error))
                return nullptr;
            return &built.emplace(id, std::move(cell)).first->second;
        };
        auto survey = [&](const sdkgame::WorldCell& cell) {
            ++cellsRead;
            for (const sdkgame::WorldObject& o : cell.objects)
            {
                if (!o.teleport)
                    continue;
                ++teleports;
                const sdkgame::TeleportComponent& t = *o.teleport;
                if (t.destinationCell == sdkgame::kNoWorldId)
                {
                    ++toUnknown;
                    continue;
                }
                destinationsSeen.insert(t.destinationCell);
                const sdkgame::WorldCell* dest = cellOf(t.destinationCell);
                if (dest == nullptr)
                {
                    ++builtFailed;
                    continue;
                }
                if (dest->interior)
                {
                    ++toInterior;
                    continue;
                }
                ++toExterior;
                sdkgame::WorldSpaceInfo info;
                if (dest->world == sdkgame::kNoWorldId || !source.worldInfo(dest->world, info))
                    ++extNoWorldInfo;
                if (!cell.interior && dest->world != cell.world)
                {
                    ++crossWorld;
                    if (dest->world == whiterunWorld || cell.world == whiterunWorld)
                        std::cout << "SKYRIMSE-EXTDOOR gate: cell " << cell.editorId << " (world " << cell.world
                                  << ") door base " << o.base << " at " << o.transform.position.x << ","
                                  << o.transform.position.y << "," << o.transform.position.z << " locked "
                                  << (o.lock && o.lock->locked) << " to world " << dest->world << " arrival "
                                  << t.destination.position.x << "," << t.destination.position.y << "\n";
                }
                else if (cell.interior && ++interiorSamples <= 5)
                    std::cout << "SKYRIMSE-EXTDOOR out: cell " << cell.editorId << " door base " << o.base << " at "
                              << o.transform.position.x << "," << o.transform.position.y << ","
                              << o.transform.position.z << " locked " << (o.lock && o.lock->locked) << " to world "
                              << dest->world << " arrival " << t.destination.position.x << ","
                              << t.destination.position.y << "\n";
                if (!dest->hasGrid)
                {
                    ++extNoGrid;
                    continue;
                }
                const int gx = static_cast<int>(std::floor(t.destination.position.x / 4096.0));
                const int gy = static_cast<int>(std::floor(t.destination.position.y / 4096.0));
                if (gx == dest->gridX && gy == dest->gridY)
                    ++gridAgrees;
                else
                {
                    ++gridDisagrees;
                    if (gridDisagrees <= 12)
                        std::cout << "SKYRIMSE-EXTDOOR sample: from " << (cell.interior ? "interior" : "exterior")
                                  << " arrival " << t.destination.position.x << "," << t.destination.position.y
                                  << " square " << gx << "," << gy << " destination cell grid " << dest->gridX << ","
                                  << dest->gridY << " world " << dest->world << " (Whiterun " << whiterunWorld << ")\n";
                }
            }
        };

        std::vector<sdkgame::WorldCellInfo> infos;
        view.listCells(0, view.cellCount(), infos);
        for (const sdkgame::WorldCellInfo& info : infos)
        {
            if (!info.interior && info.world != whiterunWorld && info.world != tamriel)
                continue;
            const sdkgame::WorldCell* head = cellOf(info.id);
            if (head == nullptr)
                continue;
            survey(*head);
            if (!head->interior)
                built.erase(info.id); // keep only what a door points at; built again on demand
        }
        std::cout << "SKYRIMSE-EXTDOOR cells surveyed " << cellsRead << ", teleports " << teleports
                  << ", destination unknown " << toUnknown << ", build failed " << builtFailed << ", to interior "
                  << toInterior << ", to exterior " << toExterior << " (world not describable " << extNoWorldInfo
                  << ", no grid " << extNoGrid << ", arrival square equals the cell's " << gridAgrees << ", differs "
                  << gridDisagrees << ", to another world than the door's " << crossWorld << ")\n";
        EXPECT_GT(teleports, 0u);
        EXPECT_GT(toExterior, 0u);
        EXPECT_EQ(extNoWorldInfo, 0u);
    }
}
