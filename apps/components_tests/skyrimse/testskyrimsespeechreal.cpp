// Task 0155, milestone M-S4: speech checks and services on the player's own Skyrim SE masters. Prints AGGREGATES and
// official function / script identifiers only (lines starting SKYRIMSE-SPEECH): never a response text. Skipped when
// OPENEDEN_SKYRIMSE_DATA is not set. The fragment scripts are read from "Skyrim - Misc.bsa".
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/speech.hpp>

#include <components/script_papyrus/pexfile.hpp>

#include <gtest/gtest.h>

#include <algorithm>
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

    TEST(SkyrimSpeechRealData, theSpeechGatesAreAnsweredAndTheServiceScriptsAreNamed)
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
        const fs::path misc = data / "Skyrim - Misc.bsa";
        if (!fs::exists(misc))
            GTEST_SKIP() << "no Skyrim - Misc.bsa";

        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            ASSERT_TRUE(order.add(n, false, err)) << err;

        ActorIndex actors;
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

        // ---- 1. the speech gates -------------------------------------------------------------------------------
        PlayerStandIn player;
        SkyrimConditionWorld world(actors, dialogue);
        world.setPlayer(&player);
        DialogueSpeaker speaker;
        speaker.mBase = 0x00013BBD; // any non-null base: answers() only asks whether a speaker is set
        world.setSpeaker(speaker);

        std::uint64_t gated = 0, gatedFullyAnswered = 0, unansweredTotal = 0;
        std::map<std::uint16_t, std::uint64_t> unansweredBy;
        std::uint64_t withFragment = 0;
        std::set<std::string> scripts;
        std::map<std::string, std::vector<const DialogInfo*>> infosOfScript;
        for (FormId topicId : dialogue.topicIds())
        {
            for (const DialogInfo* info : dialogue.infosOf(topicId))
            {
                bool isGate = false;
                for (const survey::Ctda& c : info->mConditions)
                {
                    if (c.mFunction == 654 || c.mFunction == 655 || (c.mFunction == 14 && c.mParam1 == kAvSpeechcraft))
                        isGate = true;
                }
                if (isGate)
                {
                    ++gated;
                    world.setPlugin(&dialogue.resolverOf(*info));
                    std::uint32_t unanswered = 0;
                    for (const survey::Ctda& c : info->mConditions)
                    {
                        if (!world.answers(c))
                        {
                            ++unanswered;
                            ++unansweredBy[c.mFunction];
                        }
                    }
                    unansweredTotal += unanswered;
                    gatedFullyAnswered += unanswered == 0;
                }
                if (!info->mFragmentScript.empty())
                {
                    ++withFragment;
                    scripts.insert(info->mFragmentScript);
                    infosOfScript[info->mFragmentScript].push_back(info);
                }
            }
        }
        std::cout << "SKYRIMSE-SPEECH gates: INFOs gated by Speechcraft / GetBribeSuccess / GetIntimidateSuccess "
                  << gated << ", fully answered by the stand-in " << gatedFullyAnswered
                  << ", conditions left unanswered " << unansweredTotal << "\n";
        std::vector<std::pair<std::uint64_t, std::uint16_t>> byCount;
        for (const auto& [f, n] : unansweredBy)
            byCount.emplace_back(n, f);
        std::sort(byCount.rbegin(), byCount.rend());
        for (std::size_t i = 0; i < byCount.size() && i < 12; ++i)
            std::cout << "SKYRIMSE-SPEECH gates unanswered fn_" << byCount[i].second << " x" << byCount[i].first
                      << "\n";
        EXPECT_GT(gated, 0u);

        // ---- 2. the fragment scripts ------------------------------------------------------------------------------
        std::uint64_t read = 0, missing = 0, parseFailed = 0, infosClassified = 0;
        std::map<std::string, std::uint64_t> serviceCount;
        std::map<std::string, std::uint64_t> nameInScripts; // lower-case string table entries -> scripts that hold them
        for (const std::string& script : scripts)
        {
            std::string bytes, e;
            const std::string path = "scripts/" + ScriptPapyrus::Pex::lowered(script) + ".pex";
            if (!readArchiveEntry(misc, path, bytes, e))
            {
                ++missing;
                continue;
            }
            const auto parsed = ScriptPapyrus::Pex::parse(
                { reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() }, path);
            if (!parsed.mOk)
            {
                ++parseFailed;
                continue;
            }
            ++read;
            std::vector<std::string> lowered;
            std::set<std::string> distinct;
            for (const std::string& s : parsed.mFile.mStrings)
            {
                lowered.push_back(ScriptPapyrus::Pex::lowered(s));
                distinct.insert(lowered.back());
            }
            for (const std::string& s : distinct)
                ++nameInScripts[s];
            const std::string service = classifyService(lowered);
            if (!service.empty())
            {
                ++serviceCount[service];
                infosClassified += infosOfScript[script].size();
            }
        }
        std::cout << "SKYRIMSE-SPEECH scripts: INFOs with a fragment script " << withFragment << ", distinct scripts "
                  << scripts.size() << ", read " << read << ", missing " << missing << ", parse failed " << parseFailed
                  << ", INFOs classified as a service " << infosClassified << "\n";
        for (const auto& [service, n] : serviceCount)
            std::cout << "SKYRIMSE-SPEECH service " << service << ": scripts " << n << "\n";
        // The identifiers that look like menu or service calls, with the number of scripts naming them.
        for (const auto& [name, n] : nameInScripts)
        {
            if (name.find("menu") != std::string::npos || name.find("train") != std::string::npos
                || name.find("barter") != std::string::npos || name.find("rent") != std::string::npos
                || name.find("room") != std::string::npos || name.find("inventory") != std::string::npos)
                std::cout << "SKYRIMSE-SPEECH name " << name << ": scripts " << n << "\n";
        }
        // The room-renting topics: which scripts their INFOs name and what the scripts refer to (official identifiers).
        for (FormId topicId : dialogue.topicIds())
        {
            const DialogTopic* topic = dialogue.topic(topicId);
            if (topic == nullptr || topic->mCommon.mEditorId.find("RentRoom") == std::string::npos)
                continue;
            for (const DialogInfo* info : dialogue.infosOf(topicId))
            {
                std::string bytes, e;
                std::string refers;
                if (!info->mFragmentScript.empty()
                    && readArchiveEntry(
                        misc, "scripts/" + ScriptPapyrus::Pex::lowered(info->mFragmentScript) + ".pex", bytes, e))
                {
                    const auto parsed = ScriptPapyrus::Pex::parse(
                        { reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() }, "x");
                    std::vector<std::string> lowered;
                    for (const std::string& str : parsed.mFile.mStrings)
                        lowered.push_back(ScriptPapyrus::Pex::lowered(str));
                    refers = classifyService(lowered);
                }
                std::cout << "SKYRIMSE-SPEECH rent topic " << topic->mCommon.mEditorId << " info script '"
                          << info->mFragmentScript << "' service '" << refers << "' responses "
                          << info->mResponses.size() << "\n";
            }
        }
        EXPECT_GT(read, 0u);
    }
}
