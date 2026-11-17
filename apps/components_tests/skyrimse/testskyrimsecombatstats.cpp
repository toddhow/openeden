// Task 0169 (milestone M-S5, stage S5-1 subset): Skyrim SE combat numbers: WEAP / RACE / GMST decoders, armour rating,
// a melee hit, reach and regeneration, on generated data. No game data. Every expected number is derived by hand in the
// comment above it. The formulas are RECALLED-VERIFY (plausible and tunable); these tests pin THIS implementation of
// them so a change is deliberate.
#include <game_skyrimse/combat_stats.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kWeap = 0x01000A00, kRace = 0x01000B00, kGmst = 0x01000C00;

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
        r.mOffset = 0x4321;
        return r;
    }

    Bytes weaponData(std::uint32_t value, float weight, std::uint16_t damage)
    {
        return Bytes().u32(value).f32(weight).u16(damage);
    }

    // DNAM, 100 bytes: +0 animation type, +4 speed, +8 reach, +12 flags, +76 skill actor value, +96 stagger.
    Bytes weaponDnam(
        std::uint8_t type, float speed, float reach, std::uint16_t flags, std::uint32_t skill, float stagger)
    {
        Bytes d;
        d.u8(type).u8(0).u8(0).u8(0).f32(speed).f32(reach).u16(flags);
        while (d.str().size() < 76)
            d.u8(0);
        d.u32(skill);
        while (d.str().size() < 96)
            d.u8(0);
        d.f32(stagger);
        return d;
    }

    // CRDT, 24 bytes: +0 damage u16, +4 multiplier f32.
    Bytes weaponCrdt(std::uint16_t damage, float multiplier)
    {
        Bytes d;
        d.u16(damage).u16(0).f32(multiplier);
        while (d.str().size() < 24)
            d.u8(0);
        return d;
    }

    Bytes raceData(float health, float magicka, float stamina, float damage, float reach)
    {
        Bytes d;
        while (d.str().size() < 84)
            d.u8(0);
        d.f32(health).f32(magicka).f32(stamina).f32(damage).f32(reach);
        while (d.str().size() < 164)
            d.u8(0);
        return d;
    }

    // ---- WEAP ------------------------------------------------------------------------------------------------------

    TEST(SkyrimCombatStatsWeapon, everyFieldIsDecoded)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("IronSword")));
        subs.append(sub4("DATA", weaponData(25, 9.0f, 7)));
        subs.append(sub4("DNAM", weaponDnam(1, 1.0f, 1.3f, 64, 6, 0.75f)));
        subs.append(sub4("CRDT", weaponCrdt(5, 1.5f)));
        WeaponRecord w;
        std::string why;
        ASSERT_TRUE(decodeWeapon(view(subs), raw("WEAP", kWeap), f.context(), w, why)) << why;
        EXPECT_EQ(w.mCommon.mEditorId, "IronSword");
        EXPECT_TRUE(w.mHasData);
        EXPECT_EQ(w.mValue, 25u);
        EXPECT_FLOAT_EQ(w.mWeight, 9.0f);
        EXPECT_EQ(w.mDamage, 7);
        EXPECT_TRUE(w.mHasDnam);
        EXPECT_EQ(w.mAnimType, WeaponAnimType::OneHandSword);
        EXPECT_FLOAT_EQ(w.mSpeed, 1.0f);
        EXPECT_FLOAT_EQ(w.mReach, 1.3f);
        EXPECT_EQ(w.mFlags, 64);
        EXPECT_EQ(w.mSkillValue, 6u);
        EXPECT_FLOAT_EQ(w.mStagger, 0.75f);
        EXPECT_TRUE(w.mHasCrit);
        EXPECT_EQ(w.mCritDamage, 5);
        EXPECT_FLOAT_EQ(w.mCritMultiplier, 1.5f);
        EXPECT_TRUE(w.isMelee());
        EXPECT_EQ(w.skillSlot(), 0); // one-handed: actor value 6 is slot 0
    }

    TEST(SkyrimCombatStatsWeapon, skillSlotAndMeleeFollowTheStoredValues)
    {
        WeaponRecord w;
        w.mSkillValue = 8; // archery
        w.mAnimType = WeaponAnimType::Bow;
        EXPECT_EQ(w.skillSlot(), 2);
        EXPECT_FALSE(w.isMelee());
        w.mSkillValue = 5; // below the skills
        EXPECT_EQ(w.skillSlot(), 255);
        w.mSkillValue = 6 + 18; // one past the last skill
        EXPECT_EQ(w.skillSlot(), 255);
        w.mSkillValue = 0xFFFFFFFFu;
        EXPECT_EQ(w.skillSlot(), 255);
    }

    TEST(SkyrimCombatStatsWeapon, subrecordsOfAnotherSizeAreCountedAndNotUsed)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("DATA", Bytes().u32(1).f32(1.f))); // 8 bytes, not 10
        subs.append(sub4("DNAM", Bytes().u32(1)));
        subs.append(sub4("CRDT", Bytes().u16(1)));
        WeaponRecord w;
        std::string why;
        ASSERT_TRUE(decodeWeapon(view(subs), raw("WEAP", kWeap), f.context(), w, why)) << why;
        EXPECT_FALSE(w.mHasData);
        EXPECT_FALSE(w.mHasDnam);
        EXPECT_FALSE(w.mHasCrit);
        EXPECT_EQ(f.mNotes.mOddBy["WEAP.DATA"], 1u);
        EXPECT_EQ(f.mNotes.mOddBy["WEAP.DNAM"], 1u);
        EXPECT_EQ(f.mNotes.mOddBy["WEAP.CRDT"], 1u);
    }

    TEST(SkyrimCombatStatsWeapon, aNonFiniteFloatMakesTheSubrecordUnused)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("DATA", weaponData(1, std::nanf(""), 7)));
        subs.append(sub4("DNAM", weaponDnam(1, 1.f, std::numeric_limits<float>::infinity(), 0, 6, 0.f)));
        subs.append(sub4("CRDT", weaponCrdt(5, std::nanf(""))));
        WeaponRecord w;
        std::string why;
        ASSERT_TRUE(decodeWeapon(view(subs), raw("WEAP", kWeap), f.context(), w, why)) << why;
        EXPECT_FALSE(w.mHasData);
        EXPECT_FALSE(w.mHasDnam);
        EXPECT_FALSE(w.mHasCrit);
    }

    TEST(SkyrimCombatStatsWeapon, aCutShortSubrecordIsALocatedError)
    {
        Fixture f;
        Bytes damaged; // a DNAM that claims 100 bytes and holds 10
        damaged.u32(Content::fourCC("DNAM")).u16(100);
        for (int i = 0; i < 10; ++i)
            damaged.u8(1);
        WeaponRecord w;
        std::string why;
        EXPECT_FALSE(decodeWeapon(view(damaged), raw("WEAP", kWeap), f.context(), w, why));
        EXPECT_NE(why.find("DNAM"), std::string::npos) << why;
    }

    // ---- RACE ------------------------------------------------------------------------------------------------------

    TEST(SkyrimCombatStatsRace, regenerationAndUnarmedNumbersAreDecoded)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("NordRace")));
        subs.append(sub4("DATA", raceData(0.7f, 3.f, 5.f, 4.f, 96.f)));
        RaceCombatRecord r;
        std::string why;
        ASSERT_TRUE(decodeRaceCombat(view(subs), raw("RACE", kRace), f.context(), r, why)) << why;
        EXPECT_TRUE(r.mHasData);
        EXPECT_FLOAT_EQ(r.mHealthRegen, 0.7f);
        EXPECT_FLOAT_EQ(r.mMagickaRegen, 3.f);
        EXPECT_FLOAT_EQ(r.mStaminaRegen, 5.f);
        EXPECT_FLOAT_EQ(r.mUnarmedDamage, 4.f);
        EXPECT_FLOAT_EQ(r.mUnarmedReach, 96.f);
    }

    TEST(SkyrimCombatStatsRace, aDataOfAnotherSizeOrWithANanIsNotUsed)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("DATA", Bytes().u32(1)));
        RaceCombatRecord r;
        std::string why;
        ASSERT_TRUE(decodeRaceCombat(view(subs), raw("RACE", kRace), f.context(), r, why)) << why;
        EXPECT_FALSE(r.mHasData);
        EXPECT_EQ(f.mNotes.mOddBy["RACE.DATA"], 1u);
        Bytes nan;
        nan.append(sub4("DATA", raceData(std::nanf(""), 3.f, 5.f, 4.f, 96.f)));
        ASSERT_TRUE(decodeRaceCombat(view(nan), raw("RACE", kRace), f.context(), r, why)) << why;
        EXPECT_FALSE(r.mHasData);
        EXPECT_EQ(f.mNotes.mOddBy["RACE.DATA"], 2u);
    }

    // ---- GMST ------------------------------------------------------------------------------------------------------

    TEST(SkyrimCombatStatsSetting, theFirstLetterOfTheEditorIdSaysTheType)
    {
        Fixture f;
        GameSetting g;
        std::string why;
        Bytes fl;
        fl.append(sub4("EDID", Bytes().zstring("fMaxArmorRating")));
        fl.append(sub4("DATA", Bytes().f32(80.f)));
        ASSERT_TRUE(decodeGameSetting(view(fl), raw("GMST", kGmst), f.context(), g, why)) << why;
        EXPECT_TRUE(g.mNumeric);
        EXPECT_FLOAT_EQ(g.mValue, 80.f);

        Bytes in;
        in.append(sub4("EDID", Bytes().zstring("iPerkBlockDisarmChance")));
        in.append(sub4("DATA", Bytes().u32(static_cast<std::uint32_t>(-7))));
        ASSERT_TRUE(decodeGameSetting(view(in), raw("GMST", kGmst), f.context(), g, why)) << why;
        EXPECT_TRUE(g.mNumeric);
        EXPECT_FLOAT_EQ(g.mValue, -7.f);

        Bytes bo;
        bo.append(sub4("EDID", Bytes().zstring("bFlag")));
        bo.append(sub4("DATA", Bytes().u32(5)));
        ASSERT_TRUE(decodeGameSetting(view(bo), raw("GMST", kGmst), f.context(), g, why)) << why;
        EXPECT_TRUE(g.mNumeric);
        EXPECT_FLOAT_EQ(g.mValue, 1.f);

        Bytes st;
        st.append(sub4("EDID", Bytes().zstring("sSomeText")));
        st.append(sub4("DATA", Bytes().u32(0)));
        ASSERT_TRUE(decodeGameSetting(view(st), raw("GMST", kGmst), f.context(), g, why)) << why;
        EXPECT_FALSE(g.mNumeric);
    }

    TEST(SkyrimCombatStatsSetting, aWrongSizeOrNanIsOddAndNotNumeric)
    {
        Fixture f;
        GameSetting g;
        std::string why;
        Bytes wrong;
        wrong.append(sub4("EDID", Bytes().zstring("fX")));
        wrong.append(sub4("DATA", Bytes().u16(1)));
        ASSERT_TRUE(decodeGameSetting(view(wrong), raw("GMST", kGmst), f.context(), g, why)) << why;
        EXPECT_FALSE(g.mNumeric);
        Bytes nan;
        nan.append(sub4("EDID", Bytes().zstring("fY")));
        nan.append(sub4("DATA", Bytes().f32(std::nanf(""))));
        ASSERT_TRUE(decodeGameSetting(view(nan), raw("GMST", kGmst), f.context(), g, why)) << why;
        EXPECT_FALSE(g.mNumeric);
        EXPECT_EQ(f.mNotes.mOddBy["GMST.DATA"], 2u);
    }

    // ---- the walk and the tables -----------------------------------------------------------------------------------

    Bytes topGroup(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), 0, records);
    }

    TEST(SkyrimCombatStatsWalk, aPluginGivesWeaponsRacesAndSettingsAndTheTablesTakeTheLastOne)
    {
        Bytes plugin = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes weapA, weapB;
        weapA.append(sub4("EDID", Bytes().zstring("A")));
        weapA.append(sub4("DATA", weaponData(1, 2.f, 10)));
        weapB.append(sub4("EDID", Bytes().zstring("A")));
        weapB.append(sub4("DATA", weaponData(1, 2.f, 20)));
        Bytes weapons = record4("WEAP", 0, kWeap, weapA);
        weapons.append(record4("WEAP", 0, kWeap, weapB)); // the same id again: the later wins
        plugin.append(topGroup("WEAP", weapons));
        Bytes race;
        race.append(sub4("EDID", Bytes().zstring("NordRace")));
        race.append(sub4("DATA", raceData(0.7f, 3.f, 5.f, 4.f, 96.f)));
        plugin.append(topGroup("RACE", record4("RACE", 0, kRace, race)));
        Bytes g1, g2, g3;
        g1.append(sub4("EDID", Bytes().zstring("fMaxArmorRating")));
        g1.append(sub4("DATA", Bytes().f32(70.f)));
        g2.append(sub4("EDID", Bytes().zstring("sText")));
        g2.append(sub4("DATA", Bytes().u32(0)));
        g3.append(sub4("EDID", Bytes().zstring("fCombatDistance")));
        g3.append(sub4("DATA", Bytes().f32(150.f)));
        Bytes gmsts = record4("GMST", 0, kGmst, g1);
        gmsts.append(record4("GMST", 0, kGmst + 1, g2));
        gmsts.append(record4("GMST", 0, kGmst + 2, g3));
        plugin.append(topGroup("GMST", gmsts));

        Content::Error error;
        const std::unique_ptr<Content::Plugin> file = Content::Plugin::open(
            std::make_unique<std::istringstream>(plugin.str(), std::ios::binary), "test.esp", error);
        ASSERT_TRUE(file != nullptr) << error.mMessage;
        CombatRecords records;
        ASSERT_TRUE(parseCombatRecords(*file, oneMaster(), ExteriorLimits(), records));
        EXPECT_EQ(records.mWeapons.size(), 2u);
        EXPECT_EQ(records.mRaces.size(), 1u);
        EXPECT_EQ(records.mSettings.size(), 2u); // the string setting is not kept
        EXPECT_TRUE(records.mErrors.empty());

        CombatIndex index;
        index.add(records);
        EXPECT_EQ(index.weaponCount(), 1u);
        const FormId id = records.mWeapons.front().mCommon.mId;
        ASSERT_NE(index.weapon(id), nullptr);
        EXPECT_EQ(index.weapon(id)->mDamage, 20); // the later record
        EXPECT_NE(index.race(records.mRaces.front().mCommon.mId), nullptr);
        EXPECT_EQ(index.weapon(0x12345678), nullptr);
        ASSERT_EQ(index.weaponIds().size(), 1u);

        std::vector<std::string> missing;
        const CombatSettings s = index.settings().combatSettings(&missing);
        EXPECT_FLOAT_EQ(s.mMaxArmorRating, 70.f); // from the plugin
        EXPECT_FLOAT_EQ(s.mCombatDistance, 150.f);
        EXPECT_FLOAT_EQ(s.mArmorScalingFactor, 0.12f); // the default
        EXPECT_EQ(missing.size(), 12u); // 14 settings, 2 present
        EXPECT_EQ(std::count(missing.begin(), missing.end(), "fMaxArmorRating"), 0);
    }

    TEST(SkyrimCombatStatsWalk, aDeletedWeaponLeavesTheIndex)
    {
        CombatRecords first, second;
        WeaponRecord w;
        w.mCommon.mId = 0x01000001;
        first.mWeapons.push_back(w);
        w.mCommon.mFlags = RecordFlags::Deleted;
        second.mWeapons.push_back(w);
        CombatIndex index;
        index.add(first);
        EXPECT_NE(index.weapon(0x01000001), nullptr);
        index.add(second);
        EXPECT_EQ(index.weapon(0x01000001), nullptr);
    }

    // ---- armour ----------------------------------------------------------------------------------------------------

    std::array<std::uint8_t, openeden::sdk::game::kActorSkillCount> skills(std::uint8_t light, std::uint8_t heavy)
    {
        std::array<std::uint8_t, openeden::sdk::game::kActorSkillCount> s{};
        s[5] = heavy;
        s[6] = light;
        return s;
    }

    TEST(SkyrimCombatStatsArmour, onePieceAtSkillZero)
    {
        // body piece, rating 25, heavy, skill 0: 25 * (1 + 0/200) = 25; percent = 25 * 0.12 + 100 * 0.03 * 1 = 3 + 3 =
        // 6
        const std::vector<ArmourPiece> pieces{ { 25.f, ArmourSlot::Body, 1, 0, {} } };
        const ArmourResult r = armourOf(pieces, skills(0, 0), CombatSettings());
        EXPECT_FLOAT_EQ(r.mRating, 25.f);
        EXPECT_EQ(r.mMainPieces, 1);
        EXPECT_NEAR(r.mMitigationPercent, 6.f, 1e-4);
    }

    TEST(SkyrimCombatStatsArmour, theSkillScalesAndTheTypeChoosesTheSkill)
    {
        // heavy piece 25 with heavy skill 100: 25 * (1 + 100/200) = 37.5; percent = 37.5 * 0.12 + 3 = 4.5 + 3 = 7.5
        const std::vector<ArmourPiece> heavy{ { 25.f, ArmourSlot::Body, 1, 0, {} } };
        ArmourResult r = armourOf(heavy, skills(0, 100), CombatSettings());
        EXPECT_NEAR(r.mRating, 37.5f, 1e-4);
        EXPECT_NEAR(r.mMitigationPercent, 7.5f, 1e-4);
        // the same piece as LIGHT with only heavy skill 100 gets no scale: 25 * 0.12 + 3 = 6
        const std::vector<ArmourPiece> light{ { 25.f, ArmourSlot::Body, 0, 0, {} } };
        r = armourOf(light, skills(0, 100), CombatSettings());
        EXPECT_NEAR(r.mRating, 25.f, 1e-4);
        EXPECT_NEAR(r.mMitigationPercent, 6.f, 1e-4);
        // a skill above 100 counts as 100: stored bytes reach 255. 25 * 1.5 = 37.5
        r = armourOf(heavy, skills(0, 255), CombatSettings());
        EXPECT_NEAR(r.mRating, 37.5f, 1e-4);
    }

    TEST(SkyrimCombatStatsArmour, clothingIgnoresTheSkillAndUsesTheClothingScale)
    {
        // clothing rating 10, scale 1.0, skills 100: 10; percent = 10 * 0.12 + 3 = 4.2
        const std::vector<ArmourPiece> clothes{ { 10.f, ArmourSlot::Body, 2, 0, {} } };
        ArmourResult r = armourOf(clothes, skills(100, 100), CombatSettings());
        EXPECT_NEAR(r.mRating, 10.f, 1e-4);
        EXPECT_NEAR(r.mMitigationPercent, 4.2f, 1e-4);
        CombatSettings s;
        s.mClothingArmorScale = 0.5f; // 10 * 0.5 = 5; percent = 0.6 + 3 = 3.6
        r = armourOf(clothes, skills(0, 0), s);
        EXPECT_NEAR(r.mRating, 5.f, 1e-4);
        EXPECT_NEAR(r.mMitigationPercent, 3.6f, 1e-4);
    }

    TEST(SkyrimCombatStatsArmour, theCapIsEightyPercent)
    {
        // four main pieces, ratings 141.75 each = 567 at skill 0: 567 * 0.12 + 12 = 68.04 + 12 = 80.04 -> capped at 80
        const std::vector<ArmourPiece> set{ { 141.75f, ArmourSlot::Head, 1, 0, {} },
            { 141.75f, ArmourSlot::Body, 1, 0, {} }, { 141.75f, ArmourSlot::Hands, 1, 0, {} },
            { 141.75f, ArmourSlot::Feet, 1, 0, {} } };
        ArmourResult r = armourOf(set, skills(0, 0), CombatSettings());
        EXPECT_NEAR(r.mRating, 567.f, 1e-3);
        EXPECT_EQ(r.mMainPieces, 4);
        EXPECT_FLOAT_EQ(r.mMitigationPercent, 80.f);
        // a huge rating stays at the cap
        const std::vector<ArmourPiece> huge{ { 1.0e9f, ArmourSlot::Body, 1, 0, {} } };
        r = armourOf(huge, skills(0, 100), CombatSettings());
        EXPECT_FLOAT_EQ(r.mMitigationPercent, 80.f);
        EXPECT_TRUE(std::isfinite(r.mRating));
        // below the cap: 4 pieces of 100 = 400 * 0.12 + 12 = 60
        const std::vector<ArmourPiece> mid{ { 100.f, ArmourSlot::Head, 1, 0, {} },
            { 100.f, ArmourSlot::Body, 1, 0, {} }, { 100.f, ArmourSlot::Hands, 1, 0, {} },
            { 100.f, ArmourSlot::Feet, 1, 0, {} } };
        r = armourOf(mid, skills(0, 0), CombatSettings());
        EXPECT_NEAR(r.mMitigationPercent, 60.f, 1e-3);
    }

    TEST(SkyrimCombatStatsArmour, aShieldAddsRatingButIsNotAMainPiece)
    {
        // shield 20 + body 30, skill 0: rating 50; main pieces 1; percent = 6 + 3 = 9
        const std::vector<ArmourPiece> pieces{ { 20.f, ArmourSlot::Shield, 1, 0, {} },
            { 30.f, ArmourSlot::Body, 1, 0, {} } };
        const ArmourResult r = armourOf(pieces, skills(0, 0), CombatSettings());
        EXPECT_NEAR(r.mRating, 50.f, 1e-4);
        EXPECT_EQ(r.mMainPieces, 1);
        EXPECT_NEAR(r.mMitigationPercent, 9.f, 1e-4);
    }

    TEST(SkyrimCombatStatsArmour, twoPiecesOfOneSlotAndRatingZeroPiecesCountOnce)
    {
        // two body pieces (10 and 10) count as one main piece; a head piece of rating 0 counts as none: 20 * 0.12 + 3
        const std::vector<ArmourPiece> pieces{ { 10.f, ArmourSlot::Body, 1, 0, {} },
            { 10.f, ArmourSlot::Body, 1, 0, {} }, { 0.f, ArmourSlot::Head, 1, 0, {} } };
        const ArmourResult r = armourOf(pieces, skills(0, 0), CombatSettings());
        EXPECT_EQ(r.mMainPieces, 1);
        EXPECT_NEAR(r.mMitigationPercent, 5.4f, 1e-4);
    }

    TEST(SkyrimCombatStatsArmour, noPiecesNegativeAndNonFiniteRatingsGiveNothing)
    {
        ArmourResult r = armourOf({}, skills(100, 100), CombatSettings());
        EXPECT_FLOAT_EQ(r.mRating, 0.f);
        EXPECT_FLOAT_EQ(r.mMitigationPercent, 0.f);
        const std::vector<ArmourPiece> bad{ { -5.f, ArmourSlot::Body, 1, 0, {} },
            { std::nanf(""), ArmourSlot::Head, 1, 0, {} },
            { std::numeric_limits<float>::infinity(), ArmourSlot::Feet, 0, 0, {} } };
        r = armourOf(bad, skills(100, 100), CombatSettings());
        EXPECT_FLOAT_EQ(r.mRating, 0.f);
        EXPECT_EQ(r.mMainPieces, 0);
        EXPECT_FLOAT_EQ(r.mMitigationPercent, 0.f);
    }

    // ---- a melee hit -----------------------------------------------------------------------------------------------

    WeaponRecord ironSword()
    {
        WeaponRecord w;
        w.mHasData = true;
        w.mDamage = 20;
        w.mWeight = 9.f;
        w.mHasDnam = true;
        w.mAnimType = WeaponAnimType::OneHandSword;
        w.mReach = 1.f;
        w.mStagger = 0.75f;
        return w;
    }

    TEST(SkyrimCombatStatsHit, aWeaponAtSkillZeroAgainstNoArmour)
    {
        // damage 20 * (1 + 0/200) = 20; stamina 20 + 9 * 1 = 29; no armour: 20
        const WeaponRecord sword = ironSword();
        MeleeInput in;
        in.mWeapon = &sword;
        const MeleeHit hit = computeMeleeHit(in, CombatSettings());
        EXPECT_FLOAT_EQ(hit.mDamageBefore, 20.f);
        EXPECT_FLOAT_EQ(hit.mDamageAfter, 20.f);
        EXPECT_FLOAT_EQ(hit.mStaminaCost, 29.f);
        EXPECT_FLOAT_EQ(hit.mStagger, 0.75f);
        EXPECT_FLOAT_EQ(hit.mCriticalDamage, 0.f);
    }

    TEST(SkyrimCombatStatsHit, theSkillRaisesDamageAndIsCapped)
    {
        const WeaponRecord sword = ironSword();
        MeleeInput in;
        in.mWeapon = &sword;
        in.mAttackerSkill = 50.f; // 20 * (1 + 50/200) = 25
        EXPECT_FLOAT_EQ(computeMeleeHit(in, CombatSettings()).mDamageBefore, 25.f);
        in.mAttackerSkill = 100.f; // 20 * 1.5 = 30
        EXPECT_FLOAT_EQ(computeMeleeHit(in, CombatSettings()).mDamageBefore, 30.f);
        in.mAttackerSkill = 255.f; // clamped to 100: 30
        EXPECT_FLOAT_EQ(computeMeleeHit(in, CombatSettings()).mDamageBefore, 30.f);
        CombatSettings s;
        s.mDamagePCSkillMax = 1.2f; // the GMST caps the multiplier: 20 * 1.2 = 24
        EXPECT_FLOAT_EQ(computeMeleeHit(in, s).mDamageBefore, 24.f);
    }

    TEST(SkyrimCombatStatsHit, armourTakesAPercentOff)
    {
        // skill 50: 25; armour 20 percent: 25 * 0.8 = 20
        const WeaponRecord sword = ironSword();
        MeleeInput in;
        in.mWeapon = &sword;
        in.mAttackerSkill = 50.f;
        in.mTargetMitigationPercent = 20.f;
        const MeleeHit hit = computeMeleeHit(in, CombatSettings());
        EXPECT_FLOAT_EQ(hit.mDamageBefore, 25.f);
        EXPECT_FLOAT_EQ(hit.mDamageAfter, 20.f);
        // more than the cap counts as the cap (80): 25 * 0.2 = 5
        in.mTargetMitigationPercent = 1000.f;
        EXPECT_FLOAT_EQ(computeMeleeHit(in, CombatSettings()).mDamageAfter, 5.f);
    }

    TEST(SkyrimCombatStatsHit, aPowerAttackDoublesDamageAndCostsMoreStamina)
    {
        // before: 20 * (1 + 1.0) = 40; stamina (20 + 9) * 2.0 = 58
        const WeaponRecord sword = ironSword();
        MeleeInput in;
        in.mWeapon = &sword;
        in.mPowerAttack = true;
        const MeleeHit hit = computeMeleeHit(in, CombatSettings());
        EXPECT_FLOAT_EQ(hit.mDamageBefore, 40.f);
        EXPECT_FLOAT_EQ(hit.mStaminaCost, 58.f);
    }

    TEST(SkyrimCombatStatsHit, unarmedUsesTheRaceDamageAndTheUnarmedStamina)
    {
        // race unarmed 4, no skill: 4; stamina fActionPointsAttackUnarmed = 22; power attack: 8 and 44
        MeleeInput in;
        in.mUnarmedDamage = 4.f;
        in.mAttackerSkill = 100.f;
        MeleeHit hit = computeMeleeHit(in, CombatSettings());
        EXPECT_FLOAT_EQ(hit.mDamageBefore, 4.f);
        EXPECT_FLOAT_EQ(hit.mStaminaCost, 22.f);
        EXPECT_FLOAT_EQ(hit.mStagger, 0.f);
        in.mPowerAttack = true;
        hit = computeMeleeHit(in, CombatSettings());
        EXPECT_FLOAT_EQ(hit.mDamageBefore, 8.f);
        EXPECT_FLOAT_EQ(hit.mStaminaCost, 44.f);
    }

    TEST(SkyrimCombatStatsHit, theCriticalExtraIsDamageTimesMultiplier)
    {
        // crit damage 5 * multiplier 1.5 = 7.5, reported apart from the normal hit
        WeaponRecord sword = ironSword();
        sword.mHasCrit = true;
        sword.mCritDamage = 5;
        sword.mCritMultiplier = 1.5f;
        MeleeInput in;
        in.mWeapon = &sword;
        const MeleeHit hit = computeMeleeHit(in, CombatSettings());
        EXPECT_FLOAT_EQ(hit.mCriticalDamage, 7.5f);
        EXPECT_FLOAT_EQ(hit.mDamageBefore, 20.f);
    }

    TEST(SkyrimCombatStatsHit, zeroNegativeAndNonFiniteInputsGiveZeroNotNaN)
    {
        WeaponRecord sword = ironSword();
        sword.mDamage = 0;
        MeleeInput in;
        in.mWeapon = &sword;
        EXPECT_FLOAT_EQ(computeMeleeHit(in, CombatSettings()).mDamageBefore, 0.f);
        const WeaponRecord ok = ironSword();
        in.mWeapon = &ok;
        in.mAttackerSkill = -40.f; // counts as 0: 20
        in.mTargetMitigationPercent = std::nanf(""); // counts as 0: 20
        MeleeHit hit = computeMeleeHit(in, CombatSettings());
        EXPECT_FLOAT_EQ(hit.mDamageBefore, 20.f);
        EXPECT_FLOAT_EQ(hit.mDamageAfter, 20.f);
        in.mAttackerSkill = std::numeric_limits<float>::infinity();
        in.mTargetMitigationPercent = -50.f; // negative armour does not add damage
        hit = computeMeleeHit(in, CombatSettings());
        EXPECT_FLOAT_EQ(hit.mDamageAfter, hit.mDamageBefore);
        EXPECT_TRUE(std::isfinite(hit.mDamageBefore));
        in.mWeapon = nullptr;
        in.mUnarmedDamage = -3.f;
        hit = computeMeleeHit(in, CombatSettings());
        EXPECT_FLOAT_EQ(hit.mDamageBefore, 0.f);
        EXPECT_GE(hit.mStaminaCost, 0.f);
    }

    TEST(SkyrimCombatStatsHit, aNonFiniteWeaponWeightGivesAFiniteCost)
    {
        WeaponRecord sword = ironSword();
        sword.mWeight = std::nanf("");
        MeleeInput in;
        in.mWeapon = &sword;
        EXPECT_FLOAT_EQ(computeMeleeHit(in, CombatSettings()).mStaminaCost, 20.f);
    }

    // ---- reach and regeneration ------------------------------------------------------------------------------------

    TEST(SkyrimCombatStatsReach, aWeaponSpansItsReachTimesTheCombatDistance)
    {
        // reach 1.3 * 141 = 183.3; unarmed: the race's 96 as stored; negative reach counts as 0
        WeaponRecord w = ironSword();
        w.mReach = 1.3f;
        EXPECT_NEAR(meleeReachUnits(&w, 96.f, CombatSettings()), 183.3f, 1e-3);
        EXPECT_FLOAT_EQ(meleeReachUnits(nullptr, 96.f, CombatSettings()), 96.f);
        w.mReach = -1.f;
        EXPECT_FLOAT_EQ(meleeReachUnits(&w, 96.f, CombatSettings()), 0.f);
    }

    TEST(SkyrimCombatStatsRegen, ratesArePercentOfTheMaximumASecond)
    {
        // Nord health 100 at 0.7 percent: 0.7 a second, 3.5 in 5 s; in combat with multiplier 0: 0
        EXPECT_NEAR(regenAmount(100.f, 0.7f, 0.f, false, 1.f), 0.7f, 1e-5);
        EXPECT_NEAR(regenAmount(100.f, 0.7f, 0.f, false, 5.f), 3.5f, 1e-5);
        EXPECT_FLOAT_EQ(regenAmount(100.f, 0.7f, 0.f, true, 5.f), 0.f);
        // stamina 120 at 5 percent in combat with 0.35: 120 * 0.05 * 0.35 = 2.1 a second
        EXPECT_NEAR(regenAmount(120.f, 5.f, 0.35f, true, 1.f), 2.1f, 1e-5);
        // magicka 50 at 3 percent out of combat, half a second: 0.75
        EXPECT_NEAR(regenAmount(50.f, 3.f, 0.33f, false, 0.5f), 0.75f, 1e-5);
    }

    TEST(SkyrimCombatStatsRegen, negativeAndNonFiniteInputsGiveZero)
    {
        EXPECT_FLOAT_EQ(regenAmount(-100.f, 5.f, 1.f, false, 1.f), 0.f);
        EXPECT_FLOAT_EQ(regenAmount(100.f, std::nanf(""), 1.f, false, 1.f), 0.f);
        EXPECT_FLOAT_EQ(regenAmount(100.f, 5.f, 1.f, false, -1.f), 0.f);
        EXPECT_FLOAT_EQ(regenAmount(std::numeric_limits<float>::infinity(), 5.f, 1.f, false, 1.f), 0.f);
    }

    // ---- settings from a table -------------------------------------------------------------------------------------

    TEST(SkyrimCombatStatsSettings, aTableAnswersCaseInsensitivelyAndFallsBack)
    {
        SettingsTable table;
        table.set("fMaxArmorRating", 75.f);
        EXPECT_TRUE(table.has("fMaxArmorRating"));
        EXPECT_FLOAT_EQ(table.get("fmaxarmorrating", 1.f), 75.f);
        EXPECT_FLOAT_EQ(table.get("fNothing", 3.f), 3.f);
        std::vector<std::string> missing;
        const CombatSettings s = table.combatSettings(&missing);
        EXPECT_FLOAT_EQ(s.mMaxArmorRating, 75.f);
        EXPECT_EQ(missing.size(), 13u);
        EXPECT_EQ(missing.front(), "fArmorScalingFactor");
    }

    TEST(SkyrimCombatStatsSettings, theDefaultsAreTheVanillaValues)
    {
        const CombatSettings s;
        EXPECT_FLOAT_EQ(s.mMaxArmorRating, 80.f);
        EXPECT_FLOAT_EQ(s.mArmorScalingFactor, 0.12f);
        EXPECT_FLOAT_EQ(s.mArmorBaseFactor, 0.03f);
        EXPECT_FLOAT_EQ(s.mDamagePCSkillMax, 1.5f);
        EXPECT_FLOAT_EQ(s.mPowerAttackDefaultBonus, 1.f);
        EXPECT_FLOAT_EQ(s.mPowerAttackStaminaPenalty, 2.f);
        EXPECT_FLOAT_EQ(s.mStaminaAttackWeaponBase, 20.f);
        EXPECT_FLOAT_EQ(s.mActionPointsAttackUnarmed, 22.f);
        EXPECT_FLOAT_EQ(s.mCombatDistance, 141.f);
        EXPECT_FLOAT_EQ(s.mCombatMagickaRegenRateMult, 0.33f);
        EXPECT_FLOAT_EQ(s.mCombatStaminaRegenRateMult, 0.35f);
    }
}
