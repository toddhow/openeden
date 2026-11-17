// Task 0235: which dialogue subtypes of the five official masters carry INFOs gated on the player's stance: IsWeaponOut
// (fn 263), IsWeaponMagicOut (fn 101) and IsSneaking (fn 286), and on which run-on subject. Prints counts only (lines
// starting SKYRIMSE-REACT), never a response text. Skipped unless OPENEDEN_SKYRIMSE_DATA names the Data folder.
#include <game_skyrimse/conditions.hpp>
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

    TEST(SkyrimBarkReactionsRealData, theStanceGatedInfosOfTheFiveMasters)
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
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            DialogueRecords dr;
            ASSERT_TRUE(parseDialogueRecords(*plugin, order, DialogueLimits(), dr));
            dialogue.add(std::move(dr), FormIdResolver(plugin->header(), n, order));
        }
        // subtype, function -> {infos, infos with run-on 1 (target), with run-on 0 (subject)}
        struct Count
        {
            std::uint32_t mInfos = 0, mTarget = 0, mSubject = 0, mOther = 0;
        };
        std::map<std::string, Count> byKey;
        std::map<std::string, std::uint32_t> byRunOn;
        for (FormId id : dialogue.topicIds())
        {
            const DialogTopic* topic = dialogue.topic(id);
            if (topic == nullptr)
                continue;
            for (const DialogInfo* info : dialogue.infosOf(id))
            {
                bool seen[3] = { false, false, false };
                for (const survey::Ctda& c : info->mConditions)
                {
                    int slot = -1;
                    if (c.mFunction == 263)
                        slot = 0;
                    else if (c.mFunction == 101)
                        slot = 1;
                    else if (c.mFunction == 286)
                        slot = 2;
                    if (slot < 0 || seen[slot])
                        continue;
                    seen[slot] = true;
                    static const char* fn[3] = { "IsWeaponOut", "IsWeaponMagicOut", "IsSneaking" };
                    Count& count = byKey[topic->mSubtypeName + " " + fn[slot]];
                    ++count.mInfos;
                    ++byRunOn[topic->mSubtypeName + " " + fn[slot] + " runon " + std::to_string(c.mRunOn) + " ref "
                        + (c.mReference == 0x14 ? "player" : (c.mReference == 0 ? "none" : "other"))];
                    if (c.mRunOn == 1)
                        ++count.mTarget;
                    else if (c.mRunOn == 0)
                        ++count.mSubject;
                    else
                        ++count.mOther;
                }
            }
        }
        // The NOTA (a noticed sound) and MURD / STEA topics: INFOs, say-once, voice-type conditions, empty texts
        for (const char* subtype : { "NOTA", "NOTC", "STEA", "MURD", "ASSA", "TRES" })
        {
            std::uint32_t topics = 0, infos = 0, sayOnce = 0, voiceCond = 0, emptyText = 0, hasScript = 0;
            for (FormId id : dialogue.topicIds())
            {
                const DialogTopic* topic = dialogue.topic(id);
                if (topic == nullptr || topic->mSubtypeName != subtype)
                    continue;
                ++topics;
                for (const DialogInfo* info : dialogue.infosOf(id))
                {
                    ++infos;
                    if ((info->mFlags & 0x4) != 0)
                        ++sayOnce;
                    bool voice = false;
                    for (const survey::Ctda& c : info->mConditions)
                        if (c.mFunction == 426)
                            voice = true;
                    voiceCond += voice ? 1 : 0;
                    hasScript += info->mHasScript ? 1 : 0;
                    emptyText += info->mResponses.empty() ? 1 : 0;
                }
            }
            std::cout << "SKYRIMSE-REACT subtype " << subtype << " topics " << topics << " infos " << infos
                      << " flag4-say-once " << sayOnce << " with-voice-type-condition " << voiceCond << " no-responses "
                      << emptyText << " with-script " << hasScript << "\n";
        }
        for (const auto& [key, n] : byRunOn)
            std::cout << "SKYRIMSE-REACT " << key << " infos " << n << "\n";
        for (const auto& [key, count] : byKey)
            std::cout << "SKYRIMSE-REACT " << key << " infos " << count.mInfos << " runon-subject " << count.mSubject
                      << " runon-target " << count.mTarget << " runon-other " << count.mOther << "\n";
    }
}
