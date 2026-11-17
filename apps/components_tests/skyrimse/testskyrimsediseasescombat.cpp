// Task 0240 (diseases), the combat half: a landed blow of a carrier on the player tells the hook, what a disease
// changes in the player's numbers is applied to his profile and taken off again, and a weapon skill's damage percent
// scales his blows. The fixture follows testskyrimsepoison.cpp (a fake data source and a recording writer). Numbers by
// hand: the player's sword hits the victim for 7 x 1.075 x (1 - 20 percent) = 6.02; with a -25 percent skill
// modifier 4.515.
#include <game_skyrimse/combat.hpp>

#include "../contentsource/pluginbuilder.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kCarrierBase = 100, kVictimBase = 200;
    constexpr FormId kRockjoint = 0xB8782, kPlayerSpell = 0x777;

    class DiseaseCombatData final : public CombatData
    {
    public:
        DiseaseCombatData()
        {
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;

            FighterProfile carrier;
            carrier.mHealthMax = 100.f;
            carrier.mStaminaMax = 100.f;
            carrier.mUnarmedDamage = 4.f;
            carrier.mUnarmedReach = 96.f;
            carrier.mAttackSpells = { kRockjoint, 0x55 };
            mProfiles[kCarrierBase] = carrier;

            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            victim.mMitigationPercent = 20.f;
            mProfiles[kVictimBase] = victim;

            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 80.f;
            mPlayer.mStaminaRegenPercent = 5.f;
            mPlayer.mMagickaRegenPercent = 3.f;
            mPlayer.mSkills.fill(15);
            mPlayer.mSpells = { kPlayerSpell };
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

        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
        CombatSettings mSettings;
    };

    class DiseaseCombatWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject subject, const SweepRequest& request) override
        {
            mSweeps.push_back({ subject, request });
            return true;
        }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
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
        std::vector<std::pair<CombatSubject, SweepRequest>> mSweeps;
        std::vector<DamageReport> mDamage;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo diseaseInfo(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct CombatDiseaseHooks : ::testing::Test
    {
        DiseaseCombatData mData;
        SkyrimCombat mCombat;
        DiseaseCombatWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.onActorAdded(diseaseInfo(10, kCarrierBase, "Carrier"));
            mCombat.onActorAdded(diseaseInfo(20, kVictimBase, "Victim"));
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, 0.25f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
        }

        const DamageReport* carrierHitsPlayer(std::uint32_t token)
        {
            mWriter.mDamage.clear();
            ClipEvent e;
            e.subject = CombatSubject::of(mA);
            e.slot = ClipSlot::Attack;
            e.token = token;
            e.kind = ClipEventKind::Hit;
            mCombat.onClipEvent(e);
            mCombat.combatUpdate(0.016f, mWriter);
            SweepHit hit;
            hit.target = SweepTarget::Player;
            hit.distance = 30.f;
            mCombat.onSweepResult(CombatSubject::of(mA), token, &hit, 1);
            mCombat.combatUpdate(0.016f, mWriter);
            return mWriter.mDamage.empty() ? nullptr : &mWriter.mDamage[0];
        }

        const DamageReport* playerHitsVictim()
        {
            mWriter.mSweeps.clear();
            mWriter.mDamage.clear();
            mCombat.onPlayerAttack(false);
            for (int i = 0; i < 6 && mWriter.mSweeps.empty(); ++i)
                mCombat.combatUpdate(0.2f, mWriter);
            if (mWriter.mSweeps.empty())
                return nullptr;
            SweepHit hit;
            hit.target = SweepTarget::Actor;
            hit.actor = mB;
            hit.distance = 20.f;
            mCombat.onSweepResult(CombatSubject::ofPlayer(), mWriter.mSweeps[0].second.token, &hit, 1);
            mCombat.combatUpdate(0.f, mWriter);
            for (int i = 0; i < 10; ++i)
                mCombat.combatUpdate(0.2f, mWriter);
            return mWriter.mDamage.empty() ? nullptr : &mWriter.mDamage[0];
        }

        SkyrimCombat::FighterView playerView()
        {
            SkyrimCombat::FighterView view;
            EXPECT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), view));
            return view;
        }
    };

    TEST_F(CombatDiseaseHooks, aLandedBlowOnThePlayerTellsTheHookTheAttackersSpellsAndReference)
    {
        std::vector<std::pair<std::vector<FormId>, std::uint32_t>> told;
        mCombat.setPlayerHitHook([&](const std::vector<FormId>& spells, std::uint32_t attacker) {
            told.push_back({ spells, attacker });
        });
        ASSERT_NE(carrierHitsPlayer(5), nullptr);
        ASSERT_EQ(told.size(), 1u);
        EXPECT_EQ(told[0].first, (std::vector<FormId>{ kRockjoint, 0x55 }));
        EXPECT_EQ(told[0].second, 10u);
    }

    TEST_F(CombatDiseaseHooks, aBlowOfThePlayerOnAnActorDoesNotTellTheHook)
    {
        int calls = 0;
        mCombat.setPlayerHitHook([&](const std::vector<FormId>&, std::uint32_t) { ++calls; });
        ASSERT_NE(playerHitsVictim(), nullptr);
        EXPECT_EQ(calls, 0);
    }

    TEST_F(CombatDiseaseHooks, aMissedSweepDoesNotTellTheHook)
    {
        int calls = 0;
        mCombat.setPlayerHitHook([&](const std::vector<FormId>&, std::uint32_t) { ++calls; });
        ClipEvent e;
        e.subject = CombatSubject::of(mA);
        e.slot = ClipSlot::Attack;
        e.token = 9;
        e.kind = ClipEventKind::Hit;
        mCombat.onClipEvent(e);
        mCombat.combatUpdate(0.016f, mWriter);
        mCombat.onSweepResult(CombatSubject::of(mA), 9, nullptr, 0);
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(calls, 0);
    }

    TEST_F(CombatDiseaseHooks, withNoHookABlowOnThePlayerIsAsBefore)
    {
        const DamageReport* d = carrierHitsPlayer(5);
        ASSERT_NE(d, nullptr);
        EXPECT_TRUE(d->victimIsPlayer);
    }

    TEST_F(CombatDiseaseHooks, theModifiersMoveThePlayersMaximaAndComeOffAgain)
    {
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_FLOAT_EQ(playerView().mStaminaMax, 100.f);
        EXPECT_FLOAT_EQ(playerView().mMagickaMax, 80.f);
        PlayerValueModifiers m;
        m.mStaminaMax = -25.f;
        m.mMagickaMax = -30.f;
        mCombat.setPlayerValueModifiers(m);
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_FLOAT_EQ(playerView().mStaminaMax, 75.f);
        EXPECT_FLOAT_EQ(playerView().mMagickaMax, 50.f);
        EXPECT_LE(playerView().mStamina, 75.f); // the current value never stands above the new maximum
        mCombat.setPlayerValueModifiers(m); // the same again moves nothing further
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_FLOAT_EQ(playerView().mStaminaMax, 75.f);
        mCombat.setPlayerValueModifiers({});
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_FLOAT_EQ(playerView().mStaminaMax, 100.f);
        EXPECT_FLOAT_EQ(playerView().mMagickaMax, 80.f);
        EXPECT_TRUE(mCombat.playerValueModifiers().isZero());
    }

    TEST_F(CombatDiseaseHooks, modifiersAskedBeforeThePlayerHasAProfileApplyWhenItAppears)
    {
        PlayerValueModifiers m;
        m.mStaminaMax = -25.f;
        mCombat.setPlayerValueModifiers(m);
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_FLOAT_EQ(playerView().mStaminaMax, 75.f);
    }

    TEST_F(CombatDiseaseHooks, aWeaponSkillPercentScalesThePlayersBlows)
    {
        PlayerValueModifiers m;
        m.mSkillPercent[0] = -25.f; // one-handed: the test sword
        mCombat.setPlayerValueModifiers(m);
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 6.02f * 0.75f, 1e-3f);
    }

    TEST_F(CombatDiseaseHooks, aSkillPercentOfAnotherWeaponSkillOrBeyondMinusHundredChangesWhatItShould)
    {
        PlayerValueModifiers m;
        m.mSkillPercent[1] = -25.f; // two-handed: not the sword in hand
        mCombat.setPlayerValueModifiers(m);
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 6.02f, 1e-3f);
        m.mSkillPercent[0] = -400.f; // never below nothing
        mCombat.setPlayerValueModifiers(m);
        EXPECT_NE(playerHitsVictim(), nullptr);
        EXPECT_GE(mWriter.mDamage[0].amount, 0.f);
        EXPECT_LE(mWriter.mDamage[0].amount, 1e-4f);
    }

    TEST_F(CombatDiseaseHooks, aCarriersOwnBlowIsNotScaledByThePlayersModifiers)
    {
        PlayerValueModifiers m;
        m.mSkillPercent[0] = -25.f;
        mCombat.setPlayerValueModifiers(m);
        const DamageReport* d = carrierHitsPlayer(5);
        ASSERT_NE(d, nullptr);
        PlayerValueModifiers none;
        mCombat.setPlayerValueModifiers(none);
        // the carrier swings with the test sword too (the test weapon overrides): the same number with and without
        const float scaled = d->amount;
        const DamageReport* e = carrierHitsPlayer(6);
        ASSERT_NE(e, nullptr);
        EXPECT_NEAR(scaled, e->amount, 1e-3f);
    }

    TEST_F(CombatDiseaseHooks, thePlayersSpellsAreReadable)
    {
        EXPECT_TRUE(mCombat.playerSpells().empty()); // before his profile
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mCombat.playerSpells(), (std::vector<FormId>{ kPlayerSpell }));
        mCombat.setPlayerExtraAbilities(
            { 0x888 }); // with no perk library the extras are not worked in: the list is his
        const std::vector<FormId> spells = mCombat.playerSpells();
        EXPECT_NE(std::find(spells.begin(), spells.end(), kPlayerSpell), spells.end());
    }

    // ---- RACE ATKD: the spells a race attacks carry
    // ---------------------------------------------------------------------

    struct RaceDecodeFixture
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
                    ContentTests::tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44).str(),
                    std::ios::binary),
                "test.esp", error);
        }();
        ExteriorLimits mLimits;
        DecodeNotes mNotes;
        FormIdResolver mResolver{ mPlugin->header(), "test.esp", mOrder };
        DecodeContext context() { return DecodeContext{ mResolver, mLimits, mNotes, false }; }
    };

    ContentTests::Bytes attack(float weight, std::uint32_t spell, std::size_t size = 44)
    {
        ContentTests::Bytes b;
        b.f32(1.f).f32(weight).u32(spell);
        for (std::size_t n = 12; n < size; n += 4)
            b.u32(0);
        return b;
    }

    TEST(SkyrimDiseasesRace, theAttackSpellsAreReadOnceEachInOrderAndOddEntriesAreCounted)
    {
        RaceDecodeFixture f;
        ContentTests::Bytes subs;
        subs.append(ContentTests::sub4("EDID", ContentTests::Bytes().zstring("WolfRace")));
        subs.append(ContentTests::sub4("ATKD", attack(0.75f, 0x000B8782))); // the disease
        subs.append(ContentTests::sub4("ATKD", attack(0.f, 0x000B8782))); // again: kept once
        subs.append(ContentTests::sub4("ATKD", attack(1.f, 0))); // no spell
        subs.append(ContentTests::sub4("ATKD", attack(1.f, 0x01000123))); // a spell of the test plugin
        subs.append(ContentTests::sub4("ATKD", attack(1.f, 0x000B8999, 40))); // a damaged entry
        const std::string bytes = subs.str();
        Content::RawRecord record;
        record.mType = Content::fourCC("RACE");
        record.mFormId = 0x01000B00;
        RaceCombatRecord race;
        std::string why;
        ASSERT_TRUE(decodeRaceCombat(
            { reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() }, record, f.context(), race, why))
            << why;
        EXPECT_EQ(race.mAttackSpells, (std::vector<FormId>{ 0x000B8782, 0x01000123 }));
    }

    TEST(SkyrimDiseasesRace, aRaceWithNoAttackDataCarriesNothing)
    {
        RaceDecodeFixture f;
        ContentTests::Bytes subs;
        subs.append(ContentTests::sub4("EDID", ContentTests::Bytes().zstring("NordRace")));
        const std::string bytes = subs.str();
        Content::RawRecord record;
        record.mType = Content::fourCC("RACE");
        RaceCombatRecord race;
        std::string why;
        ASSERT_TRUE(decodeRaceCombat(
            { reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() }, record, f.context(), race, why))
            << why;
        EXPECT_TRUE(race.mAttackSpells.empty());
    }
}
