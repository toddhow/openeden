// Task 0118, stage N5-0 of decision 0017: Skyrim SE actor and appearance records (NPC_, RACE, ARMA, ARMO extras, OTFT,
// HDPT, CLAS) and template / leveled list resolution, on generated plugins. No game data.
#include <game_skyrimse/actors.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kGrupTop = 0;

    Bytes header()
    {
        return tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
    }

    LoadOrder defaultOrder()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    struct Parsed
    {
        ActorRecords mRecords;
        bool mOk = false;
        Content::Error mError;
    };

    Parsed parse(const Bytes& bytes, ActorLimits limits = ActorLimits())
    {
        Parsed p;
        p.mOk = parseActorRecordsBytes(bytes.str(), "test.esp", defaultOrder(), limits, p.mRecords, p.mError);
        return p;
    }

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    Bytes top(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), kGrupTop, records);
    }

    Bytes plugin(const char (&type)[5], const Bytes& records)
    {
        Bytes bytes = header();
        bytes.append(top(type, records));
        return bytes;
    }

    Bytes acbs(std::uint32_t flags, std::uint16_t level, std::uint16_t templateFlags)
    {
        Bytes b;
        // flags, magicka offset, stamina offset, level, calc min, calc max, speed, disposition, template flags,
        // health offset, bleedout override
        b.u32(flags).u16(0).u16(0).u16(level).u16(1).u16(50).u16(100).u16(0).u16(templateFlags).u16(0).u16(0);
        return b;
    }

    Bytes ref(std::uint32_t id)
    {
        return Bytes().u32(id);
    }

    Bytes npcRecord(std::uint32_t id, const std::string& name, const Bytes& extra = Bytes(), std::uint16_t tflags = 0,
        std::uint32_t flags = 0)
    {
        Bytes npc;
        npc.append(edid(name));
        npc.append(sub4("ACBS", acbs(flags, 5, tflags)));
        npc.append(sub4("RNAM", ref(0x00013746)));
        npc.append(sub4("CNAM", ref(0x01000300)));
        npc.append(extra);
        return record4("NPC_", 0, id, npc);
    }

    // ---- NPC_ ----------------------------------------------------------------------------------------------------

    TEST(SkyrimActorsNpc, everyFieldIsRead)
    {
        Bytes extra;
        extra.append(sub4("FULL", Bytes().zstring("Bob")));
        extra.append(sub4("VTCK", ref(0x00019001)));
        extra.append(sub4("TPLT", ref(0x01000111)));
        extra.append(sub4("DOFT", ref(0x01000201)));
        extra.append(sub4("SOFT", ref(0x01000202)));
        extra.append(sub4("WNAM", ref(0x00000D64)));
        extra.append(sub4("ANAM", ref(0x00000D65)));
        extra.append(sub4("INAM", ref(0x01000203)));
        extra.append(sub4("CRIF", ref(0x01000204)));
        extra.append(sub4("ZNAM", ref(0x01000205)));
        extra.append(sub4("FTST", ref(0x01000206)));
        extra.append(sub4("HCLF", ref(0x01000207)));
        extra.append(sub4("DPLT", ref(0x01000208)));
        extra.append(sub4("PKID", ref(0x01000301))).append(sub4("PKID", ref(0x01000302)));
        extra.append(sub4("PNAM", ref(0x01000401))).append(sub4("PNAM", ref(0x01000402)));
        extra.append(sub4("SPLO", ref(0x01000501)));
        extra.append(sub4("KWDA", Bytes().u32(0x01000601).u32(0x01000602)));
        extra.append(sub4("NAM6", Bytes().f32(1.05f))).append(sub4("NAM7", Bytes().f32(40.0f)));
        extra.append(sub4("SNAM", Bytes().u32(0x01000701).u8(3).u8(0).u8(0).u8(0)));
        extra.append(sub4("SNAM", Bytes().u32(0x01000702).u8(255).u8(0).u8(0).u8(0)));
        extra.append(sub4("CNTO", Bytes().u32(0x01000801).u32(2)));
        extra.append(sub4("CNTO", Bytes().u32(0x00000F00).u32(1)));
        extra.append(sub4("QNAM", Bytes().f32(0.5f).f32(0.25f).f32(1.0f)));
        Bytes nam9;
        for (int i = 0; i < 19; ++i)
            nam9.f32(i * 0.1f);
        extra.append(sub4("NAM9", nam9));
        extra.append(sub4("NAMA", Bytes().u32(10).u32(0xFFFFFFFF).u32(17).u32(4)));
        extra.append(sub4("TINI", Bytes().u16(1))).append(sub4("TINI", Bytes().u16(2)));
        Bytes aidt;
        aidt.u8(1).u8(4).u8(50).u8(3).u8(0).u8(2).u8(1).u8(0).u32(100).u32(200).u32(300);
        extra.append(sub4("AIDT", aidt));
        Bytes dnam;
        for (int i = 0; i < 18; ++i)
            dnam.u8(static_cast<std::uint8_t>(i + 10));
        for (int i = 0; i < 18; ++i)
            dnam.u8(0);
        dnam.u16(150).u16(60).u16(70).u16(0).f32(0.0f).u8(0).u8(0).u8(0).u8(0);
        extra.append(sub4("DNAM", dnam));

        const Parsed p = parse(plugin("NPC_", npcRecord(0x01000200, "NpcOne", extra, 0x0185, NpcFlags::Female)));
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        ASSERT_EQ(p.mRecords.mNpcs.size(), 1u);
        const ActorNpc& n = p.mRecords.mNpcs[0];
        EXPECT_EQ(n.mCommon.mEditorId, "NpcOne");
        EXPECT_EQ(n.mName.mText, "Bob");
        EXPECT_EQ(n.mRace, 0x00013746u);
        EXPECT_EQ(n.mClass, 0x01000300u);
        EXPECT_EQ(n.mVoiceType, 0x00019001u);
        EXPECT_EQ(n.mTemplate, 0x01000111u);
        EXPECT_EQ(n.mTemplateFlags, 0x0185u);
        EXPECT_EQ(n.mLevel, 5);
        EXPECT_TRUE(n.female());
        EXPECT_EQ(n.sex(), Sex::Female);
        EXPECT_EQ(n.mDefaultOutfit, 0x01000201u);
        EXPECT_EQ(n.mSleepOutfit, 0x01000202u);
        EXPECT_EQ(n.mSkin, 0x00000D64u);
        EXPECT_EQ(n.mFarAwaySkin, 0x00000D65u);
        EXPECT_EQ(n.mDeathItem, 0x01000203u);
        EXPECT_EQ(n.mCrimeFaction, 0x01000204u);
        EXPECT_EQ(n.mCombatStyle, 0x01000205u);
        EXPECT_EQ(n.mHeadTextureSet, 0x01000206u);
        EXPECT_EQ(n.mHairColor, 0x01000207u);
        EXPECT_EQ(n.mDefaultPackageList, 0x01000208u);
        EXPECT_EQ(n.mPackages, (std::vector<FormId>{ 0x01000301u, 0x01000302u }));
        EXPECT_EQ(n.mHeadParts, (std::vector<FormId>{ 0x01000401u, 0x01000402u }));
        EXPECT_EQ(n.mSpells, (std::vector<FormId>{ 0x01000501u }));
        EXPECT_EQ(n.mKeywords, (std::vector<FormId>{ 0x01000601u, 0x01000602u }));
        ASSERT_TRUE(n.mHeight);
        EXPECT_FLOAT_EQ(*n.mHeight, 1.05f);
        ASSERT_TRUE(n.mWeight);
        EXPECT_FLOAT_EQ(*n.mWeight, 40.0f);
        ASSERT_EQ(n.mFactions.size(), 2u);
        EXPECT_EQ(n.mFactions[0].mFaction, 0x01000701u);
        EXPECT_EQ(n.mFactions[0].mRank, 3);
        EXPECT_EQ(n.mFactions[1].mRank, -1);
        ASSERT_EQ(n.mInventory.size(), 2u);
        EXPECT_EQ(n.mInventory[0].mItem, 0x01000801u);
        EXPECT_EQ(n.mInventory[0].mCount, 2);
        EXPECT_EQ(n.mInventory[1].mItem, 0x00000F00u);
        ASSERT_TRUE(n.mSkinTone);
        EXPECT_FLOAT_EQ((*n.mSkinTone)[1], 0.25f);
        ASSERT_TRUE(n.mFaceMorphs);
        EXPECT_FLOAT_EQ((*n.mFaceMorphs)[18], 1.8f);
        ASSERT_TRUE(n.mFaceParts);
        EXPECT_EQ((*n.mFaceParts)[0], 10u);
        EXPECT_EQ((*n.mFaceParts)[1], 0xFFFFFFFFu);
        EXPECT_EQ((*n.mFaceParts)[3], 4u);
        EXPECT_EQ(n.mTintLayerCount, 2u);
        ASSERT_TRUE(n.mAi);
        EXPECT_EQ(n.mAi->mAggression, 1);
        EXPECT_EQ(n.mAi->mConfidence, 4);
        EXPECT_EQ(n.mAi->mAssistance, 2);
        EXPECT_EQ(n.mAi->mRadii[2], 300u);
        ASSERT_TRUE(n.mHasStats);
        EXPECT_EQ(n.mSkills[17], 27);
        EXPECT_EQ(n.mHealth, 150);
        EXPECT_EQ(p.mRecords.mOddSubrecords, 0u);
        EXPECT_EQ(p.mRecords.mCounts.at(Content::fourCC("NPC_")).mParsed, 1u);
    }

    TEST(SkyrimActorsNpc, wrongSizesAreCountedAndDecodeToNothing)
    {
        Bytes extra;
        extra.append(sub4("SNAM", Bytes().u32(1).u8(0))); // 5 bytes, not 8
        extra.append(sub4("CNTO", Bytes().u32(1))); // 4 bytes, not 8
        extra.append(sub4("AIDT", Bytes().u32(0))); // not 20
        extra.append(sub4("NAM9", Bytes().f32(0))); // not 76
        extra.append(sub4("NAMA", Bytes().u32(0))); // not 16
        extra.append(sub4("QNAM", Bytes().u32(0))); // not 12
        extra.append(sub4("TPLT", Bytes().u16(0))); // not 4
        extra.append(sub4("NAM6", Bytes().u16(0))); // not 4
        const Parsed p = parse(plugin("NPC_", npcRecord(0x01000200, "Odd", extra)));
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mRecords.mNpcs.size(), 1u);
        const ActorNpc& n = p.mRecords.mNpcs[0];
        EXPECT_TRUE(n.mFactions.empty());
        EXPECT_TRUE(n.mInventory.empty());
        EXPECT_FALSE(n.mAi);
        EXPECT_FALSE(n.mFaceMorphs);
        EXPECT_FALSE(n.mFaceParts);
        EXPECT_FALSE(n.mSkinTone);
        EXPECT_EQ(n.mTemplate, kNullFormId);
        EXPECT_FALSE(n.mHeight);
        EXPECT_EQ(p.mRecords.mOddSubrecords, 8u);
        EXPECT_EQ(p.mRecords.mOddBy.at("NPC_.AIDT"), 1u);
        EXPECT_EQ(p.mRecords.mOddBy.at("NPC_.TPLT"), 1u);
    }

    TEST(SkyrimActorsNpc, aNonFiniteHeightIsOddNotKept)
    {
        Bytes extra;
        extra.append(sub4("NAM6", Bytes().u32(0x7FC00000u))); // NaN
        const Parsed p = parse(plugin("NPC_", npcRecord(0x01000200, "Nan", extra)));
        ASSERT_TRUE(p.mOk);
        EXPECT_FALSE(p.mRecords.mNpcs[0].mHeight);
        EXPECT_EQ(p.mRecords.mOddBy.at("NPC_.NAM6"), 1u);
    }

    TEST(SkyrimActorsNpc, idsOfMissingPluginsAreUnresolvedAndCounted)
    {
        // Master index 1 is Gone.esm, which the load order lacks; index 2 is the plugin itself.
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 }, { "Gone.esm", 0 } }, 24, 44);
        bytes.append(top("NPC_", npcRecord(0x02000200, "Lost", sub4("TPLT", ref(0x01000001)))));
        const Parsed p = parse(bytes);
        ASSERT_TRUE(p.mOk);
        EXPECT_EQ(p.mRecords.mNpcs[0].mTemplate, kUnresolvedFormId);
        EXPECT_EQ(p.mRecords.mUnresolvedFormIds, 2u); // the class and the template
    }

    TEST(SkyrimActorsNpc, aLocalizedNameIsAnIdNotText)
    {
        Bytes bytes = tes4Header(1.7f, 0x80, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(top("NPC_", npcRecord(0x01000200, "Loc", sub4("FULL", Bytes().u32(77)))));
        const Parsed p = parse(bytes);
        ASSERT_TRUE(p.mOk);
        EXPECT_TRUE(p.mRecords.mNpcs[0].mName.mLocalized);
        EXPECT_EQ(p.mRecords.mNpcs[0].mName.mId, 77u);
    }

    // ---- RACE ----------------------------------------------------------------------------------------------------

    Bytes empty(const char (&name)[5])
    {
        return sub4(name, Bytes());
    }

    Bytes raceBytes()
    {
        Bytes r;
        r.append(edid("TestRace"));
        r.append(sub4("FULL", Bytes().zstring("Test Race")));
        r.append(sub4("WNAM", ref(0x00000D64)));
        r.append(sub4("BOD2", Bytes().u32(0x0000025C).u32(0)));
        Bytes data;
        for (int i = 0; i < 14; ++i)
            data.u8(0);
        data.u16(0).f32(1.03f).f32(1.01f).f32(0.9f).f32(0.8f).u32(0x00000043);
        while (data.size() < 164)
            data.u8(0);
        r.append(sub4("DATA", data));
        r.append(empty("MNAM"));
        r.append(sub4("ANAM", Bytes().zstring("Actors\\Character\\Character Assets\\skeleton.nif")));
        r.append(sub4("MODT", Bytes().u32(2).u32(0).u32(0)));
        r.append(empty("FNAM"));
        r.append(sub4("ANAM", Bytes().zstring("actors\\Character\\Character Assets Female\\skeleton_female.nif")));
        r.append(sub4("VTCK", Bytes().u32(0x00013AD2).u32(0x00013ADD)));
        r.append(sub4("DNAM", Bytes().u32(0x00000AAA).u32(0x00000BBB)));
        r.append(empty("NAM1"));
        r.append(empty("MNAM"));
        r.append(sub4("INDX", Bytes().u32(0)));
        r.append(sub4("MODL", Bytes().zstring("Actors\\Character\\UpperBodyHumanMale.egt")));
        r.append(empty("FNAM"));
        r.append(sub4("INDX", Bytes().u32(1)));
        r.append(sub4("MODL", Bytes().zstring("Actors\\Character\\LowerBodyHumanFemale.egt")));
        r.append(empty("NAM3"));
        r.append(empty("MNAM"));
        r.append(sub4("MODL", Bytes().zstring("Actors\\Character\\DefaultMale.hkx")));
        r.append(empty("FNAM"));
        r.append(sub4("MODL", Bytes().zstring("Actors\\Character\\DefaultFemale.hkx")));
        r.append(empty("NAM0"));
        r.append(empty("MNAM"));
        r.append(sub4("INDX", Bytes().u32(0))).append(sub4("HEAD", ref(0x01000901)));
        r.append(sub4("INDX", Bytes().u32(1))).append(sub4("HEAD", ref(0x01000902)));
        r.append(sub4("AHCM", ref(0x01000A01))).append(sub4("AHCM", ref(0x01000A02)));
        r.append(sub4("FTSM", ref(0x01000B01)));
        r.append(sub4("DFTM", ref(0x01000B01)));
        r.append(empty("NAM0"));
        r.append(empty("FNAM"));
        r.append(sub4("INDX", Bytes().u32(0))).append(sub4("HEAD", ref(0x01000903)));
        r.append(sub4("AHCF", ref(0x01000A03)));
        r.append(sub4("DFTF", ref(0x01000B02)));
        return r;
    }

    TEST(SkyrimActorsRace, skeletonsBodyHeadAndPresets)
    {
        const Parsed p = parse(plugin("RACE", record4("RACE", 0, 0x01000100, raceBytes())));
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        ASSERT_EQ(p.mRecords.mRaces.size(), 1u);
        const RaceRecord& r = p.mRecords.mRaces[0];
        EXPECT_EQ(r.mCommon.mEditorId, "TestRace");
        EXPECT_EQ(r.mName.mText, "Test Race");
        EXPECT_EQ(r.skeleton(Sex::Male), "Actors\\Character\\Character Assets\\skeleton.nif");
        EXPECT_EQ(r.skeleton(Sex::Female), "actors\\Character\\Character Assets Female\\skeleton_female.nif");
        EXPECT_EQ(r.mSkin, 0x00000D64u);
        EXPECT_EQ(r.mSlotMask, 0x25Cu);
        EXPECT_TRUE(r.mHasData);
        EXPECT_FLOAT_EQ(r.mHeight[0], 1.03f);
        EXPECT_FLOAT_EQ(r.mHeight[1], 1.01f);
        EXPECT_FLOAT_EQ(r.mWeight[0], 0.9f);
        EXPECT_FLOAT_EQ(r.mWeight[1], 0.8f);
        EXPECT_EQ(r.mFlags, 0x43u);
        EXPECT_TRUE(r.playable());
        EXPECT_EQ(r.mVoiceTypes[0], 0x00013AD2u);
        EXPECT_EQ(r.mVoiceTypes[1], 0x00013ADDu);
        EXPECT_EQ(r.mDecapitateArmor[1], 0x00000BBBu);
        ASSERT_EQ(r.mBodyModels.size(), 2u);
        EXPECT_EQ(r.mBodyModels[0].mSex, Sex::Male);
        EXPECT_EQ(r.mBodyModels[0].mPath, "Actors\\Character\\UpperBodyHumanMale.egt");
        EXPECT_EQ(r.mBodyModels[1].mSex, Sex::Female);
        EXPECT_EQ(r.mBodyModels[1].mIndex, 1u);
        ASSERT_EQ(r.mBehaviourGraphs.size(), 2u);
        EXPECT_EQ(r.mBehaviourGraphs[1].mPath, "Actors\\Character\\DefaultFemale.hkx");
        ASSERT_EQ(r.mHeadParts.size(), 3u);
        EXPECT_EQ(r.mHeadParts[0].mSex, Sex::Male);
        EXPECT_EQ(r.mHeadParts[1].mIndex, 1u);
        EXPECT_EQ(r.mHeadParts[1].mHeadPart, 0x01000902u);
        EXPECT_EQ(r.mHeadParts[2].mSex, Sex::Female);
        EXPECT_EQ(r.mHeadParts[2].mHeadPart, 0x01000903u);
        EXPECT_EQ(r.mAvailableHairColors[0].size(), 2u);
        EXPECT_EQ(r.mAvailableHairColors[1], (std::vector<FormId>{ 0x01000A03u }));
        EXPECT_EQ(r.mFaceTextureSets[0], (std::vector<FormId>{ 0x01000B01u }));
        EXPECT_EQ(r.mDefaultFaceTexture[1], 0x01000B02u);
        EXPECT_EQ(p.mRecords.mOddSubrecords, 0u);
    }

    TEST(SkyrimActorsRace, anAnamAfterTheBodySectionIsNotASkeleton)
    {
        Bytes r;
        r.append(edid("R")).append(empty("MNAM"));
        r.append(sub4("ANAM", Bytes().zstring("skel.nif")));
        r.append(empty("NAM1")).append(empty("MNAM"));
        r.append(sub4("ANAM", Bytes().zstring("later.nif")));
        const Parsed p = parse(plugin("RACE", record4("RACE", 0, 0x01000100, r)));
        ASSERT_TRUE(p.mOk);
        EXPECT_EQ(p.mRecords.mRaces[0].skeleton(Sex::Male), "skel.nif");
        EXPECT_TRUE(p.mRecords.mRaces[0].skeleton(Sex::Female).empty());
    }

    TEST(SkyrimActorsRace, aShortDataSubrecordIsOddAndGivesDefaults)
    {
        Bytes r;
        r.append(edid("R")).append(sub4("DATA", Bytes().u32(0)));
        const Parsed p = parse(plugin("RACE", record4("RACE", 0, 0x01000100, r)));
        ASSERT_TRUE(p.mOk);
        EXPECT_FALSE(p.mRecords.mRaces[0].mHasData);
        EXPECT_FLOAT_EQ(p.mRecords.mRaces[0].mHeight[0], 1.0f);
        EXPECT_EQ(p.mRecords.mOddBy.at("RACE.DATA"), 1u);
    }

    // ---- ARMA, ARMO ----------------------------------------------------------------------------------------------

    TEST(SkyrimActorsArmature, slotsModelsRacesAndPriority)
    {
        Bytes a;
        a.append(edid("IronCuirassAA"));
        a.append(sub4("BODT", Bytes().u32(0x114).u8(1).u8(0).u8(0).u8(0).u32(2)));
        a.append(sub4("RNAM", ref(0x00000019)));
        Bytes dnam;
        dnam.u8(5).u8(6).u8(2).u8(0).u8(0).u8(0).u8(0).u8(0x17).f32(0.25f);
        a.append(sub4("DNAM", dnam));
        a.append(sub4("MOD2", Bytes().zstring("Armor\\Iron\\Male\\Cuirass.nif")));
        a.append(sub4("MOD3", Bytes().zstring("Armor\\Iron\\F\\Cuirass.nif")));
        a.append(sub4("MOD4", Bytes().zstring("Armor\\Iron\\Male\\1stCuirass.nif")));
        a.append(sub4("MOD5", Bytes().zstring("Armor\\Iron\\F\\1stCuirass.nif")));
        a.append(sub4("MODL", ref(0x00013740))).append(sub4("MODL", ref(0x00013741)));
        const Parsed p = parse(plugin("ARMA", record4("ARMA", 0, 0x01000500, a)));
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        ASSERT_EQ(p.mRecords.mArmatures.size(), 1u);
        const ArmatureRecord& r = p.mRecords.mArmatures[0];
        EXPECT_EQ(r.mSlotMask, 0x114u);
        EXPECT_EQ(r.mArmorType, 2u);
        EXPECT_EQ(r.mRace, 0x00000019u);
        EXPECT_EQ(r.mAdditionalRaces.size(), 2u);
        EXPECT_EQ(r.model(Sex::Male), "Armor\\Iron\\Male\\Cuirass.nif");
        EXPECT_EQ(r.model(Sex::Female), "Armor\\Iron\\F\\Cuirass.nif");
        EXPECT_EQ(r.model(Sex::Male, true), "Armor\\Iron\\Male\\1stCuirass.nif");
        EXPECT_EQ(r.model(Sex::Female, true), "Armor\\Iron\\F\\1stCuirass.nif");
        EXPECT_TRUE(r.mHasDnam);
        EXPECT_EQ(r.mMalePriority, 5);
        EXPECT_EQ(r.mFemalePriority, 6);
        EXPECT_EQ(r.mMaleWeightSlider, 2);
        EXPECT_FLOAT_EQ(r.mWeaponAdjust, 0.25f);
        EXPECT_TRUE(r.appliesTo(0x00000019u));
        EXPECT_TRUE(r.appliesTo(0x00013741u));
        EXPECT_FALSE(r.appliesTo(0x00013742u));
        EXPECT_FALSE(r.appliesTo(kNullFormId));
        EXPECT_EQ(p.mRecords.mOddSubrecords, 0u);
    }

    TEST(SkyrimActorsArmature, bod2IsEightBytesAndAWrongSizeIsOdd)
    {
        Bytes a;
        a.append(sub4("BOD2", Bytes().u32(0x4).u32(1)));
        Bytes b;
        b.append(sub4("BOD2", Bytes().u32(0x4)));
        Bytes both = record4("ARMA", 0, 0x01000500, a);
        both.append(record4("ARMA", 0, 0x01000501, b));
        const Parsed p = parse(plugin("ARMA", both));
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mRecords.mArmatures.size(), 2u);
        EXPECT_EQ(p.mRecords.mArmatures[0].mSlotMask, 4u);
        EXPECT_EQ(p.mRecords.mArmatures[1].mSlotMask, 0u);
        EXPECT_EQ(p.mRecords.mOddBy.at("ARMA.BOD2"), 1u);
    }

    TEST(SkyrimActorsArmor, slotsArmaListTemplateAndRating)
    {
        Bytes a;
        a.append(edid("ArmorIronCuirass"));
        a.append(sub4("MOD2", Bytes().zstring("Armor\\Iron\\Male\\CuirassGND.nif")));
        a.append(sub4("BOD2", Bytes().u32(0x4).u32(1)));
        a.append(sub4("RNAM", ref(0x00000019)));
        a.append(sub4("MODL", ref(0x01000500))).append(sub4("MODL", ref(0x01000501)));
        a.append(sub4("TNAM", ref(0x01000600)));
        a.append(sub4("DNAM", Bytes().u32(2500)));
        const Parsed p = parse(plugin("ARMO", record4("ARMO", 0, 0x01000700, a)));
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        ASSERT_EQ(p.mRecords.mArmors.size(), 1u);
        const ArmorExtra& r = p.mRecords.mArmors[0];
        EXPECT_EQ(r.mCommon.mEditorId, "ArmorIronCuirass");
        EXPECT_EQ(r.mSlotMask, 4u);
        EXPECT_EQ(r.mArmorType, 1u);
        EXPECT_EQ(r.mRace, 0x00000019u);
        EXPECT_EQ(r.mArmatures, (std::vector<FormId>{ 0x01000500u, 0x01000501u }));
        EXPECT_EQ(r.mTemplate, 0x01000600u);
        EXPECT_EQ(r.mRating, 2500u);
    }

    TEST(SkyrimActorsArmor, anOldStyleBodtOfTwelveBytesIsRead)
    {
        Bytes a;
        a.append(sub4("BODT", Bytes().u32(0x8).u32(0).u32(2)));
        const Parsed p = parse(plugin("ARMO", record4("ARMO", 0, 0x01000700, a)));
        ASSERT_TRUE(p.mOk);
        EXPECT_EQ(p.mRecords.mArmors[0].mSlotMask, 8u);
        EXPECT_EQ(p.mRecords.mArmors[0].mArmorType, 2u);
    }

    // ---- OTFT, HDPT, CLAS ----------------------------------------------------------------------------------------

    TEST(SkyrimActorsOutfit, itemsAreAList)
    {
        Bytes o;
        o.append(edid("IronOutfit")).append(sub4("INAM", Bytes().u32(0x01000700).u32(0x01000701).u32(0x00000F10)));
        const Parsed p = parse(plugin("OTFT", record4("OTFT", 0, 0x01000800, o)));
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mRecords.mOutfits.size(), 1u);
        EXPECT_EQ(p.mRecords.mOutfits[0].mItems.size(), 3u);
        EXPECT_EQ(p.mRecords.mOutfits[0].mItems[2], 0x00000F10u);
    }

    TEST(SkyrimActorsOutfit, aListThatIsNotWholeFormIdsIsOdd)
    {
        Bytes o;
        o.append(sub4("INAM", Bytes().u32(1).u16(0)));
        const Parsed p = parse(plugin("OTFT", record4("OTFT", 0, 0x01000800, o)));
        ASSERT_TRUE(p.mOk);
        EXPECT_TRUE(p.mRecords.mOutfits[0].mItems.empty());
        EXPECT_EQ(p.mRecords.mOddBy.at("OTFT.INAM"), 1u);
    }

    TEST(SkyrimActorsHeadPart, typeModelAndLinks)
    {
        Bytes h;
        h.append(edid("MaleEyesHuman"));
        h.append(sub4("FULL", Bytes().zstring("Eyes")));
        h.append(sub4("DATA", Bytes().u8(3)));
        h.append(sub4("PNAM", Bytes().u32(HeadPartType::Eyes)));
        h.append(sub4("MODL", Bytes().zstring("Actors\\Character\\Eyes\\MaleEyes.nif")));
        h.append(sub4("HNAM", ref(0x01000901))).append(sub4("HNAM", ref(0x01000902)));
        h.append(sub4("TNAM", ref(0x01000903)));
        h.append(sub4("CNAM", ref(0x01000904)));
        h.append(sub4("RNAM", ref(0x01000905)));
        h.append(sub4("NAM0", Bytes().u32(1)));
        h.append(sub4("NAM1", Bytes().zstring("Actors\\tex.dds")));
        const Parsed p = parse(plugin("HDPT", record4("HDPT", 0, 0x01000900, h)));
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        ASSERT_EQ(p.mRecords.mHeadParts.size(), 1u);
        const HeadPartRecord& r = p.mRecords.mHeadParts[0];
        EXPECT_EQ(r.mFlags, 3);
        EXPECT_EQ(r.mType, HeadPartType::Eyes);
        EXPECT_EQ(r.mModel, "Actors\\Character\\Eyes\\MaleEyes.nif");
        EXPECT_EQ(r.mExtraParts.size(), 2u);
        EXPECT_EQ(r.mTextureSet, 0x01000903u);
        EXPECT_EQ(r.mColor, 0x01000904u);
        EXPECT_EQ(r.mValidRaces, 0x01000905u);
    }

    TEST(SkyrimActorsClass, dataOf36Bytes)
    {
        Bytes c;
        c.append(edid("Warrior")).append(sub4("FULL", Bytes().zstring("Warrior")));
        Bytes data;
        data.u32(0).u8(7).u8(50);
        for (int i = 0; i < 18; ++i)
            data.u8(static_cast<std::uint8_t>(i % 4));
        data.f32(0.1f).u32(100).u8(4).u8(0).u8(2).u8(0);
        ASSERT_EQ(data.size(), 36u);
        c.append(sub4("DATA", data));
        const Parsed p = parse(plugin("CLAS", record4("CLAS", 0, 0x01000A00, c)));
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mRecords.mClasses.size(), 1u);
        const ClassRecord& r = p.mRecords.mClasses[0];
        EXPECT_TRUE(r.mHasData);
        EXPECT_EQ(r.mTrainedSkill, 7);
        EXPECT_EQ(r.mMaxTrainingLevel, 50);
        EXPECT_EQ(r.mSkillWeights[3], 3);
        EXPECT_FLOAT_EQ(r.mBleedoutDefault, 0.1f);
        EXPECT_EQ(r.mVoicePoints, 100u);
        EXPECT_EQ(r.mHealthWeight, 4);
        EXPECT_EQ(r.mStaminaWeight, 2);
    }

    TEST(SkyrimActorsClass, aWrongSizedDataIsOdd)
    {
        Bytes c;
        c.append(sub4("DATA", Bytes().u32(0)));
        const Parsed p = parse(plugin("CLAS", record4("CLAS", 0, 0x01000A00, c)));
        ASSERT_TRUE(p.mOk);
        EXPECT_FALSE(p.mRecords.mClasses[0].mHasData);
        EXPECT_EQ(p.mRecords.mOddBy.at("CLAS.DATA"), 1u);
    }

    // ---- hostile input -------------------------------------------------------------------------------------------

    TEST(SkyrimActorsHostile, aSubrecordPastTheRecordEndFailsThatRecordOnly)
    {
        Bytes bad;
        bad.fourCC("EDID").u16(200).text("short");
        Bytes list = record4("NPC_", 0, 0x01000200, bad);
        list.append(npcRecord(0x01000201, "Next"));
        const Parsed p = parse(plugin("NPC_", list));
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mRecords.mNpcs.size(), 1u);
        EXPECT_EQ(p.mRecords.mNpcs[0].mCommon.mEditorId, "Next");
        ASSERT_EQ(p.mRecords.mErrors.size(), 1u);
        EXPECT_EQ(p.mRecords.mErrors[0].mFile, "test.esp");
        EXPECT_EQ(p.mRecords.mErrors[0].mRecordType, Content::fourCC("NPC_"));
        EXPECT_NE(p.mRecords.mErrors[0].mOffset, 0u);
        EXPECT_NE(p.mRecords.mErrors[0].mMessage.find("EDID"), std::string::npos);
        EXPECT_EQ(p.mRecords.mCounts.at(Content::fourCC("NPC_")).mFailed, 1u);
    }

    TEST(SkyrimActorsHostile, anInventoryOverTheEntryCapIsRefused)
    {
        Bytes extra;
        for (int i = 0; i < 50; ++i)
            extra.append(sub4("CNTO", Bytes().u32(0x01000801).u32(1)));
        ActorLimits limits;
        limits.mMaxEntriesPerList = 10;
        const Parsed p = parse(plugin("NPC_", npcRecord(0x01000200, "Rich", extra)), limits);
        ASSERT_TRUE(p.mOk);
        EXPECT_TRUE(p.mRecords.mNpcs.empty());
        ASSERT_EQ(p.mRecords.mErrors.size(), 1u);
        EXPECT_NE(p.mRecords.mErrors[0].mMessage.find("CNTO"), std::string::npos);
    }

    TEST(SkyrimActorsHostile, aFormListOverTheCapIsRefused)
    {
        Bytes o;
        Bytes list;
        for (int i = 0; i < 50; ++i)
            list.u32(0x01000700 + i);
        o.append(sub4("INAM", list));
        ActorLimits limits;
        limits.mMaxEntriesPerList = 10;
        const Parsed p = parse(plugin("OTFT", record4("OTFT", 0, 0x01000800, o)), limits);
        ASSERT_TRUE(p.mOk);
        EXPECT_TRUE(p.mRecords.mOutfits.empty());
        EXPECT_EQ(p.mRecords.mErrors.size(), 1u);
    }

    TEST(SkyrimActorsHostile, aRecordOverTheByteLimitIsRefused)
    {
        Bytes a;
        a.append(sub4("MOD2", Bytes().zstring(std::string(2000, 'x'))));
        ActorLimits limits;
        limits.mMaxRecordBytes = 1000;
        const Parsed p = parse(plugin("ARMA", record4("ARMA", 0, 0x01000500, a)), limits);
        ASSERT_TRUE(p.mOk);
        EXPECT_TRUE(p.mRecords.mArmatures.empty());
        EXPECT_EQ(p.mRecords.mErrors.size(), 1u);
    }

    TEST(SkyrimActorsHostile, aLongPathIsRefusedByTheStringCap)
    {
        Bytes a;
        a.append(sub4("MOD2", Bytes().zstring(std::string(3000, 'x'))));
        ActorLimits limits;
        limits.mMaxStringBytes = 1024;
        const Parsed p = parse(plugin("ARMA", record4("ARMA", 0, 0x01000500, a)), limits);
        ASSERT_TRUE(p.mOk);
        EXPECT_TRUE(p.mRecords.mArmatures.empty());
        ASSERT_EQ(p.mRecords.mErrors.size(), 1u);
        EXPECT_NE(p.mRecords.mErrors[0].mMessage.find("MOD2"), std::string::npos);
    }

    TEST(SkyrimActorsHostile, errorsAreCappedAndTheRestCounted)
    {
        Bytes list;
        for (int i = 0; i < 20; ++i)
        {
            Bytes bad;
            bad.fourCC("EDID").u16(200).text("x");
            list.append(record4("NPC_", 0, 0x01000200 + i, bad));
        }
        ActorLimits limits;
        limits.mMaxErrors = 5;
        const Parsed p = parse(plugin("NPC_", list), limits);
        ASSERT_TRUE(p.mOk);
        EXPECT_EQ(p.mRecords.mErrors.size(), 5u);
        EXPECT_EQ(p.mRecords.mErrorsDropped, 15u);
        EXPECT_EQ(p.mRecords.mCounts.at(Content::fourCC("NPC_")).mFailed, 20u);
    }

    TEST(SkyrimActorsHostile, aTruncatedFileIsAWalkErrorNotACrash)
    {
        Bytes list = npcRecord(0x01000200, "Whole");
        list.append(npcRecord(0x01000201, "Cut"));
        Bytes bytes = plugin("NPC_", list);
        std::string cut = bytes.str();
        cut.resize(cut.size() - 20);
        Parsed p;
        p.mOk = parseActorRecordsBytes(cut, "test.esp", defaultOrder(), ActorLimits(), p.mRecords, p.mError);
        EXPECT_FALSE(p.mOk);
        ASSERT_TRUE(p.mRecords.mWalkError);
        EXPECT_FALSE(p.mRecords.mWalkError->mMessage.empty());
        EXPECT_FALSE(p.mError.mMessage.empty());
    }

    TEST(SkyrimActorsHostile, aMorrowindFileIsRefused)
    {
        const Bytes bytes = tes3Header(1.3f, 0, "A", "B", 0, {});
        Parsed p;
        EXPECT_FALSE(parseActorRecordsBytes(bytes.str(), "m.esp", defaultOrder(), ActorLimits(), p.mRecords, p.mError));
        EXPECT_FALSE(p.mError.mMessage.empty());
        EXPECT_TRUE(p.mRecords.mNpcs.empty());
    }

    TEST(SkyrimActorsHostile, aDeletedRecordIsCountedAndKeptFlagged)
    {
        const Parsed p = parse(plugin("NPC_", record4("NPC_", RecordFlags::Deleted, 0x01000200, edid("Gone"))));
        ASSERT_TRUE(p.mOk);
        EXPECT_EQ(p.mRecords.mCounts.at(Content::fourCC("NPC_")).mDeleted, 1u);
        ASSERT_EQ(p.mRecords.mNpcs.size(), 1u);
        EXPECT_TRUE(p.mRecords.mNpcs[0].mCommon.deleted());
    }

    // ---- the index: overrides, templates, leveled lists ------------------------------------------------------------

    ActorNpc makeNpc(FormId id, FormId tmpl = 0, std::uint16_t flags = 0)
    {
        ActorNpc n;
        n.mCommon.mId = id;
        n.mTemplate = tmpl;
        n.mTemplateFlags = flags;
        return n;
    }

    LeveledList makeList(FormId id, bool npcList, std::vector<LeveledEntry> entries, std::uint8_t flags = 0)
    {
        LeveledList l;
        l.mCommon.mId = id;
        l.mIsNpcList = npcList;
        l.mFlags = flags;
        l.mEntries = std::move(entries);
        return l;
    }

    LeveledEntry entry(std::uint16_t level, FormId ref)
    {
        LeveledEntry e;
        e.mLevel = level;
        e.mReference = ref;
        e.mCount = 1;
        return e;
    }

    TEST(SkyrimActorsIndex, aLaterPluginReplacesAnEarlierRecordAndADeleteRemovesIt)
    {
        ActorRecords first;
        ActorNpc a = makeNpc(0x100);
        a.mCommon.mEditorId = "first";
        first.mNpcs.push_back(a);
        first.mNpcs.push_back(makeNpc(0x101));
        ActorRecords second;
        ActorNpc b = makeNpc(0x100);
        b.mCommon.mEditorId = "second";
        second.mNpcs.push_back(b);
        ActorNpc gone = makeNpc(0x101);
        gone.mCommon.mFlags = RecordFlags::Deleted;
        second.mNpcs.push_back(gone);
        ActorIndex index;
        index.add(first);
        ASSERT_NE(index.npc(0x101), nullptr);
        index.add(second);
        ASSERT_NE(index.npc(0x100), nullptr);
        EXPECT_EQ(index.npc(0x100)->mCommon.mEditorId, "second");
        EXPECT_EQ(index.npc(0x101), nullptr);
        EXPECT_EQ(index.npcCount(), 1u);
        EXPECT_EQ(index.npcIds(), (std::vector<FormId>{ 0x100u }));
    }

    TEST(SkyrimActorsIndex, anNpcWithoutATemplateResolvesToItself)
    {
        ActorRecords r;
        ActorNpc n = makeNpc(0x100);
        n.mRace = 0x200;
        n.mClass = 0x300;
        n.mDefaultOutfit = 0x400;
        n.mName.mText = "Own";
        n.mInventory.push_back({ 0x500, 3 });
        r.mNpcs.push_back(n);
        ActorIndex index;
        index.add(r);
        ActorIndex::Resolved out;
        ASSERT_EQ(index.resolve(0x100, 10, 0, out), ActorIndex::ResolveStatus::Ok);
        EXPECT_EQ(out.mDepth, 0u);
        EXPECT_EQ(out.mRace, 0x200u);
        EXPECT_EQ(out.mClass, 0x300u);
        EXPECT_EQ(out.mDefaultOutfit, 0x400u);
        EXPECT_EQ(out.mName.mText, "Own");
        EXPECT_EQ(out.mTraitsFrom, 0x100u);
        ASSERT_EQ(out.mInventory.size(), 1u);
    }

    TEST(SkyrimActorsIndex, eachCategoryFollowsItsOwnFlag)
    {
        ActorRecords r;
        ActorNpc tmpl = makeNpc(0x200);
        tmpl.mRace = 0xA;
        tmpl.mClass = 0xB;
        tmpl.mDefaultOutfit = 0xC;
        tmpl.mName.mText = "Template";
        tmpl.mFactions.push_back({ 0xF1, 2 });
        tmpl.mPackages.push_back(0xD1);
        ActorNpc n = makeNpc(0x100, 0x200, TemplateFlags::Traits | TemplateFlags::Inventory);
        n.mRace = 0x1;
        n.mClass = 0x2;
        n.mDefaultOutfit = 0x3;
        n.mName.mText = "Own";
        n.mFactions.push_back({ 0xF2, 1 });
        r.mNpcs.push_back(tmpl);
        r.mNpcs.push_back(n);
        ActorIndex index;
        index.add(r);
        ActorIndex::Resolved out;
        ASSERT_EQ(index.resolve(0x100, 10, 0, out), ActorIndex::ResolveStatus::Ok);
        EXPECT_EQ(out.mDepth, 1u);
        EXPECT_EQ(out.mRace, 0xAu); // traits: from the template
        EXPECT_EQ(out.mDefaultOutfit, 0xCu); // inventory: from the template
        EXPECT_EQ(out.mClass, 0x2u); // stats: own
        EXPECT_EQ(out.mName.mText, "Own"); // base data: own
        ASSERT_EQ(out.mFactions.size(), 1u);
        EXPECT_EQ(out.mFactions[0].mFaction, 0xF2u); // factions: own
        EXPECT_TRUE(out.mPackages.empty()); // packages: own (none)
        EXPECT_EQ(out.mTraitsFrom, 0x200u);
        EXPECT_EQ(out.mStatsFrom, 0x100u);
    }

    TEST(SkyrimActorsIndex, aChainFollowsOnlyWhileTheFlagKeepsBeingSet)
    {
        ActorRecords r;
        ActorNpc c = makeNpc(0x300);
        c.mRace = 0xCC;
        ActorNpc b = makeNpc(0x200, 0x300, TemplateFlags::Traits); // B uses C's traits
        b.mRace = 0xBB;
        ActorNpc a = makeNpc(0x100, 0x200, TemplateFlags::Traits | TemplateFlags::Stats);
        a.mRace = 0xAA;
        a.mClass = 0x11;
        b.mClass = 0x22; // B's stats are its own: B has no Stats flag, so A's Stats stop at B
        r.mNpcs = { a, b, c };
        ActorIndex index;
        index.add(r);
        ActorIndex::Resolved out;
        ASSERT_EQ(index.resolve(0x100, 10, 0, out), ActorIndex::ResolveStatus::Ok);
        EXPECT_EQ(out.mRace, 0xCCu); // A -> B -> C
        EXPECT_EQ(out.mClass, 0x22u); // A -> B, stops
        EXPECT_EQ(out.mDepth, 2u);
    }

    TEST(SkyrimActorsIndex, aTemplateCycleIsReportedNotFollowedForever)
    {
        ActorRecords r;
        r.mNpcs = { makeNpc(0x100, 0x200, TemplateFlags::All), makeNpc(0x200, 0x100, TemplateFlags::All) };
        ActorIndex index;
        index.add(r);
        ActorIndex::Resolved out;
        EXPECT_EQ(index.resolve(0x100, 10, 0, out), ActorIndex::ResolveStatus::Cycle);
        ActorRecords self;
        self.mNpcs = { makeNpc(0x300, 0x300, TemplateFlags::All) };
        ActorIndex index2;
        index2.add(self);
        EXPECT_EQ(index2.resolve(0x300, 10, 0, out), ActorIndex::ResolveStatus::Cycle);
    }

    TEST(SkyrimActorsIndex, aVeryLongChainIsTooDeep)
    {
        ActorRecords r;
        for (FormId i = 0; i < 40; ++i)
            r.mNpcs.push_back(makeNpc(0x1000 + i, 0x1000 + i + 1, TemplateFlags::All));
        r.mNpcs.push_back(makeNpc(0x1000 + 40));
        ActorIndex index;
        index.add(r);
        ActorIndex::Resolved out;
        EXPECT_EQ(index.resolve(0x1000, 10, 0, out), ActorIndex::ResolveStatus::TooDeep);
    }

    TEST(SkyrimActorsIndex, unknownNpcAndBrokenLinks)
    {
        ActorRecords r;
        r.mNpcs = { makeNpc(0x100, 0x999, TemplateFlags::All) };
        ActorIndex index;
        index.add(r);
        ActorIndex::Resolved out;
        EXPECT_EQ(index.resolve(0x55, 10, 0, out), ActorIndex::ResolveStatus::UnknownNpc);
        EXPECT_EQ(index.resolve(0x100, 10, 0, out), ActorIndex::ResolveStatus::BrokenLink);
    }

    TEST(SkyrimActorsIndex, aLeveledNpcTemplateIsPickedByLevel)
    {
        ActorRecords r;
        ActorNpc low = makeNpc(0x201);
        low.mRace = 0x1;
        ActorNpc high = makeNpc(0x202);
        high.mRace = 0x2;
        ActorNpc user = makeNpc(0x100, 0x400, TemplateFlags::Traits);
        r.mNpcs = { low, high, user };
        ActorIndex index;
        index.add(r);
        index.addLeveledLists({ makeList(0x400, true, { entry(1, 0x201), entry(10, 0x202) }) });
        ActorIndex::Resolved out;
        ASSERT_EQ(index.resolve(0x100, 5, 0, out), ActorIndex::ResolveStatus::Ok);
        EXPECT_EQ(out.mRace, 0x1u);
        ASSERT_EQ(index.resolve(0x100, 12, 0, out), ActorIndex::ResolveStatus::Ok);
        EXPECT_EQ(out.mRace, 0x2u);
        EXPECT_EQ(out.mTraitsFrom, 0x202u);
        // nothing eligible below level 1 -> a broken link
        EXPECT_EQ(index.resolve(0x100, 0, 0, out), ActorIndex::ResolveStatus::BrokenLink);
    }

    TEST(SkyrimActorsIndex, pickLeveledIsDeterministic)
    {
        ActorIndex index;
        index.addLeveledLists({ makeList(0x500, false, { entry(1, 0xA), entry(1, 0xB), entry(3, 0xC) }),
            makeList(0x501, false, { entry(1, 0xA), entry(1, 0xB), entry(3, 0xC) },
                LeveledFlags::CalcFromAllLevelsLessThanPlayer) });
        // Without the flag only the highest eligible level is used.
        EXPECT_EQ(index.pickLeveled(0x500, 5, 0)->mReference, 0xCu);
        EXPECT_EQ(index.pickLeveled(0x500, 5, 7)->mReference, 0xCu);
        EXPECT_EQ(index.pickLeveled(0x500, 2, 0)->mReference, 0xAu);
        EXPECT_EQ(index.pickLeveled(0x500, 2, 1)->mReference, 0xBu);
        EXPECT_EQ(index.pickLeveled(0x500, 2, 2)->mReference, 0xAu);
        // With the flag, every entry at or below the level.
        EXPECT_EQ(index.pickLeveled(0x501, 5, 0)->mReference, 0xAu);
        EXPECT_EQ(index.pickLeveled(0x501, 5, 2)->mReference, 0xCu);
        EXPECT_EQ(index.pickLeveled(0x501, 0, 0), nullptr);
        EXPECT_EQ(index.pickLeveled(0x777, 5, 0), nullptr);
    }

    TEST(SkyrimActorsIndex, wornModelsOfAnOutfitFollowItsItemsAndTheRace)
    {
        ActorRecords r;
        OutfitRecord outfit;
        outfit.mCommon.mId = 0x800;
        outfit.mItems = { 0x700, 0x701, 0x600 }; // two armors, then a leveled list holding a third
        r.mOutfits.push_back(outfit);
        ArmorExtra boots;
        boots.mCommon.mId = 0x700;
        boots.mArmatures = { 0x500, 0x501 };
        ArmorExtra cuirass;
        cuirass.mCommon.mId = 0x701;
        cuirass.mArmatures = { 0x502 };
        ArmorExtra hat;
        hat.mCommon.mId = 0x702;
        hat.mArmatures = { 0x503 };
        r.mArmors = { boots, cuirass, hat };
        ArmatureRecord otherRace;
        otherRace.mCommon.mId = 0x500;
        otherRace.mRace = 0x2000;
        otherRace.mMaleModel = "wrong.nif";
        ArmatureRecord forRace;
        forRace.mCommon.mId = 0x501;
        forRace.mRace = 0x19;
        forRace.mAdditionalRaces = { 0x13746 };
        forRace.mSlotMask = 0x80;
        forRace.mMaleModel = "boots_m.nif";
        forRace.mFemaleModel = "boots_f.nif";
        forRace.mMalePriority = 4;
        forRace.mFemalePriority = 9;
        ArmatureRecord noFemale;
        noFemale.mCommon.mId = 0x502;
        noFemale.mRace = 0x13746;
        noFemale.mMaleModel = "cuirass_m.nif";
        noFemale.mSlotMask = 0x4;
        ArmatureRecord hatAddon;
        hatAddon.mCommon.mId = 0x503;
        hatAddon.mRace = 0x13746;
        hatAddon.mMaleModel = "hat_m.nif";
        hatAddon.mFemaleModel = "hat_f.nif";
        r.mArmatures = { otherRace, forRace, noFemale, hatAddon };
        ActorIndex index;
        index.add(r);
        index.addLeveledLists({ makeList(0x600, false, { entry(1, 0x702) }) });

        const auto male = index.wornModels(0x800, 0x13746, Sex::Male, 5, 0);
        ASSERT_EQ(male.size(), 3u);
        EXPECT_EQ(male[0].mArmor, 0x700u);
        EXPECT_EQ(male[0].mArmature, 0x501u); // the first addon that fits the race
        EXPECT_EQ(male[0].mModel, "boots_m.nif");
        EXPECT_EQ(male[0].mSlotMask, 0x80u);
        EXPECT_EQ(male[0].mPriority, 4);
        EXPECT_EQ(male[1].mModel, "cuirass_m.nif");
        EXPECT_EQ(male[2].mModel, "hat_m.nif"); // from the leveled list, in outfit order
        const auto female = index.wornModels(0x800, 0x13746, Sex::Female, 5, 0);
        ASSERT_EQ(female.size(), 2u); // the cuirass has no female model: left out
        EXPECT_EQ(female[0].mModel, "boots_f.nif");
        EXPECT_EQ(female[0].mPriority, 9);
        EXPECT_TRUE(index.wornModels(0x999, 0x13746, Sex::Male, 5, 0).empty());
        EXPECT_TRUE(index.wornModels(0x800, 0x7777, Sex::Male, 5, 0).empty());
    }

    TEST(SkyrimActorsIndex, aLeveledListThatContainsItselfEnds)
    {
        ActorRecords r;
        OutfitRecord outfit;
        outfit.mCommon.mId = 0x800;
        outfit.mItems = { 0x600 };
        r.mOutfits.push_back(outfit);
        ActorIndex index;
        index.add(r);
        index.addLeveledLists({ makeList(0x600, false, { entry(1, 0x600) }) });
        EXPECT_TRUE(index.wornModels(0x800, 0x13746, Sex::Male, 5, 0).empty());
    }
}
