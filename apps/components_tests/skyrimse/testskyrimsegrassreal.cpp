// Task 0153: the gated real-data survey behind Skyrim SE grass (docs/tasks/0153-skyrim-grass.md). Not run by default.
// With OPENEDEN_SKYRIMSE_DATA set to the game's Data folder it reads GRAS and LTEX of the five masters and prints
// AGGREGATE numbers only (lines starting SKYGRASS-SURVEY): the sizes of GRAS DATA, the ranges of its fields, the units
// from water types and flags in use, how many ground textures name grass and whether every GNAM entry is a GRAS, and
// what the placement makes of the plains round Whiterun. No path or record name is printed.
#include <game_skyrimse/terrain_source.hpp>
#include <game_skyrimse/worldspace.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
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
    namespace sdk = openeden::sdk::game;
    using namespace game_skyrimse;

    const char* const kMasters[] = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };

    void line(const std::string& text)
    {
        std::cout << "SKYGRASS-SURVEY " << text << std::endl;
    }

    TEST(SkyrimGrassReal, theFiveMastersOnTheInstalledData)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == 0)
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);

        LoadOrder order;
        std::string err;
        for (const char* name : kMasters)
            ASSERT_TRUE(order.add(name, false, err)) << err;

        // 1. The raw records: DATA sizes and field ranges.
        std::uint64_t records = 0, withData = 0, odd = 0, withModel = 0;
        std::map<std::uint32_t, std::uint64_t> typeCount;
        std::map<int, std::uint64_t> flagCount;
        std::uint64_t densityMin = 255, densityMax = 0, slopeMinMax = 0, slopeMaxMin = 255;
        float posMax = 0, heightMax = 0, colourMax = 0, waveMin = 1e9f, waveMax = 0;
        std::uint16_t unitsMax = 0;
        std::set<FormId> grassIds;
        for (const char* name : kMasters)
        {
            ExteriorData out;
            Content::Error error;
            ASSERT_TRUE(parseExteriorFile(data / name, order, ExteriorLimits(), out, error)) << error.describe();
            records += out.mGrass.size();
            for (const GrassRecord& g : out.mGrass)
            {
                grassIds.insert(g.mCommon.mId);
                withModel += g.mModel.empty() ? 0 : 1;
                if (!g.mHasData)
                    continue;
                ++withData;
                densityMin = std::min<std::uint64_t>(densityMin, g.mDensity);
                densityMax = std::max<std::uint64_t>(densityMax, g.mDensity);
                slopeMinMax = std::max<std::uint64_t>(slopeMinMax, g.mMinSlope);
                slopeMaxMin = std::min<std::uint64_t>(slopeMaxMin, g.mMaxSlope);
                ++typeCount[g.mUnitsFromWaterType];
                ++flagCount[g.mFlags];
                posMax = std::max(posMax, g.mPositionRange);
                heightMax = std::max(heightMax, g.mHeightRange);
                colourMax = std::max(colourMax, g.mColorRange);
                waveMin = std::min(waveMin, g.mWavePeriod);
                waveMax = std::max(waveMax, g.mWavePeriod);
                unitsMax = std::max(unitsMax, g.mUnitsFromWater);
            }
            for (const auto& [sub, count] : out.mNotes.mOddBy)
                if (sub.rfind("GRAS", 0) == 0)
                    odd += count;
        }
        line("GRAS records " + std::to_string(records) + " with DATA " + std::to_string(withData) + " with model "
            + std::to_string(withModel) + " odd subrecords " + std::to_string(odd));
        line("density " + std::to_string(densityMin) + ".." + std::to_string(densityMax) + " largest min slope "
            + std::to_string(slopeMinMax) + " smallest max slope " + std::to_string(slopeMaxMin)
            + " largest units from water " + std::to_string(unitsMax));
        line("position range max " + std::to_string(posMax) + " height range max " + std::to_string(heightMax)
            + " colour range max " + std::to_string(colourMax) + " wave period " + std::to_string(waveMin) + ".."
            + std::to_string(waveMax));
        for (const auto& [type, count] : typeCount)
            line("units from water type " + std::to_string(type) + ": " + std::to_string(count));
        for (const auto& [flags, count] : flagCount)
            line("flags 0x" + std::to_string(flags) + ": " + std::to_string(count));
        EXPECT_GT(records, 50u);
        EXPECT_EQ(odd, 0u);

        // 2. The merged index: which ground textures name grass, and do the entries resolve?
        SkyrimTerrainIndex index;
        for (const char* name : kMasters)
        {
            std::string error;
            ASSERT_TRUE(index.addPluginFile(data / name, order, error)) << error;
        }
        const GrassTables& tables = index.grassTables();
        std::uint64_t entries = 0, unresolved = 0, maxPerTexture = 0;
        for (const auto& [texture, list] : tables.mGrassOfTexture)
        {
            entries += list.size();
            maxPerTexture = std::max<std::uint64_t>(maxPerTexture, list.size());
            for (const FormId id : list)
                unresolved += tables.mKinds.count(id) == 0 ? 1 : 0;
        }
        line("merged GRAS " + std::to_string(tables.mKinds.size()) + " ground textures with grass "
            + std::to_string(tables.mGrassOfTexture.size()) + " of " + std::to_string(index.stats().mGroundTextures)
            + " GNAM entries " + std::to_string(entries) + " not a GRAS " + std::to_string(unresolved)
            + " most on one texture " + std::to_string(maxPerTexture));
        EXPECT_EQ(unresolved, 0u);

        // 3. The placement round Whiterun (WhiterunWorld borrows Tamriel's land).
        std::vector<sdk::WorldSpaceInfo> worlds;
        index.listWorlds(worlds);
        sdk::WorldId whiterun = 0;
        for (const auto& w : worlds)
            if (w.editorId == "WhiterunWorld")
                whiterun = w.id;
        ASSERT_NE(whiterun, 0u);
        std::uint64_t cellsWithGrass = 0, cellsAsked = 0, total = 0, requested = 0, capped = 0, maxCell = 0;
        double milliseconds = 0.0, worst = 0.0;
        for (int x = 2; x <= 9; ++x)
            for (int y = -4; y <= 3; ++y)
            {
                sdk::GrassCell cell;
                std::string error;
                const auto start = std::chrono::steady_clock::now();
                const bool ok = index.grassForCell(whiterun, x, y, cell, error);
                const double ms
                    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                if (!ok)
                    continue;
                ++cellsAsked;
                milliseconds += ms;
                worst = std::max(worst, ms);
                total += cell.instances.size();
                requested += cell.requested;
                maxCell = std::max<std::uint64_t>(maxCell, cell.instances.size());
                capped += cell.requested > cell.instances.size() * 11 / 10 ? 1 : 0;
                cellsWithGrass += cell.instances.empty() ? 0 : 1;
            }
        line("Whiterun 8x8 block: cells with land " + std::to_string(cellsAsked) + " with grass "
            + std::to_string(cellsWithGrass) + " tufts " + std::to_string(total) + " requested "
            + std::to_string(requested) + " largest cell " + std::to_string(maxCell) + " cells over the cap "
            + std::to_string(capped) + " mean ms "
            + std::to_string(cellsAsked ? milliseconds / static_cast<double>(cellsAsked) : 0.0) + " worst ms "
            + std::to_string(worst));
        EXPECT_GT(cellsAsked, 0u);
        EXPECT_GT(cellsWithGrass, 0u);
    }
}
