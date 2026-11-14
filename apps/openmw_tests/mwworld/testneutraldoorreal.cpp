// Task 0115 (stage N4 engine half): the aggregate check of the doors on the user's own Skyrim SE masters. For every
// interior cell it takes the teleport doors the pack hands over and counts, through the engine's own decision
// (MWWorld::decideNeutralDoor), what activating each would do: teleport, refuse as locked, refuse as an exterior,
// lead nowhere, and whether the destination door leads back and the arrival lies near the destination's objects.
// Prints counts only (lines starting NEUTRALDOOR-REAL), never an id, a name or a position. Writes nothing; skipped when
// the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the game's five masters are loaded in order
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "apps/openmw/mwworld/neutraldoor.hpp"

namespace
{
    namespace fs = std::filesystem;
    namespace sdkgame = openeden::sdk::game;
    using MWWorld::NeutralDestination;
    using MWWorld::NeutralDoorOutcome;

    // A sanity bound, not a Skyrim rule: an arrival further than this from every object of the destination cell
    // would be a sign that the position was read wrongly.
    constexpr double kArrivalMargin = 4000.0;

    TEST(NeutralDoorRealData, every_interior_teleport_door_of_the_masters_resolves_or_is_refused)
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

        std::vector<sdkgame::WorldCellInfo> infos;
        view.listCells(0, view.cellCount(), infos);
        std::map<sdkgame::WorldId, bool> interior; // cell id -> interior
        for (const sdkgame::WorldCellInfo& c : infos)
            interior[c.id] = c.interior;

        std::map<sdkgame::WorldId, sdkgame::WorldCell> built; // interior cells built once
        auto build = [&](sdkgame::WorldId id) -> const sdkgame::WorldCell* {
            const auto found = built.find(id);
            if (found != built.end())
                return &found->second;
            sdkgame::WorldCell cell;
            std::string error;
            if (!game_skyrimse::loadNeutralCell(view, id, cell, error))
                return nullptr;
            return &built.emplace(id, std::move(cell)).first->second;
        };

        std::uint64_t interiors = 0, doors = 0, teleports = 0, noTeleport = 0, locked = 0, nowhere = 0, toInterior = 0,
                      toExterior = 0, unbuildable = 0, noReturn = 0, farArrival = 0, teleportOutcome = 0;
        for (const sdkgame::WorldCellInfo& c : infos)
        {
            if (!c.interior)
                continue;
            ++interiors;
            const sdkgame::WorldCell* cell = build(c.id);
            if (cell == nullptr)
                continue;
            for (const sdkgame::WorldObject& o : cell->objects)
            {
                if (o.kind != sdkgame::ObjectKind::Door)
                    continue;
                ++doors;
                if (!o.teleport)
                {
                    ++noTeleport;
                    continue;
                }
                ++teleports;
                locked += (o.lock && o.lock->locked) ? 1 : 0;
                NeutralDestination seen = NeutralDestination::Unavailable;
                const auto d = MWWorld::decideNeutralDoor(o, true, true, [&](sdkgame::WorldId dest) {
                    const auto known = interior.find(dest);
                    if (known == interior.end())
                        return NeutralDestination::Unavailable;
                    if (!known->second)
                        return seen = NeutralDestination::Exterior;
                    return seen
                        = (build(dest) != nullptr ? NeutralDestination::Interior : NeutralDestination::Unavailable);
                });
                switch (d.mOutcome)
                {
                    case NeutralDoorOutcome::Teleport:
                    {
                        ++teleportOutcome;
                        ++toInterior;
                        const sdkgame::WorldCell* dest = build(d.mDestinationCell);
                        ASSERT_NE(dest, nullptr);
                        // The destination door leads back to this cell.
                        bool back = false;
                        for (const sdkgame::WorldObject& other : dest->objects)
                            if (other.id == o.teleport->destinationDoor && other.teleport
                                && other.teleport->destinationCell == c.id)
                                back = true;
                        noReturn += back ? 0 : 1;
                        bool near = dest->objects.empty();
                        for (const sdkgame::WorldObject& other : dest->objects)
                        {
                            const double dx = other.transform.position.x - o.teleport->destination.position.x;
                            const double dy = other.transform.position.y - o.teleport->destination.position.y;
                            const double dz = other.transform.position.z - o.teleport->destination.position.z;
                            if (std::sqrt(dx * dx + dy * dy + dz * dz) < kArrivalMargin)
                            {
                                near = true;
                                break;
                            }
                        }
                        farArrival += near ? 0 : 1;
                        break;
                    }
                    case NeutralDoorOutcome::Exterior:
                        ++toExterior;
                        break;
                    case NeutralDoorOutcome::Nowhere:
                        ++nowhere;
                        unbuildable += seen == NeutralDestination::Unavailable ? 1 : 0;
                        break;
                    default:
                        break;
                }
            }
        }
        std::cout << "NEUTRALDOOR-REAL interior cells " << interiors << " doors " << doors << " without teleport "
                  << noTeleport << " with teleport " << teleports << " locked " << locked << '\n'
                  << "NEUTRALDOOR-REAL (locked opened by the aid) to an interior " << toInterior << " to an exterior "
                  << toExterior << " leads nowhere " << nowhere << " of which the cell could not be built "
                  << unbuildable << '\n'
                  << "NEUTRALDOOR-REAL interior destinations: no way back " << noReturn
                  << " arrival far from every object " << farArrival << '\n';

        EXPECT_GT(teleports, 0u);
        EXPECT_EQ(nowhere, 0u) << "a door of an interior has a destination cell the pack cannot resolve";
        EXPECT_EQ(unbuildable, 0u);
        EXPECT_EQ(noReturn, 0u);
        EXPECT_EQ(farArrival, 0u);
    }
}
