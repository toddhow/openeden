// Task 0257: the IMAD records on the player's own Skyrim SE masters (Night Eye and the others).
// Gated: skipped unless OPENEDEN_SKYRIMSE_DATA names the game's Data folder. Prints aggregates (lines SKYRIMSE-IMAD)
// and checks the numbers the Night Eye view reads: the winning records are Update.esm's, a milder version than
// Skyrim.esm's.
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/image_space_mod.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    bool loadAll(CombatIndex& index, std::uint64_t& imadSeen, std::uint64_t& odd, std::string& skip)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
        {
            skip = "OPENEDEN_SKYRIMSE_DATA is not set";
            return false;
        }
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            if (!fs::exists(data / n) || !order.add(n, false, err))
            {
                skip = "the five official masters are not all in the Data folder";
                return false;
            }
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            if (file == nullptr)
            {
                skip = "cannot open " + n;
                return false;
            }
            CombatRecords records;
            (void)parseCombatRecords(*file, order, ExteriorLimits(), records);
            imadSeen += records.mImageModifiers.size();
            odd += records.mNotes.mOddSubrecords;
            index.add(records);
        }
        return true;
    }

    TEST(SkyrimImageModReal, theNightEyeRecordsAreTheWinningOnesAndHaveTheTracksTheCoreReads)
    {
        CombatIndex index;
        std::uint64_t seen = 0, odd = 0;
        std::string skip;
        if (!loadAll(index, seen, odd, skip))
            GTEST_SKIP() << skip;
        std::cout << "SKYRIMSE-IMAD records decoded " << seen << " odd subrecords (all types) " << odd << "\n";
        EXPECT_GE(seen, 225u); // 225 distinct, more with the overrides

        const ImageModifierRecord* intro = index.imageModifierByEditorId("NightEyeImodIntro");
        const ImageModifierRecord* outro = index.imageModifierByEditorId("NightEyeImodOutro");
        ASSERT_NE(intro, nullptr);
        ASSERT_NE(outro, nullptr);
        EXPECT_NE(index.imageModifierByEditorId("NightEyeImod"), nullptr);
        EXPECT_TRUE(intro->mHasData);
        EXPECT_TRUE(intro->mAnimatable);
        EXPECT_TRUE(outro->mAnimatable);

        // Update.esm overrides both with a 1 second version (Skyrim.esm's intro is 4 seconds and stronger).
        EXPECT_FLOAT_EQ(intro->mDuration, 1.f);
        EXPECT_FLOAT_EQ(outro->mDuration, 1.f);
        const imagespace::State held = imagespace::sample(*intro, 1.f);
        std::cout << "SKYRIMSE-IMAD night eye held: sat x" << held.saturationMult << " bright +" << held.brightnessAdd
                  << " contrast +" << held.contrastAdd << " tint " << held.tint[0] << "," << held.tint[1] << ","
                  << held.tint[2] << " amount " << held.tintAmount << "\n";
        EXPECT_NEAR(held.saturationMult, 0.8f, 1e-3f);
        EXPECT_NEAR(held.brightnessAdd, 0.3f, 1e-3f);
        EXPECT_NEAR(held.contrastAdd, 0.1f, 1e-3f);
        EXPECT_NEAR(held.tintAmount, 0.502f, 1e-3f);
        EXPECT_NEAR(held.tint[0], 0.592f, 1e-3f);
        EXPECT_NEAR(held.tint[2], 0.875f, 1e-3f);
        // The intro starts from nothing, the outro ends at nothing.
        const imagespace::State start = imagespace::sample(*intro, 0.f);
        EXPECT_NEAR(start.tintAmount, 0.f, 1e-6f);
        EXPECT_NEAR(start.brightnessAdd, 0.f, 1e-6f);
        const imagespace::State end = imagespace::sample(*outro, 1.f);
        EXPECT_NEAR(end.tintAmount, 0.f, 1e-6f);
        EXPECT_NEAR(end.brightnessAdd, 0.f, 1e-6f);
    }
}
