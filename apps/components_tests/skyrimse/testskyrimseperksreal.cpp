// Task 0179 (milestone M-S5, stage S5-7): the perk records on the player's own Skyrim SE masters. Gated: skipped unless
// OPENEDEN_SKYRIMSE_DATA names the game's Data folder. Prints aggregates only (lines SKYRIMSE-PERKS, no game text) and
// checks the layout facts the stage log states: every PERK decodes with no odd subrecord, every NPC_ PRKR names a decoded
// PERK, and three named perks hold the numbers an independent Python scan read from the same files.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/perks.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    TEST(SkyrimPerksRealData, everyPerkDecodesAndTheNamedOnesHaveTheScannedNumbers)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
        {
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
            ASSERT_TRUE(order.add(n, false, err)) << err;
        }
        CombatIndex combat;
        ActorIndex actors;
        std::uint64_t odd = 0, errors = 0;
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            ASSERT_TRUE(file != nullptr) << n;
            CombatRecords records;
            (void)parseCombatRecords(*file, order, ExteriorLimits(), records);
            odd += records.mNotes.mOddSubrecords;
            errors += records.mErrors.size();
            combat.add(records);
            ActorRecords actorRecords;
            (void)parseActorRecords(*file, order, ActorLimits(), actorRecords);
            actors.add(actorRecords);
        }
        std::map<std::uint8_t, std::uint64_t> byEntryPoint;
        std::uint64_t effects = 0, abilities = 0, conditions = 0;
        std::set<FormId> ids;
        for (const FormId id : combat.perkIds())
            ids.insert(id);
        std::uint64_t npcPerks = 0, npcPerksUnknown = 0, npcsWithPerks = 0;
        for (const FormId npcId : actors.npcIds())
        {
            const ActorNpc* npc = actors.npc(npcId);
            if (npc == nullptr || npc->mPerks.empty())
                continue;
            ++npcsWithPerks;
            for (const FormId perk : npc->mPerks)
            {
                ++npcPerks;
                const PerkRecord* rec = combat.perk(perk);
                if (rec == nullptr)
                    ++npcPerksUnknown;
            }
        }
        for (const FormId id : ids)
        {
            const PerkRecord* perk = combat.perk(id);
            ASSERT_TRUE(perk != nullptr);
            for (const PerkEffect& e : perk->mEffects)
            {
                ++effects;
                if (e.mKind == PerkEffectKind::EntryPoint)
                    ++byEntryPoint[e.mEntryPoint];
                else if (e.mKind == PerkEffectKind::Ability)
                    ++abilities;
                for (const PerkTab& t : e.mTabs)
                    conditions += t.mConditions.size();
            }
        }
        std::cout << "SKYRIMSE-PERKS perks-found-by-id " << ids.size() << " effects " << effects << " abilities " << abilities
                  << " tab-conditions " << conditions << " odd-subrecords " << odd << " errors " << errors
                  << " npcs-with-perks " << npcsWithPerks << " prkr-entries " << npcPerks << " unknown " << npcPerksUnknown
                  << " entry-point-kinds " << byEntryPoint.size() << " modAttackDamage " << byEntryPoint[35] << std::endl;
        EXPECT_EQ(odd, 0u);
        EXPECT_EQ(errors, 0u);
        EXPECT_EQ(npcPerksUnknown, 0u);
        EXPECT_GT(effects, 0u);

        auto byName = [&](const char* name) -> const PerkRecord* {
            for (const FormId id : ids)
            {
                const PerkRecord* p = combat.perk(id);
                if (p != nullptr && p->mCommon.mEditorId == name)
                    return p;
            }
            return nullptr;
        };
        const PerkRecord* armsman = byName("Armsman00");
        ASSERT_TRUE(armsman != nullptr);
        ASSERT_EQ(armsman->mEffects.size(), 1u);
        EXPECT_EQ(armsman->mEffects[0].mEntryPoint, PerkEntry::ModAttackDamage);
        EXPECT_EQ(armsman->mEffects[0].mFunction, PerkFunction::Multiply);
        EXPECT_EQ(armsman->mEffects[0].mTabCount, 3);
        EXPECT_FLOAT_EQ(armsman->mEffects[0].mValue, 1.2f);
        const PerkRecord* stance = byName("FightingStance");
        ASSERT_TRUE(stance != nullptr);
        ASSERT_EQ(stance->mEffects.size(), 1u);
        EXPECT_EQ(stance->mEffects[0].mEntryPoint, PerkEntry::ModPowerAttackStamina);
        EXPECT_FLOAT_EQ(stance->mEffects[0].mValue, 0.75f);
        const PerkRecord* quick = byName("QuickShot");
        ASSERT_TRUE(quick != nullptr);
        ASSERT_GE(quick->mEffects.size(), 1u);
        EXPECT_EQ(quick->mEffects[0].mKind, PerkEffectKind::Ability);
        bool found = false;
        const float speed = abilityModifier(combat, std::vector<FormId>{ quick->mEffects[0].mForm },
            static_cast<std::int32_t>(ActorValueIndex::WeaponSpeedMult), found);
        EXPECT_TRUE(found);
        EXPECT_NEAR(speed, 1.3f, 1e-5f);
    }
}
