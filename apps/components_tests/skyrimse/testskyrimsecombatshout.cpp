// Task 0205 (milestone M-S7): the player's shout through the combat contract 0.6 (game_skyrimse/combat.hpp,
// combat_shout.cpp), with a fake data source and a recording writer. Numbers by hand:
//  - the shout "UnrelentingForceShout" has three words with spells UF1, UF2, UF3 (cost 2 magicka each in the data,
// which
//    a shout never pays), recovery times 15, 20 and 45 seconds; each spell is aimed fire and forget with a Stagger
//    effect (which carries the projectile, no condition) and a damage effect of 2, 5 and 10 on health;
//  - a hold reaches word n at (n - 1) x 0.9 s (a release before 0.2 s is a cancel); the last unlocked word fires by
//    itself;
//  - the player's clip timing is MRh_Release's 0.167 s; the cooldown starts when the spell goes.
// The victim has 50 health. HOW the hold maps to words is OURS and pinned here.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kVictimBase = 200;
    constexpr FormId kShout = 0x13E07, kFus = 0x13E08, kRo = 0x13E09, kDah = 0x13E0A;
    constexpr FormId kUf1 = 0x1001, kUf2 = 0x1002, kUf3 = 0x1003;
    constexpr FormId kStagger = 0x2001, kDamage1 = 0x2002, kDamage2 = 0x2003, kDamage3 = 0x2004, kProj = 0x3000;
    constexpr float kPi = 3.14159265358979f;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mProj.mCommon.mId = kProj;
            mProj.mHasData = true;
            mProj.mType = 1;
            mProj.mSpeed = 3000.f;
            mProj.mRange = 6000.f;
            mProj.mModel = "Magic\\Voice\\UnrelentingForce.nif";
            MagicEffectRecord st;
            st.mCommon.mId = kStagger;
            st.mHasData = true;
            st.mArchetype = MagicArchetype::Stagger;
            st.mPrimaryValue = -1;
            st.mFlags = MagicFlag::Hostile;
            st.mProjectile = kProj;
            mEffects[kStagger] = st;
            for (const auto& [id, value] : { std::pair<FormId, int>{ kDamage1, 2 }, { kDamage2, 5 }, { kDamage3, 10 } })
            {
                MagicEffectRecord d;
                d.mCommon.mId = id;
                d.mHasData = true;
                d.mArchetype = 0;
                d.mPrimaryValue = 24;
                d.mFlags = MagicFlag::Hostile | MagicFlag::Detrimental;
                mEffects[id] = d;
                (void)value;
            }
            addSpell(kUf1, "VoiceUnrelentingForce1", { { kStagger, 0.75f }, { kDamage1, 2.f } });
            addSpell(kUf2, "VoiceUnrelentingForce2", { { kStagger, 1.25f }, { kDamage2, 5.f } });
            addSpell(kUf3, "VoiceUnrelentingForce3", { { kStagger, 1.5f }, { kDamage3, 10.f } });
            mShout.mCommon.mId = kShout;
            mShout.mCommon.mEditorId = "UnrelentingForceShout";
            mShout.mHasData = true;
            mShout.mWordCount = 3;
            mShout.mWords[0] = { kFus, kUf1, 15.f };
            mShout.mWords[1] = { kRo, kUf2, 20.f };
            mShout.mWords[2] = { kDah, kUf3, 45.f };
            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            victim.mMagickaMax = 100.f;
            mProfiles[kVictimBase] = victim;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 1.f; // too little for any spell of cost 2
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
        const WeaponRecord* weaponByEditorId(std::string_view) override { return nullptr; }
        CombatSettings settings() override { return CombatSettings(); }
        const SpellRecord* spellById(FormId id) override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const SpellRecord* spellByEditorId(std::string_view) override { return nullptr; }
        const MagicEffectRecord* effectById(FormId id) override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        const ProjectileRecord* projectileById(FormId id) override { return id == kProj ? &mProj : nullptr; }
        const ShoutRecord* shoutById(FormId id) override { return id == kShout ? &mShout : nullptr; }
        const ShoutRecord* shoutByEditorId(std::string_view name) override
        {
            return name == "unrelentingforceshout" || name == "UnrelentingForceShout" ? &mShout : nullptr;
        }

        void addSpell(FormId id, const char* name, std::vector<std::pair<FormId, float>> effects)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mType = 11; // voice power
            s.mCost = 2;
            s.mCastType = MagicCast::FireAndForget;
            s.mDelivery = MagicDelivery::Aimed;
            for (const auto& [effect, magnitude] : effects)
            {
                SpellEffect se;
                se.mEffect = effect;
                se.mMagnitude = magnitude;
                s.mEffects.push_back(se);
            }
            mSpells[id] = s;
        }

        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        ProjectileRecord mProj;
        ShoutRecord mShout;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        struct Clip
        {
            CombatSubject mSubject;
            ClipSlot mSlot;
            std::string mName;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.slot, std::string(request.clip) });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool clipEventTime(std::string_view clip, bool, ClipEventKind kind, float& out) override
        {
            out = 0.f;
            if (kind != ClipEventKind::SpellFire || clip != "MRh_Release")
                return false;
            out = 0.167f;
            return true;
        }
        bool launchProjectile(CombatSubject owner, const ProjectileRequest& request) override
        {
            mLaunches.push_back({ owner, request.token });
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
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
        struct Launch
        {
            CombatSubject mOwner;
            std::uint32_t mToken;
        };
        std::vector<Clip> mClips;
        std::vector<Launch> mLaunches;
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

    struct CombatShout : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        shouts::ShoutState mState;
        ActorHandle mB{ 20, 1 };
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
        }
        void start(std::size_t unlockedWords = 3, bool equip = true)
        {
            mCombat.setData(&mData);
            mCombat.setShoutState(&mState);
            mCombat.onActorAdded(info(20, kVictimBase, "Brenuin"));
            SubjectState victim;
            victim.heading = kPi / 2.f;
            victim.x = 800.0;
            mWriter.mStates[20] = victim;
            mWriter.mPlayerState = victim;
            mWriter.mPlayerState.x = 0.0;
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
            const FormId words[3] = { kFus, kRo, kDah };
            for (std::size_t i = 0; i < unlockedWords; ++i)
                mState.forceUnlock(words[i]);
            if (equip)
                mState.equip(kShout);
        }
        void update(float seconds = 0.1f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n, float seconds = 0.1f)
        {
            for (int i = 0; i < n; ++i)
                update(seconds);
        }
        bool traceHas(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
        SkyrimCombat::FighterView viewOf(CombatSubject subject)
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(subject, v));
            return v;
        }
        // Presses, holds for `n` updates of 0.1 s (so n x 0.1 s), releases, and lets the spell go (1.0 s).
        void shout(int n)
        {
            mCombat.onPlayerShoutEdge(true);
            updates(n);
            mCombat.onPlayerShoutEdge(false);
            updates(10);
        }
    };

    // ---- the contract ----------------------------------------------------------------------------------------------

    TEST(CombatShoutContract, theContractIsZeroPointSixAndTheShoutEdgeIsIgnoredByDefault)
    {
        EXPECT_EQ(kCombatRulesVersion.major, 0);
        EXPECT_EQ(kCombatRulesVersion.minor, 6);
        struct Plain final : CombatRules
        {
            void onClipEvent(const ClipEvent&) override {}
            void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
            void combatUpdate(float, CombatRequestWriter&) override {}
        } plain;
        plain.onPlayerShoutEdge(true); // does nothing, does not throw
        SUCCEED();
    }

    TEST(CombatShoutContract, theCellSourceForwardsTheShoutEdgeToTheCombatRules)
    {
        FakeData data;
        shouts::ShoutState state;
        state.forceUnlock(kFus);
        state.equip(kShout);
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        source.combat().setData(&data);
        source.combat().setShoutState(&state);
        WorldCellSource* asBase = &source;
        auto* rules = dynamic_cast<CombatRules*>(asBase);
        ASSERT_NE(rules, nullptr);
        FakeWriter writer;
        SubjectState player;
        player.heading = kPi / 2.f;
        writer.mPlayerState = player;
        rules->combatUpdate(0.1f, writer);
        rules->onPlayerShoutEdge(true);
        for (int i = 0; i < 4; ++i)
            rules->combatUpdate(0.1f, writer);
        rules->onPlayerShoutEdge(false);
        for (int i = 0; i < 10; ++i)
            rules->combatUpdate(0.1f, writer);
        EXPECT_EQ(writer.mLaunches.size(), 1u); // the edges arrived
    }

    // ---- refusals --------------------------------------------------------------------------------------------------

    TEST_F(CombatShout, withNoShoutStateThePressDoesNothing)
    {
        mCombat.setData(&mData);
        mCombat.onActorAdded(info(20, kVictimBase, "Brenuin"));
        mWriter.mPlayerState.healthMax = 100.f;
        mCombat.onPlayerShoutEdge(true);
        updates(5);
        mCombat.onPlayerShoutEdge(false);
        updates(10);
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_EQ(mCombat.shoutsSpoken(), 0u);
    }

    TEST_F(CombatShout, withNoShoutEquippedNothingHappensAndTheTraceSaysWhy)
    {
        start(3, false);
        shout(4);
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_TRUE(traceHas("no shout equipped"));
        EXPECT_TRUE(mState.ready());
    }

    TEST_F(CombatShout, withNoWordUnlockedNothingHappensAndTheTraceSaysWhy)
    {
        start(0);
        shout(4);
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_TRUE(traceHas("no word unlocked"));
        EXPECT_TRUE(mState.ready());
    }

    TEST_F(CombatShout, aReleaseBeforeTheMinimumHoldIsACancelWithNoCooldown)
    {
        start();
        shout(1); // 0.1 s held
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_EQ(mCombat.shoutsSpoken(), 0u);
        EXPECT_TRUE(mState.ready());
    }

    // ---- the words and the cooldown --------------------------------------------------------------------------------

    TEST_F(CombatShout, aShortHoldSpeaksTheFirstWordAndStartsItsFifteenSecondCooldown)
    {
        start();
        shout(4); // 0.4 s: word 1
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_TRUE(mWriter.mLaunches[0].mOwner.player);
        EXPECT_EQ(mCombat.shoutsSpoken(), 1u);
        EXPECT_FALSE(mState.ready());
        EXPECT_NEAR(mState.cooldownTotal(), 15.f, 0.001f);
        // it started when the spell went, about 0.6 s before the end of shout()
        EXPECT_NEAR(mState.cooldown(), 15.f - 0.8f, 0.45f);
        EXPECT_TRUE(traceHas("shout UnrelentingForceShout word 1"));
    }

    TEST_F(CombatShout, aLongerHoldSpeaksTheSecondAndThirdWordsWithTheirOwnRecovery)
    {
        start();
        shout(12); // 1.2 s: word 2 (0.9)
        EXPECT_NEAR(mState.cooldownTotal(), 20.f, 0.001f);
        EXPECT_TRUE(traceHas("word 2"));
        updates(200); // 20 s: ready again
        EXPECT_TRUE(mState.ready());
        shout(25); // 2.5 s: word 3 (1.8)
        EXPECT_NEAR(mState.cooldownTotal(), 45.f, 0.001f);
        EXPECT_TRUE(traceHas("word 3"));
        EXPECT_EQ(mCombat.shoutsSpoken(), 2u);
    }

    TEST_F(CombatShout, theWordsAreCappedByWhatIsUnlockedAndTheLastOneFiresWhileStillHeld)
    {
        start(2);
        mCombat.onPlayerShoutEdge(true);
        updates(15); // 1.5 s held: word 2 (the last unlocked) was reached at 0.9 s
        EXPECT_EQ(mWriter.mLaunches.size(), 1u) << "the cast went before the release";
        EXPECT_NEAR(mState.cooldownTotal(), 20.f, 0.001f);
        mCombat.onPlayerShoutEdge(false);
        updates(10);
        EXPECT_EQ(mWriter.mLaunches.size(), 1u) << "the release did not shout again";
        EXPECT_EQ(mCombat.shoutsSpoken(), 1u);
    }

    TEST_F(CombatShout, aPressWhileTheCooldownRunsIsRefused)
    {
        start();
        shout(4);
        ASSERT_EQ(mCombat.shoutsSpoken(), 1u);
        shout(4);
        EXPECT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_EQ(mCombat.shoutsSpoken(), 1u);
        EXPECT_TRUE(traceHas("cooldown"));
    }

    TEST_F(CombatShout, whenTheCooldownHasRunOutAShoutWorksAgain)
    {
        start();
        shout(4);
        updates(150); // 15 s
        EXPECT_TRUE(mState.ready());
        shout(4);
        EXPECT_EQ(mWriter.mLaunches.size(), 2u);
        EXPECT_EQ(mCombat.shoutsSpoken(), 2u);
    }

    TEST_F(CombatShout, aShoutCostsNoMagickaAndTheStandInHasNoneToSpare)
    {
        start();
        shout(4); // the spell's cost is 2 and the player's magicka is 1
        EXPECT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_FLOAT_EQ(viewOf(CombatSubject::ofPlayer()).mMagicka, 1.f);
    }

    // ---- Unrelenting Force -----------------------------------------------------------------------------------------

    TEST_F(CombatShout, unrelentingForceOneStaggersAndHurtsWhatItHits)
    {
        start();
        shout(4);
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        ProjectileResult r;
        r.owner = CombatSubject::ofPlayer();
        r.token = mWriter.mLaunches[0].mToken;
        r.fate = ProjectileFate::HitActor;
        r.actor = mB;
        mCombat.onProjectileResult(r);
        update();
        EXPECT_FLOAT_EQ(viewOf(CombatSubject::of(mB)).mHealth, 48.f); // 50 - 2
        EXPECT_TRUE(viewOf(CombatSubject::of(mB)).mStaggered);
        ASSERT_FALSE(mWriter.mClips.empty());
        EXPECT_EQ(mWriter.mClips.back().mSlot, ClipSlot::Stagger);
        EXPECT_TRUE(mWriter.mClips.back().mSubject == CombatSubject::of(mB));
    }

    TEST_F(CombatShout, unrelentingForceTwoHurtsMoreAndAMissHurtsNobody)
    {
        start();
        shout(12);
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        ProjectileResult miss;
        miss.owner = CombatSubject::ofPlayer();
        miss.token = mWriter.mLaunches[0].mToken;
        miss.fate = ProjectileFate::HitWorld;
        mCombat.onProjectileResult(miss);
        update();
        SkyrimCombat::FighterView untouched;
        if (mCombat.fighterView(CombatSubject::of(mB), untouched)) // a fighter nothing has touched may not exist yet
            EXPECT_FLOAT_EQ(untouched.mHealth, 50.f);
        EXPECT_TRUE(mWriter.mDamage.empty());
        updates(200);
        shout(12);
        ASSERT_EQ(mWriter.mLaunches.size(), 2u);
        ProjectileResult hit;
        hit.owner = CombatSubject::ofPlayer();
        hit.token = mWriter.mLaunches[1].mToken;
        hit.fate = ProjectileFate::HitActor;
        hit.actor = mB;
        mCombat.onProjectileResult(hit);
        update();
        EXPECT_FLOAT_EQ(viewOf(CombatSubject::of(mB)).mHealth, 45.f); // 50 - 5
    }

    TEST_F(CombatShout, aShoutIsNotATrainedSpellOrARegenerationDelay)
    {
        start();
        mData.mPlayer.mMagickaMax = 1.f;
        shout(4);
        EXPECT_EQ(mCombat.spellsCast(), 0u); // the magic counter is for spells; a shout has its own
        EXPECT_EQ(mCombat.shoutsSpoken(), 1u);
    }

    // ---- the test settings -----------------------------------------------------------------------------------------

    TEST_F(CombatShout, theTestShoutEquipsTheNamedShoutWhenTheDataAreThere)
    {
        start(3, false);
        EXPECT_TRUE(mCombat.setTestPlayerShout("UnrelentingForceShout"));
        update();
        EXPECT_EQ(mState.equipped(), kShout);
        EXPECT_FALSE(mCombat.setTestPlayerShout(std::string(300, 'x')));
    }

    TEST_F(CombatShout, theAutomaticPressShoutsEveryPeriodAndHonoursTheCooldown)
    {
        start();
        mCombat.setTestShoutEvery(20.f);
        mCombat.setTestShoutHold(0.3f);
        updates(450); // 45 s: presses at about 20 s and 40 s
        EXPECT_EQ(mCombat.shoutsSpoken(), 2u);
        EXPECT_EQ(mWriter.mLaunches.size(), 2u);
    }

    TEST_F(CombatShout, theAutomaticPressIsOffUnlessAPeriodIsGiven)
    {
        start();
        mCombat.setTestShoutEvery(0.f);
        updates(300);
        EXPECT_EQ(mCombat.shoutsSpoken(), 0u);
        mCombat.setTestShoutEvery(std::nanf(""));
        updates(300);
        EXPECT_EQ(mCombat.shoutsSpoken(), 0u);
    }

    // ---- a dead or absent caster ------------------------------------------------------------------------------------

    TEST_F(CombatShout, theCooldownCountsOnWithNoShoutInProgress)
    {
        start();
        mState.startCooldown(5.f);
        updates(30); // 3 s
        EXPECT_NEAR(mState.cooldown(), 2.f, 0.15f);
        updates(30);
        EXPECT_TRUE(mState.ready());
    }
}
