// Task 0255: what the stablemaster's horse purchase is made of (survey; gated by OPENEDEN_SKYRIMSE_DATA). Prints
// official identifiers only (lines starting SKYRIMSE-STABLES): topic editor ids, fragment script names, the names a
// fragment script refers to, condition function indexes. Never a response text.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/pack_core.hpp>

#include <components/script_papyrus/pexfile.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    TEST(SkyrimRidingStablesSurvey, theHorsePurchaseTopicAndItsFragments)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
        {
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        }
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        for (const std::string& n : names)
            if (!fs::exists(data / n))
            {
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
            }
        const fs::path misc = data / "Skyrim - Misc.bsa";
        if (!fs::exists(misc))
        {
            GTEST_SKIP() << "no Skyrim - Misc.bsa";
        }
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            ASSERT_TRUE(order.add(n, false, err)) << err;
        DialogueIndex dialogue;
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            DialogueRecords dr;
            ASSERT_TRUE(parseDialogueRecords(*plugin, order, DialogueLimits(), dr));
            dialogue.add(std::move(dr), FormIdResolver(plugin->header(), n, order));
        }
        std::uint64_t topics = 0;
        for (FormId topicId : dialogue.topicIds())
        {
            const DialogTopic* topic = dialogue.topic(topicId);
            if (topic == nullptr || topic->mCommon.mEditorId.find("Horse") == std::string::npos)
                continue;
            ++topics;
            std::cout << "SKYRIMSE-STABLES topic " << topic->mCommon.mEditorId << " quest " << std::hex << topic->mQuest
                      << std::dec << " infos " << dialogue.infosOf(topicId).size() << "\n";
            std::set<std::string> done;
            for (const DialogInfo* info : dialogue.infosOf(topicId))
            {
                std::cout << "SKYRIMSE-STABLES  info " << std::hex << info->mCommon.mId << std::dec << " script '"
                          << info->mFragmentScript << "' conditions";
                for (const survey::Ctda& c : info->mConditions)
                    std::cout << " fn_" << c.mFunction;
                std::cout << "\n";
                if (info->mFragmentScript.empty() || !done.insert(info->mFragmentScript).second)
                    continue;
                std::string bytes, e;
                const std::string path = "scripts/" + ScriptPapyrus::Pex::lowered(info->mFragmentScript) + ".pex";
                if (!readArchiveEntry(misc, path, bytes, e))
                {
                    std::cout << "SKYRIMSE-STABLES  script missing\n";
                    continue;
                }
                const auto parsed = ScriptPapyrus::Pex::parse(
                    { reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() }, path);
                if (!parsed.mOk)
                    continue;
                std::cout << "SKYRIMSE-STABLES  strings:";
                for (const std::string& s : parsed.mFile.mStrings)
                    std::cout << " " << s;
                std::cout << "\n";
            }
        }
        std::cout << "SKYRIMSE-STABLES topics " << topics << "\n";
        EXPECT_GT(topics, 0u);
    }
}
