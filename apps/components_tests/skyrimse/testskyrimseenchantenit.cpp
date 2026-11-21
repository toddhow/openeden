// Task 0309 (second part at the end: JobInnkeeperFaction): four ENCH records of Skyrim.esm store an ENIT of 32 bytes,
// not 36 (the real-data test SkyrimPerksRealData counted them as odd subrecords once task 0295 added the ENCH decoder).
// Every field the decoder reads lies in the first 24 bytes, so a 32 byte ENIT is as good as a 36 byte one. Generated
// records only.
#include <game_skyrimse/actor_table.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/scroll.hpp>

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    struct EnitFixture
    {
        LoadOrder mOrder = [] {
            LoadOrder order;
            std::string err;
            EXPECT_TRUE(order.add("Skyrim.esm", false, err));
            EXPECT_TRUE(order.add("test.esp", false, err));
            return order;
        }();
        std::unique_ptr<Content::Plugin> mPlugin = [] {
            Content::Error error;
            return Content::Plugin::open(
                std::make_unique<std::istringstream>(
                    tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44).str(), std::ios::binary),
                "test.esp", error);
        }();
        ExteriorLimits mLimits;
        DecodeNotes mNotes;
        FormIdResolver mResolver{ mPlugin->header(), "test.esp", mOrder };
        DecodeContext context() { return DecodeContext{ mResolver, mLimits, mNotes, false }; }
    };

    Bytes enchWithEnit(std::size_t enitBytes)
    {
        Bytes e;
        e.append(sub4("EDID", Bytes().zstring("EnchOldEnit")));
        Bytes enit;
        enit.u32(0).u32(0).u32(1).u32(40).u32(1).u32(6).f32(0.f).u32(0).u32(0);
        std::string text = enit.str();
        text.resize(enitBytes);
        e.append(sub4("ENIT", Bytes().text(text)));
        e.append(sub4("EFID", Bytes().u32(0x00000555)));
        e.append(sub4("EFIT", Bytes().f32(8.f).u32(0).u32(3)));
        return e;
    }

    Content::RawRecord rawEnch()
    {
        Content::RawRecord r;
        r.mType = Content::fourCC("ENCH");
        r.mFormId = 0x01000C10;
        r.mOffset = 0x1234;
        return r;
    }

    TEST(SkyrimEnchantEnit, aThirtyTwoByteEnitDecodesLikeAThirtySixByteOne)
    {
        for (const std::size_t size : { std::size_t(32), std::size_t(36) })
        {
            EnitFixture f;
            const Bytes bytes = enchWithEnit(size);
            std::string why;
            SpellRecord s;
            const std::span<const std::byte> view{ reinterpret_cast<const std::byte*>(bytes.str().data()),
                bytes.str().size() };
            ASSERT_TRUE(decodeEnchantment(view, rawEnch(), f.context(), s, why)) << why;
            EXPECT_TRUE(s.mHasData) << size;
            EXPECT_EQ(s.mCastType, MagicCast::FireAndForget) << size;
            EXPECT_EQ(s.mDelivery, MagicDelivery::Touch) << size;
            EXPECT_EQ(s.mEnchantType, 6u) << size;
            EXPECT_EQ(f.mNotes.mOddSubrecords, 0u) << size;
        }
    }

    TEST(SkyrimEnchantEnit, otherSizesAreStillOdd)
    {
        for (const std::size_t size : { std::size_t(20), std::size_t(33), std::size_t(40) })
        {
            EnitFixture f;
            const Bytes bytes = enchWithEnit(size);
            std::string why;
            SpellRecord s;
            const std::span<const std::byte> view{ reinterpret_cast<const std::byte*>(bytes.str().data()),
                bytes.str().size() };
            ASSERT_TRUE(decodeEnchantment(view, rawEnch(), f.context(), s, why)) << why;
            EXPECT_FALSE(s.mHasData) << size;
            EXPECT_GE(f.mNotes.mOddSubrecords, 1u) << size;
        }
    }

    // Task 0309: every innkeeper of the five masters is listed in JobInnkeeperFaction (FACT 0x5091B) at rank -1 (stored
    // byte 255), and the Innkeeper alias of the bounty quests BQ01 to BQ04 asks GetInFaction on it. Task 0239 made rank
    // -1 "not a member" (hirelings), so the alias found nobody until the faction was listed as one that counts at -1
    // (as JobStewardFaction and JobHostlerFaction are, task 0261 and 0278).
    TEST(SkyrimRankMinusOne, theInnkeeperFactionCountsAtRankMinusOneWithNoSettingAndAnotherFactionDoesNot)
    {
        constexpr FormId kBaseJ = 0x00000A10;
        constexpr FormId kOther = 0x00000B02;
        ActorRecords records;
        ActorNpc npc;
        npc.mCommon.mId = kBaseJ;
        FactionMembership innkeeper;
        innkeeper.mFaction = kJobInnkeeperFaction;
        innkeeper.mRank = -1;
        FactionMembership other;
        other.mFaction = kOther;
        other.mRank = -1;
        npc.mFactions = { innkeeper, other };
        records.mNpcs.push_back(npc);
        ActorIndex actors;
        actors.add(records);
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        DialogueSpeaker speaker;
        speaker.mBase = kBaseJ;
        world.setSpeaker(speaker);
        conditions::Evaluator eval;
        const auto holds = [&](std::uint32_t faction) {
            survey::Ctda c;
            c.mFunction = 71;
            c.mParam1 = faction;
            c.mCompare = 0;
            c.mComparison = 1.f;
            return eval.evaluate(std::span<const survey::Ctda>(&c, 1), world);
        };
        EXPECT_TRUE(holds(kJobInnkeeperFaction));
        EXPECT_FALSE(holds(kOther));
    }
}
