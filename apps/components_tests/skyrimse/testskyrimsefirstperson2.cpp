// Task 0247: the first-person arms of every playable race on the installed data (a gated read-only run; it prints the
// part models of each race and sex and asserts that the race has arms). Skipped without OPENEDEN_SKYRIMSE_DATA.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/player_body.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using openeden::sdk::game::ActorAppearance;

    TEST(SkyrimFirstPersonRacesRealData, everyPlayableRaceHasFirstPersonArmsForBothSexes)
    {
        namespace fs = std::filesystem;
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        for (const std::string& n : names)
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            ASSERT_TRUE(order.add(n, false, err)) << err;
        ActorIndex index;
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            ActorRecords actors;
            EXPECT_TRUE(parseActorRecords(*plugin, order, ActorLimits(), actors));
            index.add(actors);
        }
        // the ten player races of the data (RACE 0x13740 to 0x13749, VERIFIED by task 0225)
        for (FormId race = 0x13740; race <= 0x13749; ++race)
            for (const bool female : { false, true })
            {
                PlayerBodyRequest request;
                request.mRace = race;
                request.mFemale = female;
                ActorAppearance a;
                std::string error;
                const bool ok = makeFirstPersonAppearance(index, request, a, error);
                EXPECT_TRUE(ok) << std::hex << race << ": " << error;
                std::cout << "SKYRIMSE-FIRSTPERSON-RACE " << std::hex << race << std::dec
                          << (female ? " female" : " male") << " parts " << a.parts.size();
                for (const auto& part : a.parts)
                    std::cout << " [" << part.modelPath << "]";
                std::cout << "\n";
            }
    }
}
