// Task 0259 (werewolf, first slice): the pure half (game_skyrimse/werewolf.hpp) and the combat half (the beast form in
// SkyrimCombat). Numbers by hand:
//  - the clock: 150 s standard, warning at 15 s left, feeding adds 15 s (an animal) or 30 s (a person), capped at 600;
//  - the abilities by level: 10 -> 0x10C6E8 ... 45 -> 0x10F18A, 46 and over -> 0x10C6EB;
//  - the clips of the player's beast carry their folder: `werewolfbeast::ww_attackright`.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/werewolf.hpp>
#include <game_skyrimse/werewolf_service.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::werewolf;
    using namespace openeden::sdk::game;

    // ---- the clock --------------------------------------------------------------------------------------------------

    TEST(SkyrimWerewolfClock, isOffUntilStartedAndOffAfterStop)
    {
        BeastClock clock;
        EXPECT_FALSE(clock.active());
        EXPECT_EQ(clock.tick(1.f), BeastClock::Event::None);
        EXPECT_FALSE(clock.feed(true));
        clock.start(0.f);
        EXPECT_TRUE(clock.active());
        EXPECT_FLOAT_EQ(clock.remaining(), 150.f) << "a bad length gives the quest script's 150";
        clock.stop();
        EXPECT_FALSE(clock.active());
        EXPECT_FLOAT_EQ(clock.remaining(), 0.f);
    }

    TEST(SkyrimWerewolfClock, runsOutAfterItsSecondsAndSaysEndedOnce)
    {
        BeastClock clock;
        clock.start(3.f);
        EXPECT_EQ(clock.tick(1.f), BeastClock::Event::None);
        EXPECT_EQ(clock.tick(1.f), BeastClock::Event::None);
        EXPECT_NEAR(clock.remaining(), 1.f, 1e-4f);
        EXPECT_EQ(clock.tick(1.f), BeastClock::Event::Ended);
        EXPECT_FALSE(clock.active());
        EXPECT_EQ(clock.tick(1.f), BeastClock::Event::None);
    }

    TEST(SkyrimWerewolfClock, warnsOnceWhenFifteenSecondsAreLeft)
    {
        BeastClock clock;
        clock.start(20.f);
        int warned = 0;
        for (int i = 0; i < 18; ++i)
            warned += clock.tick(1.f) == BeastClock::Event::Warned ? 1 : 0;
        EXPECT_EQ(warned, 1);
        EXPECT_TRUE(clock.warned());
        EXPECT_NEAR(clock.remaining(), 2.f, 1e-3f);
    }

    TEST(SkyrimWerewolfClock, aFormStartedBelowTheWarningDoesNotWarn)
    {
        BeastClock clock;
        clock.start(10.f);
        EXPECT_EQ(clock.tick(1.f), BeastClock::Event::None);
    }

    TEST(SkyrimWerewolfClock, feedingAnAnimalAddsFifteenAndAPersonThirty)
    {
        BeastClock clock;
        clock.start(60.f);
        ASSERT_TRUE(clock.feed(false));
        EXPECT_NEAR(clock.remaining(), 75.f, 1e-4f);
        ASSERT_TRUE(clock.feed(true));
        EXPECT_NEAR(clock.remaining(), 105.f, 1e-4f);
    }

    TEST(SkyrimWerewolfClock, feedingNeverPassesTheCap)
    {
        BeastClock clock;
        clock.start(590.f);
        for (int i = 0; i < 5; ++i)
            ASSERT_TRUE(clock.feed(true));
        EXPECT_FLOAT_EQ(clock.remaining(), kMaxSeconds);
        clock.start(100000.f);
        EXPECT_FLOAT_EQ(clock.remaining(), kMaxSeconds);
    }

    TEST(SkyrimWerewolfClock, feedingAfterTheWarningLetsItWarnAgain)
    {
        BeastClock clock;
        clock.start(16.f);
        EXPECT_EQ(clock.tick(1.f), BeastClock::Event::Warned);
        ASSERT_TRUE(clock.feed(true)); // 15 + 30 = 45
        EXPECT_FALSE(clock.warned());
        int warned = 0;
        for (int i = 0; i < 40; ++i)
            warned += clock.tick(1.f) == BeastClock::Event::Warned ? 1 : 0;
        EXPECT_EQ(warned, 1);
    }

    TEST(SkyrimWerewolfClock, refusesBadFrameLengths)
    {
        BeastClock clock;
        clock.start(100.f);
        (void)clock.tick(std::numeric_limits<float>::quiet_NaN());
        (void)clock.tick(-5.f);
        EXPECT_FLOAT_EQ(clock.remaining(), 100.f);
        (void)clock.tick(1000.f); // one frame never takes more than a second
        EXPECT_FLOAT_EQ(clock.remaining(), 99.f);
    }

    // ---- the records ------------------------------------------------------------------------------------------------

    TEST(SkyrimWerewolfAbility, followsTheLevelSteps)
    {
        EXPECT_EQ(levelAbility(0), 0x10C6E8u);
        EXPECT_EQ(levelAbility(1), 0x10C6E8u);
        EXPECT_EQ(levelAbility(10), 0x10C6E8u);
        EXPECT_EQ(levelAbility(11), 0x10F186u);
        EXPECT_EQ(levelAbility(15), 0x10F186u);
        EXPECT_EQ(levelAbility(20), 0x10C6E9u);
        EXPECT_EQ(levelAbility(25), 0x10F187u);
        EXPECT_EQ(levelAbility(30), 0x10C6EAu);
        EXPECT_EQ(levelAbility(35), 0x10F188u);
        EXPECT_EQ(levelAbility(40), 0x10F189u);
        EXPECT_EQ(levelAbility(45), 0x10F18Au);
        EXPECT_EQ(levelAbility(46), 0x10C6EBu);
        EXPECT_EQ(levelAbility(500), 0x10C6EBu);
    }

    TEST(SkyrimWerewolfClips, theFolderComesOffAClipName)
    {
        EXPECT_EQ(bareClip("werewolfbeast::ww_attackrightfast"), "ww_attackrightfast");
        EXPECT_EQ(clipFolder("werewolfbeast::ww_attackrightfast"), "werewolfbeast");
        EXPECT_EQ(bareClip("ww_walkforward"), "ww_walkforward");
        EXPECT_TRUE(clipFolder("ww_walkforward").empty());
        EXPECT_EQ(bareClip(""), "");
    }

    TEST(SkyrimWerewolfProfile, thePlayerRowCarriesTheFolderOnItsAttacksOnly)
    {
        const CreatureProfile& player = playerProfile();
        const CreatureProfile& bare = bareProfile();
        EXPECT_EQ(player.mProject, "werewolfbeastproject");
        EXPECT_EQ(clipFolder(player.mAttack), kCreatureFolder);
        EXPECT_EQ(clipFolder(player.mPower), kCreatureFolder);
        EXPECT_EQ(bareClip(player.mAttack), bare.mAttack);
        EXPECT_EQ(bareClip(bare.mPower), "ww_attackrightpowercombo"); // task 0316: only the player's row has the single power clip
        EXPECT_TRUE(clipFolder(player.mIdle).empty());
        EXPECT_EQ(player.mIdle, bare.mIdle);
        EXPECT_EQ(player.mWalk, "ww_walkforward");
        EXPECT_EQ(player.mRun, "ww_runforward");
        // ww_walkforward: 93.4308 units of root motion in 1.33333 s; ww_runforward: 222.219 units in 0.733333 s
        EXPECT_NEAR(player.mWalkStride, 93.4308f, 1e-3f);
        EXPECT_NEAR(player.mWalkSpeed, 93.4308f / 1.33333f, 0.01f);
        EXPECT_NEAR(player.mRunStride, 222.219f, 1e-3f);
        EXPECT_NEAR(player.mRunSpeed, 222.219f / 0.733333f, 0.05f);
        // the skin's z extent
        EXPECT_NEAR(player.mCapsuleHeight, 139.6f, 0.1f);
        EXPECT_EQ(bareClip(player.mAttack), "ww_attackrightfast");
        EXPECT_EQ(bareClip(player.mPower), "ww_attackright"); // task 0316: the single power clip, not the combo
    }

    // ---- the combat half --------------------------------------------------------------------------------------------

    constexpr FormId kEffect = 0x92C45;

    class BeastData final : public CombatData
    {
    public:
        BeastData()
        {
            MagicEffectRecord e;
            e.mCommon.mId = kEffect;
            e.mCommon.mEditorId = "WerewolfChangeEffect";
            e.mHasData = true;
            e.mArchetype = kArchetypeWerewolf;
            mEffects[kEffect] = e;
            mSpell.mCommon.mId = kBeastPower;
            mSpell.mCommon.mEditorId = "WerewolfChange";
            mSpell.mHasData = true;
            mSpell.mType = 2; // a power
            mSpell.mCastType = MagicCast::FireAndForget;
            mSpell.mDelivery = MagicDelivery::Self;
            SpellEffect se;
            se.mEffect = kEffect;
            mSpell.mEffects.push_back(se);
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mUnarmedDamage = 4.f;
            mPlayer.mUnarmedReach = 96.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId, FighterProfile&) override { return false; }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view) override { return nullptr; }
        CombatSettings settings() override { return CombatSettings(); }
        const SpellRecord* spellById(FormId id) override { return id == kBeastPower ? &mSpell : nullptr; }
        const SpellRecord* spellByEditorId(std::string_view) override { return nullptr; }
        const MagicEffectRecord* effectById(FormId id) override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        SpellRecord mSpell;
        std::map<FormId, MagicEffectRecord> mEffects;
        FighterProfile mPlayer;
    };

    class BeastWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject, const SweepRequest& request) override
        {
            mSweepReach = request.reach;
            return true;
        }
        bool reportDamage(const DamageReport&) override { return true; }
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
            out = {};
            if (!subject.player)
                return false;
            out = { 0.0, 0.0, 0.0, 0.f, 100.f, 100.f };
            return true;
        }
        std::vector<std::string> mAsked;
        float mSweepReach = 0.f;
    };

    struct CombatWerewolf : ::testing::Test
    {
        BeastData mData;
        SkyrimCombat mCombat;
        BeastWriter mWriter;
        void SetUp() override
        {
            mCombat.setData(&mData);
            mWriter.mAsked.clear();
        }
        void swing()
        {
            mCombat.onPlayerAttack(false);
            for (int i = 0; i < 6; ++i)
                mCombat.combatUpdate(0.05f, mWriter);
        }
    };

    TEST_F(CombatWerewolf, theBeastPowerIsASupportedPowerBecauseItsEffectIsTheWerewolfArchetype)
    {
        const std::vector<SkyrimCombat::RacePower> powers = mCombat.racePowers({ kBeastPower });
        ASSERT_EQ(powers.size(), 1u);
        EXPECT_EQ(powers[0].mSupport, SkyrimCombat::PowerSupport::Works);
    }

    TEST_F(CombatWerewolf, castingTheBeastPowerCallsTheHookInsteadOfCasting)
    {
        int calls = 0;
        mCombat.setBeastStartHook([&] { ++calls; });
        EXPECT_TRUE(mCombat.castPlayerPower(kBeastPower));
        EXPECT_EQ(calls, 1);
        mCombat.combatUpdate(0.05f, mWriter);
        EXPECT_EQ(calls, 1) << "it was not queued as an ordinary power";
    }

    TEST_F(CombatWerewolf, withNoHookTheBeastPowerDoesNothing)
    {
        EXPECT_FALSE(mCombat.castPlayerPower(kBeastPower));
    }

    TEST_F(CombatWerewolf, aHumanSwingUsesNoCreatureFolder)
    {
        swing();
        EXPECT_FALSE(mCombat.playerBeast());
        for (const std::string& clip : mWriter.mAsked)
            EXPECT_TRUE(clipFolder(clip).empty()) << clip;
    }

    TEST_F(CombatWerewolf, inBeastFormTheSwingIsTheWerewolfsAndItsTimingIsAskedByTheFolderedName)
    {
        SkyrimCombat::BeastForm form;
        form.mUnarmedDamage = 74.f;
        form.mUnarmedReach = 150.f;
        mCombat.setPlayerBeast(&form);
        EXPECT_TRUE(mCombat.playerBeast());
        swing();
        EXPECT_EQ(mCombat.playerSwingCount(), 1u);
        EXPECT_EQ(mCombat.playerSwingClip(), "werewolfbeast::ww_attackrightfast");
        ASSERT_FALSE(mWriter.mAsked.empty());
        for (const std::string& clip : mWriter.mAsked)
            EXPECT_EQ(clipFolder(clip), kCreatureFolder) << clip;
    }

    TEST_F(CombatWerewolf, theClawsReachAsFarAsTheRaceSays)
    {
        SkyrimCombat::BeastForm form;
        form.mUnarmedDamage = 74.f;
        form.mUnarmedReach = 150.f;
        mCombat.setPlayerBeast(&form);
        swing();
        // the swing's hit comes at 0.4 s of the clip: run on until the sweep is asked for
        for (int i = 0; i < 20 && mWriter.mSweepReach == 0.f; ++i)
            mCombat.combatUpdate(0.05f, mWriter);
        EXPECT_GT(mWriter.mSweepReach, 140.f);
        EXPECT_LT(mWriter.mSweepReach, 200.f);
    }

    TEST_F(CombatWerewolf, turningBackToHumanGivesTheHumanClipsAgain)
    {
        SkyrimCombat::BeastForm form;
        form.mUnarmedDamage = 74.f;
        form.mUnarmedReach = 150.f;
        mCombat.setPlayerBeast(&form);
        swing();
        for (int i = 0; i < 40; ++i)
            mCombat.combatUpdate(0.05f, mWriter);
        mCombat.setPlayerBeast(nullptr);
        EXPECT_FALSE(mCombat.playerBeast());
        mWriter.mAsked.clear();
        swing();
        EXPECT_EQ(mCombat.playerSwingCount(), 2u);
        EXPECT_TRUE(clipFolder(mCombat.playerSwingClip()).empty()) << mCombat.playerSwingClip();
    }

    // ---- the service ------------------------------------------------------------------------------------------------

    TEST(SkyrimWerewolfService, feedsOnlyWhileTheFormIsOnAndOnlyWhatTheHookAccepts)
    {
        std::uint64_t seen = 0;
        WerewolfService service([&](std::uint64_t placement, FormId) {
            seen = placement;
            return placement != 7;
        });
        EXPECT_FALSE(service.feed(5, 1)) << "not a werewolf yet";
        EXPECT_EQ(seen, 0u);
        service.setState(true, 100.f);
        EXPECT_TRUE(service.feed(5, 1));
        EXPECT_EQ(seen, 5u);
        EXPECT_FALSE(service.feed(7, 1));
        service.setState(false, 0.f);
        EXPECT_FALSE(service.feed(5, 1));
    }

    TEST(SkyrimWerewolfService, noticesAreNumberedFromOneAndTheOldestGoFirst)
    {
        WerewolfService service(nullptr);
        EXPECT_EQ(service.noticeCount(), 0u);
        EXPECT_TRUE(service.noticeAt(1).empty());
        service.announce("first");
        service.announce("second");
        EXPECT_EQ(service.noticeCount(), 2u);
        EXPECT_EQ(service.noticeAt(1), "first");
        EXPECT_EQ(service.noticeAt(2), "second");
        EXPECT_TRUE(service.noticeAt(0).empty());
        EXPECT_TRUE(service.noticeAt(3).empty());
        for (int i = 0; i < 200; ++i)
            service.announce("n" + std::to_string(i));
        EXPECT_LE(service.noticeCount(), 64u);
        EXPECT_EQ(service.noticeAt(service.noticeCount()), "n199");
    }
}
