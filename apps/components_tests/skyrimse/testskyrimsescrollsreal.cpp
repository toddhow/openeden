// Task 0234: the SCRL records of the player's own Skyrim SE masters, through the pack's combat record walk (the path
// the
// game takes). Gated: skipped unless OPENEDEN_SKYRIMSE_DATA names the game's Data folder. Prints the aggregates (lines
// SKYRIMSE-SCROLLS, game numbers and editor ids only) and checks what the scroll cast relies on: every SPIT is the
// 36 byte layout, the cast type of a scroll is normalised, a scroll costs nothing, and the Firebolt and Fast Healing
// scrolls are the aimed and the self scroll the task shows in game.
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

    TEST(SkyrimScrollsReal, everyScrollOfTheFiveMastersDecodesAsACastableSpell)
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
        std::vector<SpellRecord> scrolls; // the winners, by form id
        std::map<FormId, std::size_t> slot;
        std::uint64_t odd = 0;
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(file, nullptr) << n;
            CombatRecords records;
            ASSERT_TRUE(parseCombatRecords(*file, order, ExteriorLimits(), records));
            odd += records.mNotes.mOddSubrecords;
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
        std::cout << "SKYRIMSE-SCROLLS scrolls " << scrolls.size() << " odd subrecords in the walks " << odd << "\n";
        ASSERT_GE(scrolls.size(), 90u);

        std::map<std::uint32_t, std::size_t> casts, deliveries, archetypes;
        std::size_t all = 0, some = 0, none = 0, works = 0;
        std::vector<std::string> notDeliverable;
        std::vector<std::string> notAll;
        const auto find = [&](FormId id) { return index.magicEffect(id); };
        for (const SpellRecord& s : scrolls)
        {
            EXPECT_TRUE(s.mScroll) << s.mCommon.mEditorId;
            EXPECT_TRUE(s.mHasData) << s.mCommon.mEditorId << " has no 36 byte SPIT";
            EXPECT_EQ(s.mCost, 0u) << s.mCommon.mEditorId;
            EXPECT_TRUE(s.mCastType == MagicCast::FireAndForget || s.mCastType == MagicCast::Concentration)
                << s.mCommon.mEditorId << " cast type " << s.mCastType;
            ++casts[s.mCastType];
            ++deliveries[s.mDelivery];
            for (const SpellEffect& e : s.mEffects)
            {
                const MagicEffectRecord* m = find(e.mEffect);
                ++archetypes[m != nullptr ? m->mArchetype : 9999];
            }
            // what the cast really does: every effect applied AND a delivery the cast handles (self and aimed; touch
            // only
            // with the beams setting, target actor and target location are refused by fireCast) AND fire and forget
            const bool deliverable = s.mDelivery == MagicDelivery::Self || s.mDelivery == MagicDelivery::Aimed;
            const ScrollSupport support = classifyScroll(s, find);
            if (support == ScrollSupport::All && deliverable && s.mCastType == MagicCast::FireAndForget)
                ++works;
            else if (support == ScrollSupport::All)
                notDeliverable.push_back(s.mCommon.mEditorId + "(delivery " + std::to_string(s.mDelivery) + ")");
            switch (support)
            {
                case ScrollSupport::All:
                    ++all;
                    break;
                case ScrollSupport::Some:
                    ++some;
                    notAll.push_back(s.mCommon.mEditorId + "(some)");
                    break;
                case ScrollSupport::None:
                    ++none;
                    notAll.push_back(s.mCommon.mEditorId + "(none)");
                    break;
            }
            // the scroll is reachable by form id through the index the game reads, and by no editor id
            EXPECT_EQ(index.spell(s.mCommon.mId) != nullptr, true) << s.mCommon.mEditorId;
        }
        for (const auto& [cast, n] : casts)
            std::cout << "SKYRIMSE-SCROLLS cast type " << cast << " : " << n << "\n";
        for (const auto& [delivery, n] : deliveries)
            std::cout << "SKYRIMSE-SCROLLS delivery " << delivery << " : " << n << "\n";
        for (const auto& [archetype, n] : archetypes)
            std::cout << "SKYRIMSE-SCROLLS effect archetype " << archetype
                      << (scrollArchetypeApplied(archetype) ? " (applied)" : " (not applied)") << " : " << n << "\n";
        std::cout << "SKYRIMSE-SCROLLS support: all " << all << " some " << some << " none " << none << "\n";
        std::cout << "SKYRIMSE-SCROLLS cast completely (every effect applied, self or aimed, fire and forget): "
                  << works << "\n";
        for (const std::string& name : notDeliverable)
            std::cout << "SKYRIMSE-SCROLLS effects applied but the delivery is not cast: " << name << "\n";
        for (const std::string& name : notAll)
            std::cout << "SKYRIMSE-SCROLLS not fully applied: " << name << "\n";

        for (const SpellRecord& s : scrolls)
        {
            if (s.mCommon.mEditorId == "FireboltScroll")
            {
                EXPECT_EQ(s.mDelivery, MagicDelivery::Aimed);
                EXPECT_EQ(classifyScroll(s, find), ScrollSupport::All);
            }
            if (s.mCommon.mEditorId == "FastHealingScroll")
            {
                EXPECT_EQ(s.mDelivery, MagicDelivery::Self);
                EXPECT_EQ(classifyScroll(s, find), ScrollSupport::All);
            }
        }
        EXPECT_EQ(index.spellByEditorId("FireboltScroll"), nullptr) << "scrolls are not reachable by editor id";
    }
}
