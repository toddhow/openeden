// Task 0190 (milestone M-S5, stage S5-9): the combat AI's inputs on the player's own Skyrim SE masters. Gated: skipped
// unless OPENEDEN_SKYRIMSE_DATA names the game's Data folder. Prints aggregates only (lines SKYRIMSE-COMBATAI, no game
// text) and checks that every CSTY decodes with finite, non-negative numbers, that every NPC's AIDT is inside the
// ranges
// the AI relies on and that a combat style named by an NPC exists.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/actor_mechanics.hpp>
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/combat_ai.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

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

    TEST(SkyrimCombatAiRealData, everyStyleDecodesAndEveryNpcsAiDataIsInRange)
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
        ActorIndex actors;
        CombatIndex combat;
        std::uint64_t oddStyle = 0, errors = 0;
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(file, nullptr) << n;
            ActorRecords records;
            (void)parseActorRecords(*file, order, ActorLimits(), records);
            actors.add(records);
            ParsedPlugin parsed;
            ParseLimits limits;
            limits.mSkipReferences = true;
            (void)parseRecords(*file, order, limits, parsed);
            actors.addLeveledLists(parsed.mLeveledLists);
            CombatRecords records2;
            (void)parseCombatRecords(*file, order, ExteriorLimits(), records2);
            combat.add(records2);
            errors += records2.mErrors.size();
            for (const auto& entry : records2.mNotes.mOddBy)
                if (entry.first.rfind("CSTY", 0) == 0)
                    oddStyle += entry.second;
        }
        EXPECT_EQ(errors, 0u);
        EXPECT_EQ(oddStyle, 0u);
        EXPECT_GE(combat.styleCount(), 140u);

        // Every NPC: AIDT in range, its style (if it names one) is in the index, hostile on sight counted.
        std::uint64_t npcs = 0, withAi = 0, namedStyle = 0, styleFound = 0, hostile = 0, outOfRange = 0;
        std::map<int, std::uint64_t> byAggression, byConfidence;
        std::map<int, std::uint64_t> fleeBand; // the flee fraction in percent
        for (FormId id : actors.npcIds())
        {
            ++npcs;
            ActorIndex::Resolved resolved;
            if (actors.resolve(id, 30, 0, resolved) != ActorIndex::ResolveStatus::Ok)
                continue;
            const ActorNpc* owner = actors.npc(resolved.mAiFrom);
            if (owner == nullptr || !owner->mAi)
                continue;
            ++withAi;
            const AiData& ai = *owner->mAi;
            ++byAggression[ai.mAggression];
            ++byConfidence[ai.mConfidence];
            if (ai.mAggression > 3 || ai.mConfidence > 4)
                ++outOfRange;
            ++fleeBand[static_cast<int>(std::lround(fleeHealthFraction(ai.mConfidence) * 100.f))];
            if (owner->mCombatStyle != kNullFormId)
            {
                ++namedStyle;
                if (combat.style(owner->mCombatStyle) != nullptr)
                    ++styleFound;
            }
            openeden::sdk::game::ActorDisposition disposition;
            std::string dispositionError;
            if (computeActorDisposition(actors, id, 0, MechanicsOptions(), disposition, dispositionError)
                && disposition.hostileOnSight)
                ++hostile;
        }
        EXPECT_EQ(outOfRange, 0u);
        EXPECT_EQ(namedStyle, styleFound) << "an NPC names a combat style that no plugin of the list has";

        // The styles' numbers: finite and not negative.
        std::uint64_t badStyles = 0;
        std::map<std::uint32_t, std::uint64_t> flags;
        for (FormId id : actors.npcIds())
        {
            const ActorNpc* npc = actors.npc(id);
            if (npc == nullptr || npc->mCombatStyle == kNullFormId)
                continue;
            const CombatStyleRecord* s = combat.style(npc->mCombatStyle);
            if (s == nullptr)
                continue;
            const float v[] = { s->mOffensive, s->mDefensive, s->mGroupOffensive, s->mMagicScore, s->mRangedScore,
                s->mBash, s->mPowerAttackBlocking, s->mCircle, s->mFallback };
            for (float x : v)
                if (!std::isfinite(x) || x < 0.f)
                    ++badStyles;
            ++flags[s->mFlags];
        }
        EXPECT_EQ(badStyles, 0u);

        std::cout << "SKYRIMSE-COMBATAI styles " << combat.styleCount() << " odd " << oddStyle << " npcs " << npcs
                  << " with-aidt " << withAi << " name-a-style " << namedStyle << " found " << styleFound
                  << " hostile-on-sight " << hostile << " bad " << badStyles << "\n";
        for (const auto& e : byAggression)
            std::cout << "SKYRIMSE-COMBATAI aggression " << e.first << " npcs " << e.second << "\n";
        for (const auto& e : byConfidence)
            std::cout << "SKYRIMSE-COMBATAI confidence " << e.first << " npcs " << e.second << " flee-percent "
                      << static_cast<int>(std::lround(fleeHealthFraction(static_cast<std::uint8_t>(e.first)) * 100.f))
                      << "\n";
        for (const auto& e : flags)
            std::cout << "SKYRIMSE-COMBATAI style-flags " << e.first << " npcs " << e.second << "\n";
    }
}
