// Task 0221: the harvest data of the five Skyrim SE masters (aggregates only: lines SKYRIMSE-HARVEST). Counts the FLOR and
// TREE records that carry an ingredient, the form types the ingredient link points at, the seasonal chances, the verbs
// and the placed references of those bases; with OPENEDEN_HARVEST_NEAR=<world editor id> it also lists the nearest
// placements to Whiterun's gate for the in-game check. Skipped without OPENEDEN_SKYRIMSE_DATA.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/inventory.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
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
    using namespace game_skyrimse;

    const std::vector<std::string>& masters()
    {
        static const std::vector<std::string> names
            = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
        return names;
    }

    TEST(SkyrimHarvestRealData, theMastersHarvestableBasesDecodeAndTheirIngredientsResolve)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        for (const std::string& n : masters())
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";

        LoadOrder order;
        std::string err;
        for (const std::string& n : masters())
            ASSERT_TRUE(order.add(n, false, err)) << err;

        std::map<FormId, const ModelRecord*> harvestable; // later plugin wins
        std::vector<ParsedPlugin> plugins(masters().size());
        std::uint64_t odd = 0, oddHarvest = 0;
        for (std::size_t i = 0; i < masters().size(); ++i)
        {
            Content::Error error;
            ASSERT_TRUE(parseRecordsFile(data / masters()[i], order, ParseLimits(), plugins[i], error))
                << error.describe();
            odd += plugins[i].mOddSubrecords;
            for (const auto& [key, count] : plugins[i].mOddBy)
                if (key.find("PFIG") != std::string::npos || key.find("PFPC") != std::string::npos
                    || key.find("RNAM") != std::string::npos)
                    oddHarvest += count;
        }
                std::map<std::string, std::uint64_t> verbs;
        std::uint64_t flor = 0, tree = 0, withIngredient = 0, nullIngredient = 0, noChances = 0, allZero = 0,
                      someZero = 0, ingr = 0, lvli = 0, otherForm = 0, withVerb = 0;
        inventory::ItemTable table;
        for (std::size_t i = 0; i < masters().size(); ++i)
        {
            Content::Error error;
            const auto opened = Content::Plugin::openFile(data / masters()[i], error);
            ASSERT_NE(opened, nullptr) << error.describe();
            inventory::InventoryRecords records;
            ASSERT_TRUE(inventory::parseInventoryRecords(*opened, order, inventory::InventoryLimits(), records));
            table.add(std::move(records));
            table.addLeveledLists(plugins[i].mLeveledLists);
        }
        for (const ParsedPlugin& plugin : plugins)
            for (const ModelRecord& model : plugin.mModels)
            {
                if (model.mType != Content::fourCC("FLOR") && model.mType != Content::fourCC("TREE"))
                    continue;
                (model.mType == Content::fourCC("FLOR") ? flor : tree)++;
                if (!model.mHarvest)
                    continue;
                const HarvestData& h = *model.mHarvest;
                if (h.mIngredient == kNullFormId)
                {
                    ++nullIngredient;
                    continue;
                }
                ++withIngredient;
                harvestable[model.mCommon.mId] = &model;
                if (!h.mHasChances)
                    ++noChances;
                else if (h.mChances == std::array<std::uint8_t, 4>{ 0, 0, 0, 0 })
                    ++allZero;
                else if (h.mChances[0] == 0 || h.mChances[1] == 0 || h.mChances[2] == 0 || h.mChances[3] == 0)
                    ++someZero;
                if (table.item(h.mIngredient) != nullptr)
                    ++ingr;
                else if (table.leveled(h.mIngredient) != nullptr)
                    ++lvli;
                else
                    ++otherForm;
                if (h.mActivateText.mPresent)
                {
                    ++withVerb;
                    ++verbs[h.mActivateText.mLocalized ? std::string("<localised>") : h.mActivateText.mText];
                }
            }
        std::cout << "SKYRIMSE-HARVEST FLOR " << flor << " TREE " << tree << " with ingredient " << withIngredient
                  << " null ingredient " << nullIngredient << " ingredient is an item " << ingr << " leveled "
                  << lvli << " other " << otherForm << " no chances " << noChances << " all four zero " << allZero
                  << " some zero " << someZero << " verbs present " << withVerb << " odd " << odd << "\n";
        for (const auto& [verb, count] : verbs)
            std::cout << "SKYRIMSE-HARVEST verb [" << verb << "] " << count << "\n";
        EXPECT_EQ(oddHarvest, 0u) << "PFIG, PFPC and RNAM have the sizes the header says";
        EXPECT_GT(withIngredient, 50u);
        EXPECT_EQ(otherForm, 0u) << "every PFIG names an item or a leveled list";

        // The placements of harvestable bases and the ones nearest to Whiterun's gate (Tamriel, world 0x3C, cells 4..8 by -4..0).
        std::uint64_t placed = 0, placedFlor = 0, placedTree = 0;
        struct Near
        {
            double mDistance;
            FormId mRef;
            FormId mBase;
            std::string mEditorId;
            float x, y, z;
        };
        std::vector<Near> near;
        for (const ParsedPlugin& plugin : plugins)
            for (const CellRecord& cell : plugin.mCells)
            {
                std::vector<const PlacedRef*> refs;
                for (const auto* list : { &cell.mPersistent, &cell.mTemporary, &cell.mVisibleDistant, &cell.mOther })
                    for (const PlacedRef& ref : *list)
                        refs.push_back(&ref);
                for (const PlacedRef* ref : refs)
                {
                    const auto it = harvestable.find(ref->mBase);
                    if (it == harvestable.end())
                        continue;
                    ++placed;
                    (it->second->mType == Content::fourCC("FLOR") ? placedFlor : placedTree)++;
                    if (ref->mHasTransform && cell.mWorld == 0x0000003C && cell.mGrid && cell.mGrid->x >= 4 && cell.mGrid->x <= 8
                        && cell.mGrid->y >= -4 && cell.mGrid->y <= 0)
                    {
                        const double dx = ref->mPosition.x - 25000.0;
                        const double dy = ref->mPosition.y - (-8000.0);
                        near.push_back({ std::sqrt(dx * dx + dy * dy), ref->mCommon.mId, ref->mBase,
                            it->second->mCommon.mEditorId, ref->mPosition.x, ref->mPosition.y, ref->mPosition.z });
                    }
                }
            }
        std::cout << "SKYRIMSE-HARVEST placements " << placed << " flora " << placedFlor << " trees " << placedTree
                  << " in the Whiterun cells " << near.size() << "\n";
        std::sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.mDistance < b.mDistance; });
        for (std::size_t i = 0; i < near.size() && i < 6; ++i)
            std::cout << "SKYRIMSE-HARVEST near ref " << std::hex << near[i].mRef << " base " << near[i].mBase << std::dec
                      << " " << near[i].mEditorId << " at " << near[i].x << "," << near[i].y << "," << near[i].z
                      << " d " << near[i].mDistance << "\n";
        EXPECT_GT(placed, 100u);
    }
}
