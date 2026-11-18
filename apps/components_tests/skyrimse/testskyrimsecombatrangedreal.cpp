// Task 0300: the ranged and dual wield facts of the five official masters (gated by OPENEDEN_SKYRIMSE_DATA; prints
// aggregates, lines SKYRIMSE-RANGED). Checks that the ammunition flag tells bolts from arrows by name, that every
// ranged weapon an NPC carries has ammunition that fits it, and that the choice is the same twice.
#include <game_skyrimse/actor_mechanics.hpp>
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/combat_gear.hpp>
#include <game_skyrimse/combat_ranged.hpp>
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

    std::string lowerOf(std::string text)
    {
        for (char& c : text)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + 32);
        return text;
    }

    TEST(SkyrimRangedRealData, theNonBoltFlagSeparatesBoltsFromArrowsByTheirNames)
    {
        World world;
        std::string skip;
        if (!loadMasters(world, skip))
            GTEST_SKIP() << skip;
        std::uint64_t machine = 0, bolts = 0, arrows = 0, wrongBolt = 0, wrongArrow = 0, other = 0;
        for (const FormId id : world.mCombat.ammoIds())
        {
            const AmmoRecord* ammo = world.mCombat.ammo(id);
            ASSERT_NE(ammo, nullptr);
            const std::string name = lowerOf(ammo->mCommon.mEditorId);
            if (name.find("ballista") != std::string::npos || name.find("sphere") != std::string::npos)
            {
                ++machine; // the ammunition of the dwarven ballista traps and sphere centurions: flagged as non-bolt
                continue;
            }
            if (name.find("bolt") != std::string::npos)
            {
                ++bolts;
                wrongBolt += isBolt(*ammo) ? 0 : 1;
                if (!isBolt(*ammo))
                    std::cout << "SKYRIMSE-RANGED named bolt, flag set: " << ammo->mCommon.mEditorId << " flags " << ammo->mFlags << ' ' << (ammo->mProjectile != 0 ? "projectile" : "no projectile") << std::endl;
            }
            else if (name.find("arrow") != std::string::npos)
            {
                ++arrows;
                wrongArrow += isBolt(*ammo) ? 1 : 0;
            }
            else
                ++other;
        }
        std::cout << "SKYRIMSE-RANGED ammo named bolt " << bolts << " (wrong " << wrongBolt << ") named arrow " << arrows
                  << " (wrong " << wrongArrow << ") machine ammunition " << machine << " other " << other << '\n';
        EXPECT_GT(bolts, 0u);
        EXPECT_GT(arrows, 0u);
        EXPECT_EQ(wrongBolt, 0u);
        EXPECT_EQ(wrongArrow, 0u);
    }

    TEST(SkyrimRangedRealData, everyRangedWeaponAnNpcCarriesHasAmmunitionThatFitsIt)
    {
        World world;
        std::string skip;
        if (!loadMasters(world, skip))
            GTEST_SKIP() << skip;
        std::uint64_t bows = 0, crossbows = 0, npcs = 0, withBow = 0, withCrossbow = 0, dualCandidates = 0, unfit = 0,
                      differs = 0, dualAllowed = 0;
        std::vector<std::string> dualExamples, withExamples;
        for (const FormId id : world.mCombat.weaponIds())
        {
            const WeaponRecord* weapon = world.mCombat.weapon(id);
            if (weapon != nullptr && weapon->mHasDnam)
            {
                bows += weapon->mAnimType == WeaponAnimType::Bow ? 1 : 0;
                crossbows += weapon->mAnimType == WeaponAnimType::Crossbow ? 1 : 0;
            }
        }
        for (FormId npc : world.mActors.npcIds())
        {
            std::vector<ArmourPiece> worn;
            std::string error;
            (void)wornArmour(world.mActors, npc, 0, kAppearanceLevel, worn, error);
            FighterGear gear, again;
            if (!resolveFighterGear(world.mActors, world.mCombat, npc, 0, kAppearanceLevel, worn, gear))
                continue;
            ++npcs;
            ASSERT_TRUE(resolveFighterGear(world.mActors, world.mCombat, npc, 0, kAppearanceLevel, worn, again));
            if (gear.mBow != again.mBow || gear.mAmmo != again.mAmmo || gear.mOffHand != again.mOffHand)
                ++differs;
            if (gear.mBow != nullptr)
            {
                (gear.mBow->mAnimType == WeaponAnimType::Crossbow ? withCrossbow : withBow) += 1;
                if (gear.mAmmo == nullptr || !ammoFitsWeapon(*gear.mBow, *gear.mAmmo))
                    ++unfit;
            }
            if (gear.mOffHand != nullptr && gear.mWeapon != nullptr)
            {
                ++dualCandidates;
                const ActorNpc* record = world.mActors.npc(npc);
                const CombatStyleRecord* style = record != nullptr && record->mCombatStyle != kNullFormId
                    ? world.mCombat.style(record->mCombatStyle)
                    : nullptr;
                if (style != nullptr && dualWieldAllowed(style->mFlags, gear.mWeapon, gear.mOffHand))
                {
                    ++dualAllowed;
                    if (dualExamples.size() < 12)
                        dualExamples.push_back(record->mCommon.mEditorId);
                }
            }
            if (gear.mBow != nullptr && withExamples.size() < 12)
                withExamples.push_back(world.mActors.npc(npc)->mCommon.mEditorId
                    + (gear.mBow->mAnimType == WeaponAnimType::Crossbow ? " crossbow" : " bow"));
        }
        std::cout << "SKYRIMSE-RANGED weapons: bows " << bows << " crossbows " << crossbows << "; npcs " << npcs
                  << " with a bow and arrows " << withBow << " with a crossbow and bolts " << withCrossbow
                  << " with a second one-handed weapon " << dualCandidates << '\n';
        std::cout << "SKYRIMSE-RANGED dual wield allowed by the style: " << dualAllowed << "; examples";
        for (const std::string& e : dualExamples)
            std::cout << ' ' << e;
        std::cout << "\nSKYRIMSE-RANGED ranged examples";
        for (const std::string& e : withExamples)
            std::cout << ' ' << e;
        std::cout << std::endl;
        EXPECT_EQ(unfit, 0u);
        EXPECT_EQ(differs, 0u);
        EXPECT_GT(crossbows, 0u);
    }
}
