// Task 0224: the mining and woodcutting data of the five Skyrim SE masters (aggregates only: lines SKYRIMSE-MINING).
// Counts the ACTI with MineOreScript and the FURN with ResourceFurnitureScript, what their properties name, the
// message records, and the placements nearest to Whiterun's gate for the in-game check. Skipped without
// OPENEDEN_SKYRIMSE_DATA.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/mining.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
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
    using namespace game_skyrimse;

    const std::vector<std::string>& masters()
    {
        static const std::vector<std::string> names
            = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
        return names;
    }

    TEST(SkyrimMiningRealData, theMastersResourceBasesDecodeAndTheirPropertiesNameWhatTheScriptsUse)
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
        std::vector<ParsedPlugin> plugins(masters().size());
        for (std::size_t i = 0; i < masters().size(); ++i)
        {
            Content::Error error;
            ASSERT_TRUE(parseRecordsFile(data / masters()[i], order, ParseLimits(), plugins[i], error))
                << error.describe();
        }
        std::map<FormId, mining::ResourceInfo> resources; // later plugin wins
        std::map<FormId, std::string> names;
        std::map<FormId, std::uint64_t> oreItems;
        std::uint64_t ore = 0, wood = 0, withTools = 0, withGems = 0, withFailure = 0, withDepleted = 0;
        std::uint64_t nonDefaultTotal = 0, nonDefaultStrikes = 0, withActivateText = 0;
        std::uint64_t mesg = 0, mesgWithText = 0, mesgLocalized = 0;
        for (const ParsedPlugin& plugin : plugins)
            for (const ModelRecord& model : plugin.mModels)
            {
                if (model.mType == Content::fourCC("MESG"))
                {
                    ++mesg;
                    mesgWithText += model.mDescription.mPresent ? 1 : 0;
                    mesgLocalized += model.mDescription.mLocalized ? 1 : 0;
                    continue;
                }
                if (model.mType != Content::fourCC("ACTI") && model.mType != Content::fourCC("FURN"))
                    continue;
                const mining::ResourceInfo info = mining::resourceFromScripts(model.mScripts);
                if (info.mKind == mining::ResourceKind::None)
                    continue;
                resources[model.mCommon.mId] = info;
                names[model.mCommon.mId] = model.mCommon.mEditorId;
                if (info.mKind == mining::ResourceKind::Ore)
                {
                    ++ore;
                    ++oreItems[info.mItem];
                    nonDefaultTotal += info.mTotal != mining::kDefaultTotal ? 1 : 0;
                    nonDefaultStrikes += info.mStrikes != mining::kDefaultStrikes ? 1 : 0;
                    withActivateText += model.mActivateText.mPresent ? 1 : 0;
                }
                else
                    ++wood;
                withTools += info.mToolList != kNullFormId ? 1 : 0;
                withGems += info.mGems != kNullFormId ? 1 : 0;
                withFailure += info.mFailureMessage != kNullFormId ? 1 : 0;
                withDepleted += info.mDepletedMessage != kNullFormId ? 1 : 0;
            }
        std::cout << "SKYRIMSE-MINING ore bases " << ore << " wood bases " << wood << " with a tool list " << withTools
                  << " with gems " << withGems << " with failure message " << withFailure << " with depleted message "
                  << withDepleted << " total != 3: " << nonDefaultTotal << " strikes != 1: " << nonDefaultStrikes
                  << " ore with an activate text " << withActivateText << " distinct ore items " << oreItems.size()
                  << " MESG " << mesg << " with DESC " << mesgWithText << " localised " << mesgLocalized << "\n";
        EXPECT_GT(ore, 5u);
        EXPECT_GE(wood, 1u);

        std::uint64_t placedOre = 0, placedWood = 0;
        struct Near
        {
            double mDistance;
            FormId mRef;
            FormId mBase;
            std::string mEditorId;
            bool mOre;
            float x, y, z;
        };
        std::vector<Near> near;
        for (const ParsedPlugin& plugin : plugins)
            for (const CellRecord& cell : plugin.mCells)
                for (const auto* list : { &cell.mPersistent, &cell.mTemporary, &cell.mVisibleDistant, &cell.mOther })
                    for (const PlacedRef& ref : *list)
                    {
                        const auto it = resources.find(ref.mBase);
                        if (it == resources.end())
                            continue;
                        const bool isOre = it->second.mKind == mining::ResourceKind::Ore;
                        (isOre ? placedOre : placedWood)++;
                        if (ref.mHasTransform && cell.mWorld == 0x0000003C)
                        {
                            const double dx = ref.mPosition.x - 25000.0;
                            const double dy = ref.mPosition.y - (-8000.0);
                            near.push_back({ std::sqrt(dx * dx + dy * dy), ref.mCommon.mId, ref.mBase,
                                names[ref.mBase], isOre, ref.mPosition.x, ref.mPosition.y, ref.mPosition.z });
                        }
                    }
        std::cout << "SKYRIMSE-MINING placements ore " << placedOre << " wood " << placedWood << " in Tamriel "
                  << near.size() << "\n";
        std::sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.mDistance < b.mDistance; });
        for (std::size_t i = 0; i < near.size() && i < 8; ++i)
            std::cout << "SKYRIMSE-MINING near ref " << std::hex << near[i].mRef << " base " << near[i].mBase
                      << std::dec << " " << near[i].mEditorId << (near[i].mOre ? " (ore)" : " (wood)") << " at "
                      << near[i].x << "," << near[i].y << "," << near[i].z << " d " << near[i].mDistance << "\n";
        EXPECT_GT(placedOre, 20u);
    }
}
