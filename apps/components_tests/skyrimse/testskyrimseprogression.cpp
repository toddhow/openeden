// Task 0200: the player's skills, levelling and perks (game_skyrimse/progression.hpp) on generated data. No game data.
// The AVIF layout and the GMST numbers are the data's (stage log of task 0200); the formulas are RECALLED-VERIFY and
// these tests pin THIS implementation. Every number is derived by hand.
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/progression.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::progression;

    LoadOrder oneMaster()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    struct Fixture
    {
        LoadOrder mOrder = oneMaster();
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

    std::span<const std::byte> view(const Bytes& b)
    {
        return { reinterpret_cast<const std::byte*>(b.str().data()), b.str().size() };
    }

    Content::RawRecord raw(const char (&type)[5], std::uint32_t id)
    {
        Content::RawRecord r;
        r.mType = Content::fourCC(type);
        r.mFormId = id;
        r.mOffset = 0x99;
        return r;
    }

    survey::Ctda cond(std::uint16_t fn, std::uint8_t compare, float value, std::uint32_t p1 = 0)
    {
        survey::Ctda c;
        c.mCompare = compare;
        c.mComparison = value;
        c.mFunction = fn;
        c.mParam1 = p1;
        return c;
    }

    class FakePerks final : public PerkLibrary
    {
    public:
        const PerkRecord* perk(FormId id) const override
        {
            const auto it = mPerks.find(id);
            return it == mPerks.end() ? nullptr : &it->second;
        }
        void add(FormId id, FormId next, std::vector<survey::Ctda> prerequisites = {})
        {
            PerkRecord p;
            p.mCommon.mId = id;
            p.mNext = next;
            p.mPrerequisites = std::move(prerequisites);
            mPerks[id] = std::move(p);
        }
        std::map<FormId, PerkRecord> mPerks;
    };

    constexpr FormId kA1 = 0x100, kA2 = 0x101, kB = 0x200;

    // A skill table of simple numbers: use mult 1, offset 0, improve mult 1, offset 0 for every skill; skill 0 (One-
    // Handed) has a two node tree. The curve is set to 2 in the settings so that the need at level L is L * L.
    SkillTable makeTable()
    {
        SkillTable table;
        for (std::size_t s = 0; s < kSkillCount; ++s)
        {
            SkillRecord r;
            r.mCommon.mId = 0x44C + static_cast<FormId>(s);
            r.mCommon.mEditorId = std::string(skillEditorId(s));
            r.mHasSkillData = true;
            r.mUseMult = 1.f;
            r.mUseOffset = 0.f;
            r.mImproveMult = 1.f;
            r.mImproveOffset = 0.f;
            if (s == 0)
            {
                PerkNode root;
                root.mIndex = 0;
                root.mChildren = { 1, 2 };
                PerkNode a;
                a.mIndex = 1;
                a.mPerk = kA1;
                PerkNode b;
                b.mIndex = 2;
                b.mPerk = kB;
                r.mNodes = { root, a, b };
            }
            table.add(r);
        }
        return table;
    }

    Settings simple()
    {
        Settings s;
        s.mSkillUseCurve = 2.f;
        return s;
    }

    struct Rig
    {
        FakePerks mPerks;
        SkillTable mTable = makeTable();
        std::unique_ptr<PlayerProgression> mP;
        Rig()
        {
            mPerks.add(kA1, kA2);
            mPerks.add(kA2, 0, { cond(448, 0, 1.f, kA1) }); // HasPerk(A1)
            mPerks.add(kB, 0, { cond(277, 3, 30.f, 6), cond(448, 0, 1.f, kA1) }); // One-Handed >= 30 and A1
            mP = std::make_unique<PlayerProgression>(mTable, &mPerks, simple());
        }
    };

    // ---- names -----------------------------------------------------------------------------------------------------

    TEST(SkyrimProgressionNames, theEighteenSkillsHaveEditorIdsAndNamesInActorValueOrder)
    {
        EXPECT_EQ(skillEditorId(0), "AVOneHanded");
        EXPECT_EQ(skillEditorId(2), "AVMarksman");
        EXPECT_EQ(skillEditorId(15), "AVMysticism"); // Illusion's editor id
        EXPECT_EQ(skillEditorId(17), "AVEnchanting");
        EXPECT_TRUE(skillEditorId(18).empty());
        EXPECT_EQ(skillName(2), "Archery");
        EXPECT_EQ(skillName(15), "Illusion");
        EXPECT_EQ(slotOfSkillEditorId("avmysticism"), 15u);
        EXPECT_EQ(slotOfSkillEditorId("AVHealth"), kSkillCount);
        EXPECT_EQ(slotOfMagicSkillValue(20), Slot::Destruction);
        EXPECT_EQ(slotOfMagicSkillValue(18), Slot::Alteration);
        EXPECT_EQ(slotOfMagicSkillValue(22), Slot::Restoration);
        EXPECT_EQ(slotOfMagicSkillValue(-1), kSkillCount);
        EXPECT_EQ(slotOfMagicSkillValue(25), kSkillCount);
    }

    // ---- the AVIF decoder ------------------------------------------------------------------------------------------

    TEST(SkyrimProgressionDecode, anAvifWithSkillDataAndATreeDecodes)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("AVOneHanded")));
        subs.append(sub4("CNAM", Bytes().u32(1)));
        subs.append(sub4("AVSK", Bytes().f32(6.3f).f32(0.5f).f32(2.f).f32(10.f)));
        // the root node
        subs.append(sub4("PNAM", Bytes().u32(0)));
        subs.append(sub4("FNAM", Bytes().u32(300)));
        subs.append(sub4("XNAM", Bytes().u32(398)));
        subs.append(sub4("YNAM", Bytes().u32(40)));
        subs.append(sub4("HNAM", Bytes().f32(0.f)));
        subs.append(sub4("VNAM", Bytes().f32(0.f)));
        subs.append(sub4("SNAM", Bytes().u32(0x44C)));
        subs.append(sub4("CNAM", Bytes().u32(3)));
        subs.append(sub4("CNAM", Bytes().u32(12)));
        subs.append(sub4("INAM", Bytes().u32(0)));
        // a perk node: a form of the master (index 0), grid (2, 1), sky position, one child
        subs.append(sub4("PNAM", Bytes().u32(0x00000ABC)));
        subs.append(sub4("FNAM", Bytes().u32(1)));
        subs.append(sub4("XNAM", Bytes().u32(2)));
        subs.append(sub4("YNAM", Bytes().u32(1)));
        subs.append(sub4("HNAM", Bytes().f32(-0.25f)));
        subs.append(sub4("VNAM", Bytes().f32(0.5f)));
        subs.append(sub4("SNAM", Bytes().u32(0x44C)));
        subs.append(sub4("CNAM", Bytes().u32(7)));
        subs.append(sub4("INAM", Bytes().u32(3)));
        SkillRecord r;
        std::string why;
        ASSERT_TRUE(decodeSkill(view(subs), raw("AVIF", 0x44C), f.context(), r, why)) << why;
        EXPECT_EQ(r.mCommon.mEditorId, "AVOneHanded");
        EXPECT_EQ(r.mCategory, 1u);
        ASSERT_TRUE(r.mHasSkillData);
        EXPECT_FLOAT_EQ(r.mUseMult, 6.3f);
        EXPECT_FLOAT_EQ(r.mUseOffset, 0.5f);
        EXPECT_FLOAT_EQ(r.mImproveMult, 2.f);
        EXPECT_FLOAT_EQ(r.mImproveOffset, 10.f);
        ASSERT_EQ(r.mNodes.size(), 2u);
        EXPECT_EQ(r.mNodes[0].mPerk, 0u);
        EXPECT_EQ(r.mNodes[0].mIndex, 0u);
        EXPECT_EQ(r.mNodes[0].mChildren, (std::vector<std::uint32_t>{ 3, 12 }));
        EXPECT_NE(r.mNodes[1].mPerk, 0u);
        EXPECT_EQ(r.mNodes[1].mIndex, 3u);
        EXPECT_EQ(r.mNodes[1].mColumn, 2u);
        EXPECT_EQ(r.mNodes[1].mRow, 1u);
        EXPECT_FLOAT_EQ(r.mNodes[1].mHorizontal, -0.25f);
        EXPECT_FLOAT_EQ(r.mNodes[1].mVertical, 0.5f);
        EXPECT_EQ(r.mNodes[1].mChildren, (std::vector<std::uint32_t>{ 7 }));
    }

    TEST(SkyrimProgressionDecode, anAvifWithoutSkillDataHasNone)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("AVHealth")));
        SkillRecord r;
        std::string why;
        ASSERT_TRUE(decodeSkill(view(subs), raw("AVIF", 0x3E), f.context(), r, why)) << why;
        EXPECT_FALSE(r.mHasSkillData);
        EXPECT_TRUE(r.mNodes.empty());
    }

    TEST(SkyrimProgressionDecode, hostileValuesAreRefused)
    {
        Fixture f;
        {
            Bytes subs;
            subs.append(sub4("EDID", Bytes().zstring("AVOneHanded")));
            subs.append(sub4("AVSK", Bytes().f32(std::numeric_limits<float>::quiet_NaN()).f32(0).f32(1).f32(0)));
            SkillRecord r;
            std::string why;
            EXPECT_FALSE(decodeSkill(view(subs), raw("AVIF", 1), f.context(), r, why));
            EXPECT_FALSE(why.empty());
        }
        {
            Bytes subs;
            subs.append(sub4("EDID", Bytes().zstring("AVOneHanded")));
            for (std::size_t i = 0; i < kMaxNodesPerSkill + 2; ++i)
            {
                subs.append(sub4("PNAM", Bytes().u32(0)));
                subs.append(sub4("INAM", Bytes().u32(static_cast<std::uint32_t>(i))));
            }
            SkillRecord r;
            std::string why;
            EXPECT_FALSE(decodeSkill(view(subs), raw("AVIF", 1), f.context(), r, why));
        }
    }

    TEST(SkyrimProgressionTable, aLaterRecordReplacesAndTheTableKnowsItsNodes)
    {
        SkillTable table = makeTable();
        EXPECT_TRUE(table.complete());
        ASSERT_NE(table.skill(0), nullptr);
        EXPECT_EQ(table.skill(18), nullptr);
        std::size_t slot = 99;
        const PerkNode* node = table.nodeOfPerk(kA1, &slot);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(slot, 0u);
        EXPECT_EQ(node->mIndex, 1u);
        EXPECT_EQ(table.nodeOfPerk(0x999), nullptr);
        EXPECT_EQ(table.perkNodeCount(), 2u);
        SkillRecord r = *table.skill(0);
        r.mUseMult = 9.f;
        table.add(r);
        EXPECT_FLOAT_EQ(table.skill(0)->mUseMult, 9.f);
        SkillTable empty;
        EXPECT_FALSE(empty.complete());
    }

    TEST(SkyrimProgressionSettings, gmstsReplaceTheDefaults)
    {
        SettingsTable gmsts;
        gmsts.set("fSkillUseCurve", 2.5f);
        gmsts.set("fXPLevelUpBase", 80.f);
        const Settings s = settingsFrom(gmsts);
        EXPECT_FLOAT_EQ(s.mSkillUseCurve, 2.5f);
        EXPECT_FLOAT_EQ(s.mXpBase, 80.f);
        EXPECT_FLOAT_EQ(s.mXpMult, 25.f);
        EXPECT_FLOAT_EQ(settingsFrom(SettingsTable()).mSkillUseCurve, 1.95f);
    }

    // ---- skill experience ------------------------------------------------------------------------------------------

    TEST(SkyrimProgression, aNewPlayerStartsAtLevelOneWithSkills15)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        EXPECT_EQ(p.level(), 1u);
        for (std::size_t s = 0; s < kSkillCount; ++s)
            EXPECT_EQ(p.skill(s), 15u);
        EXPECT_FLOAT_EQ(p.skillNeed(0), 225.f); // 1 * 15 ^ 2 + 0
        EXPECT_FLOAT_EQ(p.levelNeed(), 100.f); // 75 + 25 * 1
        EXPECT_FALSE(p.levelUpReady());
        EXPECT_EQ(p.perkPoints(), 0u);
        p.setStart(3, 20);
        EXPECT_EQ(p.level(), 3u);
        EXPECT_EQ(p.skill(5), 20u);
    }

    TEST(SkyrimProgression, useAddsExperienceAndTheNextLevelNeedsTheCurve)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        EXPECT_EQ(p.useSkill(0, 100.f), 0u); // gain 1 * 100 + 0 of 225
        EXPECT_FLOAT_EQ(p.skillXp(0), 100.f);
        EXPECT_EQ(p.skill(0), 15u);
        EXPECT_EQ(p.useSkill(0, 125.f), 1u); // 225: exactly enough
        EXPECT_EQ(p.skill(0), 16u);
        EXPECT_FLOAT_EQ(p.skillXp(0), 0.f);
        EXPECT_FLOAT_EQ(p.skillNeed(0), 256.f);
        EXPECT_FLOAT_EQ(p.levelXp(), 16.f); // a skill increase gives the new skill level
    }

    TEST(SkyrimProgression, theUseOffsetAndMultiplierScaleTheGain)
    {
        FakePerks perks;
        SkillTable table = makeTable();
        SkillRecord r = *table.skill(1);
        r.mUseMult = 2.f;
        r.mUseOffset = 3.f;
        table.add(r);
        PlayerProgression p(table, &perks, simple());
        (void)p.useSkill(1, 10.f);
        EXPECT_FLOAT_EQ(p.skillXp(1), 23.f); // 2 * 10 + 3
    }

    TEST(SkyrimProgression, oneUseCanRaiseSeveralLevelsAndNeverPastOneHundred)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        EXPECT_EQ(p.useSkill(3, 225.f + 256.f + 289.f + 5.f), 3u);
        EXPECT_EQ(p.skill(3), 18u);
        EXPECT_FLOAT_EQ(p.skillXp(3), 5.f);
        EXPECT_EQ(p.useSkill(4, 1.0e9f), 85u);
        EXPECT_EQ(p.skill(4), kMaxSkill);
        EXPECT_EQ(p.useSkill(4, 1000.f), 0u);
        EXPECT_EQ(p.skill(4), kMaxSkill);
    }

    TEST(SkyrimProgression, badUsesDoNothing)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        const std::uint64_t before = p.revision();
        EXPECT_EQ(p.useSkill(kSkillCount, 100.f), 0u);
        EXPECT_EQ(p.useSkill(0, -5.f), 0u);
        EXPECT_EQ(p.useSkill(0, std::numeric_limits<float>::quiet_NaN()), 0u);
        EXPECT_EQ(p.useSkill(0, std::numeric_limits<float>::infinity()), 0u);
        EXPECT_EQ(p.useSkill(0, 0.f), 0u);
        EXPECT_FLOAT_EQ(p.skillXp(0), 0.f);
        EXPECT_EQ(p.revision(), before);
    }

    TEST(SkyrimProgression, aChangeMovesTheRevision)
    {
        Rig rig;
        const std::uint64_t before = rig.mP->revision();
        (void)rig.mP->useSkill(0, 10.f);
        EXPECT_GT(rig.mP->revision(), before);
    }

    // ---- levelling -------------------------------------------------------------------------------------------------

    TEST(SkyrimProgression, enoughCharacterExperienceMakesALevelUpReadyAndAChoiceSpendsIt)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        // skill increases: 225 -> 16 (xp 16), 256 -> 17, 289 -> 18, 324 -> 19, 361 -> 20, 400 -> 21: 16+...+21 = 111
        (void)p.useSkill(0, 225.f + 256.f + 289.f + 324.f + 361.f + 400.f);
        EXPECT_EQ(p.skill(0), 21u);
        EXPECT_FLOAT_EQ(p.levelXp(), 111.f);
        EXPECT_TRUE(p.levelUpReady());
        EXPECT_EQ(p.level(), 1u); // not before the choice
        EXPECT_TRUE(p.chooseLevelUp(Vital::Health));
        EXPECT_EQ(p.level(), 2u);
        EXPECT_FLOAT_EQ(p.levelXp(), 11.f); // 111 - 100
        EXPECT_FLOAT_EQ(p.vitalBonus(Vital::Health), 10.f);
        EXPECT_FLOAT_EQ(p.vitalBonus(Vital::Stamina), 0.f);
        EXPECT_EQ(p.perkPoints(), 1u);
        EXPECT_FLOAT_EQ(p.levelNeed(), 125.f); // 75 + 25 * 2
        EXPECT_FALSE(p.levelUpReady());
        EXPECT_FALSE(p.chooseLevelUp(Vital::Magicka));
        EXPECT_EQ(p.level(), 2u);
        const std::vector<std::uint16_t> events = p.takeLevelUps();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0], 2u);
        EXPECT_TRUE(p.takeLevelUps().empty());
    }

    TEST(SkyrimProgression, aStaminaChoiceAddsCarryWeightAndMagickaAddsMagicka)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        (void)p.useSkill(0, 1.0e6f); // plenty
        ASSERT_TRUE(p.levelUpReady());
        EXPECT_TRUE(p.chooseLevelUp(Vital::Stamina));
        EXPECT_FLOAT_EQ(p.vitalBonus(Vital::Stamina), 10.f);
        EXPECT_FLOAT_EQ(p.carryWeightBonus(), 5.f);
        EXPECT_TRUE(p.chooseLevelUp(Vital::Magicka));
        EXPECT_FLOAT_EQ(p.vitalBonus(Vital::Magicka), 10.f);
        EXPECT_EQ(p.perkPoints(), 2u);
        EXPECT_EQ(p.level(), 3u);
        EXPECT_EQ(p.takeLevelUps(), (std::vector<std::uint16_t>{ 2, 3 }));
    }

    // ---- perks -----------------------------------------------------------------------------------------------------

    TEST(SkyrimProgressionPerks, aPerkNeedsAPointAndRanksFollowTheChain)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        std::string why;
        EXPECT_FALSE(p.canBuyPerk(kA1, why));
        EXPECT_NE(why.find("point"), std::string::npos) << why;
        (void)p.useSkill(0, 1.0e6f);
        ASSERT_TRUE(p.chooseLevelUp(Vital::Health));
        ASSERT_TRUE(p.chooseLevelUp(Vital::Health));
        ASSERT_EQ(p.perkPoints(), 2u);
        EXPECT_TRUE(p.canBuyPerk(kA1, why)) << why;
        EXPECT_TRUE(p.buyPerk(kA1, why)) << why;
        EXPECT_EQ(p.perkRank(kA1), 1u);
        EXPECT_EQ(p.perkPoints(), 1u);
        EXPECT_EQ(p.ownedPerks(), (std::vector<FormId>{ kA1 }));
        EXPECT_TRUE(p.buyPerk(kA1, why)) << why; // rank 2: the next record in the chain
        EXPECT_EQ(p.perkRank(kA1), 2u);
        EXPECT_EQ(p.ownedPerks(), (std::vector<FormId>{ kA2 })); // the highest rank only
        EXPECT_EQ(p.hasPerkSet(), (std::vector<FormId>{ kA1, kA2 })); // HasPerk is true for both
        EXPECT_EQ(p.perkPoints(), 0u);
        EXPECT_FALSE(p.buyPerk(kA1, why)); // no point and no further rank
        EXPECT_EQ(p.perkRank(kA1), 2u);
    }

    TEST(SkyrimProgressionPerks, noFurtherRankIsRefusedWithAReason)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        (void)p.useSkill(0, 1.0e6f);
        for (int i = 0; i < 3; ++i)
            ASSERT_TRUE(p.chooseLevelUp(Vital::Health));
        std::string why;
        ASSERT_TRUE(p.buyPerk(kA1, why));
        ASSERT_TRUE(p.buyPerk(kA1, why));
        EXPECT_FALSE(p.buyPerk(kA1, why));
        EXPECT_NE(why.find("rank"), std::string::npos) << why;
        EXPECT_EQ(p.perkPoints(), 1u); // nothing was spent
    }

    TEST(SkyrimProgressionPerks, theSkillAndThePriorPerkGateAPurchase)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        (void)p.useSkill(5, 1.0e6f);
        for (int i = 0; i < 3; ++i)
            ASSERT_TRUE(p.chooseLevelUp(Vital::Health));
        std::string why;
        EXPECT_FALSE(p.canBuyPerk(kB, why)); // One-Handed 15 < 30 and no A1
        EXPECT_NE(why.find("requires"), std::string::npos) << why;
        p.setStart(p.level(), 30); // the skill is enough, the perk A1 is still missing
        EXPECT_FALSE(p.canBuyPerk(kB, why));
        ASSERT_TRUE(p.buyPerk(kA1, why)) << why;
        EXPECT_TRUE(p.canBuyPerk(kB, why)) << why;
        EXPECT_TRUE(p.buyPerk(kB, why)) << why;
        EXPECT_EQ(p.ownedPerks(), (std::vector<FormId>{ kA1, kB }));
    }

    TEST(SkyrimProgressionPerks, anUnknownOrRootPerkCannotBeBought)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        (void)p.useSkill(0, 1.0e6f);
        ASSERT_TRUE(p.chooseLevelUp(Vital::Health));
        std::string why;
        EXPECT_FALSE(p.buyPerk(0x999, why));
        EXPECT_FALSE(why.empty());
        EXPECT_FALSE(p.buyPerk(0, why));
        EXPECT_EQ(p.perkPoints(), 1u);
        PlayerProgression noLibrary(rig.mTable, nullptr, simple());
        EXPECT_FALSE(noLibrary.canBuyPerk(kA1, why));
    }

    TEST(SkyrimProgressionPerks, nodesListWhatTheWindowShows)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        (void)p.useSkill(0, 1.0e6f);
        ASSERT_TRUE(p.chooseLevelUp(Vital::Health));
        std::string why;
        ASSERT_TRUE(p.buyPerk(kA1, why));
        const std::vector<NodeView> nodes = p.nodes(0);
        ASSERT_EQ(nodes.size(), 2u); // the root has no perk and is not listed
        EXPECT_EQ(nodes[0].mPerk, kA1);
        EXPECT_EQ(nodes[0].mRank, 1u);
        EXPECT_EQ(nodes[0].mRanks, 2u);
        EXPECT_EQ(nodes[1].mPerk, kB);
        EXPECT_EQ(nodes[1].mRank, 0u);
        EXPECT_FALSE(nodes[1].mCanBuy);
        EXPECT_TRUE(p.nodes(7).empty());
    }

    TEST(SkyrimProgression, actorValuesListTheSkillsAtSixToTwentyThree)
    {
        Rig rig;
        rig.mP->setStart(1, 33);
        const auto values = rig.mP->actorValues();
        ASSERT_EQ(values.size(), kSkillCount);
        EXPECT_EQ(values.front().first, 6u);
        EXPECT_EQ(values.back().first, 23u);
        EXPECT_FLOAT_EQ(values[4].second, 33.f);
    }

    // ---- saving ----------------------------------------------------------------------------------------------------

    TEST(SkyrimProgressionSave, aSavedPlayerRestoresExactly)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        (void)p.useSkill(0, 1.0e6f);
        (void)p.useSkill(7, 150.f);
        ASSERT_TRUE(p.chooseLevelUp(Vital::Stamina));
        std::string why;
        ASSERT_TRUE(p.buyPerk(kA1, why));
        const std::vector<std::uint8_t> bytes = p.serialise();
        ASSERT_FALSE(bytes.empty());
        Rig other;
        std::string error;
        ASSERT_TRUE(other.mP->restore(bytes, error)) << error;
        EXPECT_EQ(other.mP->level(), p.level());
        EXPECT_EQ(other.mP->skill(0), p.skill(0));
        EXPECT_FLOAT_EQ(other.mP->skillXp(7), 150.f);
        EXPECT_EQ(other.mP->perkPoints(), p.perkPoints());
        EXPECT_EQ(other.mP->ownedPerks(), p.ownedPerks());
        EXPECT_FLOAT_EQ(other.mP->vitalBonus(Vital::Stamina), 10.f);
        EXPECT_EQ(other.mP->serialise(), bytes); // canonical
    }

    TEST(SkyrimProgressionSave, theSameUsesGiveTheSameBytes)
    {
        auto run = [] {
            Rig rig;
            for (int i = 0; i < 40; ++i)
                (void)rig.mP->useSkill(static_cast<std::size_t>(i % 18), 37.5f * static_cast<float>(i + 1));
            return rig.mP->serialise();
        };
        const std::vector<std::uint8_t> a = run();
        EXPECT_FALSE(a.empty());
        EXPECT_EQ(a, run());
    }

    TEST(SkyrimProgressionSave, aDamagedOrNewerBlobIsRefusedAndNothingChanges)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        (void)p.useSkill(0, 100.f);
        const std::vector<std::uint8_t> good = p.serialise();
        ASSERT_FALSE(good.empty());
        std::string error;
        EXPECT_FALSE(p.restore({}, error));
        EXPECT_FALSE(error.empty());
        for (std::size_t cut = 1; cut < good.size(); cut += 7)
        {
            std::vector<std::uint8_t> shortened(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut));
            EXPECT_FALSE(p.restore(shortened, error)) << cut;
        }
        std::vector<std::uint8_t> trailing = good;
        trailing.push_back(0);
        EXPECT_FALSE(p.restore(trailing, error));
        std::vector<std::uint8_t> newer = good;
        newer[4] = 99; // the version
        EXPECT_FALSE(p.restore(newer, error));
        EXPECT_FLOAT_EQ(p.skillXp(0), 100.f);
        EXPECT_EQ(p.serialise(), good);
    }

    TEST(SkyrimProgression, resetMakesANewCharacter)
    {
        Rig rig;
        PlayerProgression& p = *rig.mP;
        (void)p.useSkill(0, 1.0e6f);
        ASSERT_TRUE(p.chooseLevelUp(Vital::Stamina));
        std::string why;
        ASSERT_TRUE(p.buyPerk(kA1, why));
        p.reset(1, 15);
        EXPECT_EQ(p.level(), 1u);
        EXPECT_EQ(p.skill(0), 15u);
        EXPECT_FLOAT_EQ(p.levelXp(), 0.f);
        EXPECT_EQ(p.perkPoints(), 0u);
        EXPECT_FLOAT_EQ(p.vitalBonus(Vital::Stamina), 0.f);
        EXPECT_TRUE(p.ownedPerks().empty());
        EXPECT_TRUE(p.takeLevelUps().empty());
    }
}
