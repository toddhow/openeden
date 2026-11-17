// Task 0243: the pickpocket dialogue subtypes (PICN, PICC, PICT) of the five official masters, and how many INFOs of
// the bark subtypes have no response. Prints counts and editor ids only (lines starting SKYRIMSE-SEL), never a response
// text. Skipped unless OPENEDEN_SKYRIMSE_DATA names the Data folder.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/quest.hpp>
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

    TEST(SkyrimBarkSelectionRealData, thePickpocketSubtypesAndTheEmptyBarkInfos)
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
        QuestIndex quests;
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            DialogueRecords dr;
            ASSERT_TRUE(parseDialogueRecords(*plugin, order, DialogueLimits(), dr));
            dialogue.add(std::move(dr), FormIdResolver(plugin->header(), n, order));
            QuestRecords qr;
            ASSERT_TRUE(parseQuestRecords(*plugin, order, QuestLimits(), qr));
            quests.add(std::move(qr), FormIdResolver(plugin->header(), n, order));
        }
        // The NOTA INFOs that have a response, by the voice types their GetIsVoiceType (fn_426) conditions name (ANY of
        // the OR-chained ones), and how many have no voice-type condition at all (said by every voice type) or a random
        // roll.
        {
            std::map<std::string, std::uint32_t> byVoice;
            std::uint32_t withText = 0, noVoiceCondition = 0, withRandom = 0;
            for (FormId id : dialogue.topicIds())
            {
                const DialogTopic* topic = dialogue.topic(id);
                if (topic == nullptr || topic->mSubtypeName != "NOTA")
                    continue;
                for (const DialogInfo* info : dialogue.infosOf(id))
                {
                    if (info->mResponses.empty())
                        continue;
                    ++withText;
                    bool voice = false, random = false;
                    for (const survey::Ctda& c : info->mConditions)
                    {
                        if (c.mFunction == 77)
                            random = true;
                        if (c.mFunction == 426)
                        {
                            voice = true;
                            const FormIdResolver& resolver = dialogue.resolverOf(*info);
                            const std::optional<FormId> voiceId = resolver.resolve(c.mParam1);
                            ++byVoice[voiceId ? dialogue.voiceTypeName(*voiceId) : std::string("?")];
                        }
                    }
                    noVoiceCondition += voice ? 0 : 1;
                    withRandom += random ? 1 : 0;
                }
            }
            std::cout << "SKYRIMSE-SEL NOTA with-response " << withText << " no-voice-type-condition "
                      << noVoiceCondition << " with-random-roll " << withRandom << "\n";
            for (const auto& [name, n] : byVoice)
                std::cout << "SKYRIMSE-SEL   NOTA voice " << name << " x" << n << "\n";
        }
        for (const char* subtype : { "PICN", "PICC", "PICT", "NOTA", "NOTC", "STEA", "ASSA", "TRES", "MURD" })
        {
            std::uint32_t topics = 0, infos = 0, noResponse = 0, noText = 0, sayOnce = 0, withSpeaker = 0;
            std::map<std::uint16_t, std::uint32_t> functions;
            std::map<std::string, std::uint32_t> topicNames;
            std::map<std::string, std::uint32_t> victim;
            for (FormId id : dialogue.topicIds())
            {
                const DialogTopic* topic = dialogue.topic(id);
                if (topic == nullptr || topic->mSubtypeName != subtype)
                    continue;
                ++topics;
                const QuestRecord* quest = quests.quest(topic->mQuest);
                ++topicNames[topic->mCommon.mEditorId + " quest " + (quest != nullptr ? quest->mCommon.mEditorId : "?")
                    + " cat " + std::to_string(topic->mCategory) + " prio " + std::to_string(topic->mPriority)];
                for (const DialogInfo* info : dialogue.infosOf(id))
                {
                    ++infos;
                    if ((info->mFlags & 0x4) != 0)
                        ++sayOnce;
                    if (info->mSpeaker != kNullFormId)
                        ++withSpeaker;
                    if (info->mResponses.empty())
                        ++noResponse;
                    else
                    {
                        bool anyText = false;
                        for (const DialogResponse& r : info->mResponses)
                            anyText = anyText || r.mText.mPresent;
                        noText += anyText ? 0 : 1;
                    }
                    for (const survey::Ctda& c : info->mConditions)
                    {
                        ++functions[c.mFunction];
                        if (c.mFunction == 314)
                            ++victim["op" + std::to_string(c.mCompare) + " value " + std::to_string(c.mComparison)
                                + " runon " + std::to_string(c.mRunOn)];
                    }
                }
            }
            std::cout << "SKYRIMSE-SEL subtype " << subtype << " topics " << topics << " infos " << infos
                      << " no-response-records " << noResponse << " response-without-text " << noText << " say-once "
                      << sayOnce << " with-speaker " << withSpeaker << "\n";
            for (const auto& [name, n] : topicNames)
                std::cout << "SKYRIMSE-SEL   topic " << subtype << " " << name << " x" << n << "\n";
            for (const auto& [key, n] : victim)
                std::cout << "SKYRIMSE-SEL   fn314 " << subtype << " " << key << " x" << n << "\n";
            std::string line;
            for (const auto& [fn, n] : functions)
                line += " fn" + std::to_string(fn) + "x" + std::to_string(n);
            std::cout << "SKYRIMSE-SEL   conditions " << subtype << line << "\n";
        }
    }
}
