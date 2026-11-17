// Task 0231: the aggregate survey of the dialogue subtypes (the SNAM of each DIAL) and of the greeting and idle GMSTs
// of the player's own Skyrim SE masters. Prints counts and subtype names only (lines starting SKYRIMSE-BARKS), never a
// response text. Skipped unless OPENEDEN_SKYRIMSE_DATA names the Data folder.
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

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

    TEST(SkyrimBarksRealData, theSubtypesAndTheTimingSettingsOfTheFiveMasters)
    {
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
        DialogueIndex dialogue;
        SettingsTable settings;
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            DialogueRecords dr;
            ASSERT_TRUE(parseDialogueRecords(*plugin, order, DialogueLimits(), dr));
            CombatRecords cr;
            (void)parseCombatRecords(*plugin, order, ExteriorLimits(), cr);
            settings.add(cr);
            for (const GameSetting& g : cr.mSettings)
            {
                const std::string& id = g.mCommon.mEditorId;
                if (g.mNumeric
                    && (id.find("Greeting") != std::string::npos || id.find("Hello") != std::string::npos
                        || id.find("Idle") != std::string::npos || id.find("Bark") != std::string::npos
                        || id.find("Dialogue") != std::string::npos || id.find("Chatter") != std::string::npos
                        || id.find("Speech") != std::string::npos || id.find("Voice") != std::string::npos
                        || id.find("Subtitle") != std::string::npos || id.find("Shout") != std::string::npos))
                    std::cout << "SKYRIMSE-BARKS gmst " << n << " " << id << " = " << g.mValue << "\n";
            }
            dialogue.add(std::move(dr), FormIdResolver(plugin->header(), n, order));
        }
        std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> bySubtype; // topics, infos
        std::map<std::string, std::uint32_t> byCategory;
        for (FormId id : dialogue.topicIds())
        {
            const DialogTopic* topic = dialogue.topic(id);
            if (topic == nullptr)
                continue;
            auto& entry = bySubtype[topic->mSubtypeName + " cat" + std::to_string(topic->mCategory)];
            ++entry.first;
            entry.second += static_cast<std::uint32_t>(dialogue.infosOf(id).size());
        }
        for (const auto& [name, counts] : bySubtype)
            std::cout << "SKYRIMSE-BARKS subtype " << name << " topics " << counts.first << " infos " << counts.second
                      << "\n";
    }
}
