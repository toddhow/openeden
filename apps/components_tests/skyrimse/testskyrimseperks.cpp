// Task 0179 (milestone M-S5, stage S5-7): the perk model (game_skyrimse/perks.hpp) on generated data. No game data. The
// entry point ids, function ids and tab roles are the data's (the stage log of task 0179); the evaluation rules
// (priority order, fail closed) are RECALLED-VERIFY and these tests pin THIS implementation. Every number is derived by
// hand.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/perks.hpp>

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

    constexpr std::uint32_t kPerkId = 0x01000A00;

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
        r.mOffset = 0x1234;
        return r;
    }

    // A CTDA subrecord body: compare code in the top three bits of the type byte, flags in the low five.
    Bytes ctdaBytes(std::uint16_t fn, std::uint8_t compare, float value, std::uint32_t p1 = 0, std::uint8_t flags = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>((compare << 5) | flags)).u8(0).u8(0).u8(0);
        b.f32(value).u16(fn).u16(0).u32(p1).u32(0).u32(0).u32(0).u32(0);
        return b;
    }

    // An entry point effect as the PERK record stores it.
    void addEntryEffect(Bytes& subs, std::uint8_t entry, std::uint8_t function, std::uint8_t tabs,
        std::uint8_t priority, float value,
        const std::vector<std::pair<std::uint8_t, std::vector<Bytes>>>& tabConditions = {})
    {
        subs.append(sub4("PRKE", Bytes().u8(2).u8(0).u8(priority)));
        subs.append(sub4("DATA", Bytes().u8(entry).u8(function).u8(tabs)));
        for (const auto& [tab, list] : tabConditions)
        {
            subs.append(sub4("PRKC", Bytes().u8(tab)));
            for (const Bytes& c : list)
                subs.append(sub4("CTDA", c));
        }
        subs.append(sub4("EPFT", Bytes().u8(1)));
        subs.append(sub4("EPFD", Bytes().f32(value)));
        subs.append(sub4("PRKF", Bytes()));
    }

    // ---- decoders --------------------------------------------------------------------------------------------------

    TEST(SkyrimPerkRecords, anEntryPointPerkDecodesItsEffectTabsAndConditions)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Armsman00")));
        subs.append(sub4("DATA", Bytes().u8(0).u8(1).u8(1).u8(1).u8(0)));
        subs.append(sub4("CTDA", ctdaBytes(277, 3, 20.f, 6))); // a prerequisite, before the first PRKE
        addEntryEffect(subs, 35, 3, 3, 4, 1.2f,
            { { 0, { ctdaBytes(448, 0, 0.f, 0x00000123) } }, { 1, { ctdaBytes(560, 0, 1.f, 0x0001E711, 1) } } });
        PerkRecord p;
        std::string why;
        ASSERT_TRUE(decodePerk(view(subs), raw("PERK", kPerkId), f.context(), p, why)) << why;
        EXPECT_EQ(p.mCommon.mEditorId, "Armsman00");
        EXPECT_TRUE(p.mHasData);
        EXPECT_EQ(p.mLevel, 1);
        ASSERT_EQ(p.mPrerequisites.size(), 1u);
        EXPECT_EQ(p.mPrerequisites[0].mFunction, 277);
        ASSERT_EQ(p.mEffects.size(), 1u);
        const PerkEffect& e = p.mEffects[0];
        EXPECT_EQ(e.mKind, PerkEffectKind::EntryPoint);
        EXPECT_EQ(e.mPriority, 4);
        EXPECT_EQ(e.mEntryPoint, 35);
        EXPECT_EQ(e.mFunction, PerkFunction::Multiply);
        EXPECT_EQ(e.mTabCount, 3);
        EXPECT_EQ(e.mParamType, 1);
        EXPECT_TRUE(e.mHasParam);
        EXPECT_FLOAT_EQ(e.mValue, 1.2f);
        ASSERT_EQ(e.mTabs.size(), 2u);
        EXPECT_EQ(e.mTabs[0].mIndex, 0);
        EXPECT_EQ(e.mTabs[1].mIndex, 1);
        ASSERT_EQ(e.mTabs[0].mConditions.size(), 1u);
        EXPECT_EQ(e.mTabs[0].mConditions[0].mFunction, 448);
        // The form parameter of HasPerk is resolved through the plugin's masters: local 0x123 of Skyrim.esm.
        const std::optional<FormId> expected = f.mResolver.resolve(0x00000123);
        ASSERT_TRUE(expected.has_value());
        EXPECT_EQ(e.mTabs[0].mConditions[0].mParam1, *expected);
        EXPECT_EQ(e.mTabs[1].mConditions[0].mFlags & 1, 1); // OR kept
    }

    TEST(SkyrimPerkRecords, anAbilityAndASelectSpellEffectDecode)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Mixed")));
        subs.append(sub4("DATA", Bytes().u8(0).u8(0).u8(1).u8(1).u8(0)));
        subs.append(sub4("PRKE", Bytes().u8(1).u8(0).u8(0)));
        subs.append(sub4("DATA", Bytes().u32(0x00000777)));
        subs.append(sub4("PRKF", Bytes()));
        subs.append(sub4("PRKE", Bytes().u8(2).u8(0).u8(5)));
        subs.append(sub4("DATA", Bytes().u8(51).u8(10).u8(3)));
        subs.append(sub4("EPFT", Bytes().u8(5)));
        subs.append(sub4("EPFD", Bytes().u32(0x00000888)));
        subs.append(sub4("PRKF", Bytes()));
        PerkRecord p;
        std::string why;
        ASSERT_TRUE(decodePerk(view(subs), raw("PERK", kPerkId), f.context(), p, why)) << why;
        ASSERT_EQ(p.mEffects.size(), 2u);
        EXPECT_EQ(p.mEffects[0].mKind, PerkEffectKind::Ability);
        EXPECT_EQ(p.mEffects[0].mForm, *f.mResolver.resolve(0x777));
        EXPECT_EQ(p.mEffects[1].mEntryPoint, 51);
        EXPECT_EQ(p.mEffects[1].mFunction, PerkFunction::SelectSpell);
        EXPECT_EQ(p.mEffects[1].mForm, *f.mResolver.resolve(0x888));
    }

    TEST(SkyrimPerkRecords, twoFloatsAndAQuestEffectDecode)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Boost")));
        subs.append(sub4("PRKE", Bytes().u8(2).u8(0).u8(0)));
        subs.append(sub4("DATA", Bytes().u8(35).u8(14).u8(3)));
        subs.append(sub4("EPFT", Bytes().u8(2)));
        subs.append(sub4("EPFD", Bytes().f32(146.f).f32(0.01f)));
        subs.append(sub4("PRKF", Bytes()));
        subs.append(sub4("PRKE", Bytes().u8(0).u8(0).u8(0)));
        subs.append(sub4("DATA", Bytes().u32(0x10).u32(20)));
        subs.append(sub4("PRKF", Bytes()));
        PerkRecord p;
        std::string why;
        ASSERT_TRUE(decodePerk(view(subs), raw("PERK", kPerkId), f.context(), p, why)) << why;
        ASSERT_EQ(p.mEffects.size(), 2u);
        EXPECT_EQ(p.mEffects[0].mFunction, PerkFunction::MultiplyOnePlusActorValueMult);
        EXPECT_FLOAT_EQ(p.mEffects[0].mValue, 146.f);
        EXPECT_FLOAT_EQ(p.mEffects[0].mValue2, 0.01f);
        EXPECT_EQ(p.mEffects[1].mKind, PerkEffectKind::Quest);
    }

    TEST(SkyrimPerkRecords, hostileInputIsRefusedOrCountedNeverGuessed)
    {
        Fixture f;
        std::string why;
        PerkRecord p;
        {
            // A float that is not finite.
            Bytes subs;
            subs.append(sub4("PRKE", Bytes().u8(2).u8(0).u8(0)));
            subs.append(sub4("DATA", Bytes().u8(35).u8(3).u8(1)));
            subs.append(sub4("EPFT", Bytes().u8(1)));
            subs.append(sub4("EPFD", Bytes().f32(std::numeric_limits<float>::quiet_NaN())));
            subs.append(sub4("PRKF", Bytes()));
            ASSERT_TRUE(decodePerk(view(subs), raw("PERK", kPerkId), f.context(), p, why)) << why;
            ASSERT_EQ(p.mEffects.size(), 1u);
            EXPECT_FALSE(p.mEffects[0].mHasParam);
            EXPECT_GE(f.mNotes.mOddSubrecords, 1u);
        }
        {
            // A CTDA of the wrong size is counted and dropped.
            Bytes subs;
            subs.append(sub4("PRKE", Bytes().u8(2).u8(0).u8(0)));
            subs.append(sub4("DATA", Bytes().u8(35).u8(3).u8(1)));
            subs.append(sub4("PRKC", Bytes().u8(0)));
            subs.append(sub4("CTDA", Bytes().u32(1)));
            subs.append(sub4("PRKF", Bytes()));
            const std::uint64_t before = f.mNotes.mOddSubrecords;
            ASSERT_TRUE(decodePerk(view(subs), raw("PERK", kPerkId), f.context(), p, why)) << why;
            EXPECT_EQ(f.mNotes.mOddSubrecords, before + 1);
            EXPECT_TRUE(p.mEffects[0].mTabs.empty() || p.mEffects[0].mTabs[0].mConditions.empty());
        }
        {
            // An entry point DATA of another size.
            Bytes subs;
            subs.append(sub4("PRKE", Bytes().u8(2).u8(0).u8(0)));
            subs.append(sub4("DATA", Bytes().u8(35).u8(3)));
            subs.append(sub4("PRKF", Bytes()));
            const std::uint64_t before = f.mNotes.mOddSubrecords;
            ASSERT_TRUE(decodePerk(view(subs), raw("PERK", kPerkId), f.context(), p, why)) << why;
            EXPECT_EQ(f.mNotes.mOddSubrecords, before + 1);
            EXPECT_EQ(p.mEffects[0].mEntryPoint, 0);
        }
        {
            // More effects than the cap: refused with a reason.
            Bytes subs;
            for (std::size_t i = 0; i < kMaxPerkEffects + 1; ++i)
                addEntryEffect(subs, 35, 3, 1, 0, 1.f);
            EXPECT_FALSE(decodePerk(view(subs), raw("PERK", kPerkId), f.context(), p, why));
            EXPECT_FALSE(why.empty());
        }
        {
            // A subrecord cut short.
            Bytes whole = sub4("EDID", Bytes().zstring("Cut"));
            std::string cut = whole.str();
            cut.resize(cut.size() - 2);
            Bytes truncated;
            truncated.text(cut);
            EXPECT_FALSE(decodePerk(view(truncated), raw("PERK", kPerkId), f.context(), p, why));
        }
    }

    TEST(SkyrimPerkRecords, aSpellAndItsMagicEffectDecode)
    {
        Fixture f;
        std::string why;
        Bytes spel;
        spel.append(sub4("EDID", Bytes().zstring("PerkAbQuickShot")));
        Bytes spit;
        spit.u32(0).u32(0).u32(4).u32(0).u32(0).u32(0).f32(0.5f).u32(0).u32(0); // 36 bytes, the type at +8
        spel.append(sub4("SPIT", spit));
        spel.append(sub4("EFID", Bytes().u32(0x00000555)));
        spel.append(sub4("EFIT", Bytes().f32(1.3f).u32(0).u32(0)));
        SpellRecord s;
        ASSERT_TRUE(decodeSpell(view(spel), raw("SPEL", 0x01000B00), f.context(), s, why)) << why;
        EXPECT_TRUE(s.isAbility());
        ASSERT_EQ(s.mEffects.size(), 1u);
        EXPECT_FLOAT_EQ(s.mEffects[0].mMagnitude, 1.3f);
        EXPECT_EQ(s.mEffects[0].mEffect, *f.mResolver.resolve(0x555));

        Bytes mgef;
        mgef.append(sub4("EDID", Bytes().zstring("PerkQuickShot")));
        Bytes data;
        for (int i = 0; i < 38; ++i)
            data.u32(0); // 152 bytes
        std::string text = data.str();
        auto put = [&](std::size_t offset, std::uint32_t v) { std::memcpy(text.data() + offset, &v, 4); };
        put(0, 0x208802);
        put(64, 0);
        put(68, 85);
        Bytes dataBytes;
        dataBytes.text(text);
        mgef.append(sub4("DATA", dataBytes));
        MagicEffectRecord m;
        ASSERT_TRUE(decodeMagicEffect(view(mgef), raw("MGEF", 0x01000C10), f.context(), m, why)) << why;
        EXPECT_TRUE(m.mHasData);
        EXPECT_EQ(m.mFlags, 0x208802u);
        EXPECT_EQ(m.mArchetype, 0u);
        EXPECT_EQ(m.mPrimaryValue, 85);
        EXPECT_TRUE(m.modifiesAValue());

        // A wrong sized SPIT and DATA are odd, never guessed.
        Bytes badSpel;
        badSpel.append(sub4("SPIT", Bytes().u32(1)));
        SpellRecord s2;
        ASSERT_TRUE(decodeSpell(view(badSpel), raw("SPEL", 0x01000B01), f.context(), s2, why));
        EXPECT_FALSE(s2.mHasData);
        EXPECT_FALSE(s2.isAbility());
    }

    // ---- tab roles -------------------------------------------------------------------------------------------------

    TEST(SkyrimPerkRoles, theRolesOfTheTabsPerEntryPoint)
    {
        struct Row
        {
            std::uint8_t mEntry;
            std::vector<PerkRole> mRoles;
        };
        using R = PerkRole;
        const std::vector<Row> rows = {
            { PerkEntry::CalculateWeaponDamage, { R::Owner, R::Weapon, R::Target } },
            { PerkEntry::CalculateMyCriticalHitChance, { R::Owner, R::Weapon, R::Target } },
            { PerkEntry::CalculateMyCriticalHitDamage, { R::Owner, R::Weapon, R::Target } },
            { PerkEntry::ModPowerAttackDamage, { R::Owner, R::Weapon, R::Target } },
            { PerkEntry::ModAttackDamage, { R::Owner, R::Weapon, R::Target } },
            { PerkEntry::ModTargetDamageResistance, { R::Owner, R::Weapon, R::Target } },
            { PerkEntry::ApplyCombatHitSpell, { R::Owner, R::Weapon, R::Target } },
            { PerkEntry::ModBashingDamage, { R::Owner, R::Weapon } },
            { PerkEntry::ModPowerAttackStamina, { R::Owner, R::Weapon } },
            { PerkEntry::ApplyBashingSpell, { R::Owner, R::Weapon } },
            { PerkEntry::ModIncomingDamage, { R::Owner, R::Attacker, R::Weapon } },
            { PerkEntry::ModArmorRating, { R::Owner, R::Armour } },
            { PerkEntry::ModArmorWeight, { R::Owner, R::Armour } },
            { PerkEntry::ModSpellMagnitude, { R::Owner, R::Spell, R::Target } },
            { PerkEntry::ModSpellDuration, { R::Owner, R::Spell, R::Target } },
            { PerkEntry::ModSpellRange_TargetLoc, { R::Owner, R::Spell, R::Target } },
            { PerkEntry::ModSpellCost, { R::Owner, R::Spell } },
            { PerkEntry::ModIncomingStagger, { R::Owner, R::Attacker } },
            { PerkEntry::ModTargetStagger, { R::Owner, R::Target } },
            { PerkEntry::ModPercentBlocked, { R::Owner } },
            { PerkEntry::ModRecoverArrowChance, { R::Owner } },
        };
        for (const Row& row : rows)
        {
            for (std::uint8_t tab = 0; tab < 3; ++tab)
            {
                bool known = false;
                const PerkRole role = entryPointTabRole(row.mEntry, tab, known);
                EXPECT_TRUE(known) << "entry point " << int(row.mEntry);
                EXPECT_EQ(role, tab < row.mRoles.size() ? row.mRoles[tab] : PerkRole::kCount)
                    << "entry point " << int(row.mEntry) << " tab " << int(tab);
            }
        }
        bool known = true;
        EXPECT_EQ(entryPointTabRole(200, 0, known), PerkRole::Owner);
        EXPECT_FALSE(known);
        EXPECT_EQ(entryPointTabRole(200, 1, known), PerkRole::kCount);
        EXPECT_FALSE(known);
    }

    // ---- the engine -------------------------------------------------------------------------------------------------

    class TestLibrary final : public PerkLibrary
    {
    public:
        [[nodiscard]] const PerkRecord* perk(FormId id) const override
        {
            const auto it = mPerks.find(id);
            return it == mPerks.end() ? nullptr : &it->second;
        }
        [[nodiscard]] const SpellRecord* spell(FormId id) const override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        [[nodiscard]] const MagicEffectRecord* magicEffect(FormId id) const override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        std::map<FormId, PerkRecord> mPerks;
        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
    };

    survey::Ctda cond(std::uint16_t fn, std::uint8_t compare, float value, std::uint32_t p1 = 0, std::uint8_t flags = 0)
    {
        survey::Ctda c;
        c.mCompare = compare;
        c.mFlags = flags;
        c.mComparison = value;
        c.mFunction = fn;
        c.mParam1 = p1;
        return c;
    }

    PerkEffect effect(std::uint8_t entry, std::uint8_t function, float value, std::uint8_t priority = 0,
        std::vector<PerkTab> tabs = {})
    {
        PerkEffect e;
        e.mKind = PerkEffectKind::EntryPoint;
        e.mEntryPoint = entry;
        e.mFunction = function;
        e.mTabCount = 3;
        e.mParamType = 1;
        e.mHasParam = true;
        e.mValue = value;
        e.mPriority = priority;
        e.mTabs = std::move(tabs);
        return e;
    }

    PerkTab tab(std::uint8_t index, std::vector<survey::Ctda> conditions)
    {
        PerkTab t;
        t.mIndex = index;
        t.mConditions = std::move(conditions);
        return t;
    }

    PerkRecord& addPerk(TestLibrary& lib, FormId id, std::vector<PerkEffect> effects)
    {
        PerkRecord& p = lib.mPerks[id];
        p.mCommon.mId = id;
        p.mHasData = true;
        p.mEffects = std::move(effects);
        return p;
    }

    struct Actors
    {
        std::vector<FormId> mPerks;
        std::vector<FormId> mKeywords;
        std::vector<FormId> mFactions;
        std::vector<FormId> mWorn;
        std::vector<std::pair<std::uint32_t, float>> mValues;
        PerkParticipant make(FormId base = 0x100)
        {
            PerkParticipant p;
            p.mKind = PerkParticipant::Kind::Actor;
            p.mForm = base;
            p.mPerks = &mPerks;
            p.mKeywords = &mKeywords;
            p.mFactions = &mFactions;
            p.mWornKeywords = &mWorn;
            p.mValues = &mValues;
            return p;
        }
    };

    constexpr FormId kKeywordA = 0x0001E711, kKeywordB = 0x0001E712, kPerk1 = 0x0A0001, kPerk2 = 0x0A0002;

    // One test per entry point of the top 21 (and the combat ones): a Multiply by 3 effect whose tab 1 condition is a
    // keyword of the participant the role table names. Held: 10 -> 30; not held: 10.
    TEST(SkyrimPerkEngine, everyTopEntryPointEvaluatesThroughItsOwnTab)
    {
        const std::vector<std::uint8_t> eps = { 0, 1, 2, 8, 14, 21, 26, 27, 28, 29, 30, 32, 33, 34, 35, 36, 37, 38, 39,
            40, 50, 51, 52, 56, 57, 59, 60, 66, 74, 76, 77, 85, 88 };
        for (const std::uint8_t ep : eps)
        {
            TestLibrary lib;
            // Tab 0 (owner) must hold HasPerk of itself; tab 1 holds the keyword test, on the role of tab 1.
            std::vector<PerkTab> tabs = { tab(0, { cond(448, 0, 1.f, kPerk1) }) };
            bool known = false;
            const PerkRole role1 = entryPointTabRole(ep, 1, known);
            if (known && role1 != PerkRole::kCount)
                tabs.push_back(tab(1, { cond(560, 0, 1.f, kKeywordA) }));
            addPerk(lib, kPerk1, { effect(ep, PerkFunction::Multiply, 3.f, 0, tabs) });
            PerkEngine engine(lib);
            Actors owner;
            owner.mPerks = { kPerk1 };
            PerkParticipant ownerP = owner.make();
            std::vector<FormId> withKeyword = { kKeywordA }, without = { kKeywordB };
            PerkParticipant other;
            other.mKind = PerkParticipant::Kind::Weapon;
            PerkQuery query;
            query.set(PerkRole::Owner, &ownerP);
            if (known && role1 != PerkRole::kCount)
                query.set(role1, &other);
            const std::vector<FormId> perks = { kPerk1 };

            EXPECT_TRUE(engine.hasEntryPoint(perks, ep)) << int(ep);
            EXPECT_FALSE(engine.hasEntryPoint(perks, static_cast<std::uint8_t>(ep + 1 > 91 ? 0 : ep + 1))) << int(ep);
            other.mKeywords = &withKeyword;
            EntryOutcome held = engine.apply(ep, perks, query, 10.f);
            EXPECT_FLOAT_EQ(held.mValue, 30.f) << int(ep);
            EXPECT_EQ(held.mApplied, 1u) << int(ep);
            if (known && role1 != PerkRole::kCount)
            {
                other.mKeywords = &without;
                EntryOutcome notHeld = engine.apply(ep, perks, query, 10.f);
                EXPECT_FLOAT_EQ(notHeld.mValue, 10.f) << int(ep);
                EXPECT_EQ(notHeld.mApplied, 0u) << int(ep);
                EXPECT_EQ(notHeld.mFailedConditions, 1u) << int(ep);
            }
        }
    }

    TEST(SkyrimPerkEngine, setAddMultiplyAndTheActorValueMultAreApplied)
    {
        TestLibrary lib;
        addPerk(lib, kPerk1, { effect(35, PerkFunction::Set, 4.f) });
        addPerk(lib, kPerk2, { effect(35, PerkFunction::Add, 2.f) });
        PerkEngine engine(lib);
        Actors owner;
        PerkParticipant ownerP = owner.make();
        PerkQuery query;
        query.set(PerkRole::Owner, &ownerP);
        // Set 4 (priority 0), then Add 2: both priority 0, ordered by perk form id: 4 + 2 = 6.
        EXPECT_FLOAT_EQ(engine.apply(35, std::vector<FormId>{ kPerk1, kPerk2 }, query, 10.f).mValue, 6.f);
        // Multiply 1.5 then add 1 by priority: higher priority first.
        TestLibrary lib2;
        addPerk(lib2, kPerk1, { effect(35, PerkFunction::Multiply, 1.5f, 1) });
        addPerk(lib2, kPerk2, { effect(35, PerkFunction::Add, 1.f, 9) });
        PerkEngine engine2(lib2);
        // Add first (priority 9): 10 + 1 = 11, then x 1.5 = 16.5.
        EXPECT_FLOAT_EQ(engine2.apply(35, std::vector<FormId>{ kPerk1, kPerk2 }, query, 10.f).mValue, 16.5f);
        // Function 14: x (1 + value(index 146) x 0.01); the owner's value 146 is 50: x 1.5.
        TestLibrary lib3;
        PerkEffect boost = effect(35, PerkFunction::MultiplyOnePlusActorValueMult, 146.f);
        boost.mValue2 = 0.01f;
        addPerk(lib3, kPerk1, { boost });
        PerkEngine engine3(lib3);
        EXPECT_FLOAT_EQ(engine3.apply(35, std::vector<FormId>{ kPerk1 }, query, 10.f).mValue, 10.f); // value 0: x 1
        owner.mValues = { { 146, 50.f } };
        EXPECT_FLOAT_EQ(engine3.apply(35, std::vector<FormId>{ kPerk1 }, query, 10.f).mValue, 15.f);
        // An unsupported function is counted and ignored.
        TestLibrary lib4;
        addPerk(lib4, kPerk1, { effect(35, PerkFunction::AddRange, 5.f) });
        PerkEngine engine4(lib4);
        const EntryOutcome out = engine4.apply(35, std::vector<FormId>{ kPerk1 }, query, 10.f);
        EXPECT_FLOAT_EQ(out.mValue, 10.f);
        EXPECT_EQ(out.mUnsupported, 1u);
        EXPECT_EQ(out.mApplied, 0u);
    }

    TEST(SkyrimPerkEngine, noPerksAndUnknownPerksChangeNothing)
    {
        TestLibrary lib;
        PerkEngine engine(lib);
        Actors owner;
        PerkParticipant ownerP = owner.make();
        PerkQuery query;
        query.set(PerkRole::Owner, &ownerP);
        const EntryOutcome none = engine.apply(35, {}, query, 10.f);
        EXPECT_FLOAT_EQ(none.mValue, 10.f);
        EXPECT_EQ(none.mConsidered, 0u);
        const EntryOutcome missing = engine.apply(35, std::vector<FormId>{ 0x999 }, query, 10.f);
        EXPECT_FLOAT_EQ(missing.mValue, 10.f);
        EXPECT_FALSE(engine.hasEntryPoint(std::vector<FormId>{ 0x999 }, 35));
    }

    TEST(SkyrimPerkEngine, aTabWithNobodyAndAnUnknownFunctionFailClosed)
    {
        TestLibrary lib;
        addPerk(lib, kPerk1, { effect(35, PerkFunction::Multiply, 2.f, 0, { tab(2, { cond(72, 0, 1.f, 0x55) }) }) });
        addPerk(lib, kPerk2, { effect(35, PerkFunction::Multiply, 2.f, 0, { tab(0, { cond(9999, 0, 1.f) }) }) });
        PerkEngine engine(lib);
        Actors owner;
        PerkParticipant ownerP = owner.make();
        PerkQuery query;
        query.set(PerkRole::Owner, &ownerP); // no target: tab 2 has nobody
        EXPECT_FLOAT_EQ(engine.apply(35, std::vector<FormId>{ kPerk1 }, query, 10.f).mValue, 10.f);
        EXPECT_FLOAT_EQ(engine.apply(35, std::vector<FormId>{ kPerk2 }, query, 10.f).mValue, 10.f);
        EXPECT_GE(engine.unknownConditionLists(), 1u);
    }

    TEST(SkyrimPerkEngine, theConditionFunctionsAreAnsweredFromRealState)
    {
        TestLibrary lib;
        // Each effect multiplies by 2 when one condition holds; the owner is a female level 12 Nord with a sword.
        auto make = [&](FormId id, survey::Ctda c) {
            addPerk(lib, id, { effect(35, PerkFunction::Multiply, 2.f, 0, { tab(0, { c }) }) });
        };
        constexpr FormId kRace = 0x13740, kFaction = 0x555, kOther = 0x556;
        make(0x1, cond(448, 0, 1.f, kPerk2)); // HasPerk
        make(0x2, cond(448, 0, 0.f, 0x777)); // HasPerk == 0 (not held)
        make(0x3, cond(560, 0, 1.f, kKeywordA)); // HasKeyword on the owner
        make(0x4, cond(277, 3, 30.f, 6)); // GetBaseActorValue(OneHanded) >= 30
        make(0x5, cond(277, 3, 60.f, 6)); // ... >= 60 (not held)
        make(0x6, cond(80, 0, 12.f)); // GetLevel
        make(0x7, cond(69, 0, 1.f, kRace)); // GetIsRace
        make(0x8, cond(70, 0, 1.f, 1)); // GetIsSex(female) == 1
        make(0x9, cond(71, 0, 1.f, kFaction)); // GetInFaction
        make(0xA, cond(71, 0, 1.f, kOther)); // not in that faction
        make(0xB, cond(72, 0, 1.f, 0x100)); // GetIsID
        make(0xC, cond(597, 0, 1.f, 1)); // GetEquippedItemType(right hand) == sword
        make(0xD, cond(286, 0, 1.f)); // IsSneaking (not)
        make(0xE, cond(682, 0, 1.f, kKeywordB)); // WornHasKeyword
        make(0xF, cond(14, 3, 90.f, 24)); // GetActorValue(Health) >= 90
        PerkEngine engine(lib);
        Actors owner;
        owner.mPerks = { kPerk2 };
        owner.mKeywords = { kKeywordA };
        owner.mFactions = { kFaction };
        owner.mWorn = { kKeywordB };
        owner.mValues = { { 6, 40.f }, { 24, 100.f } };
        PerkParticipant ownerP = owner.make(0x100);
        ownerP.mFemale = true;
        ownerP.mLevel = 12;
        ownerP.mRace = kRace;
        ownerP.mRightHandType = 1;
        PerkQuery query;
        query.set(PerkRole::Owner, &ownerP);
        const std::map<FormId, bool> expected = { { 0x1, true }, { 0x2, true }, { 0x3, true }, { 0x4, true },
            { 0x5, false }, { 0x6, true }, { 0x7, true }, { 0x8, true }, { 0x9, true }, { 0xA, false }, { 0xB, true },
            { 0xC, true }, { 0xD, false }, { 0xE, true }, { 0xF, true } };
        for (const auto& [id, held] : expected)
            EXPECT_FLOAT_EQ(engine.apply(35, std::vector<FormId>{ id }, query, 10.f).mValue, held ? 20.f : 10.f)
                << "perk " << id;
    }

    TEST(SkyrimPerkEngine, theFiveExtraFunctionsNeedOnlyThePerkEvaluator)
    {
        TestLibrary lib;
        auto make = [&](FormId id, survey::Ctda c) {
            addPerk(lib, id, { effect(35, PerkFunction::Multiply, 2.f, 0, { tab(0, { c }) }) });
        };
        make(0x1, cond(569, 0, 1.f)); // IsBlocking
        make(0x2, cond(673, 0, 1.f)); // IsPowerAttacking
        make(0x3, cond(715, 0, 1.f)); // IsUndead (on the owner)
        make(0x4, cond(568, 0, 1.f)); // IsSprinting
        make(0x5, cond(697, 0, 1.f, 0x4242)); // IsAttackType(keyword)
        PerkEngine engine(lib);
        Actors owner;
        PerkParticipant ownerP = owner.make();
        PerkQuery query;
        query.set(PerkRole::Owner, &ownerP);
        auto run = [&](FormId id) { return engine.apply(35, std::vector<FormId>{ id }, query, 10.f).mValue; };
        for (FormId id : { 0x1, 0x2, 0x3, 0x4, 0x5 })
            EXPECT_FLOAT_EQ(run(id), 10.f) << id;
        ownerP.mBlocking = true;
        ownerP.mPowerAttacking = true;
        ownerP.mUndead = true;
        ownerP.mSprinting = true;
        ownerP.mAttackType = 0x4242;
        for (FormId id : { 0x1, 0x2, 0x3, 0x4, 0x5 })
            EXPECT_FLOAT_EQ(run(id), 20.f) << id;
        // The global table is untouched: a plain evaluator still does not know them.
        EXPECT_EQ(conditions::findFunction(569), nullptr);
        EXPECT_EQ(conditions::findFunction(715), nullptr);
    }

    TEST(SkyrimPerkEngine, weaponAndArmourKeywordsAnswerHasKeywordOnTheirOwnTab)
    {
        TestLibrary lib;
        addPerk(lib, kPerk1,
            { effect(85, PerkFunction::Multiply, 1.25f, 0, { tab(1, { cond(560, 0, 1.f, kKeywordA) }) }) });
        PerkEngine engine(lib);
        Actors owner;
        PerkParticipant ownerP = owner.make();
        std::vector<FormId> heavy = { kKeywordA }, light = { kKeywordB };
        PerkParticipant armour;
        armour.mKind = PerkParticipant::Kind::Armour;
        PerkQuery query;
        query.set(PerkRole::Owner, &ownerP);
        query.set(PerkRole::Armour, &armour);
        armour.mKeywords = &heavy;
        EXPECT_FLOAT_EQ(engine.apply(85, std::vector<FormId>{ kPerk1 }, query, 40.f).mValue, 50.f);
        armour.mKeywords = &light;
        EXPECT_FLOAT_EQ(engine.apply(85, std::vector<FormId>{ kPerk1 }, query, 40.f).mValue, 40.f);
    }

    TEST(SkyrimPerkEngine, selectSpellReturnsTheSpellsThatHoldInPriorityOrder)
    {
        TestLibrary lib;
        PerkEffect a = effect(51, PerkFunction::SelectSpell, 0.f, 1);
        a.mParamType = 5;
        a.mForm = 0xA1;
        PerkEffect b = effect(51, PerkFunction::SelectSpell, 0.f, 7);
        b.mParamType = 5;
        b.mForm = 0xB2;
        PerkEffect c = effect(51, PerkFunction::SelectSpell, 0.f, 9, { tab(0, { cond(448, 0, 1.f, 0x9999) }) });
        c.mParamType = 5;
        c.mForm = 0xC3;
        addPerk(lib, kPerk1, { a, b, c });
        PerkEngine engine(lib);
        Actors owner;
        PerkParticipant ownerP = owner.make();
        PerkQuery query;
        query.set(PerkRole::Owner, &ownerP);
        std::array<FormId, 4> out{};
        const std::size_t n = engine.selectSpells(51, std::vector<FormId>{ kPerk1 }, query, out);
        ASSERT_EQ(n, 2u); // c's HasPerk fails
        EXPECT_EQ(out[0], 0xB2u);
        EXPECT_EQ(out[1], 0xA1u);
    }

    // ---- the records the perks read the actors and items from
    // ----------------------------------------------------------

    TEST(SkyrimPerkRecords, aWeaponKeepsItsKeywordsAndAnArmourToo)
    {
        Fixture f;
        std::string why;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("IronSword")));
        subs.append(sub4("KWDA", Bytes().u32(0x0001E711).u32(0x00000999)));
        WeaponRecord w;
        ASSERT_TRUE(decodeWeapon(view(subs), raw("WEAP", 0x01000D10), f.context(), w, why)) << why;
        ASSERT_EQ(w.mKeywords.size(), 2u);
        EXPECT_EQ(w.mKeywords[0], *f.mResolver.resolve(0x0001E711));
        Bytes odd;
        odd.append(sub4("KWDA", Bytes().u32(1).u8(2)));
        WeaponRecord w2;
        ASSERT_TRUE(decodeWeapon(view(odd), raw("WEAP", 0x01000D11), f.context(), w2, why));
        EXPECT_TRUE(w2.mKeywords.empty());
        EXPECT_GE(f.mNotes.mOddSubrecords, 1u);
    }

    TEST(SkyrimPerkRecords, anNpcKeepsItsPerksFromPrkrAndAnArmourItsKeywords)
    {
        LoadOrder order;
        std::string err;
        ASSERT_TRUE(order.add("Skyrim.esm", false, err));
        ASSERT_TRUE(order.add("test.esp", false, err));
        auto header = [] { return tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44); };
        Bytes npc;
        npc.append(sub4("EDID", Bytes().zstring("PerkNpc")));
        npc.append(sub4("PRKR", Bytes().u32(0x00079343).u8(1).u8(0).u8(0).u8(0)));
        npc.append(sub4("PRKR", Bytes().u32(0x00079344).u8(1).u8(0).u8(0).u8(0)));
        npc.append(sub4("PRKR", Bytes().u32(7))); // a wrong size is odd, not guessed
        Bytes plugin = header();
        plugin.append(group4(Content::fourCC("NPC_"), 0, record4("NPC_", 0, 0x01000200, npc)));
        ActorRecords records;
        Content::Error error;
        ASSERT_TRUE(parseActorRecordsBytes(plugin.str(), "test.esp", order, ActorLimits(), records, error))
            << error.describe();
        ASSERT_EQ(records.mNpcs.size(), 1u);
        ASSERT_EQ(records.mNpcs[0].mPerks.size(), 2u);
        EXPECT_EQ(records.mNpcs[0].mPerks[0], 0x00079343u);
        EXPECT_EQ(records.mNpcs[0].mPerks[1], 0x00079344u);

        Bytes armo;
        armo.append(sub4("EDID", Bytes().zstring("IronArmor")));
        armo.append(sub4("DNAM", Bytes().u32(2500)));
        armo.append(sub4("KWDA", Bytes().u32(0x0006BBD2)));
        Bytes plugin2 = header();
        plugin2.append(group4(Content::fourCC("ARMO"), 0, record4("ARMO", 0, 0x01000300, armo)));
        ActorRecords records2;
        ASSERT_TRUE(parseActorRecordsBytes(plugin2.str(), "test.esp", order, ActorLimits(), records2, error))
            << error.describe();
        ASSERT_EQ(records2.mArmors.size(), 1u);
        ASSERT_EQ(records2.mArmors[0].mKeywords.size(), 1u);
        EXPECT_EQ(records2.mArmors[0].mKeywords[0], 0x0006BBD2u);
    }

    TEST(SkyrimPerkAbilities, anAbilitySpellAddsItsValueModifierMagnitude)
    {
        TestLibrary lib;
        PerkEffect ability;
        ability.mKind = PerkEffectKind::Ability;
        ability.mForm = 0x700;
        addPerk(lib, kPerk1, { ability });
        SpellRecord& spell = lib.mSpells[0x700];
        spell.mHasData = true;
        spell.mType = SpellRecord::kTypeAbility;
        spell.mEffects = { { 0x800, 1.3f, 0, 0, 0, {} }, { 0x801, 5.f, 0, 0, 0, {} } };
        {
            MagicEffectRecord e;
            e.mHasData = true;
            e.mPrimaryValue = 85;
            lib.mEffects[0x800] = e;
        }
        {
            MagicEffectRecord e;
            e.mHasData = true;
            e.mArchetype = 4;
            e.mPrimaryValue = 39;
            lib.mEffects[0x801] = e;
        } // archetype 4 is not a value modifier
        std::vector<FormId> spells;
        collectPerkAbilities(lib, std::vector<FormId>{ kPerk1 }, spells);
        ASSERT_EQ(spells.size(), 1u);
        EXPECT_EQ(spells[0], 0x700u);
        bool found = false;
        EXPECT_FLOAT_EQ(abilityModifier(lib, spells, 85, found), 1.3f);
        EXPECT_TRUE(found);
        EXPECT_FLOAT_EQ(abilityModifier(lib, spells, 39, found), 0.f);
        EXPECT_FALSE(found);
        // A spell that is not an ability counts for nothing.
        spell.mType = 0;
        EXPECT_FLOAT_EQ(abilityModifier(lib, spells, 85, found), 0.f);
        EXPECT_FALSE(found);
    }
}
