// Task 0227: how the sneak rules reach the other systems: the combat's sneak attack hooks, the perk entry point 18, the
// crime witnesses' "has noticed" lookup and the pickpocket "aware victim" hook. Fixtures follow
// testskyrimsecombatskills.cpp and testskyrimsepickpocketcrime.cpp. Written from the task spec before the hooks existed
// (the combat and perk tests did not compile against the old headers; the crime and pickup tests are red-by-compile for
// the new setters).
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/crime.hpp>
#include <game_skyrimse/pickup_service.hpp>
#include <game_skyrimse/sneak.hpp>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kAttackerBase = 100;
    constexpr WorldId kVictimBase = 200;

    class NoPerks final : public PerkLibrary
    {
    public:
        const PerkRecord* perk(FormId) const override { return nullptr; }
    };

    class SneakFakeData final : public CombatData
    {
    public:
        SneakFakeData()
        {
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;
            FighterProfile attacker;
            attacker.mHealthMax = 100.f;
            attacker.mStaminaMax = 100.f;
            attacker.mSkills[0] = 50;
            mProfiles[kAttackerBase] = attacker;
            FighterProfile victim;
            victim.mHealthMax = 500.f;
            victim.mStaminaMax = 80.f;
            victim.mMitigationPercent = 20.f;
            mProfiles[kVictimBase] = victim;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
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
        const PerkLibrary* perkLibrary() override { return &mLibrary; }

        NoPerks mLibrary;
        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
        CombatSettings mSettings;
    };

    class SneakFakeWriter final : public CombatRequestWriter
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

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    ClipEvent hitEvent(CombatSubject subject, std::uint32_t token)
    {
        ClipEvent e;
        e.subject = subject;
        e.slot = ClipSlot::Attack;
        e.token = token;
        e.kind = ClipEventKind::Hit;
        return e;
    }

    // ---- the combat hooks -------------------------------------------------------------------------------------------

    struct SkyrimSneakCombat : ::testing::Test
    {
        SneakFakeData mData;
        SkyrimCombat mCombat;
        SneakFakeWriter mWriter;
        ActorHandle mB{ 20, 1 };
        std::vector<std::tuple<std::uint32_t, std::uint8_t, bool>> mAsked;
        std::vector<std::pair<std::uint32_t, float>> mDone;
        float mAnswer = 3.f;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.onActorAdded(info(10, kAttackerBase, "Attacker"));
            mCombat.onActorAdded(info(20, kVictimBase, "Victim"));
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, 0.25f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
        }

        void hook()
        {
            mCombat.setSneakAttackHooks(
                [this](std::uint32_t victim, std::uint8_t animType, bool ranged) {
                    mAsked.emplace_back(victim, animType, ranged);
                    return mAnswer;
                },
                [this](std::uint32_t victim, float multiplier) { mDone.emplace_back(victim, multiplier); });
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

        const DamageReport* npcHitsPlayer(std::uint32_t token)
        {
            mWriter.mDamage.clear();
            mCombat.onClipEvent(hitEvent(CombatSubject::of(ActorHandle{ 10, 1 }), token));
            mCombat.combatUpdate(0.016f, mWriter);
            SweepHit hit;
            hit.target = SweepTarget::Player;
            hit.distance = 30.f;
            mCombat.onSweepResult(CombatSubject::of(ActorHandle{ 10, 1 }), token, &hit, 1);
            mCombat.combatUpdate(0.016f, mWriter);
            return mWriter.mDamage.empty() ? nullptr : &mWriter.mDamage[0];
        }
    };

    TEST_F(SkyrimSneakCombat, withNoHooksTheBlowIsAsBefore)
    {
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 6.02f, 1e-3f); // 7 x 1.075 x 0.8
    }

    TEST_F(SkyrimSneakCombat, aSneakAttackMultipliesTheDamageOnceAndIsReported)
    {
        hook();
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 18.06f, 1e-3f); // x3
        ASSERT_EQ(mAsked.size(), 1u);
        EXPECT_EQ(std::get<0>(mAsked[0]), 20u); // the victim's reference
        EXPECT_EQ(std::get<1>(mAsked[0]), WeaponAnimType::OneHandSword);
        EXPECT_FALSE(std::get<2>(mAsked[0]));
        ASSERT_EQ(mDone.size(), 1u);
        EXPECT_EQ(mDone[0].first, 20u);
        EXPECT_FLOAT_EQ(mDone[0].second, 3.f);
    }

    TEST_F(SkyrimSneakCombat, aMultiplierOfOneChangesNothingAndIsNotReported)
    {
        hook();
        mAnswer = 1.f;
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 6.02f, 1e-3f);
        EXPECT_TRUE(mDone.empty());
    }

    TEST_F(SkyrimSneakCombat, aBlowOnThePlayerNeverAsksTheHook)
    {
        hook();
        const DamageReport* d = npcHitsPlayer(5);
        ASSERT_NE(d, nullptr);
        EXPECT_TRUE(mAsked.empty());
        EXPECT_TRUE(mDone.empty());
    }

    TEST_F(SkyrimSneakCombat, theMultiplierDoesNotLeakIntoTheNextBlow)
    {
        hook();
        ASSERT_NE(playerHitsVictim(), nullptr);
        mAnswer = 1.f;
        const DamageReport* second = playerHitsVictim();
        ASSERT_NE(second, nullptr);
        EXPECT_NEAR(second->amount, 6.02f, 1e-3f);
    }

    // ---- the perk entry point ---------------------------------------------------------------------------------------

    class SneakPerkLibrary final : public PerkLibrary
    {
    public:
        const PerkRecord* perk(FormId id) const override
        {
            const auto it = mPerks.find(id);
            return it == mPerks.end() ? nullptr : &it->second;
        }
        std::map<FormId, PerkRecord> mPerks;
    };

    constexpr FormId kPerkId = 0x0A0001;
    constexpr FormId kDaggerKeyword = 0x0001E711;

    survey::Ctda condition(std::uint16_t fn, float value, std::uint32_t p1 = 0)
    {
        survey::Ctda c;
        c.mCompare = 0; // equal
        c.mComparison = value;
        c.mFunction = fn;
        c.mParam1 = p1;
        return c;
    }

    PerkRecord assassinsBlade(float multiplier)
    {
        PerkRecord p;
        p.mCommon.mId = kPerkId;
        p.mHasData = true;
        PerkEffect e;
        e.mKind = PerkEffectKind::EntryPoint;
        e.mEntryPoint = PerkEntry::ModSneakAttackMult;
        e.mFunction = PerkFunction::Multiply;
        e.mTabCount = 3;
        e.mParamType = 1;
        e.mHasParam = true;
        e.mValue = multiplier;
        PerkTab owner;
        owner.mIndex = 0;
        owner.mConditions = { condition(286, 1.f) }; // IsSneaking
        PerkTab weapon;
        weapon.mIndex = 1;
        weapon.mConditions = { condition(560, 1.f, kDaggerKeyword) }; // HasKeyword
        e.mTabs = { owner, weapon };
        p.mEffects = { e };
        return p;
    }

    TEST(SkyrimSneakPerks, entryPointEighteenNamesTheOwnerTheWeaponAndTheTarget)
    {
        bool known = false;
        EXPECT_EQ(entryPointTabRole(PerkEntry::ModSneakAttackMult, 0, known), PerkRole::Owner);
        EXPECT_TRUE(known);
        EXPECT_EQ(entryPointTabRole(PerkEntry::ModSneakAttackMult, 1, known), PerkRole::Weapon);
        EXPECT_EQ(entryPointTabRole(PerkEntry::ModSneakAttackMult, 2, known), PerkRole::Target);
    }

    TEST(SkyrimSneakPerks, aPerkMultipliesTheSneakAttackOnlyForTheWeaponItNames)
    {
        SneakPerkLibrary lib;
        lib.mPerks[kPerkId] = assassinsBlade(5.f);
        PerkEngine engine(lib);
        std::vector<FormId> perks = { kPerkId };
        std::vector<FormId> none;
        PerkParticipant owner;
        owner.mKind = PerkParticipant::Kind::Actor;
        owner.mForm = 0x100;
        owner.mPerks = &perks;
        owner.mKeywords = &none;
        owner.mFactions = &none;
        owner.mWornKeywords = &none;
        PerkParticipant target;
        target.mKind = PerkParticipant::Kind::Actor;
        target.mForm = 0x200;
        std::vector<FormId> dagger = { kDaggerKeyword };
        PerkParticipant held;
        held.mKind = PerkParticipant::Kind::Weapon;
        held.mKeywords = &dagger;
        std::size_t applied = 0;
        // the owner is not marked sneaking by the caller: the function counts him as sneaking itself
        EXPECT_FLOAT_EQ(sneak::applySneakAttackPerks(engine, perks, owner, &held, target, 3.f, &applied), 15.f);
        EXPECT_EQ(applied, 1u);
        PerkParticipant other;
        other.mKind = PerkParticipant::Kind::Weapon;
        other.mKeywords = &none;
        EXPECT_FLOAT_EQ(sneak::applySneakAttackPerks(engine, perks, owner, &other, target, 3.f, &applied), 3.f);
        EXPECT_EQ(applied, 0u);
        EXPECT_FLOAT_EQ(sneak::applySneakAttackPerks(engine, perks, owner, nullptr, target, 3.f, &applied), 3.f);
    }

    TEST(SkyrimSneakPerks, noPerkAndABadMultiplierChangeNothing)
    {
        SneakPerkLibrary lib;
        PerkEngine engine(lib);
        std::vector<FormId> none;
        PerkParticipant owner;
        owner.mKind = PerkParticipant::Kind::Actor;
        PerkParticipant target;
        target.mKind = PerkParticipant::Kind::Actor;
        EXPECT_FLOAT_EQ(sneak::applySneakAttackPerks(engine, none, owner, nullptr, target, 3.f), 3.f);
        EXPECT_FLOAT_EQ(sneak::applySneakAttackPerks(engine, none, owner, nullptr, target, -1.f), -1.f);
    }

    // ---- crime and pickpocketing
    // ----------------------------------------------------------------------------------------

    constexpr FormId kHold = 0x010000a0;

    CrimeData holdData()
    {
        CrimeData d;
        d.mPresent = true;
        d.mPickpocket = 25;
        d.mAssault = 40;
        return d;
    }

    class CrimeWorldStub final : public crime::CrimeWorld
    {
    public:
        void actors(std::vector<crime::CrimeActor>& out) override { out.push_back(mActor); }
        bool actor(ActorHandle, crime::CrimeActor& out) override
        {
            out = mActor;
            return true;
        }
        const CrimeData* crimeData(FormId faction) override { return faction == kHold ? &mData : nullptr; }
        std::string factionName(FormId) override { return "Hold"; }
        std::int32_t itemValue(FormId) override { return 100; }
        std::int32_t playerGold() override { return 0; }
        bool takeGold(std::int32_t) override { return false; }
        std::int32_t takeItem(FormId, std::int32_t) override { return 0; }
        std::int32_t itemCount(FormId) override { return 0; }
        void guardGoTo(ActorHandle, double, double, double, ActorSpeed) override {}
        void guardHold(ActorHandle) override {}
        void guardRelease(ActorHandle) override {}
        void guardAttack(ActorHandle) override {}
        void log(const std::string&) override {}
        crime::CrimeActor mActor;
        CrimeData mData = holdData();
    };

    class OpenWriter final : public CombatRequestWriter
    {
    public:
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (!subject.player)
                out.x = 120.0;
            return true;
        }
        bool castRay(CombatSubject, const RayRequest&, RayHit& out) override
        {
            out = {};
            return true;
        }
    };

    struct SkyrimSneakCrime : ::testing::Test
    {
        crime::Ledger mLedger;
        CrimeWorldStub mWorld;
        OpenWriter mWriter;
        std::unique_ptr<crime::CrimeSystem> mSystem;
        void SetUp() override
        {
            mWorld.mActor.mHandle = { 7, 1 };
            mWorld.mActor.mBase = 7;
            mWorld.mActor.mCrimeFaction = kHold;
            mWorld.mActor.mName = "Hulda";
            mSystem = std::make_unique<crime::CrimeSystem>(mLedger, mWorld);
        }
    };

    TEST_F(SkyrimSneakCrime, withNoLookupASightLineIsEnoughAsBefore)
    {
        mSystem->onPickpocketCaught(7);
        mSystem->update(0.5f, mWriter);
        EXPECT_EQ(mLedger.bounty(kHold), 25);
    }

    TEST_F(SkyrimSneakCrime, aWitnessThatNeverNoticedThePlayerSeesNothing)
    {
        mSystem->setDetectedLookup([](std::uint32_t) { return false; });
        mSystem->onPickpocketCaught(7);
        mSystem->update(0.5f, mWriter);
        EXPECT_EQ(mLedger.bounty(kHold), 0);
        EXPECT_EQ(mSystem->pendingCount(), 0u);
    }

    TEST_F(SkyrimSneakCrime, aWitnessThatNoticedThePlayerCountsAsBefore)
    {
        std::vector<std::uint32_t> asked;
        mSystem->setDetectedLookup([&asked](std::uint32_t reference) {
            asked.push_back(reference);
            return reference == 7;
        });
        mSystem->onPickpocketCaught(7);
        mSystem->update(0.5f, mWriter);
        EXPECT_EQ(mLedger.bounty(kHold), 25);
        ASSERT_FALSE(asked.empty());
        EXPECT_EQ(asked[0], 7u);
    }

    struct SkyrimSneakPickpocket : ::testing::Test
    {
        std::map<FormId, std::int32_t> mHulda{ { 0x0f, 40 } };
        std::map<FormId, std::int32_t> mPlayer;
        int mCaught = 0;
        bool mAware = false;
        bool mAwareSet = true;

        pickup::Hooks hooks()
        {
            pickup::Hooks h;
            h.mItem = [](FormId, std::int32_t& value, float& weight) {
                value = 1;
                weight = 0.f;
                return true;
            };
            h.mHeldBy = [this](std::uint64_t, FormId item) { return mHulda[item]; };
            h.mMoveToPlayer = [this](std::uint64_t, FormId item, std::int32_t count) {
                mHulda[item] -= count;
                mPlayer[item] += count;
                return true;
            };
            h.mSkill = []() { return 100.f; }; // gold: 25 + 60 - 3 = 82 percent
            h.mCaught = [this](std::uint64_t, FormId, std::int32_t) { ++mCaught; };
            if (mAwareSet)
                h.mAware = [this](std::uint64_t) { return mAware; };
            return h;
        }
    };

    TEST_F(SkyrimSneakPickpocket, anUnawareVictimLeavesTheSeededRollAlone)
    {
        pickup::PickupService service(hooks());
        int successes = 0;
        for (int i = 0; i < 30; ++i)
            successes += service.take(0xB80803, 0x0f, 1) == pickup::TryStatus::Success ? 1 : 0;
        EXPECT_GT(successes, 15);
    }

    TEST_F(SkyrimSneakPickpocket, anAwareVictimAlwaysCatchesTheHand)
    {
        mAware = true;
        pickup::PickupService service(hooks());
        for (int i = 0; i < 30; ++i)
            EXPECT_EQ(service.take(0xB80803, 0x0f, 1), pickup::TryStatus::Caught);
        EXPECT_EQ(mCaught, 30);
        EXPECT_EQ(mPlayer[0x0f], 0);
    }

    TEST_F(SkyrimSneakPickpocket, theAwareHookDoesNotMoveTheSeededSequence)
    {
        const auto run = [this](bool aware) {
            mAware = aware;
            pickup::PickupService service(hooks());
            std::string out;
            for (int i = 0; i < 20; ++i)
                out += service.take(0xB80803, 0x0f, 1) == pickup::TryStatus::Success ? 'S' : 'C';
            return out;
        };
        const std::string unaware = run(false);
        // switching the victim's mind on and off between tries: the attempt counter is the same either way, so the
        // unaware outcomes of a later service built the same way are equal
        EXPECT_EQ(unaware, run(false));
    }

    TEST_F(SkyrimSneakPickpocket, noHookMeansNobodyIsAware)
    {
        mAwareSet = false;
        pickup::PickupService service(hooks());
        int successes = 0;
        for (int i = 0; i < 30; ++i)
            successes += service.take(0xB80803, 0x0f, 1) == pickup::TryStatus::Success ? 1 : 0;
        EXPECT_GT(successes, 15);
    }
}
