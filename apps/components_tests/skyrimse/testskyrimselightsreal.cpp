// Task 0113 stage N3a-5: the aggregate check of Skyrim SE lights and interior lighting on the user's own masters. It
// prints counts only (lines starting SKYRIMSE-LIGHTS), never a cell, record or mod name, and writes nothing. Skipped
// when the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the game's five masters are loaded in order
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/world_model.hpp>

#include <gtest/gtest.h>

#include <algorithm>
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

    TEST(SkyrimLightsRealData, everyInteriorOfTheGamesMastersLoadsWithItsLightsAndLighting)
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

        std::uint64_t interiors = 0, failed = 0, withLighting = 0, lightObjects = 0, lightsWithModel = 0,
                      lightKindWithoutLight = 0, lightOnNonLightKind = 0, offByDefault = 0, negative = 0;
        std::uint64_t ambientAllZero = 0, fogOrderBad = 0;
        std::map<int, std::uint64_t> animations;
        std::set<sdkgame::WorldId> lightBases;
        double radiusMin = 1e30, radiusMax = 0, fadeMin = 1e30, fadeMax = 0;
        std::uint32_t flagUnion = 0;
        for (const sdkgame::WorldCellInfo& info : cells)
        {
            if (!info.interior)
                continue;
            ++interiors;
            sdkgame::WorldCell cell;
            std::string error;
            if (!loadNeutralCell(view, info.id, cell, error))
            {
                ++failed;
                continue;
            }
            if (cell.lighting)
            {
                ++withLighting;
                const auto& a = cell.lighting->ambient;
                ambientAllZero += (a[0] == 0 && a[1] == 0 && a[2] == 0) ? 1 : 0;
                fogOrderBad += (cell.lighting->fogFar < cell.lighting->fogNear) ? 1 : 0;
            }
            for (const sdkgame::WorldObject& o : cell.objects)
            {
                if (o.kind == sdkgame::ObjectKind::Light)
                {
                    ++lightObjects;
                    lightsWithModel += o.modelPath.empty() ? 0 : 1;
                    lightKindWithoutLight += o.light ? 0 : 1;
                }
                else if (o.light)
                    ++lightOnNonLightKind;
                if (!o.light)
                    continue;
                lightBases.insert(o.base);
                ++animations[static_cast<int>(o.light->animation)];
                offByDefault += o.light->offByDefault;
                negative += o.light->negative;
                radiusMin = std::min(radiusMin, o.light->radius);
                radiusMax = std::max(radiusMax, o.light->radius);
                fadeMin = std::min(fadeMin, o.light->fade);
                fadeMax = std::max(fadeMax, o.light->fade);
                flagUnion |= o.light->flags;
            }
        }
        const ComponentStats& stats = view.stats().mComponents;
        std::cout << "SKYRIMSE-LIGHTS interiors " << interiors << " failed " << failed << " withLighting "
                  << withLighting << " ambientAllZero " << ambientAllZero << " fogFarBeforeNear " << fogOrderBad
                  << " lightObjects " << lightObjects << " withModel " << lightsWithModel << " lightKindWithoutLight "
                  << lightKindWithoutLight << " lightOnNonLightKind " << lightOnNonLightKind << " distinctLightBases "
                  << lightBases.size() << " offByDefault " << offByDefault << " negative " << negative << " radius "
                  << radiusMin << ".." << radiusMax << " fade " << fadeMin << ".." << fadeMax << " flagUnion 0x"
                  << std::hex << flagUnion << std::dec << " dropped " << stats.mLightsDropped << " fadesReset "
                  << stats.mLightFadesReset;
        for (const auto& [animation, count] : animations)
            std::cout << " anim" << animation << "=" << count;
        std::cout << std::endl;

        EXPECT_EQ(failed, 0u);
        EXPECT_EQ(lightOnNonLightKind, 0u);
        EXPECT_EQ(lightKindWithoutLight, 0u);
        EXPECT_EQ(stats.mLightsDropped, 0u);
        EXPECT_GT(lightObjects, 0u);
        EXPECT_GT(withLighting, 0u);
    }
}
