// Task 0105 (stage N3a-2): the aggregate check of Store<NeutralBase> on the user's own Skyrim SE masters. It feeds the
// objects of every interior cell and an even sample of about 2,000 exterior cells (as the pack hands them over, through
// loadNeutralCell) to the base store and prints counts only (lines starting NEUTRALBASE-REAL), never an id, a model or
// a mod name. It writes nothing and is skipped when the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the game's five masters are loaded in order
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "apps/openmw/mwworld/neutralstore.hpp"

namespace
{
    namespace fs = std::filesystem;
    namespace sdkgame = openeden::sdk::game;
    using Clock = std::chrono::steady_clock;
    using Status = MWWorld::Store<MWWorld::NeutralBase>::Status;

    TEST(NeutralBaseStoreRealData, the_skyrim_masters_fill_the_base_store_without_conflicts_or_refusals)
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

        game_skyrimse::DataFolderFiles files(data);
        game_skyrimse::SkyrimWorldView view({}, &files);
        std::vector<std::string> problems;
        ASSERT_GT(view.load(paths, problems), 0u);

        std::vector<sdkgame::WorldCellInfo> cells;
        view.listCells(0, view.cellCount(), cells);
        std::vector<sdkgame::WorldId> chosen;
        std::vector<sdkgame::WorldId> exteriors;
        for (const sdkgame::WorldCellInfo& c : cells)
            (c.interior ? chosen : exteriors).push_back(c.id);
        const std::size_t interiors = chosen.size();
        const std::size_t step = exteriors.size() > 2000 ? exteriors.size() / 2000 : 1;
        for (std::size_t i = 0; i < exteriors.size(); i += step)
            chosen.push_back(exteriors[i]);

        MWWorld::Store<MWWorld::NeutralBase> store;
        std::uint64_t cellsLoaded = 0, cellsFailed = 0, objects = 0, added = 0, existing = 0, conflicts = 0,
                      refused = 0, withoutBase = 0, withModel = 0, withoutModel = 0, wrongGame = 0;
        const auto t0 = Clock::now();
        for (const sdkgame::WorldId id : chosen)
        {
            sdkgame::WorldCell cell;
            std::string error;
            if (!game_skyrimse::loadNeutralCell(view, id, cell, error))
            {
                ++cellsFailed;
                continue;
            }
            ++cellsLoaded;
            for (const sdkgame::WorldObject& o : cell.objects)
            {
                ++objects;
                withoutBase += o.base == sdkgame::kNoWorldId ? 1 : 0;
                const auto r = store.addFromObject("skyrimse", o);
                switch (r.mStatus)
                {
                    case Status::Added:
                        ++added;
                        break;
                    case Status::Existing:
                        ++existing;
                        break;
                    case Status::Conflict:
                        ++conflicts;
                        break;
                    case Status::Refused:
                        ++refused;
                        break;
                }
            }
        }
        const double seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        for (const MWWorld::NeutralBase* b : store.all())
        {
            (b->mModel.empty() ? withoutModel : withModel) += 1;
            wrongGame += b->mGameId == "skyrimse" ? 0 : 1;
        }
        std::cout << "NEUTRALBASE-REAL interiors " << interiors << " cells loaded " << cellsLoaded << " failed "
                  << cellsFailed << " objects " << objects << " without base " << withoutBase << '\n'
                  << "NEUTRALBASE-REAL records " << store.getSize() << " added " << added << " existing " << existing
                  << " conflicts " << conflicts << " (store " << store.conflicts() << ") refused " << refused << '\n'
                  << "NEUTRALBASE-REAL records with a model " << withModel << " without " << withoutModel
                  << " sharing ratio " << (store.getSize() == 0 ? 0.0 : double(objects) / double(store.getSize()))
                  << " seconds " << seconds << '\n';

        EXPECT_GT(cellsLoaded, 0u);
        EXPECT_EQ(cellsFailed, 0u);
        EXPECT_EQ(added, store.getSize());
        EXPECT_EQ(refused, 0u);
        EXPECT_EQ(conflicts, 0u) << "one base form arrived with two different descriptions";
        EXPECT_EQ(wrongGame, 0u);
        EXPECT_EQ(added + existing + conflicts + refused, objects);
    }
}
