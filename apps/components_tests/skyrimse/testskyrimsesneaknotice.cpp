// Task 0235: the alert hook of the sneak system (SneakSystem::setAlertHook): every change of an observer's alert is
// told to the hook with the observer, the old and new alert and whether the player is sneaking, so the pack can make an
// NPC that begins to suspect a sneaking player speak. Over a fake combat writer; no game data. Written before the hook
// and run red against a stub (see the stage log of the task).
#include <game_skyrimse/sneak.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::sneak;
    using openeden::sdk::game::CombatRequestWriter;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::RayHit;
    using openeden::sdk::game::RayRequest;
    using openeden::sdk::game::SubjectState;

    constexpr double kPi = 3.14159265358979323846;

    class AlertWriter final : public CombatRequestWriter
    {
    public:
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            if (subject.player)
            {
                out = mPlayer;
                return true;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        bool castRay(CombatSubject, const RayRequest&, RayHit& out) override
        {
            out = {};
            return true;
        }
        void put(std::uint32_t reference, double x, float heading)
        {
            SubjectState s;
            s.x = x;
            s.heading = heading;
            mStates[reference] = s;
        }
        std::map<std::uint32_t, SubjectState> mStates;
        SubjectState mPlayer;
    };

    struct AlertEvent
    {
        std::uint32_t mReference;
        Alert mBefore;
        Alert mAfter;
        bool mSneaking;
    };

    struct SkyrimSneakAlertHook : ::testing::Test
    {
        AlertWriter mWriter;
        std::vector<crime::CrimeActor> mRoster;
        std::vector<AlertEvent> mEvents;
        std::unique_ptr<SneakSystem> mSystem;

        void SetUp() override
        {
            Settings settings;
            settings.mStillFactor = 1.f;
            Hooks hooks;
            hooks.mActors = [this](std::vector<crime::CrimeActor>& out) {
                out.insert(out.end(), mRoster.begin(), mRoster.end());
            };
            mSystem = std::make_unique<SneakSystem>(std::move(hooks), settings);
            mSystem->setAlertHook([this](std::uint32_t reference, Alert before, Alert after, bool sneaking) {
                mEvents.push_back({ reference, before, after, sneaking });
            });
        }
        void addWatcher(std::uint32_t reference, double x)
        {
            crime::CrimeActor a;
            a.mHandle = { reference, 1 };
            a.mName = "npc" + std::to_string(reference);
            mRoster.push_back(a);
            mWriter.put(reference, x, static_cast<float>(-kPi / 2.0));
        }
        void step(int n = 1, float seconds = 0.5f)
        {
            for (int i = 0; i < n; ++i)
                mSystem->update(seconds, mWriter);
        }
    };

    TEST_F(SkyrimSneakAlertHook, anObserverThatBeginsToSuspectATellsTheHookOnce)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        step(); // notice 0.24: still hidden
        EXPECT_TRUE(mEvents.empty());
        step(); // 0.48: caution
        ASSERT_EQ(mEvents.size(), 1u);
        EXPECT_EQ(mEvents[0].mReference, 7u);
        EXPECT_EQ(mEvents[0].mBefore, Alert::Hidden);
        EXPECT_EQ(mEvents[0].mAfter, Alert::Caution);
        EXPECT_TRUE(mEvents[0].mSneaking);
    }

    TEST_F(SkyrimSneakAlertHook, everyLaterChangeIsToldToo)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        step(6);
        ASSERT_GE(mEvents.size(), 2u);
        EXPECT_EQ(mEvents[1].mBefore, Alert::Caution);
        EXPECT_EQ(mEvents[1].mAfter, Alert::Detected);
    }

    TEST_F(SkyrimSneakAlertHook, aPlayerWhoIsNotSneakingIsToldAsSuch)
    {
        mWriter.mPlayer.sneaking = false;
        addWatcher(7, 300.0);
        step(4);
        ASSERT_FALSE(mEvents.empty());
        EXPECT_FALSE(mEvents[0].mSneaking);
    }

    TEST_F(SkyrimSneakAlertHook, anObserverBeyondTheRangeTellsNothing)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 2000.0);
        step(8);
        EXPECT_TRUE(mEvents.empty());
    }

    TEST_F(SkyrimSneakAlertHook, withNoHookTheAlertsAreTheSame)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        mSystem->setAlertHook({});
        step(2);
        EXPECT_EQ(mSystem->alertOf(7), Alert::Caution);
        EXPECT_TRUE(mEvents.empty());
    }

    TEST_F(SkyrimSneakAlertHook, aHookThatThrowsDoesNotChangeTheAlerts)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        mSystem->setAlertHook([](std::uint32_t, Alert, Alert, bool) { throw std::runtime_error("no"); });
        EXPECT_NO_THROW(step(2));
        EXPECT_EQ(mSystem->alertOf(7), Alert::Caution);
    }

    TEST_F(SkyrimSneakAlertHook, twoRunsTellTheSameEvents)
    {
        mWriter.mPlayer.sneaking = true;
        addWatcher(7, 300.0);
        addWatcher(3, 200.0);
        step(6);
        const std::vector<AlertEvent> first = mEvents;
        mEvents.clear();
        SetUp();
        mWriter.mPlayer.sneaking = true;
        mRoster.clear();
        addWatcher(7, 300.0);
        addWatcher(3, 200.0);
        step(6);
        ASSERT_EQ(first.size(), mEvents.size());
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            EXPECT_EQ(first[i].mReference, mEvents[i].mReference);
            EXPECT_EQ(first[i].mAfter, mEvents[i].mAfter);
        }
    }
}
