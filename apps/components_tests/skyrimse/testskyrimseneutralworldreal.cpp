// Decision 0016, task 0099 stage N0: the aggregate check of the Skyrim SE neutral world adapter on the user's own
// plugins. It prints counts and times only (lines starting SKYRIMSE-NEUTRAL), never a cell, record or mod name, and
// writes nothing. Skipped when the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the game's five masters are loaded in order
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/world_model.hpp>

#include <gtest/gtest.h>

#include <chrono>
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
    using Clock = std::chrono::steady_clock;

    TEST(SkyrimNeutralWorldRealData, theGamesOwnMastersLoadAsNeutralCells)
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
        // Every interior cell and an even sample of about 2,000 exterior cells.
        std::vector<sdkgame::WorldId> chosen;
        std::vector<sdkgame::WorldId> exteriors;
        for (const sdkgame::WorldCellInfo& c : cells)
            (c.interior ? chosen : exteriors).push_back(c.id);
        const std::size_t step = exteriors.size() > 2000 ? exteriors.size() / 2000 : 1;
        for (std::size_t i = 0; i < exteriors.size(); i += step)
            chosen.push_back(exteriors[i]);

        std::uint64_t loaded = 0, failed = 0, objects = 0, dropped = 0, contractObjects = 0, notUnit = 0,
                      notAscending = 0, unresolved = 0, boxes = 0, fromModel = 0, noCollision = 0, payloadMismatch = 0;
        std::map<int, std::uint64_t> kinds;
        // Task 0099 N4: the components the store now fills.
        std::set<sdkgame::WorldId> knownCells;
        for (const sdkgame::WorldCellInfo& c : cells)
            knownCells.insert(c.id);
        std::uint64_t owners = 0, locks = 0, teleports = 0, teleportsWithCell = 0, teleportCellUnknown = 0,
                      teleportNoDoor = 0;
        const auto t0 = Clock::now();
        for (const sdkgame::WorldId id : chosen)
        {
            sdkgame::WorldCellContent content;
            std::string error;
            if (!view.loadCell(id, content, error))
            {
                ++failed;
                continue;
            }
            sdkgame::WorldCell cell;
            if (!loadNeutralCell(view, id, cell, error))
            {
                ++failed;
                continue;
            }
            ++loaded;
            contractObjects += content.objects.size();
            objects += cell.objects.size();
            dropped += cell.droppedObjects;
            unresolved += cell.unresolvedBases;
            sdkgame::WorldId previous = 0;
            for (const sdkgame::WorldObject& o : cell.objects)
            {
                const auto& q = o.transform.rotation;
                if (std::abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.0) > 1e-9)
                    ++notUnit;
                if (o.id <= previous)
                    ++notAscending;
                previous = o.id;
                ++kinds[static_cast<int>(o.kind)];
                if (o.collision.source == sdkgame::CollisionSource::Box)
                    ++boxes;
                else if (o.collision.source == sdkgame::CollisionSource::FromModel)
                    ++fromModel;
                else
                    ++noCollision;
                if (o.payload.handle != o.base)
                    ++payloadMismatch;
                owners += o.owner ? 1 : 0;
                locks += o.lock ? 1 : 0;
                if (o.teleport)
                {
                    ++teleports;
                    teleportNoDoor += o.teleport->destinationDoor == sdkgame::kNoWorldId ? 1 : 0;
                    if (o.teleport->destinationCell != sdkgame::kNoWorldId)
                    {
                        ++teleportsWithCell;
                        teleportCellUnknown += knownCells.count(o.teleport->destinationCell) == 0 ? 1 : 0;
                    }
                }
            }
        }
        const double seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        std::cout << "SKYRIMSE-NEUTRAL masters: cells loaded " << loaded << " failed " << failed << " objects "
                  << objects << " (contract " << contractObjects << ") dropped " << dropped << " unresolved bases "
                  << unresolved << " not unit quaternions " << notUnit << " out of order " << notAscending
                  << " payload differs " << payloadMismatch << " collision: from model " << fromModel << " box "
                  << boxes << " none " << noCollision << " time " << seconds << " s\n";
        std::cout << "SKYRIMSE-NEUTRAL masters components: owners " << owners << " locks " << locks << " teleports "
                  << teleports << " with a destination cell " << teleportsWithCell
                  << " destination cell not in the cell "
                     "list "
                  << teleportCellUnknown << " without a destination door " << teleportNoDoor << " store stats: dropped "
                  << view.stats().mComponents.mTeleportsDropped << " cells unknown "
                  << view.stats().mComponents.mTeleportCellsUnknown << " owners dropped "
                  << view.stats().mComponents.mOwnersDropped << " keys unresolved "
                  << view.stats().mComponents.mKeysUnresolved << '\n';
        std::cout << "SKYRIMSE-NEUTRAL masters kinds:";
        for (const auto& [kind, count] : kinds)
            std::cout << " " << kind << "=" << count;
        std::cout << "\n";

        EXPECT_EQ(failed, 0u);
        EXPECT_EQ(objects + dropped, contractObjects);
        EXPECT_EQ(notUnit, 0u);
        EXPECT_EQ(notAscending, 0u);
        EXPECT_EQ(payloadMismatch, 0u);
        EXPECT_GT(objects, 0u);
        EXPECT_GT(teleports, 0u);
        EXPECT_EQ(teleportNoDoor, 0u);
        EXPECT_EQ(teleportCellUnknown, 0u);
    }
}
