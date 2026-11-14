// Task 0142 follow-up: every exterior cell the pack lists must come out of loadCell with placement ids that are
// strictly ascending (the neutral store refuses a cell otherwise: "ids not strictly ascending"). On the user's own
// plugins; prints counts and the first offending grid squares only (lines starting SKYIDS), no names. Skipped without
// the environment.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five masters are loaded in order
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace sdkgame = openeden::sdk::game;
    using namespace game_skyrimse;

    TEST(SkyrimCellIdsRealData, everyCellComesOutWithStrictlyAscendingIds)
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

        std::vector<sdkgame::WorldCellInfo> cells;
        view.listCells(0, view.cellCount(), cells);
        std::uint64_t read = 0, bad = 0, duplicates = 0, outOfOrder = 0, failed = 0;
        std::size_t shown = 0;
        for (const sdkgame::WorldCellInfo& info : cells)
        {
            sdkgame::WorldCellContent content;
            std::string error;
            if (!view.loadCell(info.id, content, error))
            {
                ++failed;
                continue;
            }
            ++read;
            bool cellBad = false;
            for (std::size_t i = 1; i < content.objects.size(); ++i)
            {
                if (content.objects[i].id == content.objects[i - 1].id)
                {
                    ++duplicates;
                    cellBad = true;
                    const auto& a = content.objects[i - 1];
                    const auto& b = content.objects[i];
                    std::cout << "SKYIDS duplicate id " << a.id << ": base " << a.base << "/" << b.base
                              << " persistent " << a.persistent << "/" << b.persistent << " pos (" << a.position.x
                              << ", " << a.position.y << ")/(" << b.position.x << ", " << b.position.y << ") disabled "
                              << a.initiallyDisabled << "/" << b.initiallyDisabled << std::endl;
                }
                else if (content.objects[i].id < content.objects[i - 1].id)
                {
                    ++outOfOrder;
                    cellBad = true;
                }
            }
            if (cellBad)
            {
                ++bad;
                if (shown++ < 8)
                    std::cout << "SKYIDS bad cell id " << info.id << " world " << info.world << " grid (" << info.gridX
                              << ", " << info.gridY << ") interior " << info.interior << "\n";
            }
        }
        std::cout << "SKYIDS cells read " << read << ", failed " << failed << ", cells with bad ids " << bad
                  << ", equal neighbours " << duplicates << ", descending neighbours " << outOfOrder << "\n";
        EXPECT_EQ(bad, 0u);
    }
}
