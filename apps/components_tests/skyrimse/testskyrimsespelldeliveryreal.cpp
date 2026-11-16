// Task 0242: the EXPL and PROJ records, the deliveries and the scroll counts of the player's own Skyrim SE masters,
// through the pack's combat record walk (the path the game takes). Gated: skipped unless OPENEDEN_SKYRIMSE_DATA names
// the game's Data folder. Prints aggregates (lines SKYRIMSE-DELIVERY, game numbers and editor ids only).
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/scroll.hpp>

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
    using namespace game_skyrimse;

    // Task 0242: the EXPL records and the PROJ additions of the five masters, the delivery of every spell and scroll,
    // and how many scrolls the cast now handles with the delivery setting on and off. Gated like the scrolls test.
    TEST(SkyrimDeliveryReal, theExplosionsTheRunesAndTheDeliveriesOfTheFiveMasters)
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
            if (!fs::exists(data / n) || !order.add(n, false, err))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
        CombatIndex index;
        std::vector<SpellRecord> scrolls;
        std::map<FormId, std::size_t> slot;
        std::size_t explosions = 0, explosionsWithData = 0, projectilesWithExplosion = 0, projectilesChecked = 0;
        std::uint64_t odd = 0;
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(file, nullptr) << n;
            CombatRecords records;
            ASSERT_TRUE(parseCombatRecords(*file, order, ExteriorLimits(), records));
            odd += records.mNotes.mOddSubrecords;
            explosions += records.mExplosions.size();
            for (const ExplosionRecord& x : records.mExplosions)
                if (x.mHasData)
                    ++explosionsWithData;
            for (const ProjectileRecord& p : records.mProjectiles)
            {
                ++projectilesChecked;
                if (p.mExplosion != 0)
                    ++projectilesWithExplosion;
            }
            for (const SpellRecord& s : records.mScrolls)
            {
                const auto it = slot.find(s.mCommon.mId);
                if (it == slot.end())
                {
                    slot[s.mCommon.mId] = scrolls.size();
                    scrolls.push_back(s);
                }
                else
                    scrolls[it->second] = s;
            }
            index.add(records);
        }
        std::cout << "SKYRIMSE-DELIVERY explosions " << explosions << " with data " << explosionsWithData
                  << " projectiles " << projectilesChecked << " with an explosion " << projectilesWithExplosion
                  << " odd subrecords " << odd << "\n";
        EXPECT_EQ(explosions, explosionsWithData) << "every EXPL has a DATA of 40 to 52 bytes";
        EXPECT_GE(explosions, 130u);
        EXPECT_GE(projectilesWithExplosion, 30u);

        // The three runes: the PROJ names an EXPL of radius 320 and a trigger proximity of 150 (the layout read).
        std::size_t runes = 0;
        for (const SpellRecord& s : scrolls)
        {
            if (s.mDelivery != MagicDelivery::TargetLocation)
                continue;
            for (const SpellEffect& e : s.mEffects)
            {
                const MagicEffectRecord* m = index.magicEffect(e.mEffect);
                if (m == nullptr || m->mProjectile == kNullFormId)
                    continue;
                const ProjectileRecord* p = index.projectile(m->mProjectile);
                ASSERT_NE(p, nullptr) << s.mCommon.mEditorId;
                EXPECT_FLOAT_EQ(p->mProximity, 150.f) << s.mCommon.mEditorId;
                const ExplosionRecord* x = index.explosion(p->mExplosion);
                ASSERT_NE(x, nullptr) << s.mCommon.mEditorId;
                EXPECT_FLOAT_EQ(x->mRadius, 320.f) << s.mCommon.mEditorId;
                ++runes;
            }
        }
        EXPECT_EQ(runes, 3u) << "Fire, Frost and Shock Rune scrolls";

        std::map<std::uint32_t, std::size_t> deliveries;
        std::size_t worksBefore = 0, worksAfter = 0, all = 0, some = 0, none = 0;
        std::vector<std::string> gained;
        const auto find = [&](FormId id) { return index.magicEffect(id); };
        for (const SpellRecord& s : scrolls)
        {
            ++deliveries[s.mDelivery];
            const ScrollSupport support = classifyScroll(s, find);
            const bool fireAndForget = s.mCastType == MagicCast::FireAndForget;
            if (support == ScrollSupport::All)
                ++all;
            else if (support == ScrollSupport::Some)
                ++some;
            else
                ++none;
            if (support == ScrollSupport::All && fireAndForget && deliveryIsCast(s.mDelivery, false, false))
                ++worksBefore;
            if (support == ScrollSupport::All && fireAndForget && deliveryIsCast(s.mDelivery, false, true))
            {
                ++worksAfter;
                if (!deliveryIsCast(s.mDelivery, false, false))
                    gained.push_back(s.mCommon.mEditorId);
            }
        }
        for (const auto& [delivery, n] : deliveries)
            std::cout << "SKYRIMSE-DELIVERY scroll delivery " << delivery << " : " << n << "\n";
        std::cout << "SKYRIMSE-DELIVERY scrolls " << scrolls.size() << " support: all " << all << " some " << some
                  << " none " << none << "\n";
        std::cout << "SKYRIMSE-DELIVERY scrolls cast completely: delivery off " << worksBefore << ", delivery on "
                  << worksAfter << "\n";
        for (const std::string& name : gained)
            std::cout << "SKYRIMSE-DELIVERY gained: " << name << "\n";
        EXPECT_EQ(worksAfter, worksBefore + 4) << "Heal Other and the three runes";
    }
}
