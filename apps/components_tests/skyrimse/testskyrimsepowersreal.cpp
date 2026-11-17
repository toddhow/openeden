// Task 0228: the powers of the ten playable races on the player's own Skyrim SE masters, and the text tags of their
// dialogue (the tag kinds the pack fills and the ones it leaves out). Prints counts and editor ids only (lines starting
// SKYRIMSE-POWERS), never a response text. Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
#include <game_skyrimse/character.hpp>
#include <game_skyrimse/character_service.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    TEST(SkyrimPowersRealData, thePowersOfTheTenPlayerRacesAndWhatTheMagicRuntimeDoesWithThem)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        std::vector<fs::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
        {
            if (!fs::exists(data / name))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
            paths.push_back(data / name);
        }
        DataFolderFiles files(data);
        SkyrimWorldView view({}, &files);
        std::vector<std::string> problems;
        ASSERT_GT(view.load(paths, problems), 0u);
        for (int i = 0; i < 600 && !view.combatReady(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_TRUE(view.combatReady());
        character::Access access;
        ASSERT_TRUE(view.characterAccess(access));
        const auto races = character::playableRaces(*access.mIndex, access.mText);
        ASSERT_EQ(races.size(), 10u);
        SkyrimCombat combat;
        combat.setData(&view);
        std::size_t works = 0, status = 0, none = 0, total = 0;
        for (const character::RaceOption& race : races)
        {
            const auto powers = combat.racePowers(race.mAbilities);
            std::cout << "SKYRIMSE-POWERS " << race.mEditorId << ": " << race.mAbilities.size() << " spells, "
                      << powers.size() << " powers\n";
            for (const SkyrimCombat::RacePower& power : powers)
            {
                const SpellRecord* spell = view.spellById(power.mId);
                ASSERT_NE(spell, nullptr);
                std::string archetypes;
                for (const SpellEffect& e : spell->mEffects)
                {
                    const MagicEffectRecord* fx = view.effectById(e.mEffect);
                    archetypes
                        += (fx != nullptr ? std::to_string(fx->mArchetype) + "/" + std::to_string(fx->mPrimaryValue)
                                          : std::string("?"))
                        + (e.mConditions != 0 ? "c " : " ");
                }
                const char* support = power.mSupport == SkyrimCombat::PowerSupport::Works
                    ? "works"
                    : (power.mSupport == SkyrimCombat::PowerSupport::StatusOnly ? "status" : "none");
                std::cout << "SKYRIMSE-POWERS   " << power.mName << " type " << spell->mType << " delivery "
                          << spell->mDelivery << " cost " << spell->mCost << " effects(archetype/value) " << archetypes
                          << "-> " << support << "\n";
                ++total;
                works += power.mSupport == SkyrimCombat::PowerSupport::Works;
                status += power.mSupport == SkyrimCombat::PowerSupport::StatusOnly;
                none += power.mSupport == SkyrimCombat::PowerSupport::Unsupported;
            }
        }
        std::cout << "SKYRIMSE-POWERS summary: " << total << " powers, works " << works << ", status only " << status
                  << ", unsupported " << none << "\n";
        EXPECT_GT(total, 0u);
    }
}
