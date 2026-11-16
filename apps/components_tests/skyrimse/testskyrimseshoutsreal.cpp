// Task 0205 (milestone M-S7): the SHOU and WOOP records on the player's own Skyrim SE masters.
// Gated: skipped unless OPENEDEN_SKYRIMSE_DATA names the game's Data folder. Prints aggregates and the numbers of
// Unrelenting Force
// (lines SKYRIMSE-SHOUTS, game numbers only) and checks the layouts the shout code reads.
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
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

    bool loadShouts(CombatIndex& index, std::string& skip)
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
            index.add(records);
        }
        return true;
    }

    TEST(SkyrimShoutsRealData, everyShoutHasThreeWordsAndEveryReferenceResolves)
    {
        CombatIndex index;
        std::string skip;
        if (!loadShouts(index, skip))
            GTEST_SKIP() << skip;
        const ShoutIndex& shouts = index.shouts();
        std::uint64_t full = 0, wordsKnown = 0, spellsKnown = 0, voice = 0;
        std::uint64_t odd = 0, nullWords = 0, nullSpells = 0, danglingWords = 0, danglingSpells = 0;
        float maxRecovery = 0.f;
        EXPECT_EQ(shouts.shoutCount(), 117u);
        EXPECT_EQ(shouts.wordCount(), 107u);
        EXPECT_NE(shouts.shoutByEditorId("UnrelentingForceShout"), nullptr);
        for (const ShoutRecord& record : shouts.shouts())
        {
            const ShoutRecord* s = &record;
            if (s->mWordCount == 3)
                ++full;
            else
                ++odd;
            for (std::size_t i = 0; i < s->mWordCount; ++i)
            {
                if (s->mWords[i].mWord == kNullFormId)
                    ++nullWords;
                else if (shouts.word(s->mWords[i].mWord) != nullptr)
                    ++wordsKnown;
                else
                    ++danglingWords;
                if (s->mWords[i].mSpell == kNullFormId)
                    ++nullSpells;
                else if (index.spell(s->mWords[i].mSpell) != nullptr)
                    ++spellsKnown;
                else
                    ++danglingSpells;
                if (s->mWords[i].mSpell != kNullFormId && index.spell(s->mWords[i].mSpell) != nullptr
                    && index.spell(s->mWords[i].mSpell)->mType == 11)
                    ++voice;
                maxRecovery = std::max(maxRecovery, s->mWords[i].mRecovery);
            }
        }
        std::cout << "SKYRIMSE-SHOUTS shouts " << shouts.shoutCount() << " words " << shouts.wordCount()
                  << " with three " << full << " other " << odd << "\n";
        std::cout << "SKYRIMSE-SHOUTS word ids known " << wordsKnown << " null " << nullWords << " dangling "
                  << danglingWords << "; spell ids known " << spellsKnown << " null " << nullSpells << " dangling "
                  << danglingSpells << "; voice power spells " << voice << " max recovery " << maxRecovery << "\n";
        EXPECT_EQ(full, shouts.shoutCount());
        EXPECT_EQ(danglingWords, 0u);
        EXPECT_EQ(danglingSpells, 0u);
        EXPECT_EQ(wordsKnown + nullWords, 3 * shouts.shoutCount());
        EXPECT_EQ(spellsKnown + nullSpells, 3 * shouts.shoutCount());
    }

    TEST(SkyrimShoutsRealData, unrelentingForceHasTheNumbersOfTheSurvey)
    {
        CombatIndex index;
        std::string skip;
        if (!loadShouts(index, skip))
            GTEST_SKIP() << skip;
        const ShoutRecord* uf = index.shouts().shoutByEditorId("UnrelentingForceShout");
        ASSERT_NE(uf, nullptr);
        ASSERT_EQ(uf->mWordCount, 3u);
        const char* words[3] = { "WordFus", "WordRo", "WordDah" };
        const float recovery[3] = { 15.f, 20.f, 45.f };
        const float stagger[3] = { 0.75f, 1.25f, 1.5f };
        const float damage[3] = { 2.f, 5.f, 10.f };
        for (int i = 0; i < 3; ++i)
        {
            const WordRecord* w = index.shouts().wordByEditorId(words[i]);
            ASSERT_NE(w, nullptr) << words[i];
            EXPECT_EQ(uf->mWords[i].mWord, w->mCommon.mId) << i;
            EXPECT_FLOAT_EQ(uf->mWords[i].mRecovery, recovery[i]);
            const SpellRecord* spell = index.spell(uf->mWords[i].mSpell);
            ASSERT_NE(spell, nullptr);
            EXPECT_EQ(spell->mCommon.mEditorId, "VoiceUnrelentingForce" + std::to_string(i + 1));
            EXPECT_EQ(spell->mType, 11u); // voice power
            EXPECT_EQ(spell->mCastType, 1u);
            EXPECT_EQ(spell->mDelivery, 2u);
            ASSERT_GE(spell->mEffects.size(), 2u);
            EXPECT_FLOAT_EQ(spell->mEffects[0].mMagnitude, stagger[i]);
            EXPECT_FLOAT_EQ(spell->mEffects[1].mMagnitude, damage[i]);
            const MagicEffectRecord* st = index.magicEffect(spell->mEffects[0].mEffect);
            ASSERT_NE(st, nullptr);
            EXPECT_EQ(st->mArchetype, MagicArchetype::Stagger);
            std::cout << "SKYRIMSE-SHOUTS UF word " << i + 1 << " spell cost " << spell->mCost << " recovery "
                      << uf->mWords[i].mRecovery << " stagger effect conditions "
                      << (spell->mEffects[0].mConditions + st->mConditions) << "\n";
        }
        const MagicSettings magic = index.settings().magicSettings();
        EXPECT_FLOAT_EQ(magic.mShoutTime1, 0.2f);
        EXPECT_FLOAT_EQ(magic.mShoutTime2, 0.9f);
    }
}
