// Task 0279 (werewolf, second slice): the clips the beast form plays besides its claws, the Howl shout and the power
// attack. Numbers by hand (the survey is docs/tasks/0279-skyrim-werewolf-vampire-2.md):
//  - transformation in: the file ww_howl (5.0 s; its generator `WW Howl.hkx01` carries the transformation sounds);
//  - transformation out: ww_bleedoutstart (2.333 s) whose `TransformToHuman` event is at 1.799 s, the moment the quest
//    script changes the race back;
//  - feeding: ww_specialfeeding; the Howl shout: ww_howlforward, `Voice_SpellFire_Event` at 0.733 s;
//  - the default Howl is HowlWerewolfFear 0xCF790 with the words 0xCF78E, 0xCF794 and 0xCF795 (the VMAD of the quest
//    PlayerWerewolfQuest);
//  - the real power attack is ww_attackright (generator `AttackRightPower_State`, 1.26667 s).
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/werewolf.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::werewolf;
    using namespace openeden::sdk::game;

    constexpr FormId kWord1Test = 0xCF78E;

    // ---- the constants ----------------------------------------------------------------------------------------------

    TEST(SkyrimBeastClips, theClipNamesAndTimesAreTheOnesTheDataGives)
    {
        EXPECT_EQ(kTransformInClip, "ww_howl");
        EXPECT_EQ(kTransformOutClip, "ww_bleedoutstart");
        EXPECT_NEAR(kTransformToHumanSeconds, 1.799f, 1e-4f);
        EXPECT_EQ(kFeedClip, "ww_specialfeeding");
        EXPECT_EQ(kHowlClip, "ww_howlforward");
        EXPECT_NEAR(kHowlFireSeconds, 0.733f, 1e-4f);
        EXPECT_EQ(kDefaultHowl, 0xCF790u);
        EXPECT_EQ(kDefaultHowlWords[0], 0xCF78Eu);
        EXPECT_EQ(kDefaultHowlWords[1], 0xCF794u);
        EXPECT_EQ(kDefaultHowlWords[2], 0xCF795u);
    }

    TEST(SkyrimBeastClips, thePlayerBeastRowUsesTheRealPowerAttackAndTheOldRowIsUntouched)
    {
        const CreatureProfile& beast = playerBeastProfile();
        EXPECT_EQ(clipFolder(beast.mPower), kCreatureFolder);
        EXPECT_EQ(bareClip(beast.mPower), "ww_attackright");
        EXPECT_EQ(bareClip(beast.mAttack), "ww_attackrightfast");
        EXPECT_EQ(beast.mWalk, playerProfile().mWalk);
        EXPECT_EQ(beast.mRunStride, playerProfile().mRunStride);
        EXPECT_EQ(bareClip(playerProfile().mPower), "ww_attackright"); // task 0316: both rows now use the single clip
    }

    // ---- the way back (TransformToHuman) ----------------------------------------------------------------------------

    TEST(SkyrimBeastEnding, isOffUntilBegunAndSaysDoneOnceAtTheEvent)
    {
        BeastEnding ending;
        EXPECT_FALSE(ending.active());
        EXPECT_FALSE(ending.tick(5.f));
        ending.begin();
        EXPECT_TRUE(ending.active());
        EXPECT_FALSE(ending.tick(1.0f));
        EXPECT_FALSE(ending.tick(0.7f)); // 1.7 s: not yet
        EXPECT_TRUE(ending.tick(0.2f)); // 1.9 s: the event at 1.799 s has passed
        EXPECT_FALSE(ending.active());
        EXPECT_FALSE(ending.tick(1.f)) << "it says done once";
    }

    TEST(SkyrimBeastEnding, aBadFrameLengthCountsAsNothingAndCancelStopsIt)
    {
        BeastEnding ending;
        ending.begin();
        EXPECT_FALSE(ending.tick(-1.f));
        EXPECT_FALSE(ending.tick(std::numeric_limits<float>::quiet_NaN()));
        EXPECT_TRUE(ending.active());
        ending.cancel();
        EXPECT_FALSE(ending.active());
        EXPECT_FALSE(ending.tick(10.f));
    }

    // ---- the clips the body plays besides the swing -----------------------------------------------------------------

    TEST(SkyrimBodyClips, aClipIsPublishedAFewFramesAfterItWasQueued)
    {
        BodyClips clips;
        EXPECT_EQ(clips.count(), 0u);
        clips.queue(std::string(kTransformInClip));
        for (int i = 0; i < BodyClips::kDelayFrames - 1; ++i)
        {
            clips.tickFrame();
            EXPECT_EQ(clips.count(), 0u) << "the body must be rebuilt before the clip is announced (frame " << i << ")";
        }
        clips.tickFrame();
        EXPECT_EQ(clips.count(), 1u);
        EXPECT_EQ(clips.clip(), "ww_howl");
    }

    TEST(SkyrimBodyClips, severalQueuedClipsGoOneAfterTheOtherAndTheQueueIsBounded)
    {
        BodyClips clips;
        clips.queue("a");
        clips.queue("b");
        for (int i = 0; i < BodyClips::kDelayFrames; ++i)
            clips.tickFrame();
        EXPECT_EQ(clips.count(), 1u);
        EXPECT_EQ(clips.clip(), "a");
        clips.tickFrame();
        EXPECT_EQ(clips.count(), 2u);
        EXPECT_EQ(clips.clip(), "b");
        for (int i = 0; i < 100; ++i)
            clips.queue("x");
        for (int i = 0; i < 1000; ++i)
            clips.tickFrame();
        EXPECT_LE(clips.count(), 2u + static_cast<std::uint32_t>(BodyClips::kMaxQueued));
    }

    TEST(SkyrimBodyClips, anEmptyNameIsNotQueued)
    {
        BodyClips clips;
        clips.queue("");
        for (int i = 0; i < 20; ++i)
            clips.tickFrame();
        EXPECT_EQ(clips.count(), 0u);
    }

    TEST(SkyrimBodyClipMux, theTokenGrowsWithEitherSourceAndTheClipIsTheNewestOne)
    {
        BodyClipMux mux;
        auto out = mux.compose(0, "", 0, "");
        EXPECT_EQ(out.token, 0u);
        EXPECT_TRUE(out.clip.empty());
        out = mux.compose(1, "ww_attackrightfast", 0, "");
        EXPECT_EQ(out.token, 1u);
        EXPECT_EQ(out.clip, "ww_attackrightfast");
        out = mux.compose(1, "ww_attackrightfast", 1, "ww_howl");
        EXPECT_EQ(out.token, 2u);
        EXPECT_EQ(out.clip, "ww_howl");
        out = mux.compose(2, "ww_attackright", 1, "ww_howl");
        EXPECT_EQ(out.token, 3u);
        EXPECT_EQ(out.clip, "ww_attackright");
        out = mux.compose(2, "ww_attackright", 1, "ww_howl");
        EXPECT_EQ(out.token, 3u) << "nothing new: the token holds";
        EXPECT_EQ(out.clip, "ww_attackright");
    }

    TEST(SkyrimBodyClipMux, bothNewInOnePollGiveTheSpecialClip)
    {
        BodyClipMux mux;
        (void)mux.compose(0, "", 0, "");
        const auto out = mux.compose(1, "swing", 1, "special");
        EXPECT_EQ(out.token, 2u);
        EXPECT_EQ(out.clip, "special");
    }

    TEST(SkyrimShoutForget, aWordTheFormGaveCanBeTakenAwayAgain)
    {
        shouts::ShoutState state;
        state.forceUnlock(kWord1Test);
        ASSERT_TRUE(state.unlocked(kWord1Test));
        EXPECT_TRUE(state.forget(kWord1Test));
        EXPECT_FALSE(state.learned(kWord1Test));
        EXPECT_FALSE(state.unlocked(kWord1Test));
        EXPECT_FALSE(state.forget(kWord1Test)) << "nothing left to take";
    }

    // ---- the combat half --------------------------------------------------------------------------------------------

    constexpr FormId kHowlShout = 0xCF790, kWord1 = 0xCF78E, kWord2 = 0xCF794, kWord3 = 0xCF795;
    constexpr FormId kSpell1 = 0xCF78D, kSpell2 = 0xCF793, kSpell3 = 0xCF796;
    constexpr FormId kFearEffect = 0x2001;
    constexpr FormId kPowerSpell = 0x92C48;
    constexpr WorldId kVictimBase = 200;

    class HowlData final : public CombatData
    {
    public:
        HowlData()
        {
            MagicEffectRecord fear;
            fear.mCommon.mId = kFearEffect;
            fear.mHasData = true;
            fear.mArchetype = 7;
            fear.mFlags = MagicFlag::Hostile;
            mEffects[kFearEffect] = fear;
            addSpell(kSpell1, "VoiceHowlFear1", 15.f);
            addSpell(kSpell2, "VoiceHowlFear2", 20.f);
            addSpell(kSpell3, "VoiceHowlFear3", 45.f);
            mShout.mCommon.mId = kHowlShout;
            mShout.mCommon.mEditorId = "HowlWerewolfFear";
            mShout.mHasData = true;
            mShout.mWordCount = 3;
            mShout.mWords[0] = { kWord1, kSpell1, 15.f };
            mShout.mWords[1] = { kWord2, kSpell2, 20.f };
            mShout.mWords[2] = { kWord3, kSpell3, 45.f };
            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            mProfiles[kVictimBase] = victim;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mUnarmedDamage = 4.f;
            mPlayer.mUnarmedReach = 96.f;
            mPlayer.mSkills.fill(15);
        }
        void addSpell(FormId id, const char* name, float)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mType = 11;
            s.mCastType = MagicCast::FireAndForget;
            s.mDelivery = MagicDelivery::Self;
            SpellEffect se;
            se.mEffect = kFearEffect;
            se.mMagnitude = 10.f;
            s.mEffects.push_back(se);
            mSpells[id] = s;
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
        const ShoutRecord* shoutById(FormId id) override { return id == kHowlShout ? &mShout : nullptr; }
        const ShoutRecord* shoutByEditorId(std::string_view) override { return nullptr; }

        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        ShoutRecord mShout;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    // A writer whose sidecar knows NO clip event at all: only the constants can time the howl.
    class HowlWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool stopClip(CombatSubject) override { return true; }
        bool clipEventTime(std::string_view, bool, ClipEventKind, float&) override { return false; }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            return true;
        }
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view clip, bool, ClipTiming& out) override
        {
            mAsked.emplace_back(clip);
            out = { true, 0.4f, 1.2f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = mState;
            return subject.player || mHasVictim;
        }
        std::vector<std::string> mAsked;
        std::vector<DamageReport> mDamage;
        SubjectState mState{ 0.0, 0.0, 0.0, 0.f, 100.f, 100.f };
        bool mHasVictim = false;
    };

    struct BeastHowl : ::testing::Test
    {
        HowlData mData;
        SkyrimCombat mCombat;
        HowlWriter mWriter;
        shouts::ShoutState mState;
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.setData(&mData);
            mCombat.setShoutState(&mState);
            mState.forceUnlock(kWord1);
            mState.forceUnlock(kWord2);
            mState.forceUnlock(kWord3);
            mState.equip(kHowlShout);
        }
        void update(float seconds = 0.1f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n)
        {
            for (int i = 0; i < n; ++i)
                update();
        }
        bool traceHas(const std::string& text) const
        {
            for (const std::string& line : mTrace)
                if (line.find(text) != std::string::npos)
                    return true;
            return false;
        }
        void beastOn()
        {
            SkyrimCombat::BeastForm form;
            form.mUnarmedDamage = 20.f;
            form.mUnarmedReach = 150.f;
            mCombat.setPlayerBeast(&form);
        }
    };

    TEST_F(BeastHowl, aBeastShoutPlaysTheHowlClipAndGoesAtTheEventTimeOfTheData)
    {
        beastOn();
        updates(2);
        const std::uint32_t before = mCombat.playerBeastBodyCount();
        mCombat.onPlayerShoutEdge(true);
        updates(3); // 0.3 s held: the first word is reached, the release speaks it
        mCombat.onPlayerShoutEdge(false);
        update();
        EXPECT_EQ(mCombat.playerBeastBodyCount(), before + 1);
        EXPECT_EQ(mCombat.playerBeastBodyClip(), "ww_howlforward");
        EXPECT_FALSE(traceHas("shout HowlWerewolfFear word 1 spell VoiceHowlFear1 goes")) << "not before 0.733 s";
        updates(8);
        EXPECT_TRUE(traceHas("shout HowlWerewolfFear word 1 spell VoiceHowlFear1 goes"));
    }

    TEST_F(BeastHowl, aHumanShoutStillAsksTheClipsEventAndCountsNoBeastClip)
    {
        updates(2);
        mCombat.onPlayerShoutEdge(true);
        updates(3);
        mCombat.onPlayerShoutEdge(false);
        updates(10);
        EXPECT_EQ(mCombat.playerBeastBodyCount(), 0u);
        EXPECT_FALSE(traceHas("shout HowlWerewolfFear word 1 spell VoiceHowlFear1 goes"))
            << "the writer knows no event time, so a human cast never goes (as before)";
    }

    TEST_F(BeastHowl, thePowerAttackOfTheBeastIsTheRealOne)
    {
        beastOn();
        updates(2);
        mCombat.onPlayerAttack(true);
        updates(10);
        bool real = false;
        std::string all;
        for (const std::string& clip : mWriter.mAsked)
        {
            real = real || clip == "werewolfbeast::ww_attackright";
            all += clip + ";";
        }
        EXPECT_TRUE(real) << "the player's power swing asks the timing of ww_attackright; asked: " << all;
    }
}
