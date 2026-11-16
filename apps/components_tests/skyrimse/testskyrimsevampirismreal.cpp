// Task 0260 (vampirism): the real records. Gated: skipped unless OPENEDEN_SKYRIMSE_DATA names the game's Data folder. Checks
// that the four sun damage abilities carry decodable per-effect conditions that the effect-condition evaluator answers, that
// sunlight holds outdoors at noon and not at night or indoors, and that every stage spell the quest script adds is a known
// spell (lines SKYRIMSE-VAMPIRE, game numbers only).
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/vampirism.hpp>

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

    bool loadIndex(CombatIndex& index, std::string& skip)
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

    TEST(SkyrimVampirismReal, the_sun_damage_abilities_have_conditions_the_evaluator_answers)
    {
        CombatIndex index;
        std::string skip;
        if (!loadIndex(index, skip))
            GTEST_SKIP() << skip;
        for (const FormId id : vampirism::kSunDamageSpells)
        {
            const SpellRecord* spell = index.spell(id);
            ASSERT_NE(spell, nullptr) << std::hex << id;
            ASSERT_TRUE(spell->isAbility());
            std::size_t withConditions = 0;
            for (const SpellEffect& e : spell->mEffects)
            {
                EXPECT_EQ(e.mConditionList.size(), e.mConditions) << spell->mCommon.mEditorId;
                withConditions += e.mConditions > 0 ? 1 : 0;
            }
            std::cout << "SKYRIMSE-VAMPIRE sun spell " << spell->mCommon.mEditorId << " effects " << spell->mEffects.size()
                      << " with conditions " << withConditions << "\n";
            EXPECT_GT(withConditions, 0u);
        }
        const SpellRecord* first = index.spell(vampirism::kSunDamageSpells[0]);
        ASSERT_NE(first, nullptr);
        for (const SpellEffect& e : first->mEffects)
            for (const survey::Ctda& c : e.mConditionList)
                std::cout << "SKYRIMSE-VAMPIRE   ctda fn " << c.mFunction << " compare " << int(c.mCompare) << " flags "
                          << int(c.mFlags) << " value " << c.mComparison << " p1 " << std::hex << c.mParam1 << std::dec
                          << " runon " << c.mRunOn << "\n";
        effectconditions::Place outdoors;
        outdoors.mKnown = true;
        outdoors.mWorld = 0x1A26F; // WhiterunWorld
        effectconditions::Place inside = outdoors;
        inside.mInterior = true;
        inside.mWorld = 0;
        EXPECT_FALSE(vampirism::sunlit(index, vampirism::kSunDamageSpells[0], 12 * 3600.0, outdoors))
            << "the Dawnguard global is unanswered without a lookup";
        // the real game answers it with the GLOB's stored value (the Dawnguard flag that switches sun damage off is 0)
        const effectconditions::Evaluator::GlobalLookup zero = [](FormId, float& value) {
            value = 0.f;
            return true;
        };
        EXPECT_TRUE(vampirism::sunlit(index, vampirism::kSunDamageSpells[0], 12 * 3600.0, outdoors, zero));
        EXPECT_FALSE(vampirism::sunlit(index, vampirism::kSunDamageSpells[0], 23 * 3600.0, outdoors, zero));
        EXPECT_FALSE(vampirism::sunlit(index, vampirism::kSunDamageSpells[0], 12 * 3600.0, inside, zero));
    }

    TEST(SkyrimVampirismReal, the_stage_spells_of_the_quest_script_exist)
    {
        CombatIndex index;
        std::string skip;
        if (!loadIndex(index, skip))
            GTEST_SKIP() << skip;
        const std::vector<FormId> spells = { 0x2E1C3, 0xED099, 0xED09D, 0xED09E, 0x10F1EB, 0x10F1EA, 0x10F1E9, 0x10F1E8,
            0xC4DE2, 0x88821, 0xED0A4, 0xED0A5, 0xED0A6, 0xED0A7, 0xED0A8, 0xB8780 };
        for (const FormId id : spells)
            EXPECT_NE(index.spell(id), nullptr) << std::hex << id;
        const SpellRecord* sanguinare = index.spell(vampirism::kSanguinare);
        ASSERT_NE(sanguinare, nullptr);
        EXPECT_TRUE(sanguinare->isDisease());
        const SpellRecord* ability = index.spell(vampirism::kVampirismAbility);
        ASSERT_NE(ability, nullptr);
        EXPECT_TRUE(ability->isAbility());
    }
}
