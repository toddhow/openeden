// Task 0179 (milestone M-S5, stage S5-7): perks in the Skyrim pack's combat rules (game_skyrimse/combat.hpp), through the
// contract with a fake data source and a recording writer, as the melee tests do. Every number is worked out by hand in a
// comment from the default GMSTs: damage = WEAP damage x min(1.5, 1 + skill / 200) (x 2 for a power attack), after armour
// x (1 - mitigation / 100); stamina = 20 + weight (x 2 for a power attack). The "Iron Sword" has damage 7, weight 9,
// reach 1.0; the attacker's One-handed skill is 50 (x 1.25), the victim has 20 percent mitigation, 50 health, 80 stamina.
#include <game_skyrimse/combat.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kAttackerBase = 100;
    constexpr WorldId kVictimBase = 200;
    constexpr FormId kSwordKeyword = 0x1E711, kBowKeyword = 0x1E715, kHeavyKeyword = 0x6BBD2;
    constexpr FormId kPerkA = 0xA001, kPerkB = 0xA002, kAbilityPerk = 0xA003;

    class PerkData final : public PerkLibrary
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

    survey::Ctda cond(std::uint16_t fn, float value, std::uint32_t p1)
    {
        survey::Ctda c;
        c.mCompare = 0;
        c.mComparison = value;
        c.mFunction = fn;
        c.mParam1 = p1;
        return c;
    }

    // An entry point effect that multiplies, adds or sets, optionally with the weapon (tab 1) required to carry a keyword.
    PerkEffect fx(std::uint8_t ep, std::uint8_t function, float value, FormId weaponKeyword = 0)
    {
        PerkEffect e;
        e.mKind = PerkEffectKind::EntryPoint;
        e.mEntryPoint = ep;
        e.mFunction = function;
        e.mTabCount = 3;
        e.mParamType = 1;
        e.mHasParam = true;
        e.mValue = value;
        if (weaponKeyword != 0)
        {
            PerkTab t;
            t.mIndex = 1;
            t.mConditions = { cond(560, 1.f, weaponKeyword) };
            e.mTabs.push_back(t);
        }
        return e;
    }

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;
            mSword.mKeywords = { kSwordKeyword };

            FighterProfile attacker;
            attacker.mHealthMax = 100.f;
            attacker.mStaminaMax = 100.f;
            attacker.mSkills[0] = 50;
            attacker.mUnarmedDamage = 4.f;
            attacker.mUnarmedReach = 96.f;
            attacker.mBase = kAttackerBase;
            mProfiles[kAttackerBase] = attacker;

            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            victim.mMitigationPercent = 20.f;
            victim.mBase = kVictimBase;
            mProfiles[kVictimBase] = victim;

            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            const auto it = mProfiles.find(base);
            if (it == mProfiles.end())
                return false;
            out = it->second;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view editorId) override
        {
            return editorId == "ironsword" ? &mSword : nullptr;
        }
        CombatSettings settings() override { return mSettings; }
        const PerkLibrary* perkLibrary() override { return mHaveLibrary ? &mPerks : nullptr; }
        FormId perkByEditorId(std::string_view editorId) override
        {
            const auto it = mByName.find(std::string(editorId));
            return it == mByName.end() ? kNullFormId : it->second;
        }

        void addPerk(FormId id, std::vector<PerkEffect> effects)
        {
            PerkRecord& p = mPerks.mPerks[id];
            p.mCommon.mId = id;
            p.mEffects = std::move(effects);
        }

        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
        CombatSettings mSettings;
        PerkData mPerks;
        std::map<std::string, FormId> mByName;
        bool mHaveLibrary = true;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.slot });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject subject, const SweepRequest& request) override
        {
            mSweeps.push_back({ subject, request });
            return true;
        }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            return true;
        }
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = { true, 0.367f, 1.287f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out = mPlayerState;
                return true;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        std::vector<std::pair<CombatSubject, ClipSlot>> mClips;
        std::vector<std::pair<CombatSubject, SweepRequest>> mSweeps;
        std::vector<DamageReport> mDamage;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct CombatPerks : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };
        std::vector<std::string> mTrace;

        // The actors are added in start(), after the test changed the data.
        void start()
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.onActorAdded(info(10, kAttackerBase, "Attacker"));
            mCombat.onActorAdded(info(20, kVictimBase, "Victim"));
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, 0.25f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
        }

        void swing(CombatSubject attacker, std::vector<SweepHit> hits, ClipSlot slot = ClipSlot::Attack,
            std::uint32_t token = 5)
        {
            ClipEvent e;
            e.subject = attacker;
            e.slot = slot;
            e.token = token;
            e.kind = ClipEventKind::Hit;
            mCombat.onClipEvent(e);
            mCombat.combatUpdate(0.016f, mWriter);
            mCombat.onSweepResult(attacker, token, hits.data(), hits.size());
            mCombat.combatUpdate(0.016f, mWriter);
        }
        SweepHit hitOnVictim()
        {
            SweepHit h;
            h.target = SweepTarget::Actor;
            h.actor = mB;
            h.distance = 20.f;
            return h;
        }
        float damage()
        {
            EXPECT_EQ(mWriter.mDamage.size(), 1u);
            return mWriter.mDamage.empty() ? -1.f : mWriter.mDamage[0].amount;
        }
        bool traced(const std::string& text) const
        {
            for (const std::string& line : mTrace)
                if (line.find(text) != std::string::npos)
                    return true;
            return false;
        }
    };

    TEST_F(CombatPerks, withNoPerksTheNumbersAreTheSameAndNothingIsTraced)
    {
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 7.0f, 1e-4f); // 7 x 1.25 x 0.8
        EXPECT_FALSE(traced("perk"));
        EXPECT_EQ(mCombat.perkEffectsApplied(), 0u);
    }

    TEST_F(CombatPerks, modAttackDamageMultipliesTheAttackersDamageWhenItsWeaponTabHolds)
    {
        mData.addPerk(kPerkA, { fx(PerkEntry::ModAttackDamage, PerkFunction::Multiply, 1.2f, kSwordKeyword) });
        mData.mProfiles[kAttackerBase].mPerks = { kPerkA };
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 8.4f, 1e-4f); // 8.75 x 1.2 = 10.5, x 0.8
        EXPECT_NEAR(mWriter.mDamage[0].healthAfter, 41.6f, 1e-4f);
        EXPECT_TRUE(traced("perk"));
        EXPECT_EQ(mCombat.perkEffectsApplied(), 1u);
    }

    TEST_F(CombatPerks, aWeaponThatFailsTheTabLeavesTheDamageAlone)
    {
        mData.addPerk(kPerkA, { fx(PerkEntry::ModAttackDamage, PerkFunction::Multiply, 1.2f, kBowKeyword) });
        mData.mProfiles[kAttackerBase].mPerks = { kPerkA };
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 7.0f, 1e-4f);
        EXPECT_EQ(mCombat.perkEffectsApplied(), 0u);
    }

    TEST_F(CombatPerks, modIncomingDamageOfTheVictimComesAfterTheAttackersAndBeforeArmour)
    {
        mData.addPerk(kPerkB, { fx(PerkEntry::ModIncomingDamage, PerkFunction::Multiply, 0.5f) });
        mData.mProfiles[kVictimBase].mPerks = { kPerkB };
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 3.5f, 1e-4f); // 8.75 x 0.5 = 4.375, x 0.8
    }

    TEST_F(CombatPerks, modPowerAttackDamageOnlyChangesAPowerAttack)
    {
        mData.addPerk(kPerkA, { fx(PerkEntry::ModPowerAttackDamage, PerkFunction::Multiply, 1.5f) });
        mData.mProfiles[kAttackerBase].mPerks = { kPerkA };
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 7.0f, 1e-4f); // a normal blow: unchanged
        mWriter.mDamage.clear();
        swing(CombatSubject::of(mA), { hitOnVictim() }, ClipSlot::PowerAttack, 6);
        EXPECT_NEAR(damage(), 21.0f, 1e-3f); // 7 x 1.25 x 2 = 17.5, x 1.5 = 26.25, x 0.8
    }

    TEST_F(CombatPerks, calculateWeaponDamageChangesTheWeaponsBaseBeforeTheSkill)
    {
        mData.addPerk(kPerkA, { fx(PerkEntry::CalculateWeaponDamage, PerkFunction::Add, 3.f) });
        mData.mProfiles[kAttackerBase].mPerks = { kPerkA };
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 10.0f, 1e-3f); // (7 + 3) x 1.25 = 12.5, x 0.8
    }

    TEST_F(CombatPerks, modArmorRatingActsOnEachWornPieceThroughItsOwnTab)
    {
        FighterProfile& victim = mData.mProfiles[kVictimBase];
        victim.mPieces = { { 20.f, ArmourSlot::Body, 1, 0x500, { kHeavyKeyword } } };
        victim.mMitigationPercent = 5.4f; // 20 x 0.12 + 100 x 0.03 x 1 main piece
        PerkEffect e = fx(PerkEntry::ModArmorRating, PerkFunction::Multiply, 2.f);
        PerkTab t;
        t.mIndex = 1;
        t.mConditions = { cond(560, 1.f, kHeavyKeyword) };
        e.mTabs.push_back(t);
        mData.addPerk(kPerkB, { e });
        victim.mPerks = { kPerkB };
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 8.0675f, 1e-3f); // rating 40: 4.8 + 3 = 7.8 percent; 8.75 x 0.922
    }

    TEST_F(CombatPerks, anArmourWithoutTheKeywordIsNotMultiplied)
    {
        FighterProfile& victim = mData.mProfiles[kVictimBase];
        victim.mPieces = { { 20.f, ArmourSlot::Body, 1, 0x500, { 0x777 } } };
        victim.mMitigationPercent = 5.4f;
        PerkEffect e = fx(PerkEntry::ModArmorRating, PerkFunction::Multiply, 2.f);
        PerkTab t;
        t.mIndex = 1;
        t.mConditions = { cond(560, 1.f, kHeavyKeyword) };
        e.mTabs.push_back(t);
        mData.addPerk(kPerkB, { e });
        victim.mPerks = { kPerkB };
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 8.2775f, 1e-3f); // 8.75 x (1 - 0.054)
    }

    TEST_F(CombatPerks, aDamageResistAbilityAddsToTheMitigation)
    {
        mData.mProfiles[kVictimBase].mAbilityDamageResist = 50.f;
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 6.475f, 1e-3f); // 20 + 50 x 0.12 = 26 percent; 8.75 x 0.74
    }

    TEST_F(CombatPerks, anAbilityPerkOfTheVictimFillsTheDamageResistFromItsSpell)
    {
        PerkEffect ability;
        ability.mKind = PerkEffectKind::Ability;
        ability.mForm = 0x700;
        mData.addPerk(kAbilityPerk, { ability });
        SpellRecord& spell = mData.mPerks.mSpells[0x700];
        spell.mHasData = true;
        spell.mType = SpellRecord::kTypeAbility;
        spell.mEffects = { { 0x800, 50.f, 0, 0 } };
        mData.mPerks.mEffects[0x800] = { {}, true, 0, 0, 39 };
        mData.mProfiles[kVictimBase].mPerks = { kAbilityPerk };
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 6.475f, 1e-3f);
    }

    TEST_F(CombatPerks, modPowerAttackStaminaScalesWhatAPowerAttackCosts)
    {
        mData.addPerk(kPerkA, { fx(PerkEntry::ModPowerAttackStamina, PerkFunction::Multiply, 0.75f, kSwordKeyword) });
        mData.mProfiles[kAttackerBase].mPerks = { kPerkA };
        start();
        ClipEvent e;
        e.subject = CombatSubject::of(mA);
        e.slot = ClipSlot::PowerAttack;
        e.token = 5;
        e.kind = ClipEventKind::Hit;
        mCombat.onClipEvent(e);
        mCombat.combatUpdate(0.016f, mWriter);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), view));
        EXPECT_NEAR(view.mStamina, 56.5f, 1e-3f); // 100 - (20 + 9) x 2 x 0.75 = 100 - 43.5
    }

    TEST_F(CombatPerks, aNormalAttacksStaminaIsNotTouchedByThePowerAttackPerk)
    {
        mData.addPerk(kPerkA, { fx(PerkEntry::ModPowerAttackStamina, PerkFunction::Multiply, 0.75f) });
        mData.mProfiles[kAttackerBase].mPerks = { kPerkA };
        start();
        ClipEvent e;
        e.subject = CombatSubject::of(mA);
        e.slot = ClipSlot::Attack;
        e.token = 5;
        e.kind = ClipEventKind::Hit;
        mCombat.onClipEvent(e);
        mCombat.combatUpdate(0.016f, mWriter);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), view));
        EXPECT_NEAR(view.mStamina, 71.f, 1e-3f);
    }

    TEST_F(CombatPerks, modPercentBlockedScalesTheFractionABlockTakesOff)
    {
        mData.addPerk(kPerkB, { fx(PerkEntry::ModPercentBlocked, PerkFunction::Multiply, 1.5f) });
        mData.mProfiles[kVictimBase].mPerks = { kPerkB };
        mCombat.setTestBlock("Victim");
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        // Block skill 0: fraction 0.3, x 1.5 = 0.45; 7.0 after armour: taken 7 x 0.55 = 3.85; stamina 80 - 7 x 0.45 x 0.25.
        EXPECT_NEAR(damage(), 3.85f, 1e-3f);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mB), view));
        EXPECT_NEAR(view.mStamina, 79.2125f, 1e-3f);
    }

    TEST_F(CombatPerks, withoutThePerkTheBlockIsTheOldOne)
    {
        mCombat.setTestBlock("Victim");
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 4.9f, 1e-3f); // 7 x 0.7
    }

    TEST_F(CombatPerks, modTargetStaggerCanTurnARecoilIntoAStagger)
    {
        mData.mSword.mStagger = 0.5f;
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].second, ClipSlot::Recoil);

        FakeWriter second;
        FakeData data2;
        data2.mSword.mStagger = 0.5f;
        data2.addPerk(kPerkA, { fx(PerkEntry::ModTargetStagger, PerkFunction::Multiply, 3.f) });
        data2.mProfiles[kAttackerBase].mPerks = { kPerkA };
        SkyrimCombat combat2;
        combat2.setData(&data2);
        combat2.setTestWeapon("IronSword");
        combat2.onActorAdded(info(10, kAttackerBase, "Attacker"));
        combat2.onActorAdded(info(20, kVictimBase, "Victim"));
        second.mStates = mWriter.mStates;
        second.mPlayerState = mWriter.mPlayerState;
        ClipEvent e;
        e.subject = CombatSubject::of(mA);
        e.slot = ClipSlot::Attack;
        e.token = 5;
        e.kind = ClipEventKind::Hit;
        combat2.onClipEvent(e);
        combat2.combatUpdate(0.016f, second);
        SweepHit h = hitOnVictim();
        combat2.onSweepResult(CombatSubject::of(mA), 5, &h, 1);
        combat2.combatUpdate(0.016f, second);
        ASSERT_EQ(second.mClips.size(), 1u);
        EXPECT_EQ(second.mClips[0].second, ClipSlot::Stagger); // 0.5 x 3 = 1.5 >= 1
    }

    TEST_F(CombatPerks, modIncomingStaggerOfTheVictimCanTurnAStaggerIntoARecoil)
    {
        mData.mSword.mStagger = 1.5f;
        mData.addPerk(kPerkB, { fx(PerkEntry::ModIncomingStagger, PerkFunction::Multiply, 0.2f) });
        mData.mProfiles[kVictimBase].mPerks = { kPerkB };
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].second, ClipSlot::Recoil); // 1.5 x 0.2 = 0.3 < 1
    }

    TEST_F(CombatPerks, thePlayersPerksFromTheTestSettingApplyToItsSwing)
    {
        mData.addPerk(kPerkA, { fx(PerkEntry::ModAttackDamage, PerkFunction::Multiply, 1.2f, kSwordKeyword) });
        mData.mByName["Armsman00"] = kPerkA;
        mCombat.setTestPlayerPerks("Armsman00");
        start();
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.1f, mWriter);
        mCombat.combatUpdate(0.4f, mWriter);
        ASSERT_EQ(mWriter.mSweeps.size(), 1u);
        const SweepHit hit = hitOnVictim();
        mCombat.onSweepResult(CombatSubject::ofPlayer(), mWriter.mSweeps[0].second.token, &hit, 1);
        mCombat.combatUpdate(0.f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 7.224f, 1e-3f); // 7 x 1.075 x 1.2 = 9.03, x 0.8
    }

    TEST_F(CombatPerks, theNpcTestSettingGivesPerksToEveryNpcFighterButNotThePlayer)
    {
        mData.addPerk(kPerkA, { fx(PerkEntry::ModAttackDamage, PerkFunction::Multiply, 2.f) });
        mData.mByName["Double"] = kPerkA;
        mCombat.setTestNpcPerks("Double, Missing");
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 14.0f, 1e-3f); // 8.75 x 2 x 0.8
        EXPECT_TRUE(traced("Missing"));
    }

    TEST_F(CombatPerks, theSwitchAndAMissingLibraryTurnPerksOff)
    {
        mData.addPerk(kPerkA, { fx(PerkEntry::ModAttackDamage, PerkFunction::Multiply, 2.f) });
        mData.mProfiles[kAttackerBase].mPerks = { kPerkA };
        mCombat.setUsePerks(false);
        start();
        swing(CombatSubject::of(mA), { hitOnVictim() });
        EXPECT_NEAR(damage(), 7.0f, 1e-4f);

        FakeData data2;
        data2.addPerk(kPerkA, { fx(PerkEntry::ModAttackDamage, PerkFunction::Multiply, 2.f) });
        data2.mProfiles[kAttackerBase].mPerks = { kPerkA };
        data2.mHaveLibrary = false;
        SkyrimCombat combat2;
        FakeWriter writer2;
        writer2.mStates = mWriter.mStates;
        combat2.setData(&data2);
        combat2.setTestWeapon("IronSword");
        combat2.onActorAdded(info(10, kAttackerBase, "Attacker"));
        combat2.onActorAdded(info(20, kVictimBase, "Victim"));
        ClipEvent e;
        e.subject = CombatSubject::of(mA);
        e.slot = ClipSlot::Attack;
        e.token = 5;
        e.kind = ClipEventKind::Hit;
        combat2.onClipEvent(e);
        combat2.combatUpdate(0.016f, writer2);
        SweepHit h = hitOnVictim();
        combat2.onSweepResult(CombatSubject::of(mA), 5, &h, 1);
        combat2.combatUpdate(0.016f, writer2);
        ASSERT_EQ(writer2.mDamage.size(), 1u);
        EXPECT_NEAR(writer2.mDamage[0].amount, 7.0f, 1e-4f);
    }

    TEST(CombatPerkBow, theQuickShotAbilityMultipliesTheDrawSpeed)
    {
        FighterProfile p;
        EXPECT_FLOAT_EQ(fighterBowDrawSpeed(p, 0.9375f, 0.f), bowDrawSpeed(0.9375f, 0.f)); // none: the old number
        p.mHasWeaponSpeedAbility = true;
        p.mAbilityWeaponSpeed = 1.3f;
        EXPECT_NEAR(fighterBowDrawSpeed(p, 0.9375f, 0.f), 1.21875f, 1e-5f);
        EXPECT_NEAR(fighterBowDrawSpeed(p, 0.9375f, 0.5f), 1.828125f, 1e-5f); // (0.9375 x 1.5) x 1.3
        p.mAbilityWeaponSpeed = 100.f;
        EXPECT_FLOAT_EQ(fighterBowDrawSpeed(p, 0.9375f, 0.f), 10.f); // clamped
    }

    TEST(CombatPerkBlock, aFractionScaleOfOneIsTheOldBlockAndOtherScalesStayBelowOne)
    {
        BlockSettings s;
        BlockInput in;
        in.mDamage = 10.f;
        in.mBlockerStamina = 100.f;
        const BlockResult base = computeBlock(in, s);
        in.mFractionScale = 1.f;
        const BlockResult same = computeBlock(in, s);
        EXPECT_EQ(base.mFraction, same.mFraction);
        EXPECT_EQ(base.mDamageTaken, same.mDamageTaken);
        in.mFractionScale = 10.f;
        EXPECT_FLOAT_EQ(computeBlock(in, s).mFraction, 1.f); // never above 1
    }
}
