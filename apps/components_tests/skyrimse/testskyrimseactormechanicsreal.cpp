// Task 0154 (stage S7): the actor numbers and relations on the player's own Skyrim SE masters. The expected values were
// derived from the records by an independent Python walker (its own plugin reader and formulas, no C++ shared), see the
// task's stage log, EXCEPT where a comment says otherwise (Hewnon's level 23 is the pack's own half-up rounding).
// Prints aggregates (lines SKYRIMSE-MECHANICS) and checks a handful of named actors. Skipped when the environment is
// not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/actor_mechanics.hpp>
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

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
    namespace sdkgame = openeden::sdk::game;
    using namespace game_skyrimse;

    struct World
    {
        ActorIndex mIndex;
        std::map<std::string, FormId> mByEditorId;
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
            world.mIndex.add(records);
            ParsedPlugin parsed;
            ParseLimits limits;
            limits.mSkipReferences = true;
            (void)parseRecords(*file, order, limits, parsed);
            world.mIndex.addLeveledLists(parsed.mLeveledLists);
        }
        for (FormId id : world.mIndex.npcIds())
            world.mByEditorId[world.mIndex.npc(id)->mCommon.mEditorId] = id;
        return true;
    }

    std::uint32_t sum(const sdkgame::ActorStats& s)
    {
        std::uint32_t total = 0;
        for (std::uint8_t v : s.skills)
            total += v;
        return total;
    }

    TEST(SkyrimActorMechanicsRealData, namedActorsMatchTheIndependentDerivation)
    {
        World world;
        std::string skip;
        if (!loadMasters(world, skip))
            GTEST_SKIP() << skip;
        const auto base = [&](const char* name) {
            const auto it = world.mByEditorId.find(name);
            EXPECT_NE(it, world.mByEditorId.end()) << name;
            return it == world.mByEditorId.end() ? kNullFormId : it->second;
        };
        sdkgame::ActorStats stats;
        sdkgame::ActorDisposition disposition;
        std::string error;
        const MechanicsOptions options;

        // Hulda: a fixed level 4 innkeeper, stored totals 75 / 60 / 60, skills from DNAM (sum 329), a unique woman,
        // townsfolk factions with no relation to the player faction, unaggressive.
        ASSERT_TRUE(computeActorStats(world.mIndex, base("Hulda"), 0, 1, stats, error)) << error;
        EXPECT_EQ(stats.level, 4);
        EXPECT_FLOAT_EQ(stats.health, 75.f);
        EXPECT_FLOAT_EQ(stats.magicka, 60.f);
        EXPECT_FLOAT_EQ(stats.stamina, 60.f);
        EXPECT_EQ(sum(stats), 329u);
        EXPECT_EQ(stats.skills[0], 22);
        EXPECT_EQ(stats.skills[11], 27); // speech
        EXPECT_TRUE(stats.female);
        EXPECT_TRUE(stats.unique);
        EXPECT_FALSE(stats.essential);
        EXPECT_FALSE(stats.scalesWithPlayer);
        ASSERT_TRUE(computeActorDisposition(world.mIndex, base("Hulda"), 0, options, disposition, error)) << error;
        EXPECT_EQ(disposition.relationToPlayer, sdkgame::ActorRelation::Neutral);
        EXPECT_EQ(disposition.aggression, sdkgame::ActorAggression::Unaggressive);
        EXPECT_FALSE(disposition.hostileOnSight);

        // Nazeem: the same level and totals, a man, skills sum 329 as well.
        ASSERT_TRUE(computeActorStats(world.mIndex, base("Nazeem"), 0, 1, stats, error)) << error;
        EXPECT_EQ(stats.level, 4);
        EXPECT_FLOAT_EQ(stats.health, 75.f);
        EXPECT_EQ(stats.skills[0], 27);
        EXPECT_FALSE(stats.female);

        // The Shade of Kilkreath: level 26, 300 / 275 / 50, very aggressive, an enemy of the player faction.
        ASSERT_TRUE(computeActorStats(world.mIndex, base("DA09ShadeUnique"), 0, 1, stats, error)) << error;
        EXPECT_EQ(stats.level, 26);
        EXPECT_FLOAT_EQ(stats.health, 300.f);
        EXPECT_FLOAT_EQ(stats.magicka, 275.f);
        EXPECT_FLOAT_EQ(stats.stamina, 50.f);
        ASSERT_TRUE(computeActorDisposition(world.mIndex, base("DA09ShadeUnique"), 0, options, disposition, error))
            << error;
        EXPECT_EQ(disposition.relationToPlayer, sdkgame::ActorRelation::Enemy);
        EXPECT_EQ(disposition.aggression, sdkgame::ActorAggression::VeryAggressive);
        EXPECT_TRUE(disposition.hostileOnSight);

        // Drahff scales with the player: multiplier 0.5, calc min 6, calc max 25, class weights 3 0 2 (gain 11 / 0 /
        // 4), stored totals 105 / 50 / 95 at level 6.
        ASSERT_TRUE(computeActorStats(world.mIndex, base("Drahff"), 0, 1, stats, error)) << error;
        EXPECT_TRUE(stats.scalesWithPlayer);
        EXPECT_EQ(stats.level, 6);
        EXPECT_FLOAT_EQ(stats.health, 105.f);
        EXPECT_FLOAT_EQ(stats.stamina, 95.f);
        ASSERT_TRUE(computeActorStats(world.mIndex, base("Drahff"), 0, 10, stats, error));
        EXPECT_EQ(stats.level, 6); // 10 * 0.5 = 5, below calc min
        ASSERT_TRUE(computeActorStats(world.mIndex, base("Drahff"), 0, 30, stats, error));
        EXPECT_EQ(stats.level, 15);
        EXPECT_FLOAT_EQ(stats.health, 204.f);
        EXPECT_FLOAT_EQ(stats.magicka, 50.f);
        EXPECT_FLOAT_EQ(stats.stamina, 131.f);
        ASSERT_TRUE(computeActorStats(world.mIndex, base("Drahff"), 0, 200, stats, error));
        EXPECT_EQ(stats.level, 25); // calc max
        ASSERT_TRUE(computeActorDisposition(world.mIndex, base("Drahff"), 0, options, disposition, error));
        EXPECT_EQ(disposition.relationToPlayer, sdkgame::ActorRelation::Enemy);
        EXPECT_EQ(disposition.aggression, sdkgame::ActorAggression::Aggressive);
        EXPECT_TRUE(disposition.hostileOnSight);

        // Hewnon: multiplier 0.75, calc min 8, class weights 4 0 2 (gain 11.667 / 0 / 3.333). At player level 30 the
        // product is 22.5. NOT INDEPENDENT: the walker (Python's round() is banker's) said level 22 (294.33 / 170.67);
        // truncation also gives 22. These are the PACK'S half-up rule (RECALLED-VERIFY), pinned here, not checked
        // against the game: level 23, 15 levels above calc min: 131 + 15 * 35 / 3 = 306 and 124 + 15 * 10 / 3 = 174.
        ASSERT_TRUE(computeActorStats(world.mIndex, base("Hewnon"), 0, 30, stats, error));
        EXPECT_EQ(stats.level, 23);
        EXPECT_NEAR(stats.health, 306.f, 0.01f);
        EXPECT_NEAR(stats.stamina, 174.f, 0.01f);

        // A Whiterun guard whose stats come through GuardWhiterunImperialTemplate and the leveled list LvlGuardImperial
        // (LCharGuardImperial): every entry the list can pick (9 NPCs, all eligible at level 1) derives to level 20 and
        // 252 / 50 / 183 in the walker (Python, own chain walk and formula), at player level 1, so the numbers do not
        // depend on which entry the placement number picks. The pick itself is stable.
        sdkgame::ActorStats a;
        sdkgame::ActorStats b;
        ASSERT_TRUE(computeActorStats(world.mIndex, base("GuardWhiterunImperialExterior"), 7, 1, a, error)) << error;
        ASSERT_TRUE(computeActorStats(world.mIndex, base("GuardWhiterunImperialExterior"), 7, 1, b, error));
        EXPECT_EQ(a.level, 20);
        EXPECT_TRUE(a.scalesWithPlayer);
        EXPECT_FLOAT_EQ(a.health, 252.f);
        EXPECT_FLOAT_EQ(a.magicka, 50.f);
        EXPECT_FLOAT_EQ(a.stamina, 183.f);
        EXPECT_EQ(a.level, b.level);
        EXPECT_FLOAT_EQ(a.health, b.health);
        for (std::uint32_t variation : { 0u, 1u, 2u, 3u, 5u, 11u })
        {
            ASSERT_TRUE(computeActorStats(world.mIndex, base("GuardWhiterunImperialExterior"), variation, 1, b, error));
            EXPECT_FLOAT_EQ(b.health, 252.f) << variation;
            EXPECT_FLOAT_EQ(b.stamina, 183.f) << variation;
        }
        // Whiterun's guards and the player's faction are not enemies.
        ASSERT_TRUE(computeActorDisposition(
            world.mIndex, base("GuardWhiterunImperialExterior"), 7, options, disposition, error));
        EXPECT_NE(disposition.relationToPlayer, sdkgame::ActorRelation::Enemy);
    }

    TEST(SkyrimActorMechanicsRealData, aggregates)
    {
        World world;
        std::string skip;
        if (!loadMasters(world, skip))
            GTEST_SKIP() << skip;
        std::uint64_t ok = 0;
        std::uint64_t failed = 0;
        std::uint64_t scales = 0;
        std::uint64_t hostile = 0;
        std::uint64_t enemy = 0;
        std::uint64_t zeroHealth = 0;
        const MechanicsOptions options;
        for (FormId id : world.mIndex.npcIds())
        {
            sdkgame::ActorStats stats;
            sdkgame::ActorDisposition disposition;
            std::string error;
            if (!computeActorStats(world.mIndex, id, 3, 1, stats, error)
                || !computeActorDisposition(world.mIndex, id, 3, options, disposition, error))
            {
                ++failed;
                continue;
            }
            ++ok;
            scales += stats.scalesWithPlayer;
            hostile += disposition.hostileOnSight;
            enemy += disposition.relationToPlayer == sdkgame::ActorRelation::Enemy;
            zeroHealth += stats.health <= 0.f;
        }
        std::cout << "SKYRIMSE-MECHANICS npcs " << world.mIndex.npcCount() << " ok " << ok << " failed " << failed
                  << " scale-with-player " << scales << " enemy-of-player-faction " << enemy << " hostile-on-sight "
                  << hostile << " zero-health " << zeroHealth << "\n";
        EXPECT_GT(ok, 6000u);
        EXPECT_LT(failed, 200u);
    }
}
