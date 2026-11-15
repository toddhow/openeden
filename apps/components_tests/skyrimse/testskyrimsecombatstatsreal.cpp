// Task 0169 (milestone M-S5, stage S5-1 subset): the combat numbers on the player's own Skyrim SE masters. Gated:
// skipped unless OPENEDEN_SKYRIMSE_DATA names the game's Data folder. Prints aggregates only (lines
// SKYRIMSE-COMBATSTATS, no game text) and checks that every value is finite and inside bounds: it does NOT check the
// formulas against the game (they are RECALLED-VERIFY), only that the data decodes and the pure functions stay sane on
// all of it.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/actor_mechanics.hpp>
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
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

    TEST(SkyrimCombatStatsRealData, everyNpcGetsAFiniteArmourAndAnIronSwordHitIsSane)
    {
        World world;
        std::string skip;
        if (!loadMasters(world, skip))
            GTEST_SKIP() << skip;

        std::vector<std::string> missing;
        const CombatSettings settings = world.mCombat.settings().combatSettings(&missing);
        // The vanilla values from note 0056 section 4 must be what the masters hold.
        EXPECT_TRUE(missing.empty()) << missing.size() << " GMSTs missing, first "
                                     << (missing.empty() ? "" : missing[0]);
        EXPECT_FLOAT_EQ(settings.mMaxArmorRating, 80.f);
        EXPECT_FLOAT_EQ(settings.mArmorScalingFactor, 0.12f);
        EXPECT_FLOAT_EQ(settings.mCombatDistance, 141.f);
        EXPECT_FLOAT_EQ(settings.mStaminaAttackWeaponBase, 20.f);

        // An Iron Sword: the weapon named "IronSword" (editor id), one-handed.
        const WeaponRecord* iron = nullptr;
        std::uint64_t weaponsWithData = 0, melee = 0, badWeapon = 0;
        float maxDamage = 0.f;
        for (FormId id : world.mCombat.weaponIds())
        {
            const WeaponRecord* w = world.mCombat.weapon(id);
            if (w->mCommon.mEditorId == "IronSword")
                iron = w;
            if (w->mHasData && w->mHasDnam)
            {
                ++weaponsWithData;
                if (w->isMelee())
                    ++melee;
                maxDamage = std::max(maxDamage, static_cast<float>(w->mDamage));
                if (!std::isfinite(w->mWeight) || !std::isfinite(w->mReach) || !std::isfinite(w->mStagger)
                    || w->mReach < 0.f || w->mStagger < 0.f || w->mWeight < 0.f)
                    ++badWeapon;
            }
        }
        EXPECT_EQ(badWeapon, 0u);
        ASSERT_NE(iron, nullptr);
        EXPECT_EQ(iron->mAnimType, WeaponAnimType::OneHandSword);
        EXPECT_EQ(iron->skillSlot(), 0);

        // Every NPC, the Iron Sword, against nothing and against its own armour.
        std::uint64_t npcs = 0, resolved = 0, armoured = 0, capped = 0, hitsBad = 0, pieces = 0;
        float maxRating = 0.f, maxPercent = 0.f;
        std::map<std::uint8_t, std::uint64_t> byPieces;
        for (FormId npc : world.mActors.npcIds())
        {
            ++npcs;
            ArmourResult armour;
            std::string error;
            if (!computeActorArmour(world.mActors, settings, npc, 0, 1, armour, error))
                continue; // an NPC whose template chain does not resolve (counted by npcs - resolved)
            ++resolved;
            ++byPieces[armour.mMainPieces];
            if (armour.mRating > 0.f)
                ++armoured;
            if (armour.mMitigationPercent >= settings.mMaxArmorRating)
                ++capped;
            maxRating = std::max(maxRating, armour.mRating);
            maxPercent = std::max(maxPercent, armour.mMitigationPercent);
            if (!std::isfinite(armour.mRating) || !std::isfinite(armour.mMitigationPercent)
                || armour.mMitigationPercent < 0.f || armour.mMitigationPercent > settings.mMaxArmorRating)
                ++hitsBad;
            std::vector<ArmourPiece> worn;
            if (wornArmour(world.mActors, npc, 0, 1, worn, error))
                pieces += worn.size();

            MeleeInput in;
            in.mWeapon = iron;
            in.mAttackerSkill = 50.f;
            in.mTargetMitigationPercent = armour.mMitigationPercent;
            const MeleeHit hit = computeMeleeHit(in, settings);
            if (!std::isfinite(hit.mDamageBefore) || !std::isfinite(hit.mDamageAfter)
                || !std::isfinite(hit.mStaminaCost) || hit.mDamageAfter < 0.f || hit.mDamageAfter > hit.mDamageBefore
                || hit.mStaminaCost <= 0.f)
                ++hitsBad;
        }
        EXPECT_EQ(hitsBad, 0u);
        EXPECT_GT(resolved, npcs * 9 / 10);
        EXPECT_GT(armoured, 100u);

        std::cout << "SKYRIMSE-COMBATSTATS weapons " << world.mCombat.weaponCount() << " with-data-and-dnam "
                  << weaponsWithData << " melee " << melee << " max-damage " << maxDamage << " races "
                  << world.mCombat.raceCount() << " gmsts " << world.mCombat.settings().size() << " missing-gmsts "
                  << missing.size() << " odd-weap " << world.mOddWeapon << " odd-race " << world.mOddRace
                  << " odd-gmst " << world.mOddSetting << " errors " << world.mErrors << "\n";
        std::cout << "SKYRIMSE-COMBATSTATS npcs " << npcs << " resolved " << resolved << " with-armour " << armoured
                  << " worn-pieces " << pieces << " at-cap " << capped << " max-rating " << maxRating << " max-percent "
                  << maxPercent << " bad " << hitsBad << "\n";
        for (const auto& entry : byPieces)
            std::cout << "SKYRIMSE-COMBATSTATS main-pieces " << static_cast<int>(entry.first) << " npcs "
                      << entry.second << "\n";
    }

    TEST(SkyrimCombatStatsRealData, everyRaceHasSaneRegenAndUnarmedNumbers)
    {
        World world;
        std::string skip;
        if (!loadMasters(world, skip))
            GTEST_SKIP() << skip;
        std::uint64_t withData = 0, bad = 0;
        float maxUnarmed = 0.f, minReach = 1e9f, maxReach = 0.f;
        // The races are reached through the NPC's resolved race.
        std::map<FormId, bool> seen;
        for (FormId npc : world.mActors.npcIds())
        {
            ActorIndex::Resolved resolved;
            if (world.mActors.resolve(npc, kAppearanceLevel, 0, resolved) != ActorIndex::ResolveStatus::Ok)
                continue;
            if (seen.emplace(resolved.mRace, true).second == false)
                continue;
            const RaceCombatRecord* race = world.mCombat.race(resolved.mRace);
            if (race == nullptr || !race->mHasData)
                continue;
            ++withData;
            const float values[]{ race->mHealthRegen, race->mMagickaRegen, race->mStaminaRegen, race->mUnarmedDamage,
                race->mUnarmedReach };
            for (float v : values)
                if (!std::isfinite(v) || v < 0.f || v > 100000.f)
                    ++bad;
            maxUnarmed = std::max(maxUnarmed, race->mUnarmedDamage);
            minReach = std::min(minReach, race->mUnarmedReach);
            maxReach = std::max(maxReach, race->mUnarmedReach);
        }
        EXPECT_EQ(bad, 0u);
        EXPECT_GT(withData, 10u);
        std::cout << "SKYRIMSE-COMBATSTATS races-with-data " << withData << " bad " << bad << " max-unarmed-damage "
                  << maxUnarmed << " unarmed-reach " << minReach << ".." << maxReach << "\n";
    }
}
