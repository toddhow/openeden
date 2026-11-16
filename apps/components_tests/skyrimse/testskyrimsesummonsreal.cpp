// Task 0250: the summoning effects of the player's own Skyrim SE masters, through the pack's record walks (the path the
// game takes). Gated: skipped unless OPENEDEN_SKYRIMSE_DATA names the game's Data folder. Prints aggregates (lines
// SKYRIMSE-SUMMONS: game numbers and editor ids only) and counts how many scrolls and spells now cast completely.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/scroll.hpp>
#include <game_skyrimse/summons.hpp>

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

    TEST(SkyrimSummonsReal, theSummonAndReanimateEffectsOfTheFiveMasters)
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
        ActorIndex actors;
        std::map<FormId, SpellRecord> spells; // spells and scrolls, a later plugin replacing an earlier one
        std::map<FormId, bool> scrollIds;
        std::uint64_t odd = 0;
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(file, nullptr) << n;
            CombatRecords records;
            ASSERT_TRUE(parseCombatRecords(*file, order, ExteriorLimits(), records));
            odd += records.mNotes.mOddSubrecords;
            for (const SpellRecord& s : records.mSpells)
            {
                spells[s.mCommon.mId] = s;
                scrollIds[s.mCommon.mId] = false;
            }
            for (const SpellRecord& s : records.mScrolls)
            {
                spells[s.mCommon.mId] = s;
                scrollIds[s.mCommon.mId] = true;
            }
            index.add(records);
            ActorRecords actorRecords;
            ASSERT_TRUE(parseActorRecords(*file, order, ActorLimits(), actorRecords));
            actors.add(actorRecords);
            ParsedPlugin parsed;
            ParseLimits limits;
            limits.mSkipReferences = true;
            ASSERT_TRUE(parseRecords(*file, order, limits, parsed));
            actors.addLeveledLists(parsed.mLeveledLists);
        }
        const auto find = [&](FormId id) { return index.magicEffect(id); };

        // The archetype numbers: every effect that has one of ours, with its associated form.
        std::map<std::uint32_t, std::size_t> byArchetype;
        std::size_t summonEffects = 0, namedNpc = 0, namedList = 0, namedNothing = 0, reanimateEffects = 0;
        for (const auto& [id, s] : spells)
        {
            for (const SpellEffect& e : s.mEffects)
            {
                const MagicEffectRecord* m = find(e.mEffect);
                if (m == nullptr || !m->mHasData)
                    continue;
                ++byArchetype[m->mArchetype];
                if (m->mArchetype == SummonArchetype::Reanimate)
                    ++reanimateEffects;
                if (m->mArchetype != SummonArchetype::SummonCreature)
                    continue;
                ++summonEffects;
                if (actors.npc(m->mAssociatedItem) != nullptr)
                    ++namedNpc;
                else if (actors.leveledList(m->mAssociatedItem) != nullptr)
                    ++namedList;
                else
                {
                    ++namedNothing;
                    std::cout << "SKYRIMSE-SUMMONS summon effect " << m->mCommon.mEditorId << " names form "
                              << m->mAssociatedItem << " (not an NPC_ or leveled list the actor walk read)\n";
                }
            }
        }
        std::cout << "SKYRIMSE-SUMMONS spell and scroll effects by archetype:";
        for (const auto& [archetype, n] : byArchetype)
            std::cout << " " << archetype << "=" << n;
        std::cout << "\nSKYRIMSE-SUMMONS summon effects " << summonEffects << ": associated NPC_ " << namedNpc
                  << ", leveled NPC list " << namedList << ", neither " << namedNothing << "; reanimate effects "
                  << reanimateEffects << " odd subrecords " << odd << "\n";
        EXPECT_GT(summonEffects, 10u);
        EXPECT_LE(namedNothing, summonEffects / 4) << "most Summon Creature effects name an NPC_";
        EXPECT_GT(reanimateEffects, 2u);

        // Which spells and scrolls carry one, and how many now cast completely.
        std::size_t scrollsAll = 0, scrollsAllBefore = 0, spellsAll = 0, spellsAllBefore = 0, scrolls = 0, plain = 0;
        for (const auto& [id, s] : spells)
        {
            const bool scroll = scrollIds[id];
            if (!scroll && (!s.mHasData || s.mType != 0))
                continue; // abilities, powers, diseases: not cast from a book
            if (scroll)
                ++scrolls;
            else
                ++plain;
            bool hasSummon = false;
            for (const SpellEffect& e : s.mEffects)
            {
                const MagicEffectRecord* m = find(e.mEffect);
                if (m != nullptr && m->mHasData
                    && (m->mArchetype == SummonArchetype::SummonCreature
                        || m->mArchetype == SummonArchetype::Reanimate))
                    hasSummon = true;
            }
            const bool castable = (s.mCastType == MagicCast::FireAndForget || s.mCastType == 3)
                && deliveryIsCast(s.mDelivery, false, true);
            const bool castableBefore = (s.mCastType == MagicCast::FireAndForget || s.mCastType == 3)
                && deliveryIsCast(s.mDelivery, false, true);
            const bool allNow = classifyScroll(s, find, true) == ScrollSupport::All && castable;
            const bool allBefore = classifyScroll(s, find, false) == ScrollSupport::All && castableBefore;
            if (scroll)
            {
                scrollsAll += allNow ? 1 : 0;
                scrollsAllBefore += allBefore ? 1 : 0;
            }
            else
            {
                spellsAll += allNow ? 1 : 0;
                spellsAllBefore += allBefore ? 1 : 0;
            }
            if (hasSummon)
            {
                std::cout << "SKYRIMSE-SUMMONS " << (scroll ? "scroll " : "spell ") << s.mCommon.mEditorId << " cost "
                          << s.mCost << " delivery " << s.mDelivery << " cast " << s.mCastType << " effects "
                          << s.mEffects.size() << (allNow ? " cast completely" : " partly") << " [archetypes";
                for (const SpellEffect& e : s.mEffects)
                {
                    const MagicEffectRecord* m = find(e.mEffect);
                    std::cout << " " << (m != nullptr ? static_cast<int>(m->mArchetype) : -1) << "/" << e.mMagnitude
                              << "/" << e.mDuration << "s";
                }
                std::cout << "]\n";
            }
        }
        std::cout << "SKYRIMSE-SUMMONS scrolls " << scrolls << " cast completely: summons off " << scrollsAllBefore
                  << ", summons on " << scrollsAll << "\n";
        std::cout << "SKYRIMSE-SUMMONS spells (type 0) " << plain
                  << " cast completely (fire and forget, delivery on): summons off " << spellsAllBefore
                  << ", summons on " << spellsAll << "\n";
        EXPECT_GT(scrollsAll, scrollsAllBefore);
        EXPECT_GT(spellsAll, spellsAllBefore);
    }
}
