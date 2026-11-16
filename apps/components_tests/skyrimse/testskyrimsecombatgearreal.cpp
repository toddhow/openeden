// Task 0214: the gear of every NPC of the five official masters (gated by OPENEDEN_SKYRIMSE_DATA; prints aggregates and
// a few base numbers, lines SKYRIMSE-COMBATGEAR). Checks that no NPC carries a shield with a two-handed weapon, that
// every chosen weapon is a melee weapon and that the choice is deterministic (twice the same).
#include <game_skyrimse/actor_mechanics.hpp>
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/combat_gear.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    struct World
    {
        ActorIndex mActors;
        CombatIndex mCombat;
        std::uint64_t mOddWeapon = 0, mOddRace = 0, mOddSetting = 0, mErrors = 0;
    };

    bool loadMasters(World& world, std::string& skip)
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
        for (const std::string& n : names)
            if (!fs::exists(data / n))
            {
                skip = "the five official masters are not all in the Data folder";
                return false;
            }
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            if (!order.add(n, false, err))
            {
                skip = err;
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
            ActorRecords records;
            (void)parseActorRecords(*file, order, ActorLimits(), records);
            world.mActors.add(records);
            ParsedPlugin parsed;
            ParseLimits limits;
            limits.mSkipReferences = true;
            (void)parseRecords(*file, order, limits, parsed);
            world.mActors.addLeveledLists(parsed.mLeveledLists);
            CombatRecords combat;
            (void)parseCombatRecords(*file, order, ExteriorLimits(), combat);
            world.mCombat.add(combat);
            world.mErrors += combat.mErrors.size();
            const auto& odd = combat.mNotes.mOddBy;
            for (const auto& entry : odd)
            {
                if (entry.first.rfind("WEAP", 0) == 0)
                    world.mOddWeapon += entry.second;
                else if (entry.first.rfind("RACE", 0) == 0)
                    world.mOddRace += entry.second;
                else
                    world.mOddSetting += entry.second;
            }
        }
        return true;
    }

    TEST(SkyrimCombatGearRealData, theGearOfEveryNpcIsConsistent)
    {
        World world;
        std::string skip;
        if (!loadMasters(world, skip))
            GTEST_SKIP() << skip;
        std::uint64_t npcs = 0, resolved = 0, withWeapon = 0, withShield = 0, twoHandedWithShield = 0, withExtra = 0,
                      nonMelee = 0, differs = 0, shieldsWithMaterial = 0;
        std::map<int, std::uint64_t> byType;
        std::map<int, std::vector<FormId>> examples;
        for (FormId npc : world.mActors.npcIds())
        {
            ++npcs;
            std::vector<ArmourPiece> worn;
            std::string error;
            (void)wornArmour(world.mActors, npc, 0, kAppearanceLevel, worn, error);
            FighterGear gear, again;
            if (!resolveFighterGear(world.mActors, world.mCombat, npc, 0, kAppearanceLevel, worn, gear))
                continue;
            ++resolved;
            ASSERT_TRUE(resolveFighterGear(world.mActors, world.mCombat, npc, 0, kAppearanceLevel, worn, again));
            if (gear.mWeapon != again.mWeapon || gear.mHasShield != again.mHasShield
                || gear.mExtraPieces.size() != again.mExtraPieces.size())
                ++differs;
            if (gear.mWeapon != nullptr)
            {
                ++withWeapon;
                ++byType[gear.mWeapon->mAnimType];
                if (!gear.mWeapon->isMelee())
                    ++nonMelee;
                if (examples[gear.mWeapon->mAnimType].size() < 3)
                    examples[gear.mWeapon->mAnimType].push_back(npc);
                if (isTwoHanded(gear.mWeapon->mAnimType) && gear.mHasShield)
                    ++twoHandedWithShield;
            }
            if (gear.mHasShield)
            {
                ++withShield;
                if (gear.mShieldBlockMaterial != kNullFormId)
                    ++shieldsWithMaterial;
            }
            if (!gear.mExtraPieces.empty())
                ++withExtra;
        }
        std::cout << "SKYRIMSE-COMBATGEAR npcs " << npcs << " resolved " << resolved << " with weapon " << withWeapon
                  << " with shield " << withShield << " shield with BAMT " << shieldsWithMaterial
                  << " with inventory armour " << withExtra << '\n';
        for (const auto& [type, count] : byType)
        {
            std::cout << "SKYRIMSE-COMBATGEAR type " << type << " npcs " << count << " examples";
            for (FormId id : examples[type])
                std::cout << ' ' << id;
            std::cout << '\n';
        }
        EXPECT_EQ(nonMelee, 0u);
        EXPECT_EQ(twoHandedWithShield, 0u);
        EXPECT_EQ(differs, 0u);
        EXPECT_GT(withWeapon, 0u);
    }
    // A diagnostic: OPENEDEN_GEAR_NPC=<decimal base> prints what the search sees for that NPC (no game text).
    TEST(SkyrimCombatGearRealData, aNamedNpcsSearchIsPrinted)
    {
        const char* env = std::getenv("OPENEDEN_GEAR_NPC");
        if (env == nullptr || *env == '\0')
            GTEST_SKIP() << "OPENEDEN_GEAR_NPC is not set";
        World world;
        std::string skip;
        if (!loadMasters(world, skip))
            GTEST_SKIP() << skip;
        const FormId npc = static_cast<FormId>(std::strtoul(env, nullptr, 10));
        ActorIndex::Resolved resolved;
        const auto status = world.mActors.resolve(npc, kAppearanceLevel, 0, resolved);
        std::cout << "SKYRIMSE-COMBATGEAR npc " << npc << " status " << int(status) << " inventoryFrom "
                  << resolved.mInventoryFrom << " outfit " << resolved.mDefaultOutfit << " entries "
                  << resolved.mInventory.size() << '\n';
        for (const InventoryEntry& e : resolved.mInventory)
        {
            const WeaponRecord* w = world.mCombat.weapon(e.mItem);
            std::cout << "  item " << e.mItem << " x" << e.mCount
                      << (w != nullptr ? " weapon type " + std::to_string(w->mAnimType) : std::string())
                      << (world.mActors.armor(e.mItem) != nullptr ? " armour" : "")
                      << (world.mActors.leveledList(e.mItem) != nullptr ? " leveled" : "") << '\n';
        }
    }
}
