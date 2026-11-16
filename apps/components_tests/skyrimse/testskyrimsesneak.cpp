// Task 0227 (Skyrim sneaking and detection; docs/tasks/0227-skyrim-sneak-detection.md): the pure stealth rules and the
// sneak system over a fake roster and a fake combat writer. Written from the task spec before the code and run red
// against stubs first (see the stage log of the task). No game data.
#include <game_skyrimse/sneak.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::sneak;
    using openeden::sdk::game::CombatRequestWriter;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::RayFate;
    using openeden::sdk::game::RayHit;
    using openeden::sdk::game::RayRequest;
    using openeden::sdk::game::SubjectState;

    constexpr double kPi = 3.14159265358979323846;

    // ---- the pure rules ---------------------------------------------------------------------------------------------

    TEST(SkyrimSneakRules, alertsHaveNames)
    {
        EXPECT_EQ(alertName(Alert::Hidden), "hidden");
        EXPECT_EQ(alertName(Alert::Caution), "caution");
        EXPECT_EQ(alertName(Alert::Detected), "detected");
    }

    TEST(SkyrimSneakRules, theStartingRangeIsTheGmstTimesOurScaleAndTheSkillFactor)
    {
        const Settings s;
        PlayerStealth p;
        p.mSneaking = true;
        p.mSkill = 15.f;
        p.mSpeed = 100.f; // a walk: neither still nor running
        // 2500 * 0.25 * (1 - 0.5 * 15 / 100) = 578.125
        EXPECT_NEAR(stealthRange(s, p), 578.125f, 0.01f);
    }

    TEST(SkyrimSneakRules, aMasterSneakWalksAtHalfTheRange)
    {
        const Settings s;
        PlayerStealth p;
        p.mSneaking = true;
        p.mSkill = 100.f;
        p.mSpeed = 100.f;
        EXPECT_NEAR(stealthRange(s, p), 312.5f, 0.01f);
    }

    TEST(SkyrimSneakRules, standingStillHalvesTheRangeAndRunningDoublesIt)
    {
        const Settings s;
        PlayerStealth p;
        p.mSneaking = true;
        p.mSkill = 100.f;
        p.mSpeed = 100.f;
        const float walk = stealthRange(s, p);
        p.mSpeed = 0.f;
        EXPECT_NEAR(stealthRange(s, p), walk * 0.5f, 0.01f);
        p.mSpeed = 250.f;
        EXPECT_NEAR(stealthRange(s, p), walk * 2.f, 0.01f);
    }

    TEST(SkyrimSneakRules, notSneakingDoublesTheRangeAgain)
    {
        const Settings s;
        PlayerStealth p;
        p.mSkill = 15.f;
        p.mSpeed = 100.f;
        p.mSneaking = true;
        const float sneaking = stealthRange(s, p);
        p.mSneaking = false;
        EXPECT_NEAR(stealthRange(s, p), sneaking * 2.f, 0.01f);
    }

    TEST(SkyrimSneakRules, heavyArmourAddsToTheRange)
    {
        const Settings s;
        PlayerStealth p;
        p.mSneaking = true;
        p.mSkill = 100.f;
        p.mSpeed = 100.f;
        const float bare = stealthRange(s, p);
        p.mArmourWeight = 30.f; // 30 * 0.01 = +30 percent
        EXPECT_NEAR(stealthRange(s, p), bare * 1.3f, 0.01f);
    }

    TEST(SkyrimSneakRules, theSkillIsClampedAndBadNumbersAreHarmless)
    {
        const Settings s;
        PlayerStealth p;
        p.mSneaking = true;
        p.mSpeed = 100.f;
        p.mSkill = 500.f;
        const float high = stealthRange(s, p);
        p.mSkill = 100.f;
        EXPECT_FLOAT_EQ(high, stealthRange(s, p));
        p.mSkill = -40.f;
        const float low = stealthRange(s, p);
        p.mSkill = 0.f;
        EXPECT_FLOAT_EQ(low, stealthRange(s, p));
        EXPECT_GT(low, 0.f);
        p.mSkill = std::numeric_limits<float>::quiet_NaN();
        p.mSpeed = std::numeric_limits<float>::infinity();
        p.mArmourWeight = -1.f;
        const float bad = stealthRange(s, p);
        EXPECT_TRUE(std::isfinite(bad));
        EXPECT_GE(bad, 0.f);
    }

    TEST(SkyrimSneakRules, noticeGrowsWithClosenessAndFallsToNothingAtTheEdge)
    {
        const Settings s;
        // point blank: the whole rate
        EXPECT_NEAR(updateNotice(s, 0.f, 0.5f, 600.f, 0.f, true), 0.5f, 1e-5f);
        // halfway: half the rate
        EXPECT_NEAR(updateNotice(s, 0.f, 0.5f, 600.f, 300.f, true), 0.25f, 1e-5f);
        // at the edge and beyond: it decays instead (0.25 a second)
        EXPECT_NEAR(updateNotice(s, 0.5f, 1.f, 600.f, 600.f, true), 0.25f, 1e-5f);
        EXPECT_NEAR(updateNotice(s, 0.5f, 1.f, 600.f, 900.f, true), 0.25f, 1e-5f);
    }

    TEST(SkyrimSneakRules, withoutASightLineOnlyAThirdOfTheRangeIsHeardAndItStopsAtTheCap)
    {
        const Settings s;
        // range 600, heard range 180: at 300 nothing is heard, the notice decays
        EXPECT_NEAR(updateNotice(s, 0.4f, 1.f, 600.f, 300.f, false), 0.15f, 1e-5f);
        // at 90 it is heard (half the heard range): +0.5 a second
        EXPECT_NEAR(updateNotice(s, 0.f, 1.f, 600.f, 90.f, false), 0.5f, 1e-5f);
        // and it never reaches detection without seeing
        EXPECT_NEAR(updateNotice(s, 0.85f, 5.f, 600.f, 0.f, false), 0.9f, 1e-5f);
        // a notice that was already higher than the cap is not pulled down by it
        EXPECT_GE(updateNotice(s, 1.2f, 0.1f, 600.f, 0.f, false), 1.19f);
    }

    TEST(SkyrimSneakRules, noticeStaysInItsBoundsAndIgnoresBadTime)
    {
        const Settings s;
        EXPECT_FLOAT_EQ(updateNotice(s, 1.4f, 10.f, 600.f, 0.f, true), 1.5f);
        EXPECT_FLOAT_EQ(updateNotice(s, 0.1f, 10.f, 600.f, 900.f, true), 0.f);
        EXPECT_FLOAT_EQ(updateNotice(s, 0.4f, -1.f, 600.f, 0.f, true), 0.4f);
        EXPECT_FLOAT_EQ(updateNotice(s, 0.4f, std::numeric_limits<float>::quiet_NaN(), 600.f, 0.f, true), 0.4f);
        EXPECT_FLOAT_EQ(updateNotice(s, std::numeric_limits<float>::quiet_NaN(), 1.f, 600.f, 900.f, true), 0.f);
        EXPECT_NEAR(updateNotice(s, 0.4f, 0.5f, 0.f, 10.f, true), 0.275f, 1e-5f); // no range: it only decays
    }

    TEST(SkyrimSneakRules, theAlertHasHysteresis)
    {
        const Settings s;
        EXPECT_EQ(classify(s, 0.1f, Alert::Hidden), Alert::Hidden);
        EXPECT_EQ(classify(s, 0.3f, Alert::Hidden), Alert::Caution);
        EXPECT_EQ(classify(s, 0.25f, Alert::Caution), Alert::Caution); // between 0.2 and 0.3: stays
        EXPECT_EQ(classify(s, 0.25f, Alert::Hidden), Alert::Hidden);
        EXPECT_EQ(classify(s, 0.19f, Alert::Caution), Alert::Hidden);
        EXPECT_EQ(classify(s, 1.0f, Alert::Caution), Alert::Detected);
        EXPECT_EQ(classify(s, 0.8f, Alert::Detected), Alert::Detected); // between 0.75 and 1.0: stays
        EXPECT_EQ(classify(s, 0.8f, Alert::Caution), Alert::Caution);
        EXPECT_EQ(classify(s, 0.7f, Alert::Detected), Alert::Caution);
        EXPECT_EQ(classify(s, 0.1f, Alert::Detected), Alert::Hidden);
        EXPECT_EQ(classify(s, 1.2f, Alert::Hidden), Alert::Detected);
    }

    TEST(SkyrimSneakRules, theMultiplierFollowsTheWeaponType)
    {
        const Settings s;
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::OneHandSword), 3.f);
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::OneHandDagger), 3.f);
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::OneHandAxe), 3.f);
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::OneHandMace), 3.f);
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::TwoHandSword), 2.f);
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::TwoHandAxe), 2.f);
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::HandToHand), 2.f);
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::Bow), 2.f);
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::Crossbow), 2.f);
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::Staff), 2.f);
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, 200), 2.f);
    }

    TEST(SkyrimSneakRules, aChangedSettingChangesTheMultiplier)
    {
        Settings s;
        s.mMult1HDagger = 5.f;
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::OneHandDagger), 5.f);
        EXPECT_FLOAT_EQ(baseAttackMultiplier(s, WeaponAnimType::OneHandSword), 3.f);
    }

    TEST(SkyrimSneakRules, theMessageNamesTheMultiplier)
    {
        EXPECT_EQ(attackMessage(3.f), "Sneak attack for 3x damage!");
        EXPECT_EQ(attackMessage(15.f), "Sneak attack for 15x damage!");
        EXPECT_EQ(attackMessage(2.5f), "Sneak attack for 2.5x damage!");
        EXPECT_EQ(attackMessage(std::numeric_limits<float>::quiet_NaN()), "Sneak attack for 1x damage!");
    }

    TEST(SkyrimSneakRules, stayingHiddenTrainsOnlyWhileSneakingAndMovingNearAnUnawareWatcher)
    {
        const Settings s;
        EXPECT_NEAR(hiddenTraining(s, true, 100.f, 2, 0.5f), 0.3125f, 1e-6f);
        EXPECT_FLOAT_EQ(hiddenTraining(s, false, 100.f, 2, 0.5f), 0.f);
        EXPECT_FLOAT_EQ(hiddenTraining(s, true, 0.f, 2, 0.5f), 0.f); // standing still earns nothing
        EXPECT_FLOAT_EQ(hiddenTraining(s, true, 100.f, 0, 0.5f), 0.f); // nobody to hide from
        EXPECT_FLOAT_EQ(hiddenTraining(s, true, 100.f, 2, -1.f), 0.f);
        EXPECT_FLOAT_EQ(hiddenTraining(s, true, std::numeric_limits<float>::quiet_NaN(), 2, 0.5f), 0.f);
    }

    // ---- the system over a fake world --------------------------------------------------------------------------------

    class SneakWriter final : public CombatRequestWriter
    {
    public:
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            if (subject.player)
            {
                if (mPlayerGone)
                    return false;
                out = mPlayer;
                return true;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        bool castRay(CombatSubject, const RayRequest& request, RayHit& out) override
        {
            ++mRays;
            out = {};
            const double endX = request.x
                + std::sin(static_cast<double>(request.heading)) * std::cos(static_cast<double>(request.pitch))
                    * static_cast<double>(request.range);
            if (mWall && ((request.x < mWallX) != (endX < mWallX)))
                out.fate = RayFate::World;
            return true;
        }
        void put(std::uint32_t reference, double x, double y, float heading)
        {
            SubjectState s;
            s.x = x;
            s.y = y;
            s.heading = heading;
            mStates[reference] = s;
        }
        std::map<std::uint32_t, SubjectState> mStates;
        SubjectState mPlayer;
        bool mPlayerGone = false;
        bool mWall = false;
        double mWallX = 50.0;
        std::size_t mRays = 0;
    };

    struct SkyrimSneakSystem : ::testing::Test
    {
        SneakWriter mWriter;
        std::vector<crime::CrimeActor> mRoster;
        std::vector<std::uint32_t> mFollowers;
        float mSkill = 15.f;
        float mWeight = 0.f;
        std::vector<float> mTrained;
        std::vector<std::string> mLog;
        std::unique_ptr<SneakSystem> mSystem;

        void SetUp() override
        {
            Settings settings;
            settings.mStillFactor = 1.f; // the fake player stands still in most tests; a walk is the reference
            mSystem = makeSystem(settings);
        }
        std::unique_ptr<SneakSystem> makeSystem(const Settings& settings)
        {
            Hooks hooks;
            hooks.mActors = [this](std::vector<crime::CrimeActor>& out) {
                out.insert(out.end(), mRoster.begin(), mRoster.end());
            };
            hooks.mIsFollower = [this](std::uint32_t r) {
                return std::find(mFollowers.begin(), mFollowers.end(), r) != mFollowers.end();
            };
            hooks.mSkill = [this]() { return mSkill; };
            hooks.mArmourWeight = [this]() { return mWeight; };
            hooks.mTrain = [this](float amount) { mTrained.push_back(amount); };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            return std::make_unique<SneakSystem>(std::move(hooks), settings);
        }
        // An observer at (x, 0) that faces the player at the origin (facing -X is heading -pi/2).
        void addWatcher(std::uint32_t reference, double x, bool facing = true)
        {
            crime::CrimeActor a;
            a.mHandle = { reference, 1 };
            a.mName = "npc" + std::to_string(reference);
            mRoster.push_back(a);
            mWriter.put(reference, x, 0.0, static_cast<float>(facing ? -kPi / 2.0 : kPi / 2.0));
        }
        void step(int n = 1, float seconds = 0.5f)
        {
            for (int i = 0; i < n; ++i)
                mSystem->update(seconds, mWriter);
        }
    };

    TEST_F(SkyrimSneakSystem, anObserverInFrontNoticesSlowlyAndTheEyeOpensInSteps)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        // range 578.125; at 300 units the gain is (1 - 300 / 578.125) * 0.5 = 0.2405 a judgement
        step();
        EXPECT_EQ(mSystem->alertOf(7), Alert::Hidden);
        EXPECT_EQ(mSystem->eye(), 0);
        step();
        EXPECT_EQ(mSystem->alertOf(7), Alert::Caution);
        EXPECT_EQ(mSystem->eye(), 1);
        step(3);
        EXPECT_EQ(mSystem->alertOf(7), Alert::Detected);
        EXPECT_EQ(mSystem->eye(), 2);
        EXPECT_TRUE(mSystem->detected(7));
        EXPECT_EQ(mSystem->overall(), Alert::Detected);
    }

    TEST_F(SkyrimSneakSystem, thereIsNoEyeWhileNotSneaking)
    {
        mWriter.mPlayer.sneaking = false;
        addWatcher(7, 300.0);
        step(3);
        EXPECT_EQ(mSystem->eye(), -1);
        EXPECT_FALSE(mSystem->sneaking());
        // and standing up doubles the range, so the watcher has noticed him by now
        EXPECT_TRUE(mSystem->detected(7));
    }

    TEST_F(SkyrimSneakSystem, anObserverFacingAwayOnlyHearsAndNeverDetects)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 100.0, false);
        step(20);
        EXPECT_EQ(mSystem->alertOf(7), Alert::Caution);
        EXPECT_EQ(mWriter.mRays, 0u); // out of its view: no sight ray is spent
    }

    TEST_F(SkyrimSneakSystem, aWallBlocksSight)
    {
        mWriter.mPlayer.sneaking = true;
        mWriter.mWall = true;
        mWriter.mWallX = 50.0;
        addWatcher(7, 100.0);
        step(20);
        EXPECT_EQ(mSystem->alertOf(7), Alert::Caution); // heard through it, never seen
        EXPECT_GT(mWriter.mRays, 0u);
    }

    TEST_F(SkyrimSneakSystem, aFarObserverIsNeverLookedAtAndCostsNoRay)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 4000.0);
        step(4);
        EXPECT_EQ(mSystem->alertOf(7), Alert::Hidden);
        EXPECT_EQ(mWriter.mRays, 0u);
        EXPECT_EQ(mSystem->observerCount(), 0u);
    }

    TEST_F(SkyrimSneakSystem, aFollowerADeadActorAndAnObserverOutOfTheRosterDoNotCount)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(1, 100.0);
        addWatcher(2, 100.0);
        mFollowers = { 1 };
        mRoster[1].mDead = true;
        step(6);
        EXPECT_EQ(mSystem->alertOf(1), Alert::Hidden);
        EXPECT_EQ(mSystem->alertOf(2), Alert::Hidden);
        EXPECT_EQ(mSystem->overall(), Alert::Hidden);
    }

    TEST_F(SkyrimSneakSystem, anActorFightingThePlayerHasNoticedHimAtOnce)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 2000.0);
        mRoster[0].mEngagedAgainstPlayer = true;
        step();
        EXPECT_EQ(mSystem->alertOf(7), Alert::Detected);
    }

    TEST_F(SkyrimSneakSystem, anObserverThatLeavesTheRosterIsForgotten)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        step(2);
        EXPECT_EQ(mSystem->observerCount(), 1u);
        mRoster.clear();
        step();
        EXPECT_EQ(mSystem->observerCount(), 0u);
        EXPECT_EQ(mSystem->alertOf(7), Alert::Hidden);
    }

    TEST_F(SkyrimSneakSystem, noticeDecaysWhenThePlayerMovesAway)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        step(2);
        ASSERT_EQ(mSystem->alertOf(7), Alert::Caution);
        mWriter.put(7, 3000.0, 0.0, static_cast<float>(-kPi / 2.0)); // out of its range
        step(6);
        EXPECT_EQ(mSystem->alertOf(7), Alert::Hidden);
    }

    TEST_F(SkyrimSneakSystem, theRaysPerJudgementAreCappedAndTheNearestComeFirst)
    {
        mWriter.mPlayer.sneaking = true;
        for (std::uint32_t i = 1; i <= 10; ++i)
            addWatcher(i, 100.0 + 10.0 * i);
        step();
        EXPECT_EQ(mWriter.mRays, 6u);
        EXPECT_EQ(mSystem->raysCast(), 6u);
    }

    TEST_F(SkyrimSneakSystem, theWorkRunsOncePerPeriodNotEveryFrame)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        step(10, 0.05f); // 0.5 s in ten frames
        EXPECT_EQ(mSystem->judgements(), 1u);
        step(5, 0.05f);
        EXPECT_EQ(mSystem->judgements(), 1u);
        step(5, 0.05f);
        EXPECT_EQ(mSystem->judgements(), 2u);
    }

    TEST_F(SkyrimSneakSystem, badTimeAndAMissingPlayerChangeNothing)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        step(1, -1.f);
        step(1, std::numeric_limits<float>::quiet_NaN());
        step(1, 0.f);
        EXPECT_EQ(mSystem->judgements(), 0u);
        mWriter.mPlayerGone = true;
        step(4);
        EXPECT_EQ(mSystem->observerCount(), 0u);
        EXPECT_EQ(mSystem->eye(), -1);
    }

    TEST_F(SkyrimSneakSystem, theSpeedIsTheSlowerOfTheLastTwoIntervalsAndAJumpIsNotASpeed)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 3000.0);
        step(); // the first judgement has no earlier position
        EXPECT_FLOAT_EQ(mSystem->speed(), 0.f);
        mWriter.mPlayer.x = 100.0; // 100 units in 0.5 s: 200 a second, but the interval before was still
        step();
        EXPECT_FLOAT_EQ(mSystem->speed(), 0.f);
        mWriter.mPlayer.x = 200.0; // a second interval of 200 a second
        step();
        EXPECT_NEAR(mSystem->speed(), 200.f, 0.01f);
        mWriter.mPlayer.x = 100000.0; // a teleport
        step();
        EXPECT_FLOAT_EQ(mSystem->speed(), 0.f);
        mWriter.mPlayer.x = 100100.0; // a walk after it
        step();
        EXPECT_FLOAT_EQ(mSystem->speed(), 0.f);
    }

    TEST_F(SkyrimSneakSystem, aShortJumpIsNotARunEither)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 3000.0);
        step();
        mWriter.mPlayer.x = 400.0; // 800 units a second for one interval (a placement, not a run)
        step();
        EXPECT_FLOAT_EQ(mSystem->speed(), 0.f);
        step(); // standing at the new place
        EXPECT_FLOAT_EQ(mSystem->speed(), 0.f);
    }

    TEST_F(SkyrimSneakSystem, theRangeUsesTheSkillAndTheArmourWeightOfTheHooks)
    {
        mSystem = makeSystem(Settings{}); // the default still factor: the fake player stands still
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 3000.0);
        mSkill = 100.f;
        mWeight = 0.f;
        step();
        const float bare = mSystem->range();
        EXPECT_NEAR(bare, 156.25f, 0.01f); // still: 312.5 * 0.5
        mWeight = 20.f;
        step();
        EXPECT_NEAR(mSystem->range(), bare * 1.2f, 0.01f);
    }

    TEST_F(SkyrimSneakSystem, markDetectedMakesAnObserverDetectedAtOnce)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 2000.0);
        step();
        EXPECT_FALSE(mSystem->detected(7));
        mSystem->markDetected(7);
        EXPECT_TRUE(mSystem->detected(7));
        EXPECT_EQ(mSystem->overall(), Alert::Detected);
        mSystem->markDetected(99); // unknown: becomes an observer too
        EXPECT_TRUE(mSystem->detected(99));
    }

    TEST_F(SkyrimSneakSystem, aSneakAttackNeedsASneakingPlayerAndAnUnawareVictim)
    {
        addWatcher(7, 300.0);
        mWriter.mPlayer.sneaking = false;
        step();
        EXPECT_FLOAT_EQ(mSystem->attackMultiplier(7, WeaponAnimType::OneHandSword, false), 1.f);
        mWriter.mPlayer.sneaking = true;
        step();
        EXPECT_FLOAT_EQ(mSystem->attackMultiplier(7, WeaponAnimType::OneHandSword, false), 3.f);
        EXPECT_FLOAT_EQ(mSystem->attackMultiplier(7, WeaponAnimType::TwoHandSword, false), 2.f);
        EXPECT_FLOAT_EQ(mSystem->attackMultiplier(7, WeaponAnimType::Bow, true), 2.f);
        // a victim nobody has judged (it is far away or new) is unaware
        EXPECT_FLOAT_EQ(mSystem->attackMultiplier(55, WeaponAnimType::OneHandDagger, false), 3.f);
        mSystem->markDetected(7);
        EXPECT_FLOAT_EQ(mSystem->attackMultiplier(7, WeaponAnimType::OneHandSword, false), 1.f);
    }

    TEST_F(SkyrimSneakSystem, aCautiousVictimIsStillASneakAttackTarget)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        step(2);
        ASSERT_EQ(mSystem->alertOf(7), Alert::Caution);
        EXPECT_FLOAT_EQ(mSystem->attackMultiplier(7, WeaponAnimType::OneHandSword, false), 3.f);
    }

    TEST_F(SkyrimSneakSystem, aLandedSneakAttackShowsTheMessageTrainsAndAlertsTheVictim)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        step();
        const std::uint32_t id = mSystem->messageId();
        mSystem->onSneakAttack(7, 3.f);
        EXPECT_EQ(mSystem->message(), "Sneak attack for 3x damage!");
        EXPECT_EQ(mSystem->messageId(), id + 1);
        ASSERT_EQ(mTrained.size(), 1u);
        EXPECT_FLOAT_EQ(mTrained[0], 30.f);
        EXPECT_TRUE(mSystem->detected(7));
        mSystem->onSneakAttack(7, 15.f);
        EXPECT_EQ(mSystem->message(), "Sneak attack for 15x damage!");
        EXPECT_EQ(mSystem->messageId(), id + 2);
    }

    TEST_F(SkyrimSneakSystem, stayingHiddenWhileMovingTrainsTheSkill)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 2500.0);
        step();
        mWriter.mPlayer.x = 50.0; // moving at 100 units a second (it counts from the second interval on)
        step();
        mWriter.mPlayer.x = 100.0;
        step();
        mWriter.mPlayer.x = 150.0;
        step();
        ASSERT_EQ(mTrained.size(), 2u);
        EXPECT_NEAR(mTrained[0], 0.3125f, 1e-5f);
    }

    TEST_F(SkyrimSneakSystem, nothingTrainsStandingStillOrWhenEveryoneHasNoticed)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 2500.0);
        step(3);
        EXPECT_TRUE(mTrained.empty());
        mSystem->markDetected(7);
        mWriter.mPlayer.x = 50.0;
        step();
        mWriter.mPlayer.x = 100.0;
        step();
        mWriter.mPlayer.x = 150.0;
        step();
        EXPECT_TRUE(mTrained.empty());
    }

    TEST_F(SkyrimSneakSystem, theResultDoesNotDependOnTheRosterOrder)
    {
        const auto run = [this](bool reverse) {
            mRoster.clear();
            mWriter.mStates.clear();
            for (std::uint32_t i = 1; i <= 8; ++i)
                addWatcher(i, 200.0 + 40.0 * i);
            if (reverse)
                std::reverse(mRoster.begin(), mRoster.end());
            mWriter.mPlayer.sneaking = true;
            mWriter.mRays = 0;
            mSystem = makeSystem(Settings{});
            step(6);
            std::string out;
            for (std::uint32_t i = 1; i <= 8; ++i)
                out += std::string(alertName(mSystem->alertOf(i))) + ",";
            return out + std::to_string(mWriter.mRays);
        };
        EXPECT_EQ(run(false), run(true));
    }

    TEST_F(SkyrimSneakSystem, twoRunsGiveTheSameAlerts)
    {
        const auto run = [this]() {
            mRoster.clear();
            mWriter.mStates.clear();
            for (std::uint32_t i = 1; i <= 5; ++i)
                addWatcher(i, 150.0 + 90.0 * i);
            mWriter.mPlayer.sneaking = true;
            mSystem = makeSystem(Settings{});
            std::string out;
            for (int n = 0; n < 8; ++n)
            {
                step();
                for (std::uint32_t i = 1; i <= 5; ++i)
                    out += std::string(alertName(mSystem->alertOf(i))).substr(0, 1);
            }
            return out;
        };
        EXPECT_EQ(run(), run());
    }

    TEST_F(SkyrimSneakSystem, theEyeShowsTheWorstObserver)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(1, 2400.0);
        addWatcher(2, 300.0);
        step(2);
        EXPECT_EQ(mSystem->alertOf(1), Alert::Hidden);
        EXPECT_EQ(mSystem->alertOf(2), Alert::Caution);
        EXPECT_EQ(mSystem->overall(), Alert::Caution);
        EXPECT_EQ(mSystem->eye(), 1);
    }
}
