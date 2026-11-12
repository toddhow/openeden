// Task 0129: the water of the exterior grid squares on the player's own Skyrim SE masters. Prints aggregates and a small
// map around Whiterun (lines starting SKYRIMSE-WATER), never a path; writes nothing. Skipped without
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder
#include <game_skyrimse/terrain_source.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace sdk = openeden::sdk::game;
    using namespace game_skyrimse;

    const char* const kMasters[] = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };

    TEST(SkyrimWaterReal, theWaterAroundWhiterunOnTheInstalledMasters)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == 0)
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        LoadOrder order;
        std::string err;
        for (const char* name : kMasters)
            ASSERT_TRUE(order.add(name, false, err)) << err;
        SkyrimTerrainIndex index;
        for (const char* name : kMasters)
        {
            std::string error;
            ASSERT_TRUE(index.addPluginFile(data / name, order, error)) << error;
        }

        std::vector<sdk::WorldSpaceInfo> worlds;
        index.listWorlds(worlds);
        sdk::WorldId whiterun = sdk::kNoWorldId, tamriel = sdk::kNoWorldId;
        for (const auto& w : worlds)
        {
            if (w.editorId == "WhiterunWorld")
                whiterun = w.id;
            if (w.editorId == "Tamriel")
                tamriel = w.id;
        }
        ASSERT_NE(whiterun, sdk::kNoWorldId);
        ASSERT_NE(tamriel, sdk::kNoWorldId);

        // Every world answers for its own square; count what the answers are.
        for (const auto& w : worlds)
        {
            std::map<long, int> heights;
            int none = 0, total = 0;
            for (int x = -4; x <= 4; ++x)
                for (int y = -4; y <= 4; ++y)
                {
                    sdk::CellWater cw;
                    ASSERT_TRUE(index.cellWater(w.id, x, y, cw));
                    ++total;
                    if (cw.present)
                        ++heights[std::lround(cw.height)];
                    else
                        ++none;
                }
            std::cout << "SKYRIMSE-WATER world " << w.editorId << ": " << total << " squares near the origin, " << none
                      << " without water, " << heights.size() << " distinct heights" << std::endl;
        }

        for (const sdk::WorldId world : { whiterun, tamriel })
        {
            std::cout << "SKYRIMSE-WATER map of " << (world == whiterun ? "WhiterunWorld" : "Tamriel")
                      << " x 0..14 (columns), y 4 down to -14 (rows); . no water, number = height / 100" << std::endl;
            for (int y = 4; y >= -14; --y)
            {
                std::string row;
                for (int x = 0; x <= 14; ++x)
                {
                    sdk::CellWater cw;
                    ASSERT_TRUE(index.cellWater(world, x, y, cw));
                    char buf[16];
                    if (cw.present)
                        std::snprintf(buf, sizeof buf, "%6ld", std::lround(cw.height / 100.0f));
                    else
                        std::snprintf(buf, sizeof buf, "%6s", ".");
                    row += buf;
                }
                std::cout << "SKYRIMSE-WATER " << (y < 0 ? "" : " ") << y << row << std::endl;
            }
        }
    }
}
